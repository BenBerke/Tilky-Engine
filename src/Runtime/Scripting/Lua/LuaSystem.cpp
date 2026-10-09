//
// Created by berke on 5/15/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/ScriptPublicType.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptCompiler.hpp"
#include "Headers/Runtime/Sound/AudioSystem.hpp"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// ============================================================================
// Tilky Lua scripting runtime
//
// Every Lua script attached to an Entity (ComponentScript) OR to a sector
// (Sector::scripts) runs in its own sol::environment - a Behaviour instance -
// sharing one sol::state. Entity and sector scripts go through the very same
// load / reconcile / lifecycle code; the only differences are the owner
// context (ScriptOwnerKind) and the owner globals injected into the
// environment (`entity` for entities, `sector` + an unbound `entity`
// for sectors - see InjectOwnerGlobals). A script's PUBLIC FIELDS are
// declared with `public <Type> <name> = <value>` at its top level.
// LuaScriptCompiler turns the script into plain Lua plus that field schema
// (name/type/default/display name) *without ever executing the script*; the
// editor compiles from source (cached per file revision, see
// LoadOrRefreshScriptAsset), the exported game loads the bytecode and field
// manifest the exporter precompiled.
//
// Example script:
//
//   public number maxHealth = 100
//   public Entity target = nil
//
//   function Start()
//       print(entity.name .. " has " .. maxHealth .. " HP")
//   end
//
// Lifecycle: Start, Update, FixedUpdate, OnEnable, OnDisable, OnDestroy.
// Sector scripts also get OnEntityEnter(entity) / OnEntityExit(entity) - see
// DispatchSectorOccupancyEvents. Entity scripts also get
// OnCollisionEnter(other) / OnCollision(other) / OnCollisionExit(other), the
// same three as OnTrigger* for trigger colliders - see DispatchContactEvents -
// and OnSectorChange(sector) - see DispatchSectorChangeEvents.
// ============================================================================

namespace {
    namespace fs = std::filesystem;

    struct ScriptAsset {
        std::string assetId; // full relative path, no extension, posix separators - see NormalizeScriptId
        fs::path path;

        std::vector<ScriptPublicField> publicFields;

        fs::file_time_type lastWriteTime {};
        std::uint64_t schemaHash = 0;

        // What Lua loads: the compiled plain-Lua source in the editor, the
        // exported bytecode in the standalone game.
        std::string chunk;

        // LuaScriptCompiler's errors ("Scripts/Foo.lua:3: unknown type ..."),
        // or the reason the exported files couldn't be read. Empty if fine.
        std::string compileError;

        // Result of loading (not running) the chunk, computed lazily for the
        // inspector - see GetScriptLoadError. Reset whenever the asset is
        // refreshed from disk.
        bool loadErrorChecked = false;
        std::string loadError;
    };

    enum class ScriptOwnerKind : std::uint8_t {
        Entity,
        Sector
    };

    struct ScriptInstance {
        ScriptOwnerKind ownerKind = ScriptOwnerKind::Entity;

        // Entity ID or sector ID depending on ownerKind. Always a stable ID,
        // never an index or pointer - the owner is re-resolved from the
        // Level every time this instance is touched.
        ID ownerID = INVALID_ENTITY_ID;
        ScriptInstanceID instanceID = INVALID_SCRIPT_INSTANCE_ID;
        std::string scriptId; // for diagnostics only - identity is instanceID

        sol::environment environment;

        sol::protected_function startFunction;
        sol::protected_function updateFunction;
        sol::protected_function fixedUpdateFunction;
        sol::protected_function onEnableFunction;
        sol::protected_function onDisableFunction;
        sol::protected_function onDestroyFunction;

        // Sector scripts only.
        sol::protected_function onEntityEnterFunction;
        sol::protected_function onEntityExitFunction;

        // Entity scripts only.
        sol::protected_function onCollisionEnterFunction;
        sol::protected_function onCollisionFunction;
        sol::protected_function onCollisionExitFunction;
        sol::protected_function onTriggerEnterFunction;
        sol::protected_function onTriggerFunction;
        sol::protected_function onTriggerExitFunction;
        sol::protected_function onSectorChangeFunction;

        bool started = false;   // Start() has run at least once
        bool enabled = false;   // last computed effective-enabled state (script.enabled && owner.enabled)
        bool destroyed = false; // OnDestroy has already fired - guards against double teardown
    };

    struct ScriptGameTime {};

    sol::state lua;

    std::vector<ScriptInstance> scriptInstances;

    // Sector ID -> the entities that were inside it the last time
    // DispatchSectorOccupancyEvents ran. Diffed against
    // Sector::entitiesInside to find who entered and who left.
    std::unordered_map<ID, std::vector<ID>> lastSectorOccupants;

    // Entity ID -> the sector ID (INVALID_ID = outside the map) it was in the
    // last time DispatchSectorChangeEvents ran. Only entities that own a
    // script are tracked.
    std::unordered_map<ID, ID> lastEntitySectors;

    // Last frame's contacts, sorted and unique - see DispatchContactEvents.
    std::vector<PhysicsSystem::CollisionPair> lastCollisions;
    std::vector<PhysicsSystem::CollisionPair> lastTriggers;
    std::unordered_map<std::string, ScriptAsset> scriptAssets; // keyed by assetId

    // Entity instances only. An entity script's ScriptInstanceID is unique
    // across the whole level and is what Behaviour references resolve
    // through; sector script IDs are only unique within their sector, so
    // sector instances are deliberately kept out of this index (and thus
    // out of reach of Behaviour references).
    std::unordered_map<ScriptInstanceID, std::size_t> instanceIndexById;

    // Entity:Destroy() queues here; flushed once per Update() after every
    // instance has ticked. See LuaScriptRuntime::QueueEntityDestroy and
    // ProcessPendingDestroys.
    std::vector<ID> pendingDestroys;

    // FixedUpdate runs on its own fixed-step accumulator, independent from
    // the variable-dt Update() loop. NOTE: engine physics (see
    // LevelSystem::Update / PhysicsSystem::Run) still integrates at the
    // per-frame variable dt today - migrating physics itself onto this fixed
    // step is a separate, larger change that has NOT been made as part of
    // this pass. FixedUpdate is available to scripts now; it just doesn't
    // yet drive physics.
    constexpr float kFixedTimeStep = 1.0f / 60.0f;
    constexpr int kMaxFixedStepsPerFrame = 5; // avoids a spiral of death after a long stall
    float fixedUpdateAccumulator = 0.0f;

    // ------------------------------------------------------------------
    // Script identity
    //
    // A script's identity (ComponentScript::fileName) is its path relative
    // to Assets, without extension, using forward slashes - exactly what
    // AssetBrowser::ToAssetReference(kind=Script) already produces. Scripts
    // can live in any folder under Assets. NormalizeScriptId only defends
    // against callers that pass a raw OS path, backslashes, or a trailing
    // ".lua" - it must NEVER reduce the value to just its filename stem,
    // which was the previous bug that made "Player/Health.lua" and
    // "Enemies/Health.lua" collide into the same identity ("Health").
    // ------------------------------------------------------------------

    std::string NormalizeScriptId(const std::string& rawId) {
        if (rawId.empty()) return "";

        fs::path p(rawId);
        p.replace_extension();
        return p.generic_string();
    }

