//
// Created by berke on 6/22/2026.
//

#include <sol/state.hpp>

#include "Headers/Editor/Editor.hpp"
#include "Headers/Runtime/Scripting/ScriptSystem.hpp"

void ScriptSystem::RegisterGameBindings(sol::state &lua) {
    sol::table game;

    if (lua["Game"].valid()) game = lua["Game"];
    else game = lua.create_named_table("Game");

    game.set_function("LoadLevel", [](const std::string& levelName)->void {
       Editor::LoadLevel(levelName);
    });
}