//
// Created by berke on 4/13/2026.
//

#ifndef WOLFY_ENGINE_WALL_H
#define WOLFY_ENGINE_WALL_H

#include "../Math/Vector/Vector2.h"
#include "../Math/Vector/Vector4.h"

struct Wall {
    Vector2 start, end;
    Vector4 color;
    int backSector = -1, frontSector = -1;
};

#endif //WOLFY_ENGINE_WALL_H