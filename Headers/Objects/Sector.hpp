#ifndef WOLFY_ENGINE_SECTOR_H
#define WOLFY_ENGINE_SECTOR_H

#include <array>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Wall.hpp"
#include "config.h"

struct Triangle {
    Vector2 a, b, c;
};

struct Sector {
    std::vector<Vector2> vertices;
    std::vector<Triangle> triangles;

    float ceilingHeight;
    float floorHeight;

    Vector3 ceilingColor;
    Vector3 floorColor;

    int floorCount = 2;

    std::array<int, MAX_FLOOR_COUNT> ceilingTextureIndices = {};
    int floorTextureIndex = -1;

    std::vector<Wall> walls = {};
};

#endif