    // The source script in the editor, the exporter's precompiled bytecode in
    // the standalone game (its field manifest sits next to it, see
    // GetScriptFieldsPathFromId).
    fs::path GetScriptPathFromId(const std::string& assetId) {
#ifdef TILKY_STANDALONE
        return ProjectManager::GetAssetsPath() / (assetId + ".luac");
#else
        return ProjectManager::GetAssetsPath() / (assetId + ".lua");
#endif
    }

    [[maybe_unused]] fs::path GetScriptFieldsPathFromId(const std::string& assetId) {
        return ProjectManager::GetAssetsPath() / (assetId + ".fields.json");
    }

    // The name Lua errors use for a script: its Assets-relative path.
    std::string ScriptChunkName(const std::string& assetId) {
        return assetId + ".lua";
    }

    ScriptInstance* FindInstanceById(const ScriptInstanceID instanceId) {
        const auto it = instanceIndexById.find(instanceId);
        if (it == instanceIndexById.end()) return nullptr;
        if (it->second >= scriptInstances.size()) return nullptr;
        return &scriptInstances[it->second];
    }

    void RebuildInstanceIndex() {
        instanceIndexById.clear();
        for (std::size_t i = 0; i < scriptInstances.size(); ++i)
            if (scriptInstances[i].ownerKind == ScriptOwnerKind::Entity)
                instanceIndexById[scriptInstances[i].instanceID] = i;
    }

    // "entity 5" / "sector 2" - for logs and error messages.
    std::string DescribeOwner(const ScriptOwnerKind kind, const ID ownerID) {
        return fmt::format("{} {}", kind == ScriptOwnerKind::Sector ? "sector" : "entity", ownerID);
    }

    // The serialized attachment an instance was created from, or nullptr if
    // it (or its owner) no longer exists - e.g. the script was removed, the
    // entity was destroyed, or the sector was deleted.
    ScriptAttachmentData* ResolveAttachment(Level& level, const ScriptInstance& instance) {
        switch (instance.ownerKind) {
            case ScriptOwnerKind::Entity:
                return level.scripts.GetByID(instance.instanceID);

            case ScriptOwnerKind::Sector: {
                // Same ID -> sector lookup every Sector uses.
                Sector* sector = ScriptSector{&level, instance.ownerID}.GetSector();
                return sector == nullptr ? nullptr : sector->GetScript(instance.instanceID);
            }
        }

        return nullptr;
    }

    sol::protected_function GetOptionalScriptFunction(
        sol::environment environment,
        const char* functionName,
        const std::string& scriptId
    ) {
        const sol::object value = environment[functionName];

        if (value.get_type() == sol::type::nil) return {};

        if (value.get_type() != sol::type::function) {
            spdlog::warn(
                "Lua '{}' in script '{}' is not a function and will be ignored",
                functionName,
                scriptId
            );

            return {};
        }

        return value.as<sol::protected_function>();
    }

    // Every Lua script error goes through here: logged via spdlog (for the
    // engine's own logs/console window) AND pushed to the in-game console
    // in red (EditorFunctions::Print), so a broken script is visible to
    // whoever is playtesting, not just whoever is watching the log file.
    void ReportScriptError(const std::string& message) {
        const Vector3 kScriptErrorColor = {200.0f, 60.0f, 60.0f};
        spdlog::error("{}", message);
        EditorFunctions::Print(message, kScriptErrorColor, 15.0f);
    }

    template<typename... Args>
    void CallLifecycle(const ScriptInstance& instance, const sol::protected_function& fn, const char* stageName,
                       Args&&... args) {
        if (!fn.valid()) return;

        const sol::protected_function_result result = fn(std::forward<Args>(args)...);

        if (!result.valid()) {
            const sol::error error = result;

            ReportScriptError(fmt::format(
                "Lua {} error in script '{}' on {} (instance {}): {}",
                stageName,
                instance.scriptId,
                DescribeOwner(instance.ownerKind, instance.ownerID),
                instance.instanceID,
                error.what()
            ));
        }
    }

    void CallDestroy(ScriptInstance& instance) {
        if (instance.destroyed) return;
        instance.destroyed = true;

        // Never activated (e.g. the script errored during load, or the
        // Entity/script was disabled for its entire lifetime) - nothing
        // to tear down.
        if (!instance.started) return;

        CallLifecycle(instance, instance.onDestroyFunction, "OnDestroy");
    }

    // ------------------------------------------------------------------
    // Field/schema value plumbing
    // ------------------------------------------------------------------

    const char* ScriptValueTypeToString(const ScriptValueType type) {
        switch (type) {
            case ScriptValueType::Int:        return "Int";
            case ScriptValueType::Float:      return "Float";
            case ScriptValueType::Bool:       return "Bool";
            case ScriptValueType::String:     return "String";
            case ScriptValueType::Vector2:    return "Vector2";
            case ScriptValueType::Vector3:    return "Vector3";
            case ScriptValueType::Vector4:    return "Vector4";
            case ScriptValueType::Enum:       return "Enum";
            case ScriptValueType::Entity: return "Entity";
            case ScriptValueType::Component:  return "Component";
            case ScriptValueType::Behaviour:  return "Behaviour";
            case ScriptValueType::Asset:      return "Asset";
            case ScriptValueType::Wall:       return "Wall";
            case ScriptValueType::Sector:     return "Sector";
        }

        return "Unknown";
    }

    bool IsScriptValueTypeValid(const ScriptValue& value, const ScriptValueType type) {
        switch (type) {
            case ScriptValueType::Int:        return std::holds_alternative<int>(value);
            case ScriptValueType::Float:      return std::holds_alternative<float>(value);
            case ScriptValueType::Bool:       return std::holds_alternative<bool>(value);
            case ScriptValueType::String:     return std::holds_alternative<std::string>(value);
            case ScriptValueType::Vector2:    return std::holds_alternative<Vector2>(value);
            case ScriptValueType::Vector3:    return std::holds_alternative<Vector3>(value);
            case ScriptValueType::Vector4:    return std::holds_alternative<Vector4>(value);
            case ScriptValueType::Enum:       return std::holds_alternative<int>(value);
            case ScriptValueType::Entity: return std::holds_alternative<EntityRefValue>(value);
            case ScriptValueType::Component:  return std::holds_alternative<ComponentRefValue>(value);
            case ScriptValueType::Behaviour:  return std::holds_alternative<BehaviourRefValue>(value);
            case ScriptValueType::Asset:      return std::holds_alternative<AssetRefValue>(value);
            case ScriptValueType::Wall:       return std::holds_alternative<WallRefValue>(value);
            case ScriptValueType::Sector:     return std::holds_alternative<SectorRefValue>(value);
        }

        return false;
    }

    void HashCombine(std::uint64_t& seed, const std::uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    }

