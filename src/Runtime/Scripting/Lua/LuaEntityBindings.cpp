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
        // Adds a new one (Transform/UITransform: returns the existing one) and returns it.
        sol::object (*add)(Level&, Entity&, sol::state_view);
        // Removes every component of this type.
        bool (*removeAll)(Entity&);
        // If `component` is this kind's Lua object, removes that one component
        // and sets `matched`.
        bool (*removeOne)(Entity&, const sol::object& component, bool& matched);
        // The first one.
        sol::object (*get)(const ScriptEntity&, sol::state_view);
        // All of them, in order.
        sol::table (*getAll)(const ScriptEntity&, sol::state_view);
    };

    // Per-type extras when Lua adds or removes a component.
    template<typename T>
    void OnAdded(Level&, T&) {}

    // The game is already running when a script adds one, so its sound
    // source has to be made here instead of in AudioSystem::Start.
    void OnAdded(Level& level, ComponentAudioSource& audio) { AudioSystem::StartSource(level, audio); }

    // Starts unticked: a script switches to it by setting isActive = true.
    void OnAdded(Level&, ComponentCamera& camera) { camera.isActive = false; }

    template<typename T>
    void OnRemoving(T&) {}

    void OnRemoving(ComponentAudioSource& audio) { AudioSystem::DestroySource(audio); }

    // The Lua handle for one component. Transform/UITransform handles are
    // per entity; the others name an instance.
    template<typename T, typename Wrapper>
    Wrapper MakeWrapper(Level* level, const ID ownerID, const ComponentInstanceID instanceID) {
        if constexpr (IsSingleComponent<T>) return Wrapper{level, ownerID};
        else return Wrapper{level, ownerID, instanceID};
    }

    template<typename T, typename Wrapper>
    sol::object AddComponentOf(Level& level, Entity& entity, const sol::state_view lua) {
        T* component = entity.AddComponent<T>();
        OnAdded(level, *component);
        return sol::make_object(lua, MakeWrapper<T, Wrapper>(&level, entity.id, component->instanceID));
    }

    template<typename T>
    bool RemoveAllComponentsOf(Entity& entity) {
        for (T* component : entity.GetComponents<T>()) OnRemoving(*component);
        return entity.RemoveComponent<T>();
    }

    template<typename T, typename Wrapper>
    bool RemoveOneComponentOf(Entity& entity, const sol::object& object, bool& matched) {
        if (!object.is<Wrapper>()) return false;
        matched = true;

        const Wrapper wrapper = object.as<Wrapper>();
        if (wrapper.ownerID != entity.id) throw sol::error("RemoveComponent: that component belongs to another Entity");

        if constexpr (IsSingleComponent<T>) {
            return RemoveAllComponentsOf<T>(entity);
        } else {
            T* component = entity.GetComponentInstance<T>(wrapper.instanceID);
            if (component == nullptr) return false;
            OnRemoving(*component);
            return entity.RemoveComponentInstance<T>(wrapper.instanceID);
        }
    }

    template<auto Getter>
    sol::object GetComponentOf(const ScriptEntity& entity, const sol::state_view lua) {
        return sol::make_object(lua, (entity.*Getter)());
    }

    template<typename T, typename Wrapper>
    sol::table GetAllComponentsOf(const ScriptEntity& self, sol::state_view lua) {
        sol::table result = lua.create_table();
        Entity* entity = self.GetEntity();
        if (entity == nullptr) return result;

        int index = 1;
        for (const T* component : entity->GetComponents<T>())
            result[index++] = MakeWrapper<T, Wrapper>(self.level, entity->id, component->instanceID);

        return result;
    }

    template<typename T, typename Wrapper, auto Getter>
    constexpr LuaComponentKind Kind(const char* name, const ComponentType type, const bool isUI) {
        return {name, type, isUI, &AddComponentOf<T, Wrapper>, &RemoveAllComponentsOf<T>,
                &RemoveOneComponentOf<T, Wrapper>, &GetComponentOf<Getter>, &GetAllComponentsOf<T, Wrapper>};
    }

    constexpr LuaComponentKind LUA_COMPONENT_KINDS[] = {
        Kind<ComponentTransform, ScriptTransform, &ScriptEntity::GetTransform>("Transform", CMP_TRANSFORM, false),
        Kind<ComponentSprite, ScriptSprite, &ScriptEntity::GetSprite>("Sprite", CMP_SPRITE, false),
        Kind<ComponentAudioSource, ScriptAudioSource, &ScriptEntity::GetAudioSource>("AudioSource", CMP_AUDIO_SOURCE, false),
        Kind<ComponentPlayerController, ScriptPlayerController, &ScriptEntity::GetPlayerController>("PlayerController", CMP_PLAYER_CONTROLLER, false),
        Kind<ComponentCamera, ScriptCamera, &ScriptEntity::GetCamera>("Camera", CMP_CAMERA, false),
        Kind<ComponentCollider, ScriptCollider, &ScriptEntity::GetCollider>("Collider", CMP_COLLIDER, false),
        Kind<ComponentRigidbody, ScriptRigidbody, &ScriptEntity::GetRigidbody>("Rigidbody", CMP_RIGIDBODY, false),
        Kind<ComponentModel, ScriptModel, &ScriptEntity::GetModel>("Model", CMP_MODEL, false),
        Kind<ComponentUITransform, ScriptUITransform, &ScriptEntity::GetUITransform>("UITransform", CMP_UI_TRANSFORM, true),
        Kind<ComponentUISprite, ScriptUISprite, &ScriptEntity::GetUISprite>("UISprite", CMP_UI_SPRITE, true),
        Kind<ComponentUIText, ScriptUIText, &ScriptEntity::GetUIText>("UIText", CMP_UI_TEXT, true),
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
    // + LuaLS stub).
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
                 .doc = "Adds a new component (e.g. Component.Sprite) after any it already has, and returns it. An Entity has only one Transform/UITransform: for those it returns the existing one."},
                {.name = "GetComponents", .params = {{"component", "Component"}}, .returnType = "table",
                 .doc = "Every component of that type on this Entity, in order (an empty table if none). entity.sprite etc. are the first one."},
                {.name = "RemoveComponent", .params = {{"component", "Component|" + std::string(COMPONENT_TYPE)}}, .returnType = "boolean",
                 .doc = "With a type (e.g. Component.Collider), removes every component of that type. With a component (e.g. entity.collider), removes just that one. False if there was nothing to remove."},
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

        // Looks up an attached script (Behaviour) by name. Lua:
        //   local health = target:GetScript("Health")
        //   health.currentHealth = health.currentHealth - 25
        //   health:TakeDamage(25)
        // Calling a public function or reading/writing a public field on the
        // returned Behaviour needs no owner-ID or filename lookup - it is
        // forwarded straight into that script's own environment.
        "GetScript",
        &ScriptEntity::GetScript,

        // Unambiguous lookup by the script's globally-unique instance id -
        // what a serialized Behaviour-reference field resolves through.
        "GetScriptById",
        &ScriptEntity::GetScriptById,

        // Every script attached to this Entity, as Behaviour references.
        "GetScripts",
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

        "GetTag",
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

            if (kind.isUI && entity->HasComponent<ComponentTransform>())
                throw sol::error(std::string("Can't add ") + kind.name + " to a world entity (it has a Transform)");

            if (!kind.isUI && entity->HasComponent<ComponentUITransform>())
                throw sol::error(std::string("Can't add ") + kind.name + " to a UI entity (it has a UITransform)");

            return kind.add(*self.level, *entity, state);
        },

        "GetComponents",
        [](const ScriptEntity& self, const sol::object& type, const sol::this_state state) -> sol::table {
            return FindLuaComponentKind(type).getAll(self, state);
        },

        // A Component value removes every component of that type; a
        // component object removes just that one.
        "RemoveComponent",
        [](const ScriptEntity& self, const sol::object& typeOrComponent) -> bool {
            Entity* entity = self.GetEntity();

            if (typeOrComponent.get_type() == sol::type::number) {
                const LuaComponentKind& kind = FindLuaComponentKind(typeOrComponent);
                return entity != nullptr && kind.removeAll(*entity);
            }

            if (entity == nullptr) return false;

            for (const LuaComponentKind& kind : LUA_COMPONENT_KINDS) {
                bool matched = false;
                const bool removed = kind.removeOne(*entity, typeOrComponent, matched);
                if (matched) return removed;
            }

            throw sol::error(std::string("RemoveComponent expects a Component value (e.g. Component.Sprite) or a component, got ") +
                             sol::type_name(typeOrComponent.lua_state(), typeOrComponent.get_type()));
        }
    );
}
