//
// Created by berke on 5/15/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/ScriptPublicType.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
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
// Every Lua script attached to a GameObject (ComponentScript) runs in its own
// sol::environment - a Behaviour instance - sharing one sol::state. A script
// file's PUBLIC FIELDS are no longer declared through Public.Float/Int/Bool/
// String(...) calls: they are plain top-level Lua variables, and their
// schema (name/type/default/display name) is parsed directly out of the
// script's `---@field` doc comments *without ever executing the script* -
// see ExtractSchema. The same annotation syntax is what LuaLS already
// understands, so this schema and future editor IDE hovers/autocomplete
// share one source of truth (see the "LuaLS metadata" notes near the bottom
// of this file / LuaBindingMetadata.hpp).
//
// Example script:
//
//   ---@field maxHealth number
//   maxHealth = 100
//
//   ---@field target GameObject
//   target = nil
//
//   function Start()
//       print(gameObject.name .. " has " .. maxHealth .. " HP")
//   end
//
// Lifecycle: Start, Update, FixedUpdate, OnEnable, OnDisable, OnDestroy.
// ============================================================================

namespace {
    namespace fs = std::filesystem;

    struct ScriptAsset {
        std::string assetId; // full relative path, no extension, posix separators - see NormalizeScriptId
        fs::path path;

        std::vector<ScriptPublicField> publicFields;

        fs::file_time_type lastWriteTime {};
        std::uint64_t schemaHash = 0;
    };

    struct ScriptInstance {
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

        bool started = false;   // Start() has run at least once
        bool enabled = false;   // last computed effective-enabled state (script.enabled && owner.enabled)
        bool destroyed = false; // OnDestroy has already fired - guards against double teardown
    };

    struct ScriptGameTime {};

    sol::state lua;

    std::vector<ScriptInstance> scriptInstances;
    std::unordered_map<std::string, ScriptAsset> scriptAssets; // keyed by assetId
    std::unordered_map<ScriptInstanceID, std::size_t> instanceIndexById;

    // GameObject:Destroy() queues here; flushed once per Update() after every
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
    // A script's identity (ComponentScript::fileName) is a project-relative
    // path under Assets/Scripts, without extension, using forward slashes -
    // exactly what AssetBrowser::ToAssetReference(kind=Script) already
    // produces. NormalizeScriptId only defends against callers that pass a
    // raw OS path, backslashes, or a trailing ".lua" - it must NEVER reduce
    // the value to just its filename stem, which was the previous bug that
    // made "Scripts/Player/Health.lua" and "Scripts/Enemies/Health.lua"
    // collide into the same identity ("Health").
    // ------------------------------------------------------------------

    std::string NormalizeScriptId(const std::string& rawId) {
        if (rawId.empty()) return "";

        fs::path p(rawId);
        p.replace_extension();
        return p.generic_string();
    }

