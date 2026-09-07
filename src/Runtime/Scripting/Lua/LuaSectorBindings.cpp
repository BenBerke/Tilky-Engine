//
// Created by berke on 7/6/2026.
//

#include <Headers/Objects/LuaWrappers.hpp>
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

namespace {
    using namespace LuaBindingMetadata;

    void RegisterSectorMetadata() {
        RegisterType(Type("SectorFloorRef", "One floor/ceiling height interval within a Sector.", {
            Prop("index", "integer", true, "1-based."),
            Prop("isValid", "boolean", true),
            Prop("floorHeight", "number"),
            Prop("ceilingHeight", "number"),
            Prop("floorColor", "Vector4"),
            Prop("ceilingColor", "Vector4"),
            Prop("floorTexture", "string"),
            Prop("ceilingTexture", "string"),
        }, {
            Method("clearFloorTexture"),
            Method("clearCeilingTexture"),
        }));

        RegisterType(Type("SectorRef", "A safe reference to one map sector.", {
            Prop("id", "integer", true),
            Prop("isValid", "boolean", true),
            Prop("light", "Vector3"),
            Prop("floorCount", "integer", true),
            Prop("vertexCount", "integer", true),
            Prop("wallCount", "integer", true),
            Prop("entityCount", "integer", true),
            Prop("neighborCount", "integer", true),
        }, {
            Method("GetFloor", {Param("index", "integer")}, "SectorFloorRef", "1-based."),
            Method("GetVertex", {Param("index", "integer")}, "Vector2", "1-based."),
            Method("GetWall", {Param("index", "integer")}, "WallRef", "1-based."),
            Method("GetEntity", {Param("index", "integer")}, "GameObject", "1-based."),
            Method("GetNeighbor", {Param("index", "integer")}, "SectorRef", "1-based."),
        }));
    }
}

void LuaScriptSystem::RegisterSectorBindings(sol::state& lua) {
    RegisterSectorMetadata();

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

        "light", sol::property(
            &ScriptSector::GetLight,
            &ScriptSector::SetLight
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