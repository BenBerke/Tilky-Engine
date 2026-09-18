#ifndef TILKY_ENGINE_SECTOR_H
#define TILKY_ENGINE_SECTOR_H

#include <cstdint>
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

struct SectorFloor {
    SectorSurface floor;
    SectorSurface ceiling;
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

    std::vector<ID> entitiesInside;
    std::vector<Sector*> neighbors;
    std::vector<Wall*> walls;
    std::vector<Sector*> pvs;
};

#endif