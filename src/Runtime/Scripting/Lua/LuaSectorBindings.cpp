//
// Created by berke on 7/6/2026.
//

#include <Headers/Objects/LuaWrappers.hpp>
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

void LuaScriptSystem::RegisterSectorBindings(sol::state& lua) {
    lua.new_usertype<ScriptSector>(
        "SectorRef",

        "id", sol::readonly_property(
            &ScriptSector::GetID
        ),

        "isValid", sol::readonly_property(
            &ScriptSector::IsValid
        ),

        "floorHeight", sol::property(
            &ScriptSector::GetFloorHeight,
            &ScriptSector::SetFloorHeight
        ),

        "ceilingHeight", sol::property(
            &ScriptSector::GetCeilingHeight,
            &ScriptSector::SetCeilingHeight
        ),

        "floorColor", sol::property(
            &ScriptSector::GetFloorColor,
            &ScriptSector::SetFloorColor
        ),

        "ceilingColor", sol::property(
            &ScriptSector::GetCeilingColor,
            &ScriptSector::SetCeilingColor
        ),

        "floorTexture", sol::property(
            &ScriptSector::GetFloorTexture,
            &ScriptSector::SetFloorTexture
        ),

        "ceilingTexture", sol::property(
            &ScriptSector::GetCeilingTexture,
            &ScriptSector::SetCeilingTexture
        ),

        "clearFloorTexture",
        &ScriptSector::ClearFloorTexture,

        "clearCeilingTexture",
        &ScriptSector::ClearCeilingTexture,

        "lightValue", sol::property(
            &ScriptSector::GetLightValue,
            &ScriptSector::SetLightValue
        ),

        "vertexCount", sol::readonly_property(
            &ScriptSector::GetVertexCount
        ),

        "wallCount", sol::readonly_property(
            &ScriptSector::GetWallCount
        ),

        "entityCount", sol::readonly_property(
            &ScriptSector::GetEntityCount
        ),

        "neighborCount", sol::readonly_property(
            &ScriptSector::GetNeighborCount
        ),

        "GetVertex", &ScriptSector::GetVertex,
        "GetWall", &ScriptSector::GetWall,
        "GetEntity", &ScriptSector::GetEntity,
        "GetNeighbor", &ScriptSector::GetNeighbor
    );
}