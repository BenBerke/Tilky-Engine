//
// Created by berke on 6/2/2026.
//

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

namespace GameFunctions {
    std::optional<RayHit> Raycast(Level& level, const Vector3 pos, const Vector3 dir) {
        //todo make this a world setting
        constexpr int MAX_STEP_COUNT = 5000;
        constexpr float RAY_SIZE = .01f;

        Vector3 normalizedDir = Vector3Math::Normalized(dir);

        for (int i = 0; i < MAX_STEP_COUNT; ++i) {
            //todo raycasting
        }
    }
}