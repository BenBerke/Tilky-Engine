//
// Created by berke on 4/13/2026.
//

#include "Headers/Runtime/Gameplay/PlayerControllerSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"

/// This is a built-in script for handling player movement.
/// Anything in this script can technically be achieved through a user-made Lua script.

namespace {
    double jumpPressedTimeStamp = -std::numeric_limits<double>::infinity();
}

namespace PlayerControllerSystem {
    void Update(
        ComponentPlayerController& controller,
        ComponentTransform& playerTransform,
        ComponentCamera* ownCamera,
        const ComponentCamera* activeCamera,
        ComponentRigidbody& rigidbody,
        ComponentCollider* sphereCollider,
        const std::vector<Sector>& sectors
    ) {
        (void)sectors;

        Vector2 input = {0.0f, 0.0f};

        if (InputManager::GetKey(SDL_SCANCODE_W)) input.y += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_S)) input.y -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_A)) input.x += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_D)) input.x -= 1.0f;

        if (InputManager::GetKeyDown(SDL_SCANCODE_SPACE)) jumpPressedTimeStamp = GameTime::timeInSeconds;

        const double jumpBufferSeconds = static_cast<double>(controller.jumpBufferMs) / 1000.0;

        const double jumpBufferAge = GameTime::timeInSeconds - jumpPressedTimeStamp;

        const bool hasBufferedJump = jumpBufferAge >= 0.0 && jumpBufferAge <= jumpBufferSeconds;

        if (hasBufferedJump && rigidbody.isGrounded) {
            rigidbody.velocity.y = controller.jumpPower;
            rigidbody.isGrounded = false;
            rigidbody.groundNormal = {};
            jumpPressedTimeStamp = -std::numeric_limits<double>::infinity();
        }

        if (jumpBufferAge > jumpBufferSeconds) jumpPressedTimeStamp = -std::numeric_limits<double>::infinity();

        controller.currentSpeed =
            InputManager::GetKey(SDL_SCANCODE_LSHIFT) &&
            InputManager::GetKey(SDL_SCANCODE_W)
                ? controller.runningSpeed
                : controller.speed;

        if (InputManager::GetKeyDown(SDL_SCANCODE_V)) controller.noClip = !controller.noClip;

        if (sphereCollider != nullptr) sphereCollider->isActive = !controller.noClip;

        // Mouse look only while the player's own camera is the one in use.
        if (ownCamera != nullptr && ownCamera->isActive) {
            ComponentCamera& camera = *ownCamera;

            camera.yaw -= InputManager::GetMouseDelta().x * controller.sensitivityX;
            camera.pitch -= InputManager::GetMouseDelta().y * controller.sensitivityY;

            camera.pitch = std::clamp(camera.pitch, controller.minPitch, controller.maxPitch);

            camera.yaw = std::fmod(camera.yaw, 360.0f);
            if (camera.yaw < 0.0f) camera.yaw += 360.0f;

            camera.yaw = std::clamp(camera.yaw, controller.minYaw, controller.maxYaw);
        }

        const ComponentCamera* moveCamera = ownCamera != nullptr ? ownCamera : activeCamera;
        const float moveYaw = moveCamera != nullptr ? moveCamera->yaw : 0.0f;

        const float yawRadians = moveYaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = {yawSin, yawCos};
        const Vector2 right = {yawCos, -yawSin};

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDirection = Vector2Math::Normalized(right * input.x + forward * input.y);
            const Vector2 desiredVelocity = moveDirection * controller.currentSpeed;

            rigidbody.velocity.x = desiredVelocity.x;
            rigidbody.velocity.z = desiredVelocity.y;
        }
        else {
            rigidbody.velocity.x = 0.0f;
            rigidbody.velocity.z = 0.0f;
        }
    }
}