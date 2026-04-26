//
// Created by berke on 4/26/2026.
//

#ifndef WOLFY_ENGINE_SECTOR_H
#define WOLFY_ENGINE_SECTOR_H

#include "../Math/Vector/Vector2.h"
#include "../Math/Vector/Vector3.h"

struct Triangle {
    Vector2 a, b, c;
};

struct Sector {
    std::vector<Vector2> vertices;
    std::vector<Triangle> triangles;
    float ceilingHeight, floorHeight;
    Vector3 ceilingColor, floorColor;
};

#endif //WOLFY_ENGINE_SECTOR_H