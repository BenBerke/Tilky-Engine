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

    // Stable ID of the sector that is to the front or to the left of the Wall in top down view
    // Should be -1 if there is no sector
    ID frontSector  = INVALID_ID;

    // Stable ID of the sector that is to the back or to the right of the Wall in top down view
    // Should be -1 if there is no sector
    ID backSector   = INVALID_ID;
    std::string textureFileName;

    // Read only — do not change
    Vector2 dir, normal, vector;
    float lengthSq; // lengthSquared
    float length;

    std::array<std::array<Vector3, 4>, 2> quads3D = {};
    int quad3DCount = 0;

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
    const Vector4 color,
    const ID fs = INVALID_ID,
    const ID bs = INVALID_ID,
    std::string textureFileName = {},
    const int floor = 0 // Currently unused
    )
    : start(start),
      end(end),
      color(color),
      frontSector(fs),
      backSector(bs),
      textureFileName(std::move(textureFileName))
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