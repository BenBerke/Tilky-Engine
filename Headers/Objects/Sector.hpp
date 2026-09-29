#ifndef TILKY_ENGINE_SECTOR_H
#define TILKY_ENGINE_SECTOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/EntityTypes.hpp"
#include "Headers/Objects/ScriptPublicType.hpp"
#include "Wall.hpp"

using ID = uint32_t;

struct Triangle {
    Vector2 a, b, c;
};

// Towards
enum SlopeDirection {
    PLUS_X = 0,
    MINUS_X = 1,
    PLUS_Z = 2,
    MINUS_Z = 3
};

struct SectorSurface {
    float height = 0.0f;
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::string texture;

    SlopeDirection slopeDirection = PLUS_X;
    float slopeStrength = 0.0f;

    Vector2 textureOffset = {0.0f, 0.0f};
    Vector2 textureScale = {1.0f, 1.0f};
    bool flipTextureX = false;
    bool flipTextureY = false;
};

// A floor or ceiling travelling toward the opposite surface of its
// SectorFloor. Started from Lua (Sector:MoveFloorToCeiling and friends) and
// advanced once per frame by SectorFloor::UpdateMovement. Runtime-only state:
// never serialized.
struct SurfaceMove {
    bool active = false;
    float speed = 0.0f; // units per second
    float gap = 0.0f;   // distance to stop short of the opposite surface
};

struct SectorFloor {
    SectorSurface floor;
    SectorSurface ceiling;

    SurfaceMove floorMove;
    SurfaceMove ceilingMove;

    // A move never closes the room completely: the floor has to stay below
    // its ceiling, so any smaller gap is raised to this.
    static constexpr float MIN_MOVE_GAP = 0.01f;

    // Where each move stops. Follows the opposite surface, so a floor moving
    // up still stops `gap` below a ceiling that is itself moving.
    [[nodiscard]] float FloorMoveTarget() const { return ceiling.height - floorMove.gap; }
    [[nodiscard]] float CeilingMoveTarget() const { return floor.height + ceilingMove.gap; }

    void MoveFloorToCeiling(const float speed, const float gap) {
        floorMove = {true, speed, std::max(gap, MIN_MOVE_GAP)};
    }

    void MoveCeilingToFloor(const float speed, const float gap) {
        ceilingMove = {true, speed, std::max(gap, MIN_MOVE_GAP)};
    }

    // Same as above, with the speed picked so the move takes `seconds` from
    // the current heights. 0 seconds snaps on the next update.
    void MoveFloorToCeilingOverTime(const float seconds, const float gap) {
        MoveFloorToCeiling(0.0f, gap);
        floorMove.speed = SpeedForDuration(FloorMoveTarget() - floor.height, seconds);
    }

    void MoveCeilingToFloorOverTime(const float seconds, const float gap) {
        MoveCeilingToFloor(0.0f, gap);
        ceilingMove.speed = SpeedForDuration(CeilingMoveTarget() - ceiling.height, seconds);
    }

    [[nodiscard]] bool IsMoving() const {
        return floorMove.active || ceilingMove.active;
    }

    void StopMoving() {
        floorMove.active = false;
        ceilingMove.active = false;
    }

    // Advances the active moves by `deltaTime` seconds. `lowest`/`highest`
    // are the heights this interval may not leave (the previous interval's
    // ceiling and the next one's floor). Returns true if a height changed.
    bool UpdateMovement(const float deltaTime, const float lowest, const float highest) {
        const float floorBefore = floor.height;
        const float ceilingBefore = ceiling.height;

        if (floorMove.active) {
            const float target = std::max(FloorMoveTarget(), lowest);
            floor.height = MoveToward(floor.height, target, floorMove.speed * deltaTime);
            if (floor.height == target) floorMove.active = false;
        }

        if (ceilingMove.active) {
            const float target = std::min(CeilingMoveTarget(), highest);
            ceiling.height = MoveToward(ceiling.height, target, ceilingMove.speed * deltaTime);
            if (ceiling.height == target) ceilingMove.active = false;
        }

        return floor.height != floorBefore || ceiling.height != ceilingBefore;
    }

private:
    static float SpeedForDuration(const float distance, const float seconds) {
        if (seconds <= 0.0f) return std::numeric_limits<float>::infinity();
        return std::abs(distance) / seconds;
    }