    void HashScriptValue(std::uint64_t& seed, const ScriptValue& value) {
        std::visit(
            [&seed](const auto& typedValue) {
                using T = std::decay_t<decltype(typedValue)>;

                if constexpr (std::is_same_v<T, int>)
                    HashCombine(seed, std::hash<int>{}(typedValue));
                else if constexpr (std::is_same_v<T, float>)
                    HashCombine(seed, std::hash<float>{}(typedValue));
                else if constexpr (std::is_same_v<T, bool>)
                    HashCombine(seed, std::hash<bool>{}(typedValue));
                else if constexpr (std::is_same_v<T, std::string>)
                    HashCombine(seed, std::hash<std::string>{}(typedValue));
                else if constexpr (std::is_same_v<T, Vector2>) {
                    HashCombine(seed, std::hash<float>{}(typedValue.x));
                    HashCombine(seed, std::hash<float>{}(typedValue.y));
                }
                else if constexpr (std::is_same_v<T, Vector3>) {
                    HashCombine(seed, std::hash<float>{}(typedValue.x));
                    HashCombine(seed, std::hash<float>{}(typedValue.y));
                    HashCombine(seed, std::hash<float>{}(typedValue.z));
                }
                else if constexpr (std::is_same_v<T, Vector4>) {
                    HashCombine(seed, std::hash<float>{}(typedValue.x));
                    HashCombine(seed, std::hash<float>{}(typedValue.y));
                    HashCombine(seed, std::hash<float>{}(typedValue.z));
                    HashCombine(seed, std::hash<float>{}(typedValue.w));
                }
                else if constexpr (std::is_same_v<T, EntityRefValue>) {
                    HashCombine(seed, std::hash<ID>{}(typedValue.entityId));
                }
                else if constexpr (std::is_same_v<T, ComponentRefValue>) {
                    HashCombine(seed, std::hash<ID>{}(typedValue.entityId));
                    HashCombine(seed, std::hash<int>{}(typedValue.componentType));
                    HashCombine(seed, std::hash<std::uint64_t>{}(typedValue.instanceId));
                }
                else if constexpr (std::is_same_v<T, BehaviourRefValue>) {
                    HashCombine(seed, std::hash<ID>{}(typedValue.entityId));
                    HashCombine(seed, std::hash<std::uint64_t>{}(typedValue.instanceId));
                }
                else if constexpr (std::is_same_v<T, AssetRefValue>)
                    HashCombine(seed, std::hash<std::string>{}(typedValue.path));
                else if constexpr (std::is_same_v<T, WallRefValue>)
                    HashCombine(seed, std::hash<ID>{}(typedValue.wallId));
                else if constexpr (std::is_same_v<T, SectorRefValue>)
                    HashCombine(seed, std::hash<ID>{}(typedValue.sectorId));
            },
            value
        );
    }

    std::uint64_t HashPublicFields(const std::vector<ScriptPublicField>& fields) {
        std::uint64_t hash = 1469598103934665603ULL;

        for (const ScriptPublicField& field : fields) {
            HashCombine(hash, std::hash<std::string>{}(field.name));
            HashCombine(hash, static_cast<std::uint64_t>(field.type));
            HashCombine(hash, static_cast<std::uint64_t>(field.componentType + 1));
            // Only non-texture kinds, so existing scripts keep their hash.
            if (field.assetKind != ScriptAssetKind::Texture)
                HashCombine(hash, static_cast<std::uint64_t>(field.assetKind) + 1);
            HashScriptValue(hash, field.defaultValue);

            for (const ScriptEnumOption& option : field.enumOptions) {
                HashCombine(hash, std::hash<std::string>{}(option.name));
                HashCombine(hash, std::hash<int>{}(option.value));
            }
        }

        return hash;
    }

    // ------------------------------------------------------------------
    // Script assets (compiled once per file revision, never executed here)
    // ------------------------------------------------------------------

