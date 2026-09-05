#ifndef TILKY_ENGINE_SECTOR_H
#define TILKY_ENGINE_SECTOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/EntityTypes.hpp"
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

struct Sector {
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
};

#endif