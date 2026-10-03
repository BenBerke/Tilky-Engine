//
// Created by berke on 6/20/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include <sol/sol.hpp>

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Sound/AudioSystem.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace {
    constexpr const char* COMPONENT_TYPE =
        "Transform|Sprite|AudioSource|PlayerController|Camera|Collider|Rigidbody|Model|UITransform|UISprite|UIText";

    // One row per component Lua can add or remove. Its value in the Lua
    // `Component` table is its ComponentType. Script is left out: a script
    // needs a file, and its running instances belong to LuaScriptRuntime.
    struct LuaComponentKind {
        const char* name;
        ComponentType type;
        bool isUI;
        bool (*has)(Entity&);
        void (*add)(Level&, Entity&);
        bool (*remove)(Entity&);
        sol::object (*get)(const ScriptEntity&, sol::state_view);
    };

    template<typename T>
    bool HasComponentOf(Entity& entity) { return entity.HasComponent<T>(); }

    template<typename T>
    void AddComponentOf(Level&, Entity& entity) { entity.AddComponent<T>(); }

    template<typename T>
    bool RemoveComponentOf(Entity& entity) { return entity.RemoveComponent<T>(); }

    // The game is already running when a script adds one, so its sound
    // source has to be made here instead of in AudioSystem::Start.
    void AddAudioSource(Level& level, Entity& entity) {
        AudioSystem::StartSource(level, *entity.AddComponent<ComponentAudioSource>());
    }

    // Starts unticked: a script switches to it by setting isActive = true.
    void AddCamera(Level&, Entity& entity) {
        entity.AddComponent<ComponentCamera>()->isActive = false;
    }

    bool RemoveAudioSource(Entity& entity) {
        if (ComponentAudioSource* audio = entity.GetComponent<ComponentAudioSource>()) AudioSystem::DestroySource(*audio);
        return entity.RemoveComponent<ComponentAudioSource>();
    }

    template<auto Getter>
    sol::object GetComponentOf(const ScriptEntity& entity, const sol::state_view lua) {
        return sol::make_object(lua, (entity.*Getter)());
    }

    template<typename T, auto Getter>
    constexpr LuaComponentKind Kind(const char* name, const ComponentType type, const bool isUI) {
        return {name, type, isUI, &HasComponentOf<T>, &AddComponentOf<T>, &RemoveComponentOf<T>, &GetComponentOf<Getter>};
    }

    constexpr LuaComponentKind LUA_COMPONENT_KINDS[] = {
        Kind<ComponentTransform, &ScriptEntity::GetTransform>("Transform", CMP_TRANSFORM, false),
        Kind<ComponentSprite, &ScriptEntity::GetSprite>("Sprite", CMP_SPRITE, false),
        {"AudioSource", CMP_AUDIO_SOURCE, false, &HasComponentOf<ComponentAudioSource>, &AddAudioSource,
         &RemoveAudioSource, &GetComponentOf<&ScriptEntity::GetAudioSource>},
        Kind<ComponentPlayerController, &ScriptEntity::GetPlayerController>("PlayerController", CMP_PLAYER_CONTROLLER, false),
        {"Camera", CMP_CAMERA, false, &HasComponentOf<ComponentCamera>, &AddCamera,
         &RemoveComponentOf<ComponentCamera>, &GetComponentOf<&ScriptEntity::GetCamera>},
        Kind<ComponentCollider, &ScriptEntity::GetCollider>("Collider", CMP_COLLIDER, false),
        Kind<ComponentRigidbody, &ScriptEntity::GetRigidbody>("Rigidbody", CMP_RIGIDBODY, false),
        Kind<ComponentModel, &ScriptEntity::GetModel>("Model", CMP_MODEL, false),
        Kind<ComponentUITransform, &ScriptEntity::GetUITransform>("UITransform", CMP_UI_TRANSFORM, true),
        Kind<ComponentUISprite, &ScriptEntity::GetUISprite>("UISprite", CMP_UI_SPRITE, true),
        Kind<ComponentUIText, &ScriptEntity::GetUIText>("UIText", CMP_UI_TEXT, true),
    };

    // Takes a sol::object, not an int: sol would turn nil (a misspelled
    // Component.X) or a string into 0, which is Component.Transform.
    const LuaComponentKind& FindLuaComponentKind(const sol::object& value) {
        if (value.get_type() != sol::type::number)
            throw sol::error(std::string("Expected a Component value, e.g. Component.Sprite, got ") +
                             sol::type_name(value.lua_state(), value.get_type()));

        const int type = value.as<int>();

        static const std::unordered_map<int, const LuaComponentKind*> kindsByType = [] {
            std::unordered_map<int, const LuaComponentKind*> map;
            for (const LuaComponentKind& kind : LUA_COMPONENT_KINDS) map.emplace(kind.type, &kind);
            return map;
        }();

        const auto it = kindsByType.find(type);
        if (it == kindsByType.end())
            throw sol::error("Unknown component " + std::to_string(type) + " - use a value from the Component table, e.g. Component.Sprite");

        return *it->second;
    }

    void RegisterComponentMetadata() {
        std::vector<LuaBindingMetadata::EnumValueDoc> values;
        for (const LuaComponentKind& kind : LUA_COMPONENT_KINDS) values.push_back({kind.name, kind.type, {}});

        LuaBindingMetadata::RegisterType(LuaBindingMetadata::Enum(
            "Component", "Component types for Entity:AddComponent / Entity:RemoveComponent.", std::move(values)
        ));
    }

    // Registers Entity's documentation with LuaBindingMetadata (autocomplete
    // + LuaLS stub). Only the PascalCase method spellings are listed; the
    // camelCase aliases (getScript, hasTag, ...) are deliberately left out
    // so autocomplete doesn't suggest every method twice.
    void RegisterEntityMetadata() {
        LuaBindingMetadata::RegisterType({
            .name = "Entity",
            .doc = "Tilky's Entity facade - a safe handle to one entity. Never a raw entity ID.",
            .properties = {
                {.name = "id", .luaType = "integer", .readOnly = true, .doc = "Stable entity ID."},
                {.name = "isValid", .luaType = "boolean", .readOnly = true, .doc = "False once this Entity has been destroyed."},
                {.name = "name", .luaType = "string", .doc = "The Entity's display name."},
                {.name = "enabled", .luaType = "boolean", .doc = "Active state - disables every attached script's ticking when false."},
                {.name = "hasTransform", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a Transform."},
                {.name = "transform", .luaType = "Transform?", .readOnly = true, .doc = "nil if this Entity has no Transform."},
                {.name = "hasSprite", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a Sprite."},
                {.name = "sprite", .luaType = "Sprite?", .readOnly = true, .doc = "nil if this Entity has no Sprite."},
                {.name = "hasAudioSource", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has an AudioSource."},
                {.name = "audioSource", .luaType = "AudioSource?", .readOnly = true, .doc = "nil if this Entity has no AudioSource."},
                {.name = "hasPlayerController", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a PlayerController."},
                {.name = "playerController", .luaType = "PlayerController?", .readOnly = true, .doc = "nil if this Entity has no PlayerController."},
                {.name = "hasCamera", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a Camera."},
                {.name = "camera", .luaType = "Camera?", .readOnly = true, .doc = "nil if this Entity has no Camera."},
                {.name = "hasCollider", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a Collider."},
                {.name = "collider", .luaType = "Collider?", .readOnly = true, .doc = "nil if this Entity has no Collider."},
                {.name = "hasRigidbody", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a Rigidbody."},
                {.name = "rigidbody", .luaType = "Rigidbody?", .readOnly = true, .doc = "nil if this Entity has no Rigidbody."},
                {.name = "hasModel", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a Model."},
                {.name = "model", .luaType = "Model?", .readOnly = true, .doc = "nil if this Entity has no Model."},
                {.name = "hasUITransform", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a UITransform."},
                {.name = "uiTransform", .luaType = "UITransform?", .readOnly = true, .doc = "nil if this Entity has no UITransform."},
                {.name = "hasUISprite", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a UISprite."},
                {.name = "uiSprite", .luaType = "UISprite?", .readOnly = true, .doc = "nil if this Entity has no UISprite."},
                {.name = "hasUIText", .luaType = "boolean", .readOnly = true, .doc = "True if this Entity has a UIText."},
                {.name = "uiText", .luaType = "UIText?", .readOnly = true, .doc = "nil if this Entity has no UIText."},
                {.name = "hasScript", .luaType = "boolean", .readOnly = true, .doc = "True if any script is attached."},
                {.name = "tagCount", .luaType = "integer", .readOnly = true, .doc = "Tags are assigned from the editor only - there is no SetTag."},
            },
            .methods = {
                {.name = "Destroy", .params = {}, .returnType = "", .doc = "Queues this Entity for destruction at the end of the current frame."},
                {.name = "GetScript", .params = {{"name", "string"}}, .returnType = "Behaviour", .doc = "Looks up an attached script by name."},
                {.name = "GetScriptById", .params = {{"instanceId", "integer"}}, .returnType = "Behaviour", .doc = "Looks up an attached script by its unique instance id."},
                {.name = "GetScripts", .params = {}, .returnType = "Behaviour[]", .doc = "Every script attached to this Entity."},
                {.name = "HasScriptNamed", .params = {{"name", "string"}}, .returnType = "boolean", .doc = "True if a script matching `name` is attached."},
                {.name = "HasTag", .params = {{"tag", "string"}}, .returnType = "boolean", .doc = "True if this Entity has the given tag."},
                {.name = "GetTag", .params = {{"index", "integer"}}, .returnType = "string", .doc = "1-based. Tags are assigned in the editor - there is no SetTag."},
                {.name = "GetSector", .params = {}, .returnType = "Sector?", .doc = "The sector this Entity is standing in, or nil (outside the map, or no Transform)."},
                {.name = "AddComponent", .params = {{"component", "Component"}}, .returnType = COMPONENT_TYPE,
                 .doc = "Adds the component (e.g. Component.Sprite) and returns it. Returns the existing one if the Entity already has it."},
                {.name = "RemoveComponent", .params = {{"component", "Component"}}, .returnType = "boolean",
                 .doc = "Removes the component (e.g. Component.Collider). False if the Entity didn't have it."},
            }
        });
    }
}

// Registers ScriptEntity as the Lua-facing "Entity" type - Tilky's
// Entity facade. This never exposes raw entity IDs, component storages,
// or the owning Level - every accessor here is a null-checked {Level*, ID}
// handle, matching every other ScriptXxx wrapper in LuaWrappers.hpp.
void LuaScriptSystem::RegisterEntityBindings(sol::state& lua) {
    RegisterEntityMetadata();
    RegisterComponentMetadata();

    sol::table component = lua.create_named_table("Component");
    for (const LuaComponentKind& kind : LUA_COMPONENT_KINDS) component[kind.name] = static_cast<int>(kind.type);

    lua.new_usertype<ScriptEntity>(
        "Entity",

        "id",
        sol::property(&ScriptEntity::GetID),

        "isValid",
        sol::property(&ScriptEntity::IsValid),

        "name",
        sol::property(&ScriptEntity::GetName, &ScriptEntity::SetName),

        // Entity-level active state. See Entity::enabled - disabling a
        // Entity disables every attached script's ticking without
        // touching each script's own `enabled` flag.
        "enabled",
        sol::property(&ScriptEntity::GetEnabled, &ScriptEntity::SetEnabled),

        // Queues this Entity for destruction; the actual removal happens
        // once, after every script has finished running this frame.
        "Destroy",
        &ScriptEntity::Destroy,
        "destroy",
        &ScriptEntity::Destroy,

        "hasTransform",
        sol::property(&ScriptEntity::HasTransform),

        "transform",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasTransform()) return sol::nil;


                return sol::make_object(luaState, entity.GetTransform());
            }
        ),

        "hasSprite",
        sol::property(&ScriptEntity::HasSprite),

        "sprite",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasSprite()) return sol::nil;

                return sol::make_object(luaState, entity.GetSprite());
            }
        ),

        "hasAudioSource",
        sol::property(&ScriptEntity::HasAudioSource),

        "audioSource",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasAudioSource()) return sol::nil;

                return sol::make_object(luaState, entity.GetAudioSource());
            }
        ),

        // True if this Entity has ANY attached script. See GetScript /
        // HasScriptNamed below to look one up specifically.
        "hasScript",
        sol::property(&ScriptEntity::HasScript),

        // True if this Entity has an attached script matching `name`
        // (matches the final path segment of the script's asset id - see
        // ScriptEntity::HasScriptNamed).
        "HasScriptNamed",
        &ScriptEntity::HasScriptNamed,
        "hasScriptNamed",
        &ScriptEntity::HasScriptNamed,

        // Looks up an attached script (Behaviour) by name. Lua:
        //   local health = target:GetScript("Health")
        //   health.currentHealth = health.currentHealth - 25
        //   health:TakeDamage(25)
        // Calling a public function or reading/writing a public field on the
        // returned Behaviour needs no owner-ID or filename lookup - it is
        // forwarded straight into that script's own environment.
        "GetScript",
        &ScriptEntity::GetScript,
        "getScript",
        &ScriptEntity::GetScript,

        // Unambiguous lookup by the script's globally-unique instance id -
        // what a serialized Behaviour-reference field resolves through.
        "GetScriptById",
        &ScriptEntity::GetScriptById,
        "getScriptById",
        &ScriptEntity::GetScriptById,

        // Every script attached to this Entity, as Behaviour references.
        "GetScripts",
        &ScriptEntity::GetScripts,
        "getScripts",
        &ScriptEntity::GetScripts,

        "hasPlayerController",
        sol::property(&ScriptEntity::HasPlayerController),

        "playerController",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasPlayerController()) return sol::nil;

                return sol::make_object(luaState, entity.GetPlayerController());
            }
        ),

        "hasCamera",
        sol::property(&ScriptEntity::HasCamera),

        "camera",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasCamera()) return sol::nil;

                return sol::make_object(luaState, entity.GetCamera());
            }
        ),

        "hasCollider",
        sol::property(&ScriptEntity::HasCollider),

        "collider",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasCollider()) return sol::nil;

                return sol::make_object(luaState, entity.GetCollider());
            }
        ),

        "hasRigidbody",
        sol::property(&ScriptEntity::HasRigidbody),

        "rigidbody",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasRigidbody()) return sol::nil;

                return sol::make_object(luaState, entity.GetRigidbody());
            }
        ),

        "hasModel",
        sol::property(&ScriptEntity::HasModel),

        "model",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasModel()) return sol::nil;

                return sol::make_object(luaState, entity.GetModel());
            }
        ),

        "hasUITransform",
        sol::property(&ScriptEntity::HasUITransform),

        "uiTransform",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasUITransform()) return sol::nil;

                return sol::make_object(luaState, entity.GetUITransform());
            }
        ),

        "hasUISprite",
        sol::property(&ScriptEntity::HasUISprite),

        "uiSprite",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasUISprite()) return sol::nil;

                return sol::make_object(luaState, entity.GetUISprite());
            }
        ),

        "hasUIText",
        sol::property(&ScriptEntity::HasUIText),

        "uiText",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasUIText()) return sol::nil;

                return sol::make_object(luaState, entity.GetUIText());
            }
        ),

        // Tags are assigned only from the editor (Project Settings + the
        // Sector/Wall/Entity inspectors) - read-only here, no SetTag.
        "tagCount",
        sol::property(&ScriptEntity::GetTagCount),

        "HasTag",
        &ScriptEntity::HasTag,
        "hasTag",
        &ScriptEntity::HasTag,

        "GetTag",
        &ScriptEntity::GetTag,
        "getTag",
        &ScriptEntity::GetTag,

        // The sector whose entitiesInside lists this Entity - the same
        // membership Sector:ContainsEntity and OnEntityEnter use.
        "GetSector",
        [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
            const ComponentTransform* transform =
                entity.level != nullptr ? entity.level->transforms.Get(entity.ownerID) : nullptr;

            if (transform == nullptr || transform->sectorIndex < 0 ||
                transform->sectorIndex >= static_cast<int>(entity.level->sectors.size()))
                return sol::make_object(state, sol::nil);

            return sol::make_object(state, ScriptSector{
                entity.level, entity.level->sectors[transform->sectorIndex].id
            });
        },

        // World components can't go on a UI entity (one with a UITransform)
        // and UI components can't go on a world entity (one with a
        // Transform), same as in the editor.
        "AddComponent",
        [](const ScriptEntity& self, const sol::object& type, const sol::this_state state) -> sol::object {
            const LuaComponentKind& kind = FindLuaComponentKind(type);

            Entity* entity = self.GetEntity();
            if (entity == nullptr) return sol::make_object(state, sol::nil);

            if (!kind.has(*entity)) {
                if (kind.isUI && entity->HasComponent<ComponentTransform>())
                    throw sol::error(std::string("Can't add ") + kind.name + " to a world entity (it has a Transform)");

                if (!kind.isUI && entity->HasComponent<ComponentUITransform>())
                    throw sol::error(std::string("Can't add ") + kind.name + " to a UI entity (it has a UITransform)");

                kind.add(*self.level, *entity);
            }

            return kind.get(self, state);
        },

        "RemoveComponent",
        [](const ScriptEntity& self, const sol::object& type) -> bool {
            const LuaComponentKind& kind = FindLuaComponentKind(type);

            Entity* entity = self.GetEntity();
            if (entity == nullptr) return false;

            return kind.remove(*entity);
        }
    );
}