    fs::path GetScriptPathFromId(const std::string& assetId) {
        return ProjectManager::GetScriptsPath() / (assetId + ".lua");
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
            instanceIndexById[scriptInstances[i].instanceID] = i;
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

    // Red used for script errors in the in-game console (see ReportScriptError
    // below) - the same 0-255 scale EditorFunctions::Print's other callers
    // already use (see DEFAULT_COLOR in EditorFunctions.cpp).
    const Vector3 kScriptErrorColor {255.0f, 60.0f, 60.0f};

    // Every Lua script error goes through here: logged via spdlog (for the
    // engine's own logs/console window) AND pushed to the in-game console
    // in red (EditorFunctions::Print), so a broken script is visible to
    // whoever is playtesting, not just whoever is watching the log file.
    void ReportScriptError(const std::string& message) {
        spdlog::error("{}", message);
        EditorFunctions::Print(message, kScriptErrorColor);
    }

    void CallLifecycle(const ScriptInstance& instance, const sol::protected_function& fn, const char* stageName) {
        if (!fn.valid()) return;

        const sol::protected_function_result result = fn();

        if (!result.valid()) {
            const sol::error error = result;

            ReportScriptError(fmt::format(
                "Lua {} error in script '{}' on entity {} (instance {}): {}",
                stageName,
                instance.scriptId,
                instance.ownerID,
                instance.instanceID,
                error.what()
            ));
        }
    }

    void CallDestroy(ScriptInstance& instance) {
        if (instance.destroyed) return;
        instance.destroyed = true;

        // Never activated (e.g. the script errored during load, or the
        // GameObject/script was disabled for its entire lifetime) - nothing
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
            case ScriptValueType::GameObject: return "GameObject";
            case ScriptValueType::Component:  return "Component";
            case ScriptValueType::Behaviour:  return "Behaviour";
            case ScriptValueType::Asset:      return "Asset";
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
            case ScriptValueType::GameObject: return std::holds_alternative<GameObjectRefValue>(value);
            case ScriptValueType::Component:  return std::holds_alternative<ComponentRefValue>(value);
            case ScriptValueType::Behaviour:  return std::holds_alternative<BehaviourRefValue>(value);
            case ScriptValueType::Asset:      return std::holds_alternative<AssetRefValue>(value);
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

                if constexpr (std::is_same_v<T, int>) {
                    HashCombine(seed, std::hash<int>{}(typedValue));
                } else if constexpr (std::is_same_v<T, float>) {
                    HashCombine(seed, std::hash<float>{}(typedValue));
                } else if constexpr (std::is_same_v<T, bool>) {
                    HashCombine(seed, std::hash<bool>{}(typedValue));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    HashCombine(seed, std::hash<std::string>{}(typedValue));
                } else if constexpr (std::is_same_v<T, Vector2>) {
                    HashCombine(seed, std::hash<float>{}(typedValue.x));
                    HashCombine(seed, std::hash<float>{}(typedValue.y));
                } else if constexpr (std::is_same_v<T, Vector3>) {
                    HashCombine(seed, std::hash<float>{}(typedValue.x));
                    HashCombine(seed, std::hash<float>{}(typedValue.y));
                    HashCombine(seed, std::hash<float>{}(typedValue.z));
                } else if constexpr (std::is_same_v<T, Vector4>) {
                    HashCombine(seed, std::hash<float>{}(typedValue.x));
                    HashCombine(seed, std::hash<float>{}(typedValue.y));
                    HashCombine(seed, std::hash<float>{}(typedValue.z));
                    HashCombine(seed, std::hash<float>{}(typedValue.w));
                } else if constexpr (std::is_same_v<T, GameObjectRefValue>) {
                    HashCombine(seed, std::hash<ID>{}(typedValue.entityId));
                } else if constexpr (std::is_same_v<T, ComponentRefValue>) {
                    HashCombine(seed, std::hash<ID>{}(typedValue.entityId));
                    HashCombine(seed, std::hash<int>{}(typedValue.componentType));
                } else if constexpr (std::is_same_v<T, BehaviourRefValue>) {
                    HashCombine(seed, std::hash<ID>{}(typedValue.entityId));
                    HashCombine(seed, std::hash<std::uint64_t>{}(typedValue.instanceId));
                } else if constexpr (std::is_same_v<T, AssetRefValue>) {
                    HashCombine(seed, std::hash<std::string>{}(typedValue.path));
                }
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
            HashScriptValue(hash, field.defaultValue);

            for (const ScriptEnumOption& option : field.enumOptions) {
                HashCombine(hash, std::hash<std::string>{}(option.name));
                HashCombine(hash, std::hash<int>{}(option.value));
            }
        }

        return hash;
    }

    // ------------------------------------------------------------------
    // Schema extraction (text-only - never executes the script)
    // ------------------------------------------------------------------

