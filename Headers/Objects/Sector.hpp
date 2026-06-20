#ifndef TILKY_ENGINE_SECTOR_H
#define TILKY_ENGINE_SECTOR_H

#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Wall.hpp"
#include "Headers/Objects/EntityTypes.hpp"

using ID = uint32_t;

struct Triangle {
    Vector2 a, b, c;
};

struct Entity;

struct Sector {
    std::vector<Vector2> vertices;
    std::vector<Triangle> triangles;

    float ceilingHeight = 0.0f;
    float floorHeight = 0.0f;

    Vector3 ceilingColor = {255.0f, 255.0f, 255.0f};
    Vector3 floorColor = {255.0f, 255.0f, 255.0f};

    int ceilingTextureIndex = -1;
    int floorTextureIndex = -1;

    ID id = INVALID_ID;

    std::vector<ID> entitiesInside;
    std::vector<Sector*> neighbors;
    std::vector<Wall*> walls = {};
};

#endif