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
    inline Vector3 GetCameraForwardEngineSpace(
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

        // Engine/game convention:
        // x = world X
        // y = world Z/depth
        // z = height/up
        return {
            yawSin * pitchCos,
            yawCos * pitchCos,
            pitchSin
        };
    }

    inline Vector3 EngineToRenderSpace(const Vector3& v) {
        // Convert:
        // engine x = world X       -> render x
        // engine y = world Z/depth -> render z
        // engine z = height/up     -> render y
        return {
            v.x,
            v.z,
            v.y
        };
    }

    inline void RebuildCameraMatrices(
        const ComponentTransform& transform,
        ComponentCamera& camera
    ) {
        camera.forward = GetCameraForwardEngineSpace(
            camera.yaw,
            camera.pitch
        );

        const Vector3 cameraPositionEngine = {
            transform.position.x,
            transform.position.y,
            transform.position.z
        };

        const Vector3 targetEngine = {
            cameraPositionEngine.x + camera.forward.x,
            cameraPositionEngine.y + camera.forward.y,
            cameraPositionEngine.z + camera.forward.z
        };

        const Vector3 cameraPositionRender =
            EngineToRenderSpace(cameraPositionEngine);

        const Vector3 targetRender =
            EngineToRenderSpace(targetEngine);

        camera.view = Matrix4::LookAt(
            cameraPositionRender,
            targetRender,
            {0.0f, 1.0f, 0.0f} // render-space up
        );

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