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
        RegisterType(Type("SectorFloor", "One floor/ceiling height interval within a Sector.", {
            Prop("index", "integer", true, "1-based."),
            Prop("isValid", "boolean", true),
            Prop("floorHeight", "number"),
            Prop("ceilingHeight", "number"),
            Prop("floorColor", "Vector4"),
            Prop("ceilingColor", "Vector4"),
            Prop("floorTexture", "string"),
            Prop("ceilingTexture", "string"),
        }, {
            Method("ClearFloorTexture"),
            Method("ClearCeilingTexture"),
        }));

        RegisterType(Type("Sector", "A safe reference to one map sector.", {
            Prop("id", "integer", true),
            Prop("isValid", "boolean", true),
            Prop("name", "string", false, "The sector's name (empty if none was set)."),
            Prop("floorHeight", "number", false, "Floor height of the first floor - same as GetFloor(1).floorHeight."),
            Prop("light", "Vector3"),
            Prop("floorCount", "integer", true),
            Prop("vertexCount", "integer", true),
            Prop("wallCount", "integer", true),
            Prop("entityCount", "integer", true),
            Prop("neighborCount", "integer", true),
            Prop("tagCount", "integer", true),
        }, {
            Method("GetFloor", {Param("index", "integer")}, "SectorFloor", "1-based."),
            Method("GetVertex", {Param("index", "integer")}, "Vector2", "1-based."),
            Method("GetWall", {Param("index", "integer")}, "Wall", "1-based."),
            Method("GetEntity", {Param("index", "integer")}, "Entity", "1-based."),
            Method("GetNeighbor", {Param("index", "integer")}, "Sector", "1-based."),
            Method("HasTag", {Param("tag", "string")}, "boolean", "True if this sector has the given tag."),
            Method("GetTag", {Param("index", "integer")}, "string", "1-based. Tags are assigned in the editor - there is no SetTag."),
            Method("MoveFloorToCeiling", {Param("floorIndex", "integer"), Param("speed", "number"), Param("gap", "number?")}, {},
                   "Moves floor `floorIndex` (1-based) toward its ceiling at `speed` units/s, stopping `gap` below it (default 0). The move runs by itself every frame; starting another replaces it."),
            Method("MoveCeilingToFloor", {Param("floorIndex", "integer"), Param("speed", "number"), Param("gap", "number?")}, {},
                   "Moves the ceiling of floor `floorIndex` (1-based) toward its floor at `speed` units/s, stopping `gap` above it (default 0)."),
            Method("MoveFloorToCeilingOverTime", {Param("floorIndex", "integer"), Param("seconds", "number"), Param("gap", "number?")}, {},
                   "Like MoveFloorToCeiling, but arrives after `seconds` instead of moving at a set speed."),
            Method("MoveCeilingToFloorOverTime", {Param("floorIndex", "integer"), Param("seconds", "number"), Param("gap", "number?")}, {},
                   "Like MoveCeilingToFloor, but arrives after `seconds` instead of moving at a set speed."),
            Method("IsMoving", {Param("floorIndex", "integer")}, "boolean", "True while the floor or ceiling of floor `floorIndex` is still moving."),
            Method("StopMoving", {Param("floorIndex", "integer")}, {}, "Stops floor `floorIndex`'s floor and ceiling where they are."),

            // Occupancy
            Method("ContainsEntity", {Param("entity", "Entity")}, "boolean",
                   "True if `entity` is inside this sector. Only the innermost sector counts: an entity in a child sector is not inside the parent."),
            Method("ContainsEntityWithTag", {Param("tag", "string")}, "boolean", "True if any entity inside this sector has `tag`."),
            Method("GetEntities", {}, "Entity[]", "Every entity inside this sector."),
            Method("GetEntitiesWithTag", {Param("tag", "string")}, "Entity[]", "Every entity inside this sector that has `tag`."),
            Method("CountEntities", {Param("tag", "string?")}, "integer", "How many entities are inside; only those with `tag` if given."),
            Method("IsEmpty", {}, "boolean", "True if no entity is inside this sector."),

            // Moving to a set height
            Method("MoveFloorTo", {Param("floorIndex", "integer"), Param("height", "number"), Param("speed", "number")}, {},
                   "Moves floor `floorIndex`'s floor up or down to `height` at `speed` units/s. Never passes its ceiling."),
            Method("MoveCeilingTo", {Param("floorIndex", "integer"), Param("height", "number"), Param("speed", "number")}, {},
                   "Moves floor `floorIndex`'s ceiling up or down to `height` at `speed` units/s. Never passes its floor."),
            Method("MoveFloorToOverTime", {Param("floorIndex", "integer"), Param("height", "number"), Param("seconds", "number")}, {},
                   "Like MoveFloorTo, but arrives after `seconds`."),
            Method("MoveCeilingToOverTime", {Param("floorIndex", "integer"), Param("height", "number"), Param("seconds", "number")}, {},
                   "Like MoveCeilingTo, but arrives after `seconds`."),
            Method("IsFloorMoving", {Param("floorIndex", "integer")}, "boolean", "True while floor `floorIndex`'s floor is moving."),
            Method("IsCeilingMoving", {Param("floorIndex", "integer")}, "boolean", "True while floor `floorIndex`'s ceiling is moving."),

            // Light
            Method("FadeLight", {Param("color", "Vector3"), Param("seconds", "number")}, {},
                   "Fades `light` to `color` over `seconds`. Setting `light` directly cancels the fade."),
            Method("IsLightFading", {}, "boolean", "True while a FadeLight is running."),

            // Shape
            Method("GetCenter", {}, "Vector2", "Area-weighted centre (x, z). Can lie outside L-shaped or ring-shaped sectors."),
            Method("GetArea", {}, "number", "Floor area, not counting child sectors cut out of it."),
            Method("GetBounds", {}, "Vector2, Vector2", "Returns min, max of the sector's bounding rectangle (x, z)."),
            Method("DistanceToSector", {Param("entity", "Entity")}, "number",
                   "Distance from `entity` (x, z) to the nearest edge of this sector, holes included. 0 while the entity is inside."),
            Method("DistanceToSectorSquared", {Param("entity", "Entity")}, "number",
                   "DistanceToSector squared. Cheaper: compare it against range * range."),
            Method("RandomPointInside", {Param("floorIndex", "integer?")}, "Vector3",
                   "A random point inside the sector, standing on floor `floorIndex` (default 1). Follows mathT.RandomSeed."),
            Method("GetFloorHeightAt", {Param("position", "Vector2|Vector3"), Param("floorIndex", "integer?")}, "number",
                   "Floor height at `position` (x, z), slopes included. floorIndex defaults to 1."),
            Method("GetCeilingHeightAt", {Param("position", "Vector2|Vector3"), Param("floorIndex", "integer?")}, "number",
                   "Ceiling height at `position` (x, z), slopes included. floorIndex defaults to 1."),
        }));
    }
}

