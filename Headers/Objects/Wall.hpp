//
// Created by berke on 4/13/2026.
//

#ifndef WOLFY_ENGINE_WALL_H
#define WOLFY_ENGINE_WALL_H

#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector2Math.hpp"
#include "../Math/Vector/Vector4.hpp"

struct Wall {
    Vector2 start, end;
    Vector4 color;

    int frontSector = -1;
    int backSector = -1;
    int textureIndex = -1;

    int floor = 0;

    Vector2 dir, normal, vector;
    float lengthSq;
    float length;

    Wall(
    const Vector2& start,
    const Vector2& end,
    const Vector4 color,
    const int fs = -1,
    const int bs = -1,
    const int textureIndex = -1,
    const int floor = 0
)
    : start(start),
      end(end),
      color(color),
      frontSector(fs),
      backSector(bs),
      textureIndex(textureIndex),
      floor(floor)
    {
        vector = end - start;

        lengthSq = Vector2Math::Dot(vector, vector);
        length = std::sqrt(lengthSq);

        if (length > 0.00001f) {
            dir = vector / length;
            normal = {-dir.y, dir.x};
        }
        else {
            dir = {0.0f, 0.0f};
            normal = {0.0f, 0.0f};
        }
    }
};

#endif //WOLFY_ENGINE_WALL_H