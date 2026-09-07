//
// Created by berke on 7/6/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

namespace {
    using namespace LuaBindingMetadata;

    void RegisterWallMetadata() {
        RegisterType(Type("WallRef", "A safe reference to one map wall.", {
            Prop("id", "integer", true),
            Prop("isValid", "boolean", true),
            Prop("start", "Vector2", true),
            Prop("end", "Vector2", true),
            Prop("frontSector", "integer", true),
            Prop("backSector", "integer", true),
            Prop("dir", "Vector2", true),
            Prop("normal", "Vector2", true),
            Prop("length", "number", true),
            Prop("color", "Vector4"),
            Prop("textureOffset", "Vector2"),
            Prop("textureFileName", "string"),
        }, {
            Method("clearTextureFileName"),
        }));
    }
}

void LuaScriptSystem::RegisterWallBindings(sol::state& lua) {
    RegisterWallMetadata();

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
