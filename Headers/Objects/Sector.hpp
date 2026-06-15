#ifndef TILKY_ENGINE_SECTOR_H
#define TILKY_ENGINE_SECTOR_H

#include <array>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Wall.hpp"
#include "config.h"
#include "Headers/Objects/EntityTypes.hpp"

using ID = uint32_t;

struct Triangle {
    Vector2 a, b, c;
};
struct Entity;

struct Sector {
    std::vector<Vector2> vertices;
    std::vector<Triangle> triangles; // DO NOT change, triangles making up the sector after triangluation

    float ceilingHeight;
    float floorHeight;

    Vector3 ceilingColor;
    Vector3 floorColor;

    int floorCount = 2;

    std::array<int, MAX_FLOOR_COUNT> ceilingTextureIndices = {};
    int floorTextureIndex = -1;

    ID id = -1;
    std::vector<Entity*> entitiesInside;
    std::vector<Sector*> neighbors;

    std::vector<Wall*> walls = {};
};

#endif