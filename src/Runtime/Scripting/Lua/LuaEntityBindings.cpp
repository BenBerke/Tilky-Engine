//
// Created by berke on 6/20/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include <sol/sol.hpp>

#include "Headers/Objects/LuaWrappers.hpp"

void LuaScriptSystem::RegisterEntityBindings(sol::state& lua) {
    lua.new_usertype<ScriptEntity>(
        "Entity",

        "id",
        sol::property(&ScriptEntity::GetID),

        "isValid",
        sol::property(&ScriptEntity::IsValid),

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

        "hasDecal",
        sol::property(&ScriptEntity::HasDecal),

        "decal",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasDecal()) return sol::nil;

                return sol::make_object(luaState, entity.GetDecal());
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

        "hasScript",
        sol::property(&ScriptEntity::HasScript),

        "script",
        sol::property(
            [](const ScriptEntity& entity, const sol::this_state state) -> sol::object {
                const sol::state_view luaState(state);

                if (!entity.HasScript()) return sol::nil;

                return sol::make_object(luaState, entity.GetScript());
            }
        ),

        // Access another script component's public variables.
        //
        // Lua:
        // local healthScript = target:GetScript("Health")
        // healthScript.Public.health = healthScript.Public.health - 25
        "GetScript",
        &ScriptEntity::GetScriptRef,

        "getScript",
        &ScriptEntity::GetScriptRef,

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
        )
    );
}