//
// Created by berke on 4/26/2026.
//

#ifndef WOLFY_ENGINE_SECTOR_H
#define WOLFY_ENGINE_SECTOR_H
#include "Headers/Math/Vector/Vector3.hpp"

struct Vector2;

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

    int ceilingTextureIndex = -1;
    int floorTextureIndex = -1;
};

#endif //WOLFY_ENGINE_SECTOR_H