    std::string TrimCopy(std::string s) {
        const auto notSpace = [](const unsigned char c) { return std::isspace(c) == 0; };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    bool IsBlankOrPlainComment(const std::string& trimmed) {
        if (trimmed.empty()) return true;
        // A `--` comment that is NOT a `---@field` annotation is skipped
        // over when looking for a field's default-value line.
        return trimmed.rfind("--", 0) == 0 && trimmed.rfind("---@field", 0) != 0;
    }

    std::vector<std::string> SplitCommaList(const std::string& text) {
        std::vector<std::string> parts;
        std::string current;

        for (const char c : text) {
            if (c == ',') {
                parts.push_back(TrimCopy(current));
                current.clear();
            } else {
                current += c;
            }
        }

        if (!current.empty() || !parts.empty()) parts.push_back(TrimCopy(current));

        return parts;
    }

    // Friendly type names a `---@field` annotation can use for a component
    // reference, mapped to the ComponentType (Components.hpp) they mean -
    // matching the usertype names LuaComponentBindings.cpp already registers.
    const std::unordered_map<std::string, int>& ComponentAnnotationTable() {
        static const std::unordered_map<std::string, int> table = {
            {"Transform",        CMP_TRANSFORM},
            {"Sprite",           CMP_SPRITE},
            {"AudioSource",      CMP_AUDIO_SOURCE},
            {"PlayerController", CMP_PLAYER_CONTROLLER},
            {"Camera",           CMP_CAMERA},
            {"Collider",         CMP_COLLIDER},
            {"Rigidbody",        CMP_RIGIDBODY},
        };

        return table;
    }

    bool IsReservedFieldName(const std::string& name) {
        static const std::unordered_set<std::string> reserved = {
            "Start", "Update", "FixedUpdate", "OnEnable", "OnDisable", "OnDestroy",
            "gameObject", "Scripts", "GameTime", "Input", "Game", "Debug"
        };

        return reserved.contains(name);
    }

    // Parses the (already-trimmed) right-hand side of a `<name> = <rhs>`
    // default-value line into a ScriptValue of the requested type. Plain
    // text parsing only - schema extraction never runs Lua.
    ScriptValue ParseDefaultLiteral(
        const std::string& rawRhs,
        const ScriptValueType type,
        const std::vector<ScriptEnumOption>& enumOptions
    ) {
        const std::string rhs = TrimCopy(rawRhs);

        switch (type) {
            case ScriptValueType::Int: {
                try { return ScriptValue{std::stoi(rhs)}; }
                catch (...) { return ScriptValue{0}; }
            }

            case ScriptValueType::Float: {
                try { return ScriptValue{std::stof(rhs)}; }
                catch (...) { return ScriptValue{0.0f}; }
            }

            case ScriptValueType::Bool:
                return ScriptValue{rhs == "true"};

            case ScriptValueType::String: {
                if (rhs.size() >= 2 &&
                    (rhs.front() == '"' || rhs.front() == '\'') &&
                    rhs.back() == rhs.front()) {
                    return ScriptValue{rhs.substr(1, rhs.size() - 2)};
                }

                if (rhs.empty() || rhs == "nil") return ScriptValue{std::string{}};

                return ScriptValue{rhs};
            }

            case ScriptValueType::Vector2:
            case ScriptValueType::Vector3:
            case ScriptValueType::Vector4: {
                const std::size_t open = rhs.find_first_of("({");
                const std::size_t close = rhs.find_last_of(")}");

                std::vector<float> components;

                if (open != std::string::npos && close != std::string::npos && close > open) {
                    for (const std::string& part : SplitCommaList(rhs.substr(open + 1, close - open - 1))) {
                        try { components.push_back(std::stof(part)); }
                        catch (...) { components.push_back(0.0f); }
                    }
                }

                const std::size_t wanted = type == ScriptValueType::Vector2 ? 2 : type == ScriptValueType::Vector3 ? 3 : 4;
                components.resize(wanted, 0.0f);

                if (type == ScriptValueType::Vector2) return ScriptValue{Vector2{components[0], components[1]}};
                if (type == ScriptValueType::Vector3) return ScriptValue{Vector3{components[0], components[1], components[2]}};
                return ScriptValue{Vector4{components[0], components[1], components[2], components[3]}};
            }

            case ScriptValueType::Enum: {
                if (!enumOptions.empty()) {
                    for (const ScriptEnumOption& option : enumOptions)
                        if (option.name == rhs) return ScriptValue{option.value};

                    try { return ScriptValue{std::stoi(rhs)}; }
                    catch (...) {}

                    return ScriptValue{enumOptions.front().value};
                }

                return ScriptValue{0};
            }

            case ScriptValueType::GameObject: return ScriptValue{GameObjectRefValue{}};
            case ScriptValueType::Component:  return ScriptValue{ComponentRefValue{}};
            case ScriptValueType::Behaviour:  return ScriptValue{BehaviourRefValue{}};
            case ScriptValueType::Asset:      return ScriptValue{AssetRefValue{}};
        }

        return ScriptValue{0};
    }

    struct ParsedAnnotation {
        std::string name;
        ScriptValueType type {};
        std::vector<ScriptEnumOption> enumOptions;
        int componentType = -1;
        std::string displayName;
    };

    // Parses one `---@field name Type[(args)] [@ Display Name]` line. This is
    // standard LuaDoc/LuaLS syntax plus one small, backwards-compatible
    // extension: `enum(OptionA,OptionB,...)` as a type name for enum fields.
    // Returns std::nullopt (after logging why) for anything unrecognized or
    // malformed - one bad annotation only skips that field, not the script.
    std::optional<ParsedAnnotation> ParseFieldAnnotation(const std::string& line, const std::string& scriptId) {
        static const std::regex pattern(
            R"(^\s*---@field\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)(\[\])?(?:\(([^)]*)\))?\s*(?:@\s*(.*?))?\s*$)"
        );

        std::smatch match;
        if (!std::regex_match(line, match, pattern)) return std::nullopt;

        ParsedAnnotation result;
        result.name = match[1].str();
        const std::string typeName = match[2].str();
        const bool isArray = match[3].matched;
        const std::string args = match[4].str();
        result.displayName = match[5].matched && !match[5].str().empty() ? match[5].str() : result.name;

        if (isArray) {
            spdlog::warn(
                "Lua script '{}' field '{}' uses an array type ('{}[]') - list/array fields are not supported yet, skipping",
                scriptId, result.name, typeName
            );

            return std::nullopt;
        }

        if (typeName == "int" || typeName == "integer") result.type = ScriptValueType::Int;
        else if (typeName == "number" || typeName == "float") result.type = ScriptValueType::Float;
        else if (typeName == "bool" || typeName == "boolean") result.type = ScriptValueType::Bool;
        else if (typeName == "string") result.type = ScriptValueType::String;
        else if (typeName == "Vector2") result.type = ScriptValueType::Vector2;
        else if (typeName == "Vector3") result.type = ScriptValueType::Vector3;
        else if (typeName == "Vector4") result.type = ScriptValueType::Vector4;
        else if (typeName == "GameObject") result.type = ScriptValueType::GameObject;
        else if (typeName == "Behaviour" || typeName == "Script") result.type = ScriptValueType::Behaviour;
        else if (typeName == "Asset" || typeName == "Texture") result.type = ScriptValueType::Asset;
        else if (typeName == "enum") {
            result.type = ScriptValueType::Enum;

            int nextValue = 0;
            for (const std::string& optionName : SplitCommaList(args))
                if (!optionName.empty()) result.enumOptions.push_back({optionName, nextValue++});

            if (result.enumOptions.empty()) {
                spdlog::warn("Lua script '{}' field '{}' is enum() with no options, skipping", scriptId, result.name);
                return std::nullopt;
            }
        }
        else if (const auto componentIt = ComponentAnnotationTable().find(typeName); componentIt != ComponentAnnotationTable().end()) {
            result.type = ScriptValueType::Component;
            result.componentType = componentIt->second;
        }
        else {
            spdlog::warn("Lua script '{}' field '{}' has unrecognized type '{}', skipping", scriptId, result.name, typeName);
            return std::nullopt;
        }

        return result;
    }

    // Reads the script's source text and builds its schema purely from
    // `---@field` annotations - the script is never loaded into Lua or
    // executed for this. Each field's default-value line is expected
    // immediately below its annotation (blank lines and other comments are
    // skipped over) as `<name> = <literal>`.
    std::vector<ScriptPublicField> ExtractSchema(const std::string& scriptId, const fs::path& path) {
        std::vector<ScriptPublicField> fields;
        std::unordered_set<std::string> seenNames;

        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::error("Failed to open Lua script for schema extraction '{}'", path.string());
            return fields;
        }

        std::vector<std::string> lines;
        for (std::string line; std::getline(file, line);) lines.push_back(line);

        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::optional<ParsedAnnotation> annotation = ParseFieldAnnotation(lines[i], scriptId);
            if (!annotation.has_value()) continue;

            if (IsReservedFieldName(annotation->name)) {
                spdlog::warn("Lua script '{}' declares reserved field name '{}', skipping", scriptId, annotation->name);
                continue;
            }

            if (!seenNames.insert(annotation->name).second) {
                spdlog::warn("Lua script '{}' declares duplicate field '{}', skipping", scriptId, annotation->name);
                continue;
            }

            std::string rhs;
            bool foundAssignment = false;

            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                const std::string trimmed = TrimCopy(lines[j]);
                if (IsBlankOrPlainComment(trimmed)) continue;

                const std::regex assignPattern("^" + annotation->name + R"(\s*=\s*(.+?)\s*(?:--.*)?$)");
                std::smatch assignMatch;

                if (std::regex_match(trimmed, assignMatch, assignPattern)) {
                    rhs = assignMatch[1].str();
                    foundAssignment = true;
                }

                break;
            }

