//
// Created by berke on 4/13/2026.
//

#ifndef WOLFY_ENGINE_PLAYER_H
#define WOLFY_ENGINE_PLAYER_H

#include <vector>

#include "Wall.hpp"
#include "Sector.hpp"
#include "../Math/Vector/Vector2.hpp"

namespace Player {
    inline Vector2 position = {0, 0};
    inline Vector2 velocity = {0, 0};
    inline float angle = 0;

    inline float speed = 46.0f;
    inline float runningSpeed = 90.0f;
    inline float size = 1.0f;
    inline float eyeHeight = 12.0f;
    inline float stepSize = 8.0f;
    inline float bodySize = 10.0f;

    inline float camZ = 0.5;

    inline int currentSector;
    inline float currentSpeed;
    inline float currentEyeHeight;
    inline int currentFloor;

    inline bool noClip;

    int FindCurrentSector(const std::vector<Sector>& sectors);
    void Update(const std::vector<Wall>& walls, const std::vector<Sector>& sectors);
    void Start(const std::vector<Sector>& sectors);
}

#endif //WOLFY_ENGINE_PLAYER_H