    bool ReadWholeFile(const fs::path& path, std::string& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

#ifndef TILKY_STANDALONE
    void CompileScriptAsset(ScriptAsset& asset) {
        std::string source;
        if (!ReadWholeFile(asset.path, source)) {
            asset.compileError = fmt::format("{}: could not open the file", ScriptChunkName(asset.assetId));
            return;
        }

        LuaScriptCompiler::Result compiled = LuaScriptCompiler::Compile(source);

        std::vector<std::string> errors;
        for (const LuaScriptCompiler::Diagnostic& error : compiled.errors)
            errors.push_back(fmt::format("{}:{}: {}", ScriptChunkName(asset.assetId), error.line, error.message));

        asset.compileError = fmt::format("{}", fmt::join(errors, "\n"));
        asset.publicFields = std::move(compiled.publicFields);
        asset.chunk = std::move(compiled.luaSource);
    }
#else
    void LoadExportedScriptAsset(ScriptAsset& asset) {
        std::string fieldsText;
        std::string error;

        if (!ReadWholeFile(asset.path, asset.chunk)) {
            asset.compileError = fmt::format("{}: could not open {}", ScriptChunkName(asset.assetId), asset.path.string());
        } else if (!ReadWholeFile(GetScriptFieldsPathFromId(asset.assetId), fieldsText)) {
            asset.compileError = fmt::format("{}: missing field manifest", ScriptChunkName(asset.assetId));
        } else if (!LevelSerialization::ScriptPublicFieldsFromJsonText(fieldsText, asset.publicFields, &error)) {
            asset.compileError = fmt::format("{}: bad field manifest: {}", ScriptChunkName(asset.assetId), error);
        }
    }
#endif

    ScriptAsset& LoadOrRefreshScriptAsset(const std::string& assetId, const fs::path& path) {
        const fs::file_time_type lastWriteTime = fs::last_write_time(path);

        const auto it = scriptAssets.find(assetId);

        if (it != scriptAssets.end() && it->second.lastWriteTime == lastWriteTime) return it->second;

        ScriptAsset asset;
        asset.assetId = assetId;
        asset.path = path;
        asset.lastWriteTime = lastWriteTime;
#ifndef TILKY_STANDALONE
        CompileScriptAsset(asset);
#else
        LoadExportedScriptAsset(asset);
#endif
        asset.schemaHash = HashPublicFields(asset.publicFields);
        // loadErrorChecked/loadError stay at their defaults - see GetScriptLoadError.

        scriptAssets[assetId] = std::move(asset);

        return scriptAssets[assetId];
    }

    void ReconcilePublicValues(ScriptAttachmentData& script, const ScriptAsset& asset, const std::string& ownerLabel) {
        // A script that doesn't compile has an incomplete field list; keep
        // the saved values until it's fixed instead of dropping them.
        if (!asset.compileError.empty()) return;

        for (const ScriptPublicField& field : asset.publicFields) {
            const auto valueIt = script.publicValues.find(field.name);

            if (valueIt == script.publicValues.end()) {
                script.publicValues[field.name] = field.defaultValue;
                continue;
            }

            if (!IsScriptValueTypeValid(valueIt->second, field.type)) {
                spdlog::warn(
                    "Public field '{}.{}' on {} had wrong type. Expected {}. Resetting to default.",
                    script.fileName,
                    field.name,
                    ownerLabel,
                    ScriptValueTypeToString(field.type)
                );

                valueIt->second = field.defaultValue;
            }
        }

        // Drop values for fields the script no longer declares.
        std::erase_if(script.publicValues, [&asset](const auto& entry) {
            return std::ranges::none_of(asset.publicFields, [&entry](const ScriptPublicField& field) {
                return field.name == entry.first;
            });
        });

        script.schemaHash = asset.schemaHash;
    }

    // ------------------------------------------------------------------
    // Reference resolution: serialized ScriptValue -> live Lua object
    // ------------------------------------------------------------------

    // The referenced component, as long as it still exists on that entity.
    template<typename Wrapper, typename Storage>
    sol::object ResolveInstanceRef(const sol::state_view luaView, Level& level, Storage& storage, const ComponentRefValue& ref) {
        const auto* component = storage.GetInstance(ref.instanceId);
        if (component == nullptr || component->ownerID != ref.entityId) return sol::make_object(luaView, sol::nil);
        return sol::make_object(luaView, Wrapper{&level, ref.entityId, ref.instanceId});
    }

    sol::object ResolveComponentRef(const sol::state_view luaView, Level& level, const ComponentRefValue& ref) {
        if (ref.entityId == INVALID_ID) return sol::make_object(luaView, sol::nil);

        switch (ref.componentType) {
            case CMP_TRANSFORM:
                if (!level.transforms.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptTransform{&level, ref.entityId});

            case CMP_SPRITE: return ResolveInstanceRef<ScriptSprite>(luaView, level, level.sprites, ref);
            case CMP_AUDIO_SOURCE: return ResolveInstanceRef<ScriptAudioSource>(luaView, level, level.audioSources, ref);
            case CMP_PLAYER_CONTROLLER: return ResolveInstanceRef<ScriptPlayerController>(luaView, level, level.playerControllers, ref);
            case CMP_CAMERA: return ResolveInstanceRef<ScriptCamera>(luaView, level, level.cameras, ref);
            case CMP_COLLIDER: return ResolveInstanceRef<ScriptCollider>(luaView, level, level.colliders, ref);
            case CMP_RIGIDBODY: return ResolveInstanceRef<ScriptRigidbody>(luaView, level, level.rigidbodies, ref);
            case CMP_MODEL: return ResolveInstanceRef<ScriptModel>(luaView, level, level.models, ref);
            case CMP_FLIPBOOK: return ResolveInstanceRef<ScriptFlipbook>(luaView, level, level.flipbooks, ref);

            default: break;
        }

        return sol::make_object(luaView, sol::nil);
    }

    sol::object ResolveScriptValueImpl(const sol::state_view luaView, Level& level, const ScriptValue& value) {
        return std::visit(
            [&](const auto& typedValue) -> sol::object {
                using T = std::decay_t<decltype(typedValue)>;

                if constexpr (std::is_same_v<T, EntityRefValue>) {
                    if (typedValue.entityId == INVALID_ID || level.GetEntity(typedValue.entityId) == nullptr)
                        return sol::make_object(luaView, sol::nil);

                    return sol::make_object(luaView, ScriptEntity{&level, typedValue.entityId});
                }
                else if constexpr (std::is_same_v<T, ComponentRefValue>) return ResolveComponentRef(luaView, level, typedValue);
                else if constexpr (std::is_same_v<T, BehaviourRefValue>) {
                    if (typedValue.entityId == INVALID_ID || typedValue.instanceId == INVALID_SCRIPT_INSTANCE_ID)
                        return sol::make_object(luaView, sol::nil);

                    if (!LuaScriptRuntime::IsInstanceValid(typedValue.entityId, typedValue.instanceId))
                        return sol::make_object(luaView, sol::nil);

                    return sol::make_object(luaView, ScriptBehaviourRef{&level, typedValue.entityId, typedValue.instanceId});
                }
                else if constexpr (std::is_same_v<T, AssetRefValue>) return sol::make_object(luaView, typedValue.path);
                else if constexpr (std::is_same_v<T, WallRefValue>) {
                    const ScriptWall wall{&level, typedValue.wallId};
                    if (!wall.IsValid()) return sol::make_object(luaView, sol::nil);

                    return sol::make_object(luaView, wall);
                }
                else if constexpr (std::is_same_v<T, SectorRefValue>) {
                    const ScriptSector sector{&level, typedValue.sectorId};
                    if (!sector.IsValid()) return sol::make_object(luaView, sol::nil);

                    return sol::make_object(luaView, sector);
                }
                else return sol::make_object(luaView, typedValue);
            },
            value
        );
    }

    // ------------------------------------------------------------------
    // Instance load / lifecycle
    // ------------------------------------------------------------------

    struct LoadedChunk {
        std::optional<sol::protected_function> function;
        std::string error;
    };

    // Loads (doesn't run) a script asset's chunk. The editor only accepts
    // source and the standalone game only bytecode, so a stray file of the
    // other kind is an error rather than something run unchecked.
    LoadedChunk LoadScriptChunk(const ScriptAsset& asset) {
        if (!asset.compileError.empty()) return {std::nullopt, asset.compileError};

#ifdef TILKY_STANDALONE
        constexpr sol::load_mode mode = sol::load_mode::binary;
#else
        constexpr sol::load_mode mode = sol::load_mode::text;
#endif
        const sol::load_result loaded = lua.load(asset.chunk, "@" + ScriptChunkName(asset.assetId), mode);

        if (!loaded.valid()) {
            const sol::error error = loaded;
            return {std::nullopt, error.what()};
        }

        return {loaded.get<sol::protected_function>(), {}};
    }

    bool IsSectorOwnerGlobal(const std::string& name) {
        return name == "sector" || name == "entity";
    }

    // Injects the globals that identify what a script is attached to. Done
    // before the script body runs so top-level code can already use them.
    void InjectOwnerGlobals(Level& level, ScriptInstance& instance) {
        switch (instance.ownerKind) {
            case ScriptOwnerKind::Entity:
                instance.environment["entity"] = ScriptEntity{&level, instance.ownerID};
                break;

            case ScriptOwnerKind::Sector:
                // The one Sector type used everywhere else, bound to the
                // sector that owns THIS instance (by ID).
                instance.environment["sector"] = ScriptSector{&level, instance.ownerID};

                // Same environment shape as an entity script, but bound to
                // nothing: an invalid Entity (isValid == false) that can
                // never resolve to any entity. It goes through ScriptEntity's
                // ordinary invalid-reference handling - see LuaEntityBindings.cpp.
                instance.environment["entity"] = ScriptEntity{&level, INVALID_ENTITY_ID};
                break;
        }
    }

    bool LoadScriptIntoInstance(
        Level& level,
        const ScriptOwnerKind ownerKind,
        const ID ownerID,
        ScriptAttachmentData& script,
        const ScriptAsset& asset,
        ScriptInstance& instance
    ) {
        const std::string& assetId = asset.assetId;

        instance.ownerKind = ownerKind;
        instance.ownerID = ownerID;
        instance.instanceID = script.instanceID;
        instance.scriptId = assetId;
        instance.started = false;
        instance.enabled = false;
        instance.destroyed = false;

        instance.environment = sol::environment(lua, sol::create, lua.globals());

        InjectOwnerGlobals(level, instance);
        instance.environment["Global"] = lua["Global"];

        LoadedChunk loadedScript = LoadScriptChunk(asset);

        if (!loadedScript.function) {
            ReportScriptError(fmt::format("Failed to load Lua script '{}': {}", ScriptChunkName(assetId), loadedScript.error));
            return false;
        }

        sol::protected_function scriptFunction = std::move(*loadedScript.function);
        sol::set_environment(instance.environment, scriptFunction);

        // Running the script body sets every field to its own inline default
        // (`maxHealth = 100`) and defines its lifecycle functions. The
        // serialized (possibly inspector-edited) values are applied AFTER
        // this, overwriting those inline defaults - see the loop below. This
        // ordering is what lets fields stay plain Lua variables instead of a
        // Public.Float(...)-style declarative call: the variable has to
        // actually be assigned by the script for it to exist at all, so the
        // serialized override necessarily has to come second.
        const sol::protected_function_result result = scriptFunction();

        if (!result.valid()) {
            const sol::error error = result;
            ReportScriptError(fmt::format("Failed to run Lua script '{}': {}", ScriptChunkName(assetId), error.what()));
            return false;
        }

        const auto assetIt = scriptAssets.find(assetId);

        if (assetIt != scriptAssets.end()) {
            for (const ScriptPublicField& field : assetIt->second.publicFields) {
                const auto valueIt = script.publicValues.find(field.name);
                if (valueIt == script.publicValues.end()) continue;

                // Would overwrite the sector/entity binding injected above.
                if (ownerKind == ScriptOwnerKind::Sector && IsSectorOwnerGlobal(field.name)) {
                    spdlog::warn(
                        "Lua script '{}' declares public field '{}', which is reserved on sector scripts - ignoring its value",
                        assetId, field.name
                    );
                    continue;
                }

                instance.environment[field.name] = ResolveScriptValueImpl(lua, level, valueIt->second);
            }
        }

        instance.startFunction       = GetOptionalScriptFunction(instance.environment, "Start", assetId);
        instance.updateFunction      = GetOptionalScriptFunction(instance.environment, "Update", assetId);
        instance.fixedUpdateFunction = GetOptionalScriptFunction(instance.environment, "FixedUpdate", assetId);
        instance.onEnableFunction    = GetOptionalScriptFunction(instance.environment, "OnEnable", assetId);
        instance.onDisableFunction   = GetOptionalScriptFunction(instance.environment, "OnDisable", assetId);
        instance.onDestroyFunction   = GetOptionalScriptFunction(instance.environment, "OnDestroy", assetId);

        if (ownerKind == ScriptOwnerKind::Sector) {
            instance.onEntityEnterFunction = GetOptionalScriptFunction(instance.environment, "OnEntityEnter", assetId);
            instance.onEntityExitFunction  = GetOptionalScriptFunction(instance.environment, "OnEntityExit", assetId);
        }

        if (ownerKind == ScriptOwnerKind::Entity) {
            instance.onCollisionEnterFunction = GetOptionalScriptFunction(instance.environment, "OnCollisionEnter", assetId);
            instance.onCollisionFunction      = GetOptionalScriptFunction(instance.environment, "OnCollision", assetId);
            instance.onCollisionExitFunction  = GetOptionalScriptFunction(instance.environment, "OnCollisionExit", assetId);
            instance.onTriggerEnterFunction   = GetOptionalScriptFunction(instance.environment, "OnTriggerEnter", assetId);
            instance.onTriggerFunction        = GetOptionalScriptFunction(instance.environment, "OnTrigger", assetId);
            instance.onTriggerExitFunction    = GetOptionalScriptFunction(instance.environment, "OnTriggerExit", assetId);
            instance.onSectorChangeFunction   = GetOptionalScriptFunction(instance.environment, "OnSectorChange", assetId);
        }

        return true;
    }

    // `script` is what ResolveAttachment returned for `instance`.
    bool EffectiveEnabled(Level& level, const ScriptInstance& instance, const ScriptAttachmentData* script) {
        if (script == nullptr || !script->enabled) return false;

        switch (instance.ownerKind) {
            case ScriptOwnerKind::Entity: {
                const Entity* owner = level.GetEntity(instance.ownerID);
                return owner != nullptr && owner->enabled;
            }

            // A sector has no enabled flag of its own, and a resolved
            // attachment already proves the sector still exists.
            case ScriptOwnerKind::Sector:
                return true;
        }

        return false;
    }

    // Creates (loads + runs the body of) one script instance for `script`,
    // owned by the entity/sector `ownerID`, and registers it. Everything
    // that can go wrong is logged and simply skips this one script.
    void InstantiateScript(Level& level, const ScriptOwnerKind ownerKind, const ID ownerID, ScriptAttachmentData& script) {
        const std::string ownerLabel = DescribeOwner(ownerKind, ownerID);
        const std::string assetId = NormalizeScriptId(script.fileName);

        if (assetId.empty()) {
            spdlog::warn("Skipping script component with empty file name on {}", ownerLabel);
            return;
        }

        const fs::path path = GetScriptPathFromId(assetId);

        if (!fs::exists(path)) {
            spdlog::error("Lua script does not exist: {}", path.string());
            return;
        }

        ScriptAsset& asset = LoadOrRefreshScriptAsset(assetId, path);
        ReconcilePublicValues(script, asset, ownerLabel);

        ScriptInstance instance;

        if (!LoadScriptIntoInstance(level, ownerKind, ownerID, script, asset, instance)) return;

        if (ownerKind == ScriptOwnerKind::Entity) instanceIndexById[instance.instanceID] = scriptInstances.size();
        scriptInstances.push_back(std::move(instance));
    }

    // Flushes Entity:Destroy() requests queued this frame, AND drops any
    // instance already marked destroyed (e.g. Update() found its
    // ComponentScript had been removed directly, outside Entity:Destroy)
    // from the registry. Always safe to call even with nothing queued.
    void ProcessPendingDestroys(Level& level) {
        for (const ID entityId : pendingDestroys) {
            for (ScriptInstance& instance : scriptInstances) {
                // ownerID is a sector ID for sector scripts - it must never
                // be mistaken for an entity ID here.
                if (instance.ownerKind != ScriptOwnerKind::Entity || instance.ownerID != entityId) continue;
                CallDestroy(instance);
            }

            // Otherwise its OpenAL source outlives it and keeps playing where
            // the entity died.
            for (ComponentAudioSource* audio : level.audioSources.GetAll(entityId)) AudioSystem::DestroySource(*audio);

            level.DestroyEntity(entityId);
        }

        pendingDestroys.clear();

        if (std::ranges::any_of(scriptInstances, [](const ScriptInstance& instance) { return instance.destroyed; })) {
            std::erase_if(scriptInstances, [](const ScriptInstance& instance) { return instance.destroyed; });
            RebuildInstanceIndex();
        }
    }

    // The ID of the sector the entity is in, or INVALID_ID when it is outside
    // the map or has no transform.
    ID GetEntitySectorID(Level& level, const ID entityID) {
        const ComponentTransform* transform = level.transforms.Get(entityID);

        if (transform == nullptr || transform->sectorIndex < 0 ||
            transform->sectorIndex >= static_cast<int>(level.sectors.size()))
            return INVALID_ID;

        return level.sectors[transform->sectorIndex].id;
    }

    bool CollisionPairLess(const PhysicsSystem::CollisionPair& lhs, const PhysicsSystem::CollisionPair& rhs) {
        return lhs.a != rhs.a ? lhs.a < rhs.a : lhs.b < rhs.b;
    }

    using ContactCallback = sol::protected_function ScriptInstance::*;

    // One Enter/every-frame/Exit callback family, e.g. OnCollision*.
    struct ContactCallbacks {
        ContactCallback enter;
        ContactCallback stay;
        ContactCallback exit;
        const char* enterName;
        const char* stayName;
        const char* exitName;
    };

    constexpr ContactCallbacks kCollisionCallbacks = {
        &ScriptInstance::onCollisionEnterFunction,
        &ScriptInstance::onCollisionFunction,
        &ScriptInstance::onCollisionExitFunction,
        "OnCollisionEnter", "OnCollision", "OnCollisionExit"
    };

    constexpr ContactCallbacks kTriggerCallbacks = {
        &ScriptInstance::onTriggerEnterFunction,
        &ScriptInstance::onTriggerFunction,
        &ScriptInstance::onTriggerExitFunction,
        "OnTriggerEnter", "OnTrigger", "OnTriggerExit"
    };

    // Diffs this frame's `contacts` against `last` (then replaces it) and
    // fires `callbacks` on both entities of every pair, each getting the
    // other one as its argument.
    void DispatchContactSet(
        Level& level,
        const std::vector<PhysicsSystem::CollisionPair>& contacts,
        std::vector<PhysicsSystem::CollisionPair>& last,
        const ContactCallbacks& callbacks
    ) {
        if (contacts.empty() && last.empty()) return;

        std::vector<PhysicsSystem::CollisionPair> now = contacts;
        std::ranges::sort(now, CollisionPairLess);
        now.erase(std::unique(now.begin(), now.end()), now.end());

        std::vector<PhysicsSystem::CollisionPair> entered;
        std::vector<PhysicsSystem::CollisionPair> exited;
        std::ranges::set_difference(now, last, std::back_inserter(entered), CollisionPairLess);
        std::ranges::set_difference(last, now, std::back_inserter(exited), CollisionPairLess);

        last = now;

        // Entity ID -> indices of its scripts that define any of these
        // callbacks, so entities without one cost nothing below.
        std::unordered_map<ID, std::vector<size_t>> listeners;

        for (size_t i = 0; i < scriptInstances.size(); ++i) {
            const ScriptInstance& instance = scriptInstances[i];
            if (instance.ownerKind != ScriptOwnerKind::Entity) continue;

            if ((instance.*callbacks.enter).valid() ||
                (instance.*callbacks.stay).valid() ||
                (instance.*callbacks.exit).valid())
                listeners[instance.ownerID].push_back(i);
        }

        if (listeners.empty()) return;

        const auto notify = [&](const ID self, const ID other, const ContactCallback callback, const char* stageName) {
            const auto it = listeners.find(self);
            if (it == listeners.end()) return;

            for (const size_t index : it->second) {
                const ScriptInstance& instance = scriptInstances[index];
                if (instance.destroyed || !instance.enabled) continue;

                CallLifecycle(instance, instance.*callback, stageName, ScriptEntity{&level, other});
            }
        };

        const auto notifyBoth = [&](const std::vector<PhysicsSystem::CollisionPair>& pairs,
                                    const ContactCallback callback, const char* stageName) {
            for (const PhysicsSystem::CollisionPair& pair : pairs) {
                notify(pair.a, pair.b, callback, stageName);
                notify(pair.b, pair.a, callback, stageName);
            }
        };

        // Exits first, then enters, then every contact that is touching this
        // frame (enter frame included).
        notifyBoth(exited, callbacks.exit, callbacks.exitName);
        notifyBoth(entered, callbacks.enter, callbacks.enterName);
        notifyBoth(now, callbacks.stay, callbacks.stayName);
    }

    void RegisterGameTimeMetadata() {
        LuaBindingMetadata::RegisterType(LuaBindingMetadata::GlobalTable("GameTime", "Global frame-timing table.", {
            LuaBindingMetadata::Prop("deltaTime", "number", true, "Seconds since the last Update()."),
            LuaBindingMetadata::Prop("fixedDeltaTime", "number", true, "The fixed step FixedUpdate() runs on."),
            LuaBindingMetadata::Prop("osTime", "integer", true, "Wall-clock time: whole seconds since the Unix epoch (1970-01-01 UTC)."),
            LuaBindingMetadata::Prop("fps", "integer", true, "Get the current frames-per-second"),
            LuaBindingMetadata::Prop("timeInSeconds", "integer", true, "Get the time in seconds since the engine started")
        }));
    }

    void RegisterGameTimeBindings(sol::state& luaState) {
        RegisterGameTimeMetadata();

        luaState.new_usertype<ScriptGameTime>(
            "ScriptGameTime",
            sol::no_constructor,

            "deltaTime",
            sol::property([](const ScriptGameTime&) { return GameTime::deltaTime; }),

            // Matches Unity's Time.fixedDeltaTime - the fixed step FixedUpdate
            // runs on. See the kFixedTimeStep comment above for why this is a
            // local constant rather than something engine physics consumes yet.
            "fixedDeltaTime",
            sol::property([](const ScriptGameTime&) { return kFixedTimeStep; }),

            "osTime",
            sol::property([](const ScriptGameTime&) {
                const auto now = std::chrono::system_clock::now();

                // Convert duration since epoch to integer seconds
                const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                return seconds;
            }),

            "fps",
            sol::property([](const ScriptGameTime&) {
                return GameTime::GetFPS();
            }),

            "timeInSeconds",
            sol::property([](const ScriptGameTime&) {
                return GameTime::timeInSeconds;
            })
        );

        luaState["GameTime"] = ScriptGameTime {};
    }

    // Registers the "Behaviour" usertype (ScriptBehaviourRef). Only
    // __index/__newindex are bound - see the comment on
    // ScriptBehaviourRef::LuaGet/LuaSet in LuaWrappers.hpp for why isValid/
    // entity/enabled are handled inside those two functions instead of
    // being registered as ordinary usertype properties.
    void RegisterBehaviourRefBindings(sol::state& luaState) {
        luaState.new_usertype<ScriptBehaviourRef>(
            "Behaviour",
            sol::no_constructor,

            sol::meta_function::index, &ScriptBehaviourRef::LuaGet,
            sol::meta_function::new_index, &ScriptBehaviourRef::LuaSet
        );

        // Behaviour's real shape is dynamic (see LuaGet/LuaSet) so LuaLS
        // cannot infer its fields the way it can a normal usertype - this at
        // least documents the three names LuaGet/LuaSet special-case.
        // Anything else read/written through a Behaviour reference is
        // whatever the target script itself declares.
        LuaBindingMetadata::RegisterType({
            .name = "Behaviour",
            .doc = "A safe reference to one script instance, anywhere in the level. "
                   "Indexing it (behaviour.someField, behaviour:SomeFunction()) forwards "
                   "into that script's own fields/functions.",
            .properties = {
                {.name = "isValid", .luaType = "boolean", .readOnly = true, .doc = "False once the target script/Entity no longer exists."},
                {.name = "entity", .luaType = "Entity", .readOnly = true, .doc = "The Entity this script is attached to."},
                {.name = "enabled", .luaType = "boolean", .doc = "This script instance's own enabled flag."},
            }
        });
    }

    void RegisterBindings(LuaScriptSystem& scriptSystem) {
        scriptSystem.RegisterMathBindings(lua);
        scriptSystem.RegisterVectorBindings(lua);
        scriptSystem.RegisterComponentBindings(lua);
        scriptSystem.RegisterEntityBindings(lua);
        scriptSystem.RegisterInputBindings(lua);
        scriptSystem.RegisterEditorFunctionBindings(lua);
        scriptSystem.RegisterGameBindings(lua);
        scriptSystem.RegisterWallBindings(lua);
        scriptSystem.RegisterSectorBindings(lua);

        RegisterGameTimeBindings(lua);
        RegisterBehaviourRefBindings(lua);
    }
}

// ============================================================================
// LuaScriptRuntime - the seam LuaWrappers.hpp's header-only wrapper structs
// call into. See LuaScriptRuntime.hpp for the contract.
// ============================================================================

namespace LuaScriptRuntime {
    bool IsInstanceValid(const ID entityId, const ScriptInstanceID instanceId) {
        const ScriptInstance* instance = FindInstanceById(instanceId);
        return instance != nullptr && instance->ownerID == entityId && !instance->destroyed;
    }