            if (!foundAssignment) {
                spdlog::warn(
                    "Lua script '{}' field '{}' has no '{} = <value>' assignment right after its ---@field comment, using a zero default",
                    scriptId, annotation->name, annotation->name
                );
            }

            ScriptPublicField field;
            field.name = annotation->name;
            field.type = annotation->type;
            field.displayName = annotation->displayName;
            field.enumOptions = annotation->enumOptions;
            field.componentType = annotation->componentType;
            field.defaultValue = ParseDefaultLiteral(rhs, annotation->type, annotation->enumOptions);

            fields.push_back(std::move(field));
        }

        return fields;
    }

    ScriptAsset& LoadOrRefreshScriptAsset(const std::string& assetId, const fs::path& path) {
        const fs::file_time_type lastWriteTime = fs::last_write_time(path);

        const auto it = scriptAssets.find(assetId);

        if (it != scriptAssets.end() && it->second.lastWriteTime == lastWriteTime) return it->second;

        ScriptAsset asset;
        asset.assetId = assetId;
        asset.path = path;
        asset.lastWriteTime = lastWriteTime;
        asset.publicFields = ExtractSchema(assetId, path);
        asset.schemaHash = HashPublicFields(asset.publicFields);

        scriptAssets[assetId] = std::move(asset);

        return scriptAssets[assetId];
    }

    void ReconcilePublicValues(ComponentScript& script, const ScriptAsset& asset) {
        for (const ScriptPublicField& field : asset.publicFields) {
            const auto valueIt = script.publicValues.find(field.name);

            if (valueIt == script.publicValues.end()) {
                script.publicValues[field.name] = field.defaultValue;
                continue;
            }

            if (!IsScriptValueTypeValid(valueIt->second, field.type)) {
                spdlog::warn(
                    "Public field '{}.{}' on entity {} had wrong type. Expected {}. Resetting to default.",
                    script.fileName,
                    field.name,
                    script.ownerID,
                    ScriptValueTypeToString(field.type)
                );

                valueIt->second = field.defaultValue;
            }
        }

        script.schemaHash = asset.schemaHash;

        // Orphaned values (fields no longer declared by the script) are left
        // alone here - the editor inspector shows them and lets the user
        // remove them manually.
    }

    // ------------------------------------------------------------------
    // Reference resolution: serialized ScriptValue -> live Lua object
    // ------------------------------------------------------------------

    sol::object ResolveComponentRef(const sol::state_view luaView, Level& level, const ComponentRefValue& ref) {
        if (ref.entityId == INVALID_ID) return sol::make_object(luaView, sol::nil);

        switch (ref.componentType) {
            case CMP_TRANSFORM:
                if (!level.transforms.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptTransform{&level, ref.entityId});

            case CMP_SPRITE:
                if (!level.sprites.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptSprite{&level, ref.entityId});

            case CMP_AUDIO_SOURCE:
                if (!level.audioSources.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptAudioSource{&level, ref.entityId});

            case CMP_PLAYER_CONTROLLER:
                if (!level.playerControllers.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptPlayerController{&level, ref.entityId});

            case CMP_CAMERA:
                if (!level.cameras.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptCamera{&level, ref.entityId});

            case CMP_COLLIDER:
                if (!level.colliders.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptCollider{&level, ref.entityId});

            case CMP_RIGIDBODY:
                if (!level.rigidbodies.Has(ref.entityId)) break;
                return sol::make_object(luaView, ScriptRigidbody{&level, ref.entityId});

            default: break;
        }

        return sol::make_object(luaView, sol::nil);
    }

    sol::object ResolveScriptValueImpl(const sol::state_view luaView, Level& level, const ScriptValue& value) {
        return std::visit(
            [&](const auto& typedValue) -> sol::object {
                using T = std::decay_t<decltype(typedValue)>;

                if constexpr (std::is_same_v<T, GameObjectRefValue>) {
                    if (typedValue.entityId == INVALID_ID || level.GetEntity(typedValue.entityId) == nullptr)
                        return sol::make_object(luaView, sol::nil);

                    return sol::make_object(luaView, ScriptEntity{&level, typedValue.entityId});
                }
                else if constexpr (std::is_same_v<T, ComponentRefValue>) {
                    return ResolveComponentRef(luaView, level, typedValue);
                }
                else if constexpr (std::is_same_v<T, BehaviourRefValue>) {
                    if (typedValue.entityId == INVALID_ID || typedValue.instanceId == INVALID_SCRIPT_INSTANCE_ID)
                        return sol::make_object(luaView, sol::nil);

                    if (!LuaScriptRuntime::IsInstanceValid(typedValue.entityId, typedValue.instanceId))
                        return sol::make_object(luaView, sol::nil);

                    return sol::make_object(luaView, ScriptBehaviourRef{&level, typedValue.entityId, typedValue.instanceId});
                }
                else if constexpr (std::is_same_v<T, AssetRefValue>) {
                    return sol::make_object(luaView, typedValue.path);
                }
                else {
                    return sol::make_object(luaView, typedValue);
                }
            },
            value
        );
    }

    // ------------------------------------------------------------------
    // Instance load / lifecycle
    // ------------------------------------------------------------------

    bool LoadScriptIntoInstance(
        Level& level,
        ComponentScript& script,
        const std::string& assetId,
        const fs::path& path,
        ScriptInstance& instance
    ) {
        instance.ownerID = script.ownerID;
        instance.instanceID = script.instanceID;
        instance.scriptId = assetId;
        instance.started = false;
        instance.enabled = false;
        instance.destroyed = false;

        instance.environment = sol::environment(lua, sol::create, lua.globals());

        const ScriptEntity ownerGameObject {&level, script.ownerID};

        instance.environment["gameObject"] = ownerGameObject;
        instance.environment["Scripts"] = lua["Scripts"];

        const sol::load_result loadedScript = lua.load_file(path.string());

        if (!loadedScript.valid()) {
            const sol::error error = loadedScript;
            ReportScriptError(fmt::format("Failed to load Lua script '{}': {}", path.string(), error.what()));
            return false;
        }

        sol::protected_function scriptFunction = loadedScript;
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
            ReportScriptError(fmt::format("Failed to run Lua script '{}': {}", path.string(), error.what()));
            return false;
        }

        const auto assetIt = scriptAssets.find(assetId);

        if (assetIt != scriptAssets.end()) {
            for (const ScriptPublicField& field : assetIt->second.publicFields) {
                const auto valueIt = script.publicValues.find(field.name);
                if (valueIt == script.publicValues.end()) continue;

                instance.environment[field.name] = ResolveScriptValueImpl(lua, level, valueIt->second);
            }
        }

        instance.startFunction       = GetOptionalScriptFunction(instance.environment, "Start", assetId);
        instance.updateFunction      = GetOptionalScriptFunction(instance.environment, "Update", assetId);
        instance.fixedUpdateFunction = GetOptionalScriptFunction(instance.environment, "FixedUpdate", assetId);
        instance.onEnableFunction    = GetOptionalScriptFunction(instance.environment, "OnEnable", assetId);
        instance.onDisableFunction   = GetOptionalScriptFunction(instance.environment, "OnDisable", assetId);
        instance.onDestroyFunction   = GetOptionalScriptFunction(instance.environment, "OnDestroy", assetId);

        return true;
    }

    bool EffectiveEnabled(Level& level, const ScriptInstance& instance, const ComponentScript* script) {
        if (script == nullptr) return false;
        const Entity* owner = level.GetEntity(instance.ownerID);
        return script->enabled && owner != nullptr && owner->enabled;
    }

    // Flushes GameObject:Destroy() requests queued this frame, AND drops any
    // instance already marked destroyed (e.g. Update() found its
    // ComponentScript had been removed directly, outside GameObject:Destroy)
    // from the registry. Always safe to call even with nothing queued.
    void ProcessPendingDestroys(Level& level) {
        for (const ID entityId : pendingDestroys) {
            for (ScriptInstance& instance : scriptInstances) {
                if (instance.ownerID != entityId) continue;
                CallDestroy(instance);
            }

            level.DestroyEntity(entityId);
        }

        pendingDestroys.clear();

        if (std::ranges::any_of(scriptInstances, [](const ScriptInstance& instance) { return instance.destroyed; })) {
            std::erase_if(scriptInstances, [](const ScriptInstance& instance) { return instance.destroyed; });
            RebuildInstanceIndex();
        }
    }

    void RegisterGameTimeMetadata() {
        LuaBindingMetadata::RegisterType(LuaBindingMetadata::Type("GameTime", "Global frame-timing table.", {
            LuaBindingMetadata::Prop("deltaTime", "number", true, "Seconds since the last Update()."),
            LuaBindingMetadata::Prop("fixedDeltaTime", "number", true, "The fixed step FixedUpdate() runs on."),
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
            sol::property([](const ScriptGameTime&) { return kFixedTimeStep; })
        );

        luaState["GameTime"] = ScriptGameTime {};
    }

    // Registers the "Behaviour" usertype (ScriptBehaviourRef). Only
    // __index/__newindex are bound - see the comment on
    // ScriptBehaviourRef::LuaGet/LuaSet in LuaWrappers.hpp for why isValid/
    // gameObject/enabled are handled inside those two functions instead of
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
                {.name = "isValid", .luaType = "boolean", .readOnly = true, .doc = "False once the target script/GameObject no longer exists."},
                {.name = "gameObject", .luaType = "GameObject", .readOnly = true, .doc = "The GameObject this script is attached to."},
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

        lua["Scripts"] = lua.create_table();

        // Best-effort: regenerate the LuaLS stub file every time scripting
        // initializes, so it never drifts from the metadata registered
        // above. Only covers GameObject/Behaviour today - see
        // LuaBindingMetadata.hpp's scope note. Failure here (e.g. no project
        // loaded yet) is non-fatal - it only affects editor autocomplete.
        if (ProjectManager::HasProject()) {
            const fs::path stubDirectory = ProjectManager::GetScriptsPath() / ".luals";
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

    // Shared table for cross-script utilities/state.
    lua["Scripts"] = lua.create_table();

    for (ComponentScript& script : level.scripts.components) {
        const std::string assetId = NormalizeScriptId(script.fileName);

        if (assetId.empty()) {
            spdlog::warn("Skipping script component with empty file name on entity {}", script.ownerID);
            continue;
        }

        const fs::path path = GetScriptPathFromId(assetId);

        if (!fs::exists(path)) {
            spdlog::error("Lua script does not exist: {}", path.string());
            continue;
        }

        ScriptAsset& asset = LoadOrRefreshScriptAsset(assetId, path);
        ReconcilePublicValues(script, asset);

        ScriptInstance instance;

        if (!LoadScriptIntoInstance(level, script, assetId, path, instance)) continue;

        const std::size_t index = scriptInstances.size();
        instanceIndexById[instance.instanceID] = index;
        scriptInstances.push_back(std::move(instance));
    }

    // First activation: OnEnable before Start, matching Unity's ordering on
    // an object's first activation.
    for (ScriptInstance& instance : scriptInstances) {
        const ComponentScript* script = level.scripts.GetByID(instance.instanceID);
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

        const ComponentScript* script = level.scripts.GetByID(instance.instanceID);

        // The ComponentScript disappeared out from under this instance
        // (removed directly rather than through GameObject:Destroy()) - tear
        // it down the same way a queued destroy would.
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
            } else {
                CallLifecycle(instance, instance.onDisableFunction, "OnDisable");
            }
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

void LuaScriptSystem::Stop(Level&) {
    for (ScriptInstance& instance : scriptInstances) CallDestroy(instance);
}

void LuaScriptSystem::FlushPendingDestroys(Level& level) {
    ProcessPendingDestroys(level);
}

void LuaScriptSystem::Shutdown() {
    scriptInstances.clear();
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
    const std::string assetId = NormalizeScriptId(script.fileName);
    if (assetId.empty()) return false;

    const fs::path path = GetScriptPathFromId(assetId);
    if (!fs::exists(path)) return false;

    ScriptAsset& asset = LoadOrRefreshScriptAsset(assetId, path);
    ReconcilePublicValues(script, asset);

    return true;
}

void LuaScriptSystem::RefreshScriptAssets(Level& level) {
    scriptAssets.clear();

    for (ComponentScript& script : level.scripts.components) ReconcileScriptPublicValues(script);
}
