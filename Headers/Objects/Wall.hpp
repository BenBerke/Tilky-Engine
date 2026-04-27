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

    Vector2 dir, normal;
    float lengthSq;

    Wall(
        const Vector2& start,
        const Vector2& end,
        const Vector4 color,
        const int fs = -1,
        const int bs = -1,
        const int textureIndex = -1
    )
        : start(start),
          end(end),
          color(color),
          frontSector(fs),
          backSector(bs),
          textureIndex(textureIndex)
    {
        dir = end - start;
        lengthSq = Vector2Math::Dot(dir, dir);

        normal = {-dir.y, dir.x};

        if (lengthSq > 0.00001f) {
            Vector2Math::Normalize(normal);
        }
    }
};

#endif //WOLFY_ENGINE_WALL_H