    // Written as !(maxDelta < distance) so an infinite speed times a zero
    // deltaTime (NaN) snaps to the target instead of poisoning the height.
    static float MoveToward(const float current, const float target, const float maxDelta) {
        if (!(maxDelta < std::abs(target - current))) return target;
        return target > current ? current + maxDelta : current - maxDelta;
    }
};

// A Lua script attached to a sector. Deliberately NOT a ComponentScript and
// not part of Level::scripts: sectors are not entities.
using SectorScript = ScriptAttachmentData;

struct Sector {
    std::string name;

    std::vector<std::string> tags;
    std::vector<uint16_t> tagIds;

    // Scripts attached to this sector (zero or more). They live on the
    // sector itself, so deleting a sector, snapshotting/restoring geometry
    // for undo, and copying a sector all carry or drop them with it. The
    // runtime never holds a pointer to this vector - it finds the sector by
    // `id` (Level::sectorIDToIndex) and the script by instanceID every time
    // it ticks, so vector reallocation can't leave it dangling.
    std::vector<SectorScript> scripts;

    // Next instanceID AddScript() hands out. Only ever increases, so an ID
    // freed by RemoveScript() is never reused while a live instance might
    // still be bound to it. Not serialized - rebuilt from `scripts` on load.
    ScriptInstanceID nextScriptInstanceID = 1;

    SectorScript& AddScript() {
        SectorScript& script = scripts.emplace_back();
        script.instanceID = nextScriptInstanceID++;
        return script;
    }

    SectorScript* GetScript(const ScriptInstanceID instanceID) {
        for (SectorScript& script : scripts) if (script.instanceID == instanceID) return &script;
        return nullptr;
    }

    [[nodiscard]] const SectorScript* GetScript(const ScriptInstanceID instanceID) const {
        for (const SectorScript& script : scripts) if (script.instanceID == instanceID) return &script;
        return nullptr;
    }

    bool RemoveScript(const ScriptInstanceID instanceID) {
        return std::erase_if(scripts, [instanceID](const SectorScript& script) {
            return script.instanceID == instanceID;
        }) != 0;
    }

    std::vector<SectorFloor> floors = {
        {
            {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
            {40.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}}
        }
    };

    Vector3 light = {255.0f, 255.0f, 255.0f};

    std::vector<Vector2> vertices;

    // Boundary of each sector nested directly inside this one - a pillar,
    // island, or any other fully-enclosed sector cut out of this one's
    // floor/ceiling. Only direct children: a hole inside one of these
    // holes belongs to that child's own innerLoops, not here. Rebuilt
    // from scratch by MapTopology alongside `vertices`/`triangles` on
    // every topology edit, same as the outer boundary.
    std::vector<std::vector<Vector2>> innerLoops;

    std::vector<Triangle> triangles;

    ID id = INVALID_ID;

    // Logical parent, INVALID_ID for a root sector. This is the source of
    // truth for the hierarchy - only change it through Level::SetSectorParent.
    ID parentID = INVALID_ID;

    // Direct children's IDs. A cache derived from every sector's parentID,
    // kept in sync by Level::SetSectorParent and refilled from scratch by
    // Level::RebuildSectorChildren. Not a source of truth, don't serialize it.
    std::vector<ID> children;

    std::vector<ID> entitiesInside;
    std::vector<Sector*> neighbors;
    std::vector<Wall*> walls;
    std::vector<Sector*> pvs;
};

#endif