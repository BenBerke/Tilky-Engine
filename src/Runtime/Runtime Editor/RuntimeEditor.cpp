//
// Created by berke on 6/3/2026.
//

#include "../../../Headers/Runtime/Runtime Editor/RuntimeEditor.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"

namespace {
    ComponentCamera camera;
    ComponentTransform* transform;
}

namespace RuntimeEditor {
    void Start(Level& level) {
        for (const ComponentCamera &_camera: level.cameras.components)
            if (_camera.isActive) {
                camera = _camera;
                transform = level.transforms.Get(camera.ownerID);
            }
    }

    void Update(Level& level) {
        camera.yaw -= InputManager::GetMouseDelta().x * .5f;
        camera.pitch -= InputManager::GetMouseDelta().y * .5f;

        camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
        camera.yaw = std::fmod(camera.yaw, 360.0f);

        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = {yawSin, yawCos};
        const Vector2 right = {yawCos, -yawSin};
    }

    void Shutdown(Level& level) {

    }
}