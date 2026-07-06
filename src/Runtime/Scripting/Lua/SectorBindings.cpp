//
// Created by berke on 7/6/2026.
//

#include <Headers/Objects/LuaWrappers.hpp>
#include "Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "sol/sol.hpp"

void ScriptSystem::RegisterSectorBindings(sol::state &lua) {
    lua.new_usertype<ScriptSector>(
    "SectorRef",

    "id", sol::readonly_property(&ScriptSector::GetID),
    "isValid", sol::readonly_property(&ScriptSector::IsValid),

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

    "floorTextureIndex", sol::property(
        &ScriptSector::GetFloorTextureIndex,
        &ScriptSector::SetFloorTextureIndex
    ),

    "ceilingTextureIndex", sol::property(
        &ScriptSector::GetCeilingTextureIndex,
        &ScriptSector::SetCeilingTextureIndex
    ),

    "lightValue", sol::property(
        &ScriptSector::GetLightValue,
        &ScriptSector::SetLightValue
    ),

    "vertexCount", sol::readonly_property(&ScriptSector::GetVertexCount),
    "wallCount", sol::readonly_property(&ScriptSector::GetWallCount),
    "entityCount", sol::readonly_property(&ScriptSector::GetEntityCount),
    "neighborCount", sol::readonly_property(&ScriptSector::GetNeighborCount),

    "GetVertex", &ScriptSector::GetVertex,
    "GetWall", &ScriptSector::GetWall,
    "GetEntity", &ScriptSector::GetEntity,
    "GetNeighbor", &ScriptSector::GetNeighbor
);
}