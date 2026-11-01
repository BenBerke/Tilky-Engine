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
    Vector2 input = {.0f, .0f};
}

namespace PlayerControllerSystem {
    void Update(
        ComponentPlayerController& controller,
        ComponentTransform& playerTransform,
        ComponentCamera* ownCamera,
        const ComponentCamera* activeCamera,
        ComponentRigidbody& rigidbody,
        const std::vector<Sector>& sectors
    ) {
        (void)sectors;

        // Reset every frame, otherwise input accumulates forever.
        input = {0.0f, 0.0f};

        if (InputManager::GetKey(controller.forward)) input.y += 1.0f;
        if (InputManager::GetKey(controller.backwards)) input.y -= 1.0f;
        if (InputManager::GetKey(controller.left)) input.x += 1.0f;
        if (InputManager::GetKey(controller.right)) input.x -= 1.0f;

        if (InputManager::GetKeyDown(controller.jump)) jumpPressedTimeStamp = GameTime::timeInSeconds;

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
            InputManager::GetKey(controller.run) &&
            InputManager::GetKey(controller.forward)
                ? controller.runningSpeed
                : controller.speed;

        // Mouse look only while the player's own camera is the one in use,
        // and the cursor is locked: a free cursor is for pointing at things.
        if (ownCamera != nullptr && ownCamera->isActive &&
            InputManager::GetCursorLockMode() == InputManager::CursorLockMode::Locked) {
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

        // Smooth horizontal movement: accelerate toward the target velocity,
        // decelerate toward zero when there's no input.
        const Vector2 currentVelocity = {rigidbody.velocity.x, rigidbody.velocity.z};
        Vector2 targetVelocity = {0.0f, 0.0f};

        const bool hasInput = input.x != 0.0f || input.y != 0.0f;
        if (hasInput) {
            const Vector2 moveDirection = Vector2Math::Normalized(right * input.x + forward * input.y);
            targetVelocity = moveDirection * controller.currentSpeed;
        }

        const float rate = hasInput ? controller.acceleration : controller.deceleration;
        const float control = rigidbody.isGrounded ? 1.0f : controller.airControl;
        const float dt = GameTime::deltaTime;
        const float maxDelta = rate * control * dt;

        // Move velocity toward the target by at most maxDelta.
        // Constant-rate approach (not a lerp), so it doesn't get mushy near the target.
        const Vector2 diff = targetVelocity - currentVelocity;
        const float diffLen = std::sqrt(diff.x * diff.x + diff.y * diff.y);

        Vector2 newVelocity = targetVelocity;
        if (diffLen > maxDelta && diffLen > 0.0f) newVelocity = currentVelocity + diff * (maxDelta / diffLen);

        // Sliding down a slope too steep to stand on, the controller steers
        // but never brakes the slide: braking toward the target is stronger
        // than gravity along any slope, and the body barely slid at all.
        const Vector2 slideDirection = {rigidbody.slideNormal.x, rigidbody.slideNormal.z};
        const float slideDirectionLength = std::sqrt(slideDirection.x * slideDirection.x + slideDirection.y * slideDirection.y);

        if (slideDirectionLength > 0.0f) {
            const Vector2 downhill = slideDirection * (1.0f / slideDirectionLength);
            const float slideBefore = currentVelocity.x * downhill.x + currentVelocity.y * downhill.y;
            const float slideAfter = newVelocity.x * downhill.x + newVelocity.y * downhill.y;

            if (slideAfter < slideBefore) newVelocity = newVelocity + downhill * (slideBefore - slideAfter);
        }

        rigidbody.velocity.x = newVelocity.x;
        rigidbody.velocity.z = newVelocity.y;
    }
}