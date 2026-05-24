//
// Created by berke on 5/24/2026.
//

#ifndef TILKY_ENGINE_CAMERASYSTEM_HPP
#define TILKY_ENGINE_CAMERASYSTEM_HPP

#include <cmath>
#include <numbers>

#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Components.hpp"

namespace CameraSystem {
    inline Vector3 GetCameraForward(
        const float yawDegrees,
        const float pitchDegrees
    ) {
        const float yawRadians =
            yawDegrees * std::numbers::pi_v<float> / 180.0f;

        const float pitchRadians =
            pitchDegrees * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        return {
            yawSin * pitchCos,
            pitchSin,
            yawCos * pitchCos
        };
    }

    inline void RebuildCameraMatrices(
        const ComponentTransform& transform,
        ComponentCamera& camera
    ) {
        camera.forward = GetCameraForward(
            camera.yaw,
            camera.pitch
        );

        camera.target = {
            transform.position.x + camera.forward.x,
            transform.position.y + camera.forward.y,
            transform.position.z + camera.forward.z
        };

        camera.view = Matrix4::LookAt(
            transform.position,
            camera.target,
            {0.0f, 1.0f, 0.0f}
        );

        camera.projection = Matrix4::Perspective(
            camera.fov,
            camera.aspectRatio,
            camera.nearPlane,
            camera.farPlane
        );
    }
}

#endif // TILKY_ENGINE_CAMERASYSTEM_HPP