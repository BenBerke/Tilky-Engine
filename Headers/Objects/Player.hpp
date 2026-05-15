//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_PLAYER_H
#define TILKY_ENGINE_PLAYER_H

#include <vector>

#include "Wall.hpp"
#include "Sector.hpp"

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"

namespace Player {
    // Real 3D player/camera position.
    // x = world/map X
    // y = eye/world height
    // z = world/map Y
    inline Vector3 position = {0.0f, 0.0f, 0.0f};

    // Movement velocity in 3D space.
    // For normal walking, y remains 0.
    inline Vector3 velocity = {0.0f, 0.0f, 0.0f};

    inline Vector3 cameraForward = {0.0f, 0.0f, 1.0f};
    inline Vector3 cameraTarget = {0.0f, 0.0f, 1.0f};

    inline Matrix4 view = Matrix4::Identity();
    inline Matrix4 projection = Matrix4::Identity();



    inline float speed = 46.0f;
    inline float runningSpeed = 90.0f;
    inline float size = 1.0f;

    // Eye height above the floor of the current floor/storey.
    inline float eyeHeight = 12.0f;

    inline float stepSize = 8.0f;
    inline float bodySize = 10.0f;

    // angle = yaw, left/right camera rotation.
    inline float angle = 0.0f;

    // pitch = up/down camera rotation.
    inline float pitch = 0.0f;

    inline int currentSector = -1;
    inline float currentSpeed = 0.0f;
    inline float currentEyeHeight = 0.0f;
    inline int currentFloor = 0;

    inline bool noClip = false;

    void Start(const std::vector<Sector>& sectors);
    void Update(const std::vector<Wall>& walls, const std::vector<Sector>& sectors);
}

#endif // TILKY_ENGINE_PLAYER_H