namespace {
    Vector3 RandomPointInside(const ScriptSector& sector, const int floorIndex) {
        const Vector2 point = sector.PointInside(
            LuaScriptSystem::SharedRandomUnitFloat(),
            LuaScriptSystem::SharedRandomUnitFloat(),
            LuaScriptSystem::SharedRandomUnitFloat()
        );

        return {point.x, sector.GetFloorHeightAt(point, floorIndex), point.y};
    }
}

void LuaScriptSystem::RegisterSectorBindings(sol::state& lua) {
    RegisterSectorMetadata();

    lua.new_usertype<ScriptSectorFloor>(
        "SectorFloor",

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

        "ClearFloorTexture",
        &ScriptSectorFloor::ClearFloorTexture,

        "ClearCeilingTexture",
        &ScriptSectorFloor::ClearCeilingTexture
    );

    lua.new_usertype<ScriptSector>(
        "Sector",

        "id", sol::readonly_property(
            &ScriptSector::GetID
        ),

        "isValid", sol::readonly_property(
            &ScriptSector::IsValid
        ),

        "name", sol::property(
            &ScriptSector::GetName,
            &ScriptSector::SetName
        ),

        // First floor interval's floor height; see ScriptSector::GetFloorHeight.
        "floorHeight", sol::property(
            &ScriptSector::GetFloorHeight,
            &ScriptSector::SetFloorHeight
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

        // Read-only: tags are assigned from the editor only - no SetTag.
        "tagCount", sol::readonly_property(
            &ScriptSector::GetTagCount
        ),

        "GetFloor", &ScriptSector::GetFloor,
        "GetVertex", &ScriptSector::GetVertex,
        "GetWall", &ScriptSector::GetWall,
        "GetEntity", &ScriptSector::GetEntity,
        "GetNeighbor", &ScriptSector::GetNeighbor,
        "HasTag", &ScriptSector::HasTag,
        "GetTag", &ScriptSector::GetTag,

        // Each move has a gap-less overload that leaves the default gap (0).
        "MoveFloorToCeiling", sol::overload(
            &ScriptSector::MoveFloorToCeiling,
            [](const ScriptSector& self, const int floorIndex, const float speed) {
                self.MoveFloorToCeiling(floorIndex, speed, 0.0f);
            }
        ),
        "MoveCeilingToFloor", sol::overload(
            &ScriptSector::MoveCeilingToFloor,
            [](const ScriptSector& self, const int floorIndex, const float speed) {
                self.MoveCeilingToFloor(floorIndex, speed, 0.0f);
            }
        ),
        "MoveFloorToCeilingOverTime", sol::overload(
            &ScriptSector::MoveFloorToCeilingOverTime,
            [](const ScriptSector& self, const int floorIndex, const float seconds) {
                self.MoveFloorToCeilingOverTime(floorIndex, seconds, 0.0f);
            }
        ),
        "MoveCeilingToFloorOverTime", sol::overload(
            &ScriptSector::MoveCeilingToFloorOverTime,
            [](const ScriptSector& self, const int floorIndex, const float seconds) {
                self.MoveCeilingToFloorOverTime(floorIndex, seconds, 0.0f);
            }
        ),
        "IsMoving", &ScriptSector::IsMoving,
        "StopMoving", &ScriptSector::StopMoving,

        // ---- Occupancy ----
        "ContainsEntity", &ScriptSector::ContainsEntity,
        "ContainsEntityWithTag", &ScriptSector::ContainsEntityWithTag,
        "GetEntities", [](const ScriptSector& self) { return sol::as_table(self.GetEntities()); },
        "GetEntitiesWithTag", [](const ScriptSector& self, const std::string& tag) {
            return sol::as_table(self.GetEntitiesWithTag(tag));
        },
        "CountEntities", sol::overload(
            &ScriptSector::CountEntities,
            &ScriptSector::CountEntitiesWithTag
        ),
        "IsEmpty", &ScriptSector::IsEmpty,

        // ---- Moving to a set height ----
        "MoveFloorTo", &ScriptSector::MoveFloorTo,
        "MoveCeilingTo", &ScriptSector::MoveCeilingTo,
        "MoveFloorToOverTime", &ScriptSector::MoveFloorToOverTime,
        "MoveCeilingToOverTime", &ScriptSector::MoveCeilingToOverTime,
        "IsFloorMoving", &ScriptSector::IsFloorMoving,
        "IsCeilingMoving", &ScriptSector::IsCeilingMoving,

        // ---- Light ----
        "FadeLight", &ScriptSector::FadeLight,
        "IsLightFading", &ScriptSector::IsLightFading,

        // ---- Shape ----
        "GetCenter", &ScriptSector::GetCenter,
        "GetArea", &ScriptSector::GetArea,
        "GetBounds", &ScriptSector::GetBounds,
        "DistanceToSector", &ScriptSector::DistanceToSector,
        "DistanceToSectorSquared", &ScriptSector::DistanceToSectorSquared,

        "RandomPointInside", sol::overload(
            [](const ScriptSector& self) { return RandomPointInside(self, 1); },
            [](const ScriptSector& self, const int floorIndex) { return RandomPointInside(self, floorIndex); }
        ),

        // Positions are map-space: a Vector2 is (x, z), a Vector3 uses its x and z.
        "GetFloorHeightAt", sol::overload(
            [](const ScriptSector& self, const Vector2& p) { return self.GetFloorHeightAt(p, 1); },
            [](const ScriptSector& self, const Vector2& p, const int i) { return self.GetFloorHeightAt(p, i); },
            [](const ScriptSector& self, const Vector3& p) { return self.GetFloorHeightAt({p.x, p.z}, 1); },
            [](const ScriptSector& self, const Vector3& p, const int i) { return self.GetFloorHeightAt({p.x, p.z}, i); }
        ),
        "GetCeilingHeightAt", sol::overload(
            [](const ScriptSector& self, const Vector2& p) { return self.GetCeilingHeightAt(p, 1); },
            [](const ScriptSector& self, const Vector2& p, const int i) { return self.GetCeilingHeightAt(p, i); },
            [](const ScriptSector& self, const Vector3& p) { return self.GetCeilingHeightAt({p.x, p.z}, 1); },
            [](const ScriptSector& self, const Vector3& p, const int i) { return self.GetCeilingHeightAt({p.x, p.z}, i); }
        )
    );
}