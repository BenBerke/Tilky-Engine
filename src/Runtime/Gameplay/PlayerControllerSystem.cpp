//
// Created by berke on 4/13/2026.
//
#include "Headers/Runtime/Gameplay/PlayerControllerSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"

namespace {
    const Vector3 GetCameraPosition(
        const ComponentPlayerController& controller,
        const ComponentTransform& playerTransform
    ) {
        return {
            playerTransform.position.x,
            playerTransform.position.y,
            playerTransform.position.z + controller.eyeHeight
        };
    }

    void UpdateAudioListener(
        const ComponentTransform& playerTransform,
        const ComponentPlayerController& controller,
        const ComponentCamera& camera,
        const ComponentRigidbody& rigidbody
    ) {
        SoundManager::SetListenerPosition(GetCameraPosition(controller, playerTransform));
        SoundManager::SetListenerOrientation(camera.forward);
        SoundManager::SetListenerVelocity(rigidbody.velocity);
    }
}

namespace {
    double jumpPressedTimeStamp = -std::numeric_limits<double>::infinity();
}

namespace PlayerControllerSystem {
    void Start(
        ComponentPlayerController &controller,
        const ComponentTransform &playerTransform,
        const ComponentRigidbody &rigidbody,
        const ComponentCamera &camera,
        const std::vector<Sector> &sectors
    ) {
        (void) sectors;

        UpdateAudioListener(playerTransform, controller, camera, rigidbody);
    }

    void Update(
        ComponentPlayerController &controller,
        ComponentTransform &playerTransform,
        ComponentCamera &camera,
        ComponentRigidbody &rigidbody,
        ComponentCollider *sphereCollider,
        const std::vector<Sector> &sectors
    ) {
        // Use x/y for map lookup.
        // In your engine:
        // x = world X
        // y = world Z / planar depth
        // z = height


        Vector2 input = {0.0f, 0.0f};

        if (InputManager::GetKey(SDL_SCANCODE_W)) input.y += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_S)) input.y -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_A)) input.x += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_D)) input.x -= 1.0f;

        const bool grounded =
        playerTransform.sectorIndex != -1 &&
        std::abs(playerTransform.position.z - sectors[playerTransform.sectorIndex].floorHeight) < 0.05f &&
        rigidbody.velocity.z <= 0.0f;

        if (InputManager::GetKeyDown(SDL_SCANCODE_SPACE)) jumpPressedTimeStamp = GameTime::time;

        // GameTime::time is seconds. controller.jumpBufferMs is milliseconds.
        const double jumpBufferSeconds = static_cast<double>(controller.jumpBufferMs) / 1000.0;

        const double jumpBufferAge = GameTime::time - jumpPressedTimeStamp;

        const bool hasBufferedJump = jumpBufferAge >= 0.0 && jumpBufferAge <= jumpBufferSeconds;

        if (hasBufferedJump && grounded) {
            rigidbody.velocity.z = controller.jumpPower;
            jumpPressedTimeStamp = -std::numeric_limits<double>::infinity();
        }

        // Optional cleanup: expire old buffered input.
        if (jumpBufferAge > jumpBufferSeconds) {
            jumpPressedTimeStamp = -std::numeric_limits<double>::infinity();
        }

        controller.currentSpeed =
                InputManager::GetKey(SDL_SCANCODE_LSHIFT) &&
                InputManager::GetKey(SDL_SCANCODE_W)
                    ? controller.runningSpeed
                    : controller.speed;

        if (InputManager::GetKeyDown(SDL_SCANCODE_V)) controller.noClip = !controller.noClip;

        if (sphereCollider != nullptr) sphereCollider->isActive = !controller.noClip;

        camera.yaw -= InputManager::GetMouseDelta().x * controller.sensitivityX;
        camera.pitch -= InputManager::GetMouseDelta().y * controller.sensitivityY;

        camera.pitch = std::clamp(
            camera.pitch,
            controller.minPitch,
            controller.maxPitch
        );

        camera.yaw = std::fmod(camera.yaw, 360.0f);

        if (camera.yaw < 0.0f) camera.yaw += 360.0f;


        camera.yaw = std::clamp(
            camera.yaw,
            controller.minYaw,
            controller.maxYaw
        );

        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = {yawSin, yawCos};
        const Vector2 right = {yawCos, -yawSin};

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDirection =
                    Vector2Math::Normalized(
                        right * input.x + forward * input.y
                    );

            const Vector2 desiredVelocity =
                    moveDirection * controller.currentSpeed;

            rigidbody.velocity.x = desiredVelocity.x;
            rigidbody.velocity.y = desiredVelocity.y;
        } else {
            rigidbody.velocity.x = 0.0f;
            rigidbody.velocity.y = 0.0f;
        }

        (void)sectors;

        UpdateAudioListener(playerTransform, controller, camera, rigidbody);
    }
}
