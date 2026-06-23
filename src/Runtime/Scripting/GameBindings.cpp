//
// Created by berke on 6/22/2026.
//

#include <sol/state.hpp>

#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Objects/Wrappers.hpp"
#include "Headers/Runtime/Scripting/ScriptSystem.hpp"

void ScriptSystem::RegisterGameBindings(sol::state &lua) {
    sol::table game;

    if (lua["Game"].valid()) game = lua["Game"];
    else game = lua.create_named_table("Game");

    game.set_function("LoadLevel", [](const std::string& levelName)->void {
       Editor::LoadLevel(levelName);
    });

    // Example
    // game.set_function(
    //     "getFirstEntity",
    //     [](const sol::this_state state) -> sol::object {
    //         sol::state_view lua(state);
    //
    //         Level& level = LevelManager::CurrentLevel();
    //
    //         if (level.entities.empty()) {
    //             return sol::nil;
    //         }
    //
    //         const Entity& entity = level.entities.front();
    //
    //         return sol::make_object(
    //             lua,
    //             ScriptEntity{
    //                 .level = &level,
    //                 .ownerID = entity.id
    //             }
    //         );
    //     }
    //);
}