//
// Created by berke on 6/3/2026.
//

#include "Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp"

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
    ComponentCamera* camera = nullptr;
    ComponentTransform* transform = nullptr;

    constexpr float MOUSE_SENSITIVITY = 0.5f;
    constexpr float MOVE_SPEED = 50.0f;
    constexpr float FAST_MOVE_SPEED = 75.0f;
    constexpr float RAY_LENGTH = 10000.0f;

    Vector3 GetFreecamForward() {
        const float yawRadians =
            camera->yaw * std::numbers::pi_v<float> / 180.0f;

        const float pitchRadians =
            camera->pitch * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        return Vector3Math::Normalized({
            yawSin * pitchCos,
            yawCos * pitchCos,
            pitchSin
        });
    }
}

namespace RuntimeEditor {
    void Start(Level& level) {
        spdlog::info("Runtime editor started");

        camera = nullptr;
        transform = nullptr;

        for (ComponentCamera& _camera : level.cameras.components) {
            if (_camera.isActive) {
                camera = &_camera;
                transform = level.transforms.Get(camera->ownerID);
                break;
            }
        }

        if (camera == nullptr || transform == nullptr) {
            spdlog::error("RuntimeEditor::Start failed: no active camera or camera transform");
        }
    }

    void Update(Level& level) {
        if (camera == nullptr || transform == nullptr) return;

        camera->yaw -= InputManager::GetMouseDelta().x * MOUSE_SENSITIVITY;
        camera->pitch -= InputManager::GetMouseDelta().y * MOUSE_SENSITIVITY;

        camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
        camera->yaw = std::fmod(camera->yaw, 360.0f);

        const float yawRadians = camera->yaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = { yawSin, yawCos };
        const Vector2 right = { yawCos, -yawSin };

        //region movement

        Vector3 movement = { 0.0f, 0.0f, 0.0f };

        if (InputManager::GetKey(SDL_SCANCODE_W)) {
            movement.x += forward.x;
            movement.y += forward.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_S)) {
            movement.x -= forward.x;
            movement.y -= forward.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_A)) {
            movement.x += right.x;
            movement.y += right.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_D)) {
            movement.x -= right.x;
            movement.y -= right.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_SPACE)) movement.z += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_LCTRL)) movement.z -= 1.0f;

        const float length = std::sqrt(
            movement.x * movement.x +
            movement.y * movement.y +
            movement.z * movement.z
        );

        if (length > 0.0f) {
            movement.x /= length;
            movement.y /= length;
            movement.z /= length;
        }

        const float speed = InputManager::GetKey(SDL_SCANCODE_LSHIFT)
            ? FAST_MOVE_SPEED
            : MOVE_SPEED;

        transform->AddPosition(movement * speed * GameTime::deltaTime);

        //endregion

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
            const Vector3 rayOrigin = transform->position;
            const Vector3 rayDirection = GetFreecamForward();

            const std::optional<RayHit> hit = GameFunctions::Raycast(
                level,
                rayOrigin,
                rayDirection,
                RAY_LENGTH,
                camera->ownerID,
                false
            );

            if (!hit.has_value()) {
                spdlog::info("Runtime editor ray missed");
                return;
            }

            if (hit->entity != nullptr) {
                spdlog::info(
                    "Runtime editor ray hit entity {} at ({}, {}, {}), distance {}",
                    hit->entity->id,
                    hit->position.x,
                    hit->position.y,
                    hit->position.z,
                    hit->distance
                );
            }
            else if (hit->wall != nullptr) {
                spdlog::info(
                    "Runtime editor ray hit wall at ({}, {}, {}), distance {}",
                    hit->position.x,
                    hit->position.y,
                    hit->position.z,
                    hit->distance
                );
            }
        }
    }

    void Shutdown(Level& level) {
        camera = nullptr;
        transform = nullptr;

        spdlog::info("Runtime editor shut down");
    }
}