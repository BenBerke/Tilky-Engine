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
    uint_fast32_t color = std::numeric_limits<uint_fast32_t>::max();
    std::string texture;

    SlopeDirection slopeDirection = PLUS_X;
    float slopeStrength = 0.0f;
};

struct SectorFloor {
    SectorSurface floor;
    SectorSurface ceiling;
};

struct Sector {
    std::vector<SectorFloor> floors = {
        {
            {0.0f, std::numeric_limits<uint_fast32_t>::max(), {}},
            {40.0f, std::numeric_limits<uint_fast32_t>::max(), {}}
        }
    };

    Vector3 light = {255.0f, 255.0f, 255.0f};

    std::vector<Vector2> vertices;
    std::vector<Triangle> triangles;

    ID id = INVALID_ID;

    std::vector<ID> entitiesInside;
    std::vector<Sector*> neighbors;
    std::vector<Wall*> walls;
};

#endif