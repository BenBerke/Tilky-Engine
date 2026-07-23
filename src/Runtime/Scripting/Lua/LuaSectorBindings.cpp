//
// Created by berke on 7/6/2026.
//

#include <Headers/Objects/LuaWrappers.hpp>
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

void LuaScriptSystem::RegisterSectorBindings(sol::state& lua) {
    lua.new_usertype<ScriptSectorFloor>(
        "SectorFloorRef",

        "index", sol::readonly_property(
            &ScriptSectorFloor::GetIndex
        ),

        "isValid", sol::readonly_property(
            &ScriptSectorFloor::IsValid
        ),

        "floorHeight", sol::property(
            &ScriptSectorFloor::GetFloorHeight,
            &ScriptSectorFloor::SetFloorHeight
        ),

        "ceilingHeight", sol::property(
            &ScriptSectorFloor::GetCeilingHeight,
            &ScriptSectorFloor::SetCeilingHeight
        ),

        "floorColor", sol::property(
            &ScriptSectorFloor::GetFloorColor,
            &ScriptSectorFloor::SetFloorColor
        ),

        "ceilingColor", sol::property(
            &ScriptSectorFloor::GetCeilingColor,
            &ScriptSectorFloor::SetCeilingColor
        ),

        "floorTexture", sol::property(
            &ScriptSectorFloor::GetFloorTexture,
            &ScriptSectorFloor::SetFloorTexture
        ),

        "ceilingTexture", sol::property(
            &ScriptSectorFloor::GetCeilingTexture,
            &ScriptSectorFloor::SetCeilingTexture
        ),

        "clearFloorTexture",
        &ScriptSectorFloor::ClearFloorTexture,

        "clearCeilingTexture",
        &ScriptSectorFloor::ClearCeilingTexture
    );

    lua.new_usertype<ScriptSector>(
        "SectorRef",

        "id", sol::readonly_property(
            &ScriptSector::GetID
        ),

        "isValid", sol::readonly_property(
            &ScriptSector::IsValid
        ),

        "lightValue", sol::property(
            &ScriptSector::GetLightValue,
            &ScriptSector::SetLightValue
        ),

        "floorCount", sol::readonly_property(
            &ScriptSector::GetFloorCount
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

        "GetFloor", &ScriptSector::GetFloor,
        "GetVertex", &ScriptSector::GetVertex,
        "GetWall", &ScriptSector::GetWall,
        "GetEntity", &ScriptSector::GetEntity,
        "GetNeighbor", &ScriptSector::GetNeighbor
    );
}