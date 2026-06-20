//
// Created by berke on 6/20/2026.
//

#include "Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "sol/sol.hpp"
#include "Headers/Objects/Wrappers.hpp"

void ScriptSystem::RegisterEntityBindings(sol::state& lua) {
    lua.new_usertype<ScriptEntity>(
            "Entities",

            "id", sol::property(&ScriptEntity::GetID),
            "isValid", sol::property(&ScriptEntity::IsValid),

            "hasTransform", sol::property(&ScriptEntity::HasTransform),
            "transform", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasTransform()) return sol::nil;
                    return sol::make_object(lua, entity.GetTransform());
                }
            ),

            "hasSprite", sol::property(&ScriptEntity::HasSprite),
            "sprite", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasSprite()) return sol::nil;
                    return sol::make_object(lua, entity.GetSprite());
                }
            ),

            "hasDecal", sol::property(&ScriptEntity::HasDecal),
            "decal", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasDecal()) return sol::nil;
                    return sol::make_object(lua, entity.GetDecal());
                }
            ),

            "hasAudioSource", sol::property(&ScriptEntity::HasAudioSource),
            "audioSource", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasAudioSource()) return sol::nil;
                    return sol::make_object(lua, entity.GetAudioSource());
                }
            ),

            "hasScript", sol::property(&ScriptEntity::HasScript),
            "script", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasScript()) return sol::nil;
                    return sol::make_object(lua, entity.GetScript());
                }
            ),

            "hasPlayerController", sol::property(&ScriptEntity::HasPlayerController),
            "playerController", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasPlayerController()) return sol::nil;
                    return sol::make_object(lua, entity.GetPlayerController());
                }
            ),

            "hasCamera", sol::property(&ScriptEntity::HasCamera),
            "camera", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasCamera()) return sol::nil;
                    return sol::make_object(lua, entity.GetCamera());
                }
            ),

            "hasCollider", sol::property(&ScriptEntity::HasCollider),
            "collider", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasCollider()) return sol::nil;
                    return sol::make_object(lua, entity.GetCollider());
                }
            ),

            "hasRigidbody", sol::property(&ScriptEntity::HasRigidbody),
            "rigidbody", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasRigidbody()) return sol::nil;
                    return sol::make_object(lua, entity.GetRigidbody());
                }
            ),

            "hasUITransform", sol::property(&ScriptEntity::HasUITransform),
            "uiTransform", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasUITransform()) return sol::nil;
                    return sol::make_object(lua, entity.GetUITransform());
                }
            ),

            "hasUISprite", sol::property(&ScriptEntity::HasUISprite),
            "uiSprite", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasUISprite()) return sol::nil;
                    return sol::make_object(lua, entity.GetUISprite());
                }
            ),

            "hasUIText", sol::property(&ScriptEntity::HasUIText),
            "uiText", sol::property(
                [](const ScriptEntity &entity, const sol::this_state state) -> sol::object {
                    const sol::state_view lua(state);
                    if (!entity.HasUIText()) return sol::nil;
                    return sol::make_object(lua, entity.GetUIText());
                }
            )
        );
}
