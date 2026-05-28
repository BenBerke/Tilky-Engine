//
// Created by berke on 4/13/2026.
//

#include "../../Headers/Runtime/PlayerControllerSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../../Headers/Engine/InputManager.hpp"
#include "../../Headers/Engine/GameTime.hpp"

#include "../../Headers/Math/Vector/Vector2Math.hpp"
#include "../../Headers/Math/Geometry/Geometry.hpp"

#include "../../Headers/Map/MapQueries.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"

constexpr int COLLISION_ITERATIONS = 4;

namespace {
    bool IsValidSectorIndex(const int index, const std::vector<Sector> &sectors) {
        return index >= 0 && index < static_cast<int>(sectors.size());
    }

    float GetSectorHeight(const Sector &sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    int GetSafeFloorCount(const Sector &sector) {
        return std::max(1, sector.floorCount);
    }

    float GetFloorBaseHeight(
        const Sector &sector,
        const int floorIndex
    ) {
        const float sectorHeight = GetSectorHeight(sector);

        return sector.floorHeight +
               sectorHeight * static_cast<float>(floorIndex);
    }

    float GetEyeHeightForFloor(
        const ComponentPlayerController &controller,
        const Sector &sector,
        const int floorIndex
    ) {
        return GetFloorBaseHeight(sector, floorIndex) + controller.eyeHeight;
    }

    float GetWorldBodyZ(
        const ComponentTransform &transform,
        const std::vector<Sector> &sectors
    ) {
        if (!IsValidSectorIndex(transform.sectorIndex, sectors)) {
            return transform.position.z;
        }

        const Sector &sector = sectors[transform.sectorIndex];

        const int floor = std::clamp(
            transform.floor,
            0,
            GetSafeFloorCount(sector) - 1
        );

        return GetFloorBaseHeight(sector, floor) + transform.position.z;
    }

    float GetWorldEyeHeight(
        const ComponentPlayerController &controller,
        const ComponentTransform &transform,
        const std::vector<Sector> &sectors
    ) {
        return GetWorldBodyZ(transform, sectors) + controller.eyeHeight;
    }

    int GetFloorFromWorldEyeHeight(
        const ComponentPlayerController &controller,
        const Sector &sector,
        const float worldEyeHeight
    ) {
        const float sectorHeight = GetSectorHeight(sector);

        if (sectorHeight <= 0.0001f) {
            return 0;
        }

        const int floorCount = GetSafeFloorCount(sector);

        int bestFloor = 0;
        float bestDifference = std::numeric_limits<float>::max();

        for (int floor = 0; floor < floorCount; ++floor) {
            const float candidateEyeHeight =
                    GetEyeHeightForFloor(controller, sector, floor);

            const float difference =
                    std::abs(candidateEyeHeight - worldEyeHeight);

            if (difference < bestDifference) {
                bestDifference = difference;
                bestFloor = floor;
            }
        }

        return bestFloor;
    }

    void EnterSectorKeepingWorldEyeHeight(
        ComponentPlayerController &controller,
        ComponentTransform &playerTransform,
        const int newSector,
        const std::vector<Sector> &sectors
    ) {
        if (!IsValidSectorIndex(newSector, sectors)) {
            return;
        }

        const float oldWorldBodyZ = GetWorldBodyZ(playerTransform, sectors);
        const float oldWorldEyeHeight = oldWorldBodyZ + controller.eyeHeight;

        const Sector &sector = sectors[newSector];

        const int newFloor = GetFloorFromWorldEyeHeight(
            controller,
            sector,
            oldWorldEyeHeight
        );

        const float newFloorBase = GetFloorBaseHeight(sector, newFloor);

        playerTransform.sectorIndex = newSector;
        playerTransform.floor = newFloor;

        playerTransform.position.z = oldWorldBodyZ - newFloorBase;

        if (playerTransform.position.z < 0.0f) {
            playerTransform.position.z = 0.0f;
        }

        controller.currentEyeHeight = GetWorldEyeHeight(controller, playerTransform, sectors);
    }

    void UpdateAudioListener(
    const ComponentTransform& playerTransform,
    const ComponentPlayerController& controller,
    const ComponentCamera& camera,
    const ComponentRigidbody& rigidbody
) {
        SoundManager::SetListenerPosition(playerTransform.position);
        SoundManager::SetListenerOrientation(camera.forward);
        SoundManager::SetListenerVelocity(rigidbody.velocity);
    }
}

namespace PlayerControllerSystem {
    void Start(
        ComponentPlayerController &controller,
        ComponentTransform &playerTransform,
        const ComponentRigidbody &rigidbody,
        const ComponentCamera &camera,
        const std::vector<Sector> &sectors
    ) {
        const Vector2 planarPosition = GetPlanarPosition(playerTransform);

        int foundSector = MapQueries::FindSectorContainingPoint(
            sectors,
            planarPosition
        );

        if (foundSector == -1) foundSector = sectors.empty() ? -1 : 0;

        playerTransform.sectorIndex = foundSector;

        if (IsValidSectorIndex(playerTransform.sectorIndex, sectors)) {
            const Sector &sector = sectors[playerTransform.sectorIndex];

            playerTransform.floor = std::clamp(
                playerTransform.floor,
                0,
                GetSafeFloorCount(sector) - 1
            );

            if (playerTransform.position.z < 0.0f) {
                playerTransform.position.z = 0.0f;
            }

            controller.currentEyeHeight =
                    GetWorldEyeHeight(controller, playerTransform, sectors);
        } else {
            playerTransform.floor = 0;
            controller.currentEyeHeight =
                    playerTransform.position.z + controller.eyeHeight;
        }

        UpdateAudioListener(playerTransform, controller, camera, rigidbody);
    }

    void Update(
        ComponentPlayerController &controller,
        const ComponentTransform &playerTransform,
        ComponentCamera &camera,
        ComponentRigidbody &rigidbody,
        ComponentSphereCollider &sphereCollider,
        const std::vector<Sector> &sectors
    ) {
        (void) playerTransform;
        (void) sectors;

        Vector2 input = {0.0f, 0.0f};

        if (InputManager::GetKey(SDL_SCANCODE_W)) input.y += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_S)) input.y -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_A)) input.x -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_D)) input.x += 1.0f;

        controller.currentSpeed =
                InputManager::GetKey(SDL_SCANCODE_LSHIFT) && InputManager::GetKey(SDL_SCANCODE_W)
                    ? controller.runningSpeed
                    : controller.speed;

        if (InputManager::GetKeyDown(SDL_SCANCODE_V)) controller.noClip = !controller.noClip;

        sphereCollider.isActive = !controller.noClip;

        camera.yaw -= InputManager::GetMouseDelta().x * controller.sensitivityX;
        camera.pitch -= InputManager::GetMouseDelta().y * controller.sensitivityY;

        camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
        camera.yaw = std::fmod(camera.yaw, 360.0f);

        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = {yawSin, yawCos};
        const Vector2 right = {yawCos, -yawSin};

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDirection =
                Vector2Math::Normalized(right * input.x + forward * input.y);

            const Vector2 desiredVelocity =
                moveDirection * controller.currentSpeed;

          //  rigidbody.velocity.x = desiredVelocity.x;
          //  rigidbody.velocity.y = desiredVelocity.y;
        } else {
            rigidbody.velocity.x = 0.0f;
            rigidbody.velocity.y = 0.0f;
        }

        SoundManager::SetListenerPosition(playerTransform.position);
        SoundManager::SetListenerOrientation(camera.forward);
        SoundManager::SetListenerVelocity(rigidbody.velocity);
    }
}
