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
    Vector2 ClosestPointOnSegment(
        const Wall& wall,
        const Vector2& point
    ) {
        if (wall.length <= 0.00001f) {
            return wall.start;
        }

        float distanceAlongWall =
            Vector2Math::Dot(point - wall.start, wall.dir);

        distanceAlongWall = std::clamp(
            distanceAlongWall,
            0.0f,
            wall.length
        );

        return wall.start + wall.dir * distanceAlongWall;
    }

    int activePortalWallIndex = -1;

    bool IsPortalWall(const Wall& wall) {
        return wall.frontSector != -1 &&
               wall.backSector != -1 &&
               wall.backSector != wall.frontSector;
    }

    bool IsValidSectorIndex(const int index, const std::vector<Sector> &sectors) {
        return index >= 0 && index < static_cast<int>(sectors.size());
    }

    float GetSectorHeight(const Sector& sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    int GetSafeFloorCount(const Sector& sector) {
        return std::max(1, sector.floorCount);
    }

    float GetFloorBaseHeight(
        const Sector& sector,
        const int floorIndex
    ) {
        const float sectorHeight = GetSectorHeight(sector);

        return sector.floorHeight +
            sectorHeight * static_cast<float>(floorIndex);
    }

    float GetEyeHeightForFloor(
        const ComponentPlayerController& controller,
        const Sector& sector,
        const int floorIndex
    ) {
        return GetFloorBaseHeight(sector, floorIndex) + controller.eyeHeight;
    }

    int GetFloorFromWorldEyeHeight(
        const ComponentPlayerController& controller,
        const Sector& sector,
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

        const Sector &sector = sectors[newSector];

        const int newFloor =
                GetFloorFromWorldEyeHeight(
                    controller,
                    sector,
                    controller.currentEyeHeight
                );

        const float newEyeHeight =
                GetEyeHeightForFloor(
                    controller,
                    sector,
                    newFloor
                );

        controller.currentSector = newSector;
        controller.currentFloor = newFloor;
        controller.currentEyeHeight = newEyeHeight;

        playerTransform.position.y = controller.currentEyeHeight;
    }

    int GetOtherPortalSector(
        const Wall& wall,
        const int currentSector
    ) {
        if (currentSector == wall.frontSector) {
            return wall.backSector;
        }

        if (currentSector == wall.backSector) {
            return wall.frontSector;
        }

        return -1;
    }

    bool CanStepIntoSector(
        const ComponentPlayerController& controller,
        const int currentSector,
        const int newSector,
        const std::vector<Sector>& sectors
    ) {
        if (!IsValidSectorIndex(currentSector, sectors) ||
            !IsValidSectorIndex(newSector, sectors)) {
            return false;
        }

        const Sector& next = sectors[newSector];

        const int newFloor =
            GetFloorFromWorldEyeHeight(
                controller,
                next,
                controller.currentEyeHeight
            );

        const float newEyeHeight =
            GetEyeHeightForFloor(
                controller,
                next,
                newFloor
            );

        const float heightDifference =
            newEyeHeight - controller.currentEyeHeight;

        if (heightDifference > 0.0f) {
            return heightDifference <= controller.stepSize;
        }

        if (heightDifference < 0.0f) {
            const float fallDistance = -heightDifference;

            if (fallDistance <= controller.stepSize) {
                return true;
            }

            if (fallDistance >= controller.bodySize) {
                return true;
            }

            return false;
        }

        return true;
    }

    int FindCurrentSectorBetweenPortalSides(
        const ComponentPlayerController &controller,
        const ComponentTransform &playerTransform,
        const Wall &wall,
        const std::vector<Sector> &sectors
    ) {
        const Vector2 planarPosition =
            PlayerControllerSystem::GetPlanarPosition(playerTransform);

        if (IsValidSectorIndex(wall.frontSector, sectors) &&
            Geometry::IsPointInPolygon(
                sectors[wall.frontSector].vertices,
                planarPosition
            )) {
            return wall.frontSector;
            }

        if (IsValidSectorIndex(wall.backSector, sectors) &&
            Geometry::IsPointInPolygon(
                sectors[wall.backSector].vertices,
                planarPosition
            )) {
            return wall.backSector;
            }

        return controller.currentSector;
    }

    void UpdateAudioListener(const ComponentTransform& playerTransform,const ComponentPlayerController& controller,const ComponentCamera& camera) {
        SoundManager::SetListenerPosition(playerTransform.position);
        SoundManager::SetListenerOrientation(camera.forward);
        SoundManager::SetListenerVelocity(controller.velocity);
    }
}