    bool GetInstanceEnabled(Level& level, const ID entityId, const ScriptInstanceID instanceId) {
        const ComponentScript* script = level.scripts.GetByID(instanceId);
        return script != nullptr && script->ownerID == entityId && script->enabled;
    }

    void SetInstanceEnabled(Level& level, const ID entityId, const ScriptInstanceID instanceId, const bool enabled) {
        ComponentScript* script = level.scripts.GetByID(instanceId);
        if (script == nullptr || script->ownerID != entityId) return;
        script->enabled = enabled;
    }

    sol::object GetInstanceField(const ID entityId, const ScriptInstanceID instanceId, const std::string& key, const sol::this_state state) {
        const sol::state_view luaView(state);

        const ScriptInstance* instance = FindInstanceById(instanceId);

        if (instance == nullptr || instance->ownerID != entityId || instance->destroyed || !instance->environment.valid())
            return sol::make_object(luaView, sol::nil);

        return instance->environment.get<sol::object>(key);
    }

    void SetInstanceField(const ID entityId, const ScriptInstanceID instanceId, const std::string& key, sol::object value) {
        ScriptInstance* instance = FindInstanceById(instanceId);

        if (instance == nullptr || instance->ownerID != entityId || instance->destroyed || !instance->environment.valid())
            return;

        instance->environment[key] = std::move(value);
    }

