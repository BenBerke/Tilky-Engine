//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_WALL_H
#define TILKY_ENGINE_WALL_H

#include <array>
#include <algorithm>
#include <cmath>

#include "EntityTypes.hpp"
#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector2Math.hpp"
#include "../Math/Vector/Vector3.hpp"
#include "../Math/Vector/Vector4.hpp"

struct Wall {
    ID id = INVALID_ID;

    Vector2 start, end;
    Vector4 color;
    Vector2 textureOffset;

    ID frontSector  = INVALID_ID;
    ID backSector   = INVALID_ID;
    int textureIndex = INVALID_ID;

    // Read only — do not change
    Vector2 dir, normal, vector;
    float   lengthSq;
    float   length;

    std::array<std::array<Vector3, 4>, 2> quads3D     = {};
    int                                   quad3DCount  = 0;

    // Per-quad 3D AABBs for physics early-out.
    // Filled by RebuildQuadAabbs() whenever quads3D is written.
    std::array<Vector3, 2> quadAabbMin = {};
    std::array<Vector3, 2> quadAabbMax = {};

    //quads3D[0] // first quad
    //quads3D[1] // second quad

    // quad[0] = bottomStart
    // quad[1] = bottomEnd
    // quad[2] = topEnd
    // quad[3] = topStart

    Wall(
        const Vector2& start,
        const Vector2& end,
        const Vector4  color,
        const int fs           = -1,
        const int bs           = -1,
        const int textureIndex = -1,
        const int floor        = 0
    )
    : start(start),
      end(end),
      color(color),
      frontSector(fs),
      backSector(bs),
      textureIndex(textureIndex)
    {
        vector   = end - start;
        lengthSq = Vector2Math::Dot(vector, vector);
        length   = std::sqrt(lengthSq);

        if (length > 0.00001f) {
            dir    = vector / length;
            normal = { -dir.y, dir.x };
        }
        else {
            dir    = { 0.0f, 0.0f };
            normal = { 0.0f, 0.0f };
        }
    }

    void RebuildQuadAabbs() {
        for (int i = 0; i < quad3DCount; ++i) {
            const auto& q = quads3D[i];
            quadAabbMin[i] = {
                std::min({ q[0].x, q[1].x, q[2].x, q[3].x }),
                std::min({ q[0].y, q[1].y, q[2].y, q[3].y }),
                std::min({ q[0].z, q[1].z, q[2].z, q[3].z })
            };
            quadAabbMax[i] = {
                std::max({ q[0].x, q[1].x, q[2].x, q[3].x }),
                std::max({ q[0].y, q[1].y, q[2].y, q[3].y }),
                std::max({ q[0].z, q[1].z, q[2].z, q[3].z })
            };
        }
    }
};

#endif //TILKY_ENGINE_WALL_H