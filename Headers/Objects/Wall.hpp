//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_WALL_H
#define TILKY_ENGINE_WALL_H

#include <array>

#include "EntityTypes.hpp"
#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector2Math.hpp"
#include "../Math/Vector/Vector4.hpp"

struct Wall {
    ID id = INVALID_ID;

    Vector2 start, end;
    Vector4 color;
    Vector2 textureOffset;

    ID frontSector = INVALID_ID;
    ID backSector = INVALID_ID;
    int textureIndex = INVALID_ID;

    int floor = 0;

    // Read only do not change
    Vector2 dir, normal, vector;
    float lengthSq;
    float length;

    std::array<std::array<Vector3, 4>, 2> quads3D = {};
    int quad3DCount = 0;

    //quads3D[0] // first quad
    //quads3D[1] // second quad

    // quad[0] = bottomStart
    // quad[1] = bottomEnd
    // quad[2] = topEnd
    // quad[3] = topStart

    Wall(
        const Vector2 &start,
        const Vector2 &end,
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

#endif //TILKY_ENGINE_WALL_H