    void QueueEntityDestroy(const ID entityId) {
        if (entityId == INVALID_ENTITY_ID) return;
        if (std::ranges::find(pendingDestroys, entityId) == pendingDestroys.end())
            pendingDestroys.push_back(entityId);
    }

    sol::object ResolveScriptValue(const sol::state_view luaView, Level& level, const ScriptValue& value) {
        return ResolveScriptValueImpl(luaView, level, value);
    }
}

// ============================================================================
// LuaScriptSystem
// ============================================================================

bool LuaScriptSystem::Initialize() {
    try {
        lua.open_libraries(
            sol::lib::base,
            sol::lib::math,
            sol::lib::table,
            sol::lib::string
        );

        RegisterBindings(*this);

        // Shared by every script. Made here and not in Start() so it lives as
        // long as this Lua state - the whole game session, across level
        // changes - and starts empty again only on the next Initialize().
        lua["Global"] = lua.create_table();

        // Best-effort: regenerate the LuaLS stub file every time scripting
        // initializes, so it never drifts from the metadata registered
        // above. Only covers Entity/Behaviour today - see
        // LuaBindingMetadata.hpp's scope note. Failure here (e.g. no project
        // loaded yet) is non-fatal - it only affects editor autocomplete.
        if (ProjectManager::HasProject()) {
            // Project root, not Assets, so it is never exported or shown as an asset.
            const fs::path stubDirectory = ProjectManager::GetProjectFolder() / ".luals";
            std::error_code ec;
            fs::create_directories(stubDirectory, ec);

            if (!ec) LuaBindingMetadata::GenerateLuaLSStub((stubDirectory / "tilky_api.lua").string());
        }

        spdlog::info("Lua scripting initialized");
        return true;
    }
    catch (const std::exception &e) {
        spdlog::critical("Failed to initialize Lua scripting {}", e.what());
        return false;
    }
}

void LuaScriptSystem::Start(Level& level) {
    scriptInstances.clear();
    instanceIndexById.clear();
    pendingDestroys.clear();
    fixedUpdateAccumulator = 0.0f;

    for (ComponentScript& script : level.scripts.components)
        InstantiateScript(level, ScriptOwnerKind::Entity, script.ownerID, script);

    // Sector scripts come after every entity script, in sector order.
    for (Sector& sector : level.sectors)
        for (SectorScript& script : sector.scripts)
            InstantiateScript(level, ScriptOwnerKind::Sector, sector.id, script);

    // Whoever is already inside a sector when the level starts is the
    // baseline - no OnEntityEnter for them.
    lastSectorOccupants.clear();
    for (const Sector& sector : level.sectors) lastSectorOccupants[sector.id] = sector.entitiesInside;

    // Same for OnSectorChange: the sector an entity starts in is the baseline.
    lastEntitySectors.clear();
    for (const ScriptInstance& instance : scriptInstances)
        if (instance.ownerKind == ScriptOwnerKind::Entity)
            lastEntitySectors[instance.ownerID] = GetEntitySectorID(level, instance.ownerID);

    lastCollisions.clear();
    lastTriggers.clear();

    // First activation: OnEnable before Start, matching Unity's ordering on
    // an object's first activation.
    for (ScriptInstance& instance : scriptInstances) {
        const ScriptAttachmentData* script = ResolveAttachment(level, instance);
        if (!EffectiveEnabled(level, instance, script)) continue;

        instance.enabled = true;
        CallLifecycle(instance, instance.onEnableFunction, "OnEnable");
        CallLifecycle(instance, instance.startFunction, "Start");
        instance.started = true;
    }
}

void LuaScriptSystem::Update(Level& level) {
    for (ScriptInstance& instance : scriptInstances) {
        if (instance.destroyed) continue;

        const ScriptAttachmentData* script = ResolveAttachment(level, instance);

        // The attachment disappeared out from under this instance (a
        // ComponentScript removed directly rather than through
        // Entity:Destroy(), or a sector script whose script or whole
        // sector was deleted) - tear it down the same way a queued destroy
        // would. destroyed instances are skipped from here on and dropped at
        // the end of the frame by ProcessPendingDestroys.
        if (script == nullptr) {
            CallDestroy(instance);
            continue;
        }

        const bool effectiveEnabled = EffectiveEnabled(level, instance, script);

        if (effectiveEnabled != instance.enabled) {
            instance.enabled = effectiveEnabled;

            if (effectiveEnabled) {
                CallLifecycle(instance, instance.onEnableFunction, "OnEnable");

                if (!instance.started) {
                    CallLifecycle(instance, instance.startFunction, "Start");
                    instance.started = true;
                }
            }
            else CallLifecycle(instance, instance.onDisableFunction, "OnDisable");
        }

        if (!instance.enabled) continue;

        CallLifecycle(instance, instance.updateFunction, "Update");
    }

    fixedUpdateAccumulator += GameTime::deltaTime;
    int fixedSteps = 0;

    while (fixedUpdateAccumulator >= kFixedTimeStep && fixedSteps < kMaxFixedStepsPerFrame) {
        for (ScriptInstance& instance : scriptInstances) {
            if (instance.destroyed || !instance.enabled) continue;
            CallLifecycle(instance, instance.fixedUpdateFunction, "FixedUpdate");
        }

        fixedUpdateAccumulator -= kFixedTimeStep;
        ++fixedSteps;
    }

    // Deliberately NOT flushing pendingDestroys here - see
    // FlushPendingDestroys's declaration comment in LuaScripting.hpp.
    // LevelSystem::Update() calls it once, after this frame's physics and
    // transform-sync work has finished.
}

void LuaScriptSystem::DispatchSectorOccupancyEvents(Level& level) {
    for (const Sector& sector : level.sectors) {
        std::vector<ID>& before = lastSectorOccupants[sector.id];

        // Copied: a handler can move entities, which rewrites entitiesInside.
        const std::vector<ID> now = sector.entitiesInside;
        if (now == before) continue;

        std::vector<ID> entered;
        std::vector<ID> exited;

        for (const ID id : now)
            if (std::ranges::find(before, id) == before.end()) entered.push_back(id);
        for (const ID id : before)
            if (std::ranges::find(now, id) == now.end()) exited.push_back(id);

        before = now;

        const ID sectorID = sector.id;

        // By index: a handler can't add instances, but keep it safe anyway.
        for (size_t i = 0; i < scriptInstances.size(); ++i) {
            const ScriptInstance& instance = scriptInstances[i];

            if (instance.ownerKind != ScriptOwnerKind::Sector || instance.ownerID != sectorID) continue;
            if (instance.destroyed || !instance.enabled) continue;

            // Exits first, so a script counting occupants never sees one
            // entity in two places.
            for (const ID id : exited)
                CallLifecycle(instance, instance.onEntityExitFunction, "OnEntityExit", ScriptEntity{&level, id});
            for (const ID id : entered)
                CallLifecycle(instance, instance.onEntityEnterFunction, "OnEntityEnter", ScriptEntity{&level, id});
        }
    }
}

void LuaScriptSystem::DispatchSectorChangeEvents(Level& level) {
    // Entity ID -> its new sector ID. Collected first, so an entity with
    // several scripts is only compared once and every script sees the change.
    std::vector<std::pair<ID, ID>> changed;

    for (const ScriptInstance& instance : scriptInstances) {
        if (instance.ownerKind != ScriptOwnerKind::Entity || instance.destroyed) continue;

        const ID sectorID = GetEntitySectorID(level, instance.ownerID);
        const auto [it, inserted] = lastEntitySectors.try_emplace(instance.ownerID, sectorID);

        // First time this entity is seen: its current sector is the baseline.
        if (inserted || it->second == sectorID) continue;

        it->second = sectorID;
        changed.emplace_back(instance.ownerID, sectorID);
    }

    for (const auto& [entityID, sectorID] : changed) {
        const sol::object sectorObject = sectorID == INVALID_ID
            ? sol::make_object(lua, sol::nil)
            : sol::make_object(lua, ScriptSector{&level, sectorID});

        // By index: a handler can't add instances, but keep it safe anyway.
        for (size_t i = 0; i < scriptInstances.size(); ++i) {
            const ScriptInstance& instance = scriptInstances[i];

            if (instance.ownerKind != ScriptOwnerKind::Entity || instance.ownerID != entityID) continue;
            if (instance.destroyed || !instance.enabled) continue;

            CallLifecycle(instance, instance.onSectorChangeFunction, "OnSectorChange", sectorObject);
        }
    }
}

void LuaScriptSystem::DispatchContactEvents(Level& level, const PhysicsSystem::Contacts& contacts) {
    DispatchContactSet(level, contacts.collisions, lastCollisions, kCollisionCallbacks);
    DispatchContactSet(level, contacts.triggers, lastTriggers, kTriggerCallbacks);
}

bool LuaScriptSystem::DispatchEntityEvent(Level&, const ID entityID, const std::string& functionName,
                                          const std::string& argument) {
    bool defined = false;

    // By index: a handler can attach a script, which can grow the vector.
    for (size_t i = 0; i < scriptInstances.size(); ++i) {
        if (scriptInstances[i].ownerKind != ScriptOwnerKind::Entity || scriptInstances[i].ownerID != entityID) continue;
        if (scriptInstances[i].destroyed) continue;

        const sol::object value = scriptInstances[i].environment[functionName];
        if (value.get_type() != sol::type::function) continue;

        defined = true;
        if (!scriptInstances[i].enabled) continue;

        // Copied: the instance can move while the handler runs.
        const sol::protected_function function = value.as<sol::protected_function>();
        const ScriptInstance instance = scriptInstances[i];
        CallLifecycle(instance, function, functionName.c_str(), argument);
    }

    return defined;
}

void LuaScriptSystem::Stop(Level&) {
    for (ScriptInstance& instance : scriptInstances) CallDestroy(instance);
}

void LuaScriptSystem::FlushPendingDestroys(Level& level) {
    ProcessPendingDestroys(level);
}

void LuaScriptSystem::Shutdown() {
    scriptInstances.clear();
    lastEntitySectors.clear();
    lastCollisions.clear();
    lastTriggers.clear();
    instanceIndexById.clear();
    pendingDestroys.clear();
    scriptAssets.clear();

    lua = sol::state {};

    spdlog::info("Lua scripting shutdown");
}

const std::vector<ScriptPublicField>* LuaScriptSystem::GetPublicFieldsForScript(const std::string& fileName) {
    const std::string assetId = NormalizeScriptId(fileName);
    if (assetId.empty()) return nullptr;

    const fs::path path = GetScriptPathFromId(assetId);
    if (!fs::exists(path)) return nullptr;

    ScriptAsset& asset = LoadOrRefreshScriptAsset(assetId, path);
    return &asset.publicFields;
}

bool LuaScriptSystem::ReconcileScriptPublicValues(ComponentScript& script) {
    return ReconcileScriptPublicValues(script, DescribeOwner(ScriptOwnerKind::Entity, script.ownerID));
}

bool LuaScriptSystem::ReconcileScriptPublicValues(ScriptAttachmentData& script, const std::string& ownerLabel) {
    const std::string assetId = NormalizeScriptId(script.fileName);
    if (assetId.empty()) return false;

    const fs::path path = GetScriptPathFromId(assetId);
    if (!fs::exists(path)) return false;

    ScriptAsset& asset = LoadOrRefreshScriptAsset(assetId, path);
    ReconcilePublicValues(script, asset, ownerLabel);

    return true;
}

void LuaScriptSystem::RefreshScriptAssets(Level& level) {
    scriptAssets.clear();

    for (ComponentScript& script : level.scripts.components) ReconcileScriptPublicValues(script);

    for (Sector& sector : level.sectors)
        for (SectorScript& script : sector.scripts)
            ReconcileScriptPublicValues(script, DescribeOwner(ScriptOwnerKind::Sector, sector.id));
}

const std::string* LuaScriptSystem::GetScriptLoadError(const std::string& fileName) {
    const std::string assetId = NormalizeScriptId(fileName);
    if (assetId.empty()) return nullptr;

    const fs::path path = GetScriptPathFromId(assetId);
    if (!fs::exists(path)) return nullptr;

    ScriptAsset& asset = LoadOrRefreshScriptAsset(assetId, path);

    // Compile only - loading never runs the chunk - and only once per
    // on-disk revision, so polling this from the inspector every frame is
    // cheap.
    if (!asset.loadErrorChecked) {
        asset.loadErrorChecked = true;

        asset.loadError = LoadScriptChunk(asset).error;
    }

    return asset.loadError.empty() ? nullptr : &asset.loadError;
}