namespace PlayerControllerSystem {
    void Start(
    ComponentPlayerController& controller,
    ComponentTransform& playerTransform,
    const ComponentCamera& camera,
    const std::vector<Sector>& sectors
) {
        const Vector2 planarPosition = GetPlanarPosition(playerTransform);

        controller.currentSector = MapQueries::FindSectorContainingPoint(
            sectors,
            planarPosition
        );

        if (IsValidSectorIndex(controller.currentSector, sectors)) {
            controller.currentFloor = std::clamp(
                controller.currentFloor,
                0,
                GetSafeFloorCount(sectors[controller.currentSector]) - 1
            );

            controller.currentEyeHeight = GetEyeHeightForFloor(
                controller,
                sectors[controller.currentSector],
                controller.currentFloor
            );
        }
        else {
            controller.currentSector = sectors.empty() ? -1 : 0;
            controller.currentFloor = 0;

            if (!sectors.empty()) {
                controller.currentEyeHeight = GetEyeHeightForFloor(
                    controller,
                    sectors[0],
                    0
                );
            }
            else {
                controller.currentEyeHeight = controller.eyeHeight;
            }
        }

        playerTransform.position.y = controller.currentEyeHeight;

        UpdateAudioListener(
            playerTransform,
            controller,
            camera
        );
    }

    void Update(
        ComponentPlayerController& controller,
        ComponentTransform& playerTransform,
        ComponentCamera& camera,
        const std::vector<Wall>& walls,
        const std::vector<Sector>& sectors
    ) {
        // The full wall list is kept in the signature for compatibility,
        // but collision currently uses sectors[currentSector].walls.
        (void)walls;

        Vector2 input = {0.0f, 0.0f};

        if (InputManager::GetKey(SDL_SCANCODE_W)) {
            input.y += 1.0f;
        }

        if (InputManager::GetKey(SDL_SCANCODE_A)) {
            input.x += 1.0f;
        }

        if (InputManager::GetKey(SDL_SCANCODE_S)) {
            input.y -= 1.0f;
        }

        if (InputManager::GetKey(SDL_SCANCODE_D)) {
            input.x -= 1.0f;
        }

        if (InputManager::GetKey(SDL_SCANCODE_LSHIFT)) {
            controller.currentSpeed = controller.runningSpeed;
        }
        else {
            controller.currentSpeed = controller.speed;
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_V)) {
            controller.noClip = !controller.noClip;
        }

        camera.yaw -= InputManager::GetMouseDelta().x * controller.sensitivityX;
        camera.pitch -= InputManager::GetMouseDelta().y * controller.sensitivityY;

        camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);

        if (camera.yaw >= 360.0f) {
            camera.yaw -= 360.0f;
        }
        else if (camera.yaw < 0.0f) {
            camera.yaw += 360.0f;
        }

        const float yawRadians =
            camera.yaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = {
            yawSin,
            yawCos
        };

        const Vector2 right = {
            yawCos,
            -yawSin
        };

        Vector2 planarVelocity = GetPlanarVelocity(controller);

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDirection =
                right * input.x + forward * input.y;

            planarVelocity =
                Vector2Math::Normalized(moveDirection) *
                controller.currentSpeed;
        }
        else {
            planarVelocity *= controller.friction;
        }

        Vector2 planarPosition = GetPlanarPosition(playerTransform);
        planarPosition += planarVelocity * GameTime::deltaTime;

        SetPlanarPosition(playerTransform, planarPosition);
        SetPlanarVelocity(controller, planarVelocity);

        bool touchingPortalThisFrame = false;

        if (IsValidSectorIndex(controller.currentSector, sectors)) {
            for (int iter = 0; iter < COLLISION_ITERATIONS; ++iter) {
                bool collided = false;

                const Sector& currentSector =
                    sectors[controller.currentSector];

                for (
                    int i = 0;
                    i < static_cast<int>(currentSector.walls.size());
                    ++i
                ) {
                    const Wall& wall = currentSector.walls[i];

                    if (wall.floor != controller.currentFloor) {
                        continue;
                    }

                    planarPosition = GetPlanarPosition(playerTransform);
                    planarVelocity = GetPlanarVelocity(controller);

                    const Vector2 closest =
                        ClosestPointOnSegment(wall, planarPosition);

                    const Vector2 delta =
                        planarPosition - closest;

                    const float distSq =
                        Vector2Math::Dot(delta, delta);

                    const float radiusSq =
                        controller.size * controller.size;

                    if (distSq >= radiusSq) {
                        continue;
                    }

                    const float dist =
                        std::sqrt(distSq);

                    Vector2 normal;

                    if (dist > 0.00001f) {
                        normal = delta * (1.0f / dist);
                    }
                    else {
                        normal = wall.normal;
                    }

                    if (IsPortalWall(wall)) {
                        const int newSector = GetOtherPortalSector(
                            wall,
                            controller.currentSector
                        );

                        if (CanStepIntoSector(
                            controller,
                            controller.currentSector,
                            newSector,
                            sectors
                        )) {
                            touchingPortalThisFrame = true;
                            activePortalWallIndex = i;

                            const int sectorOnOtherSide =
                                FindCurrentSectorBetweenPortalSides(
                                    controller,
                                    playerTransform,
                                    wall,
                                    sectors
                                );

                            if (sectorOnOtherSide != controller.currentSector) {
                                EnterSectorKeepingWorldEyeHeight(
                                    controller,
                                    playerTransform,
                                    sectorOnOtherSide,
                                    sectors
                                );
                            }

                            continue;
                        }
                    }

                    const float penetration =
                        controller.size - dist;

                    if (!controller.noClip) {
                        planarPosition += normal * penetration;
                    }

                    const float intoWall =
                        Vector2Math::Dot(normal, planarVelocity);

                    if (intoWall < 0.0f) {
                        planarVelocity -= normal * intoWall;
                    }

                    SetPlanarPosition(playerTransform, planarPosition);
                    SetPlanarVelocity(controller, planarVelocity);

                    collided = true;
                }

                if (!collided) {
                    break;
                }
            }
        }

        planarPosition = GetPlanarPosition(playerTransform);

        const int foundSector =
            MapQueries::FindSectorContainingPoint(
                sectors,
                planarPosition
            );

        if (
            foundSector != -1 &&
            foundSector != controller.currentSector
        ) {
            EnterSectorKeepingWorldEyeHeight(
                controller,
                playerTransform,
                foundSector,
                sectors
            );
        }

        if (IsValidSectorIndex(controller.currentSector, sectors)) {
            const Sector& sector = sectors[controller.currentSector];

            controller.currentFloor = std::clamp(
                controller.currentFloor,
                0,
                GetSafeFloorCount(sector) - 1
            );

            controller.currentEyeHeight = GetEyeHeightForFloor(
                controller,
                sector,
                controller.currentFloor
            );
        }

        playerTransform.position.y = controller.currentEyeHeight;

        if (!touchingPortalThisFrame) {
            activePortalWallIndex = -1;
        }

        UpdateAudioListener(
            playerTransform,
            controller,
            camera
        );
    }
}