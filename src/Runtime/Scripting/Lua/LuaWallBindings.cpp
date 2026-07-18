//
// Created by berke on 7/6/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "sol/sol.hpp"

void LuaScriptSystem::RegisterWallBindings(sol::state& lua) {
    lua.new_usertype<ScriptWall>(
        "WallRef",

        "id", sol::readonly_property(
            &ScriptWall::GetID
        ),

        "isValid", sol::readonly_property(
            &ScriptWall::IsValid
        ),

        "start", sol::readonly_property(
            &ScriptWall::GetStart
        ),

        "end", sol::readonly_property(
            &ScriptWall::GetEnd
        ),

        "frontSector", sol::readonly_property(
            &ScriptWall::GetFrontSector
        ),

        "backSector", sol::readonly_property(
            &ScriptWall::GetBackSector
        ),

        "dir", sol::readonly_property(
            &ScriptWall::GetDir
        ),

        "normal", sol::readonly_property(
            &ScriptWall::GetNormal
        ),

        "length", sol::readonly_property(
            &ScriptWall::GetLength
        ),

        "color", sol::property(
            &ScriptWall::GetColor,
            &ScriptWall::SetColor
        ),

        "textureOffset", sol::property(
            &ScriptWall::GetTextureOffset,
            &ScriptWall::SetTextureOffset
        ),

        "textureFileName", sol::property(
            &ScriptWall::GetTextureFileName,
            &ScriptWall::SetTextureFileName
        ),

        "clearTextureFileName",
        &ScriptWall::ClearTextureFileName
    );
}
