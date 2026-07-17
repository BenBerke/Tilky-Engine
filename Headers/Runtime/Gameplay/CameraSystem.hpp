//
// Created by berke on 5/24/2026.
//

#ifndef TILKY_ENGINE_CAMERASYSTEM_HPP
#define TILKY_ENGINE_CAMERASYSTEM_HPP

#include <cmath>

#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Components.hpp"

namespace CameraSystem {
    inline Vector3 GetCameraForwardEngineSpace(const float yawDegrees, const float pitchDegrees) {
        const float yawRadians = yawDegrees * Constants::DegToRad;

        const float pitchRadians = pitchDegrees * Constants::DegToRad;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        return {yawSin * pitchCos, pitchSin, yawCos * pitchCos};
    }

    inline void RebuildCameraMatrices(const ComponentTransform& transform, ComponentCamera& camera) {
        camera.forward = GetCameraForwardEngineSpace(camera.yaw, camera.pitch);

        const Vector3 cameraPosition = {
            transform.position.x,
            transform.position.y,
            transform.position.z
        };

        const Vector3 target = {
            cameraPosition.x + camera.forward.x,
            cameraPosition.y + camera.forward.y,
            cameraPosition.z + camera.forward.z
        };

        camera.view = Matrix4::LookAt(cameraPosition, target, {0.0f, 1.0f, 0.0f});

        // camera.projection = Matrix4::PerspectiveReverseZ(
        //     camera.fov,
        //     camera.aspectRatio,
        //     camera.nearPlane
        // );

        camera.projection = Matrix4::PerspectiveReverseZ(
            camera.fov,
            camera.aspectRatio,
            camera.nearPlane,
            camera.farPlane
        );
    }
}

#endif // TILKY_ENGINE_CAMERASYSTEM_HPP