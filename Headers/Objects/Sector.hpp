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

struct SectorSurface {
    float height = 0.0f;
    Vector3 color = {255.0f, 255.0f, 255.0f};
    std::string texture;
};

struct SectorFloor {
    SectorSurface floor;
    SectorSurface ceiling;
};

struct Entity;

struct Sector {
    std::vector<Vector2> vertices;
    std::vector<Triangle> triangles;

    std::vector<SectorFloor> floors = {
        {
            {0.0f, {255.0f, 255.0f, 255.0f}, {}},
            {40.0f, {255.0f, 255.0f, 255.0f}, {}}
        }
    };

    float lightValue = 255.0f;

    ID id = INVALID_ID;

    std::vector<ID> entitiesInside;
    std::vector<Sector*> neighbors;
    std::vector<Wall*> walls;
};

#endif