//
// Created by berke on 4/13/2026.
//

#include "../../Headers/Objects/Player.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "../../Headers/Engine/InputManager.hpp"
#include "../../Headers/Engine/GameTime.hpp"

#include "../../Headers/Math/Vector/Vector2Math.hpp"
#include "../../Headers/Math/Geometry/Geometry.hpp"

#include "../../Headers/Map/MapQueries.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"

#define FRICTION 0.8f
#define TURN_SPEED 90.0f
#define SENSITIVITY_X 0.5f
#define SENSITIVITY_Y 0.5f

constexpr int COLLISION_ITERATIONS = 4;

namespace {
    Vector2 GetPlanarPosition() {
        return {
            Player::position.x,
            Player::position.z
        };
    }

    void SetPlanarPosition(const Vector2& planarPosition) {
        Player::position.x = planarPosition.x;
        Player::position.z = planarPosition.y;
    }

    Vector2 GetPlanarVelocity() {
        return {
            Player::velocity.x,
            Player::velocity.z
        };
    }

    void SetPlanarVelocity(const Vector2& planarVelocity) {
        Player::velocity.x = planarVelocity.x;
        Player::velocity.y = 0.0f;
        Player::velocity.z = planarVelocity.y;
    }

    float GetSectorHeight(const Sector& sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    int GetSafeFloorCount(const Sector& sector) {
        return std::max(1, sector.floorCount);
    }

    float GetFloorBaseHeight(const Sector& sector, const int floorIndex) {
        const float sectorHeight = GetSectorHeight(sector);

        return sector.floorHeight + sectorHeight * static_cast<float>(floorIndex);
    }

    float GetEyeHeightForFloor(const Sector& sector, const int floorIndex) {
        return GetFloorBaseHeight(sector, floorIndex) + Player::eyeHeight;
    }

    int GetFloorFromWorldEyeHeight(
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
                GetEyeHeightForFloor(sector, floor);

            const float difference =
                std::abs(candidateEyeHeight - worldEyeHeight);

            if (difference < bestDifference) {
                bestDifference = difference;
                bestFloor = floor;
            }
        }

        return bestFloor;
    }

    Vector2 ClosestPointOnSegment(const Wall& wall, const Vector2& point) {
        if (wall.length <= 0.00001f) {
            return wall.start;
        }

        float distanceAlongWall = Vector2Math::Dot(point - wall.start, wall.dir);

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

    bool IsValidSectorIndex(const int index, const std::vector<Sector>& sectors) {
        return index >= 0 && index < static_cast<int>(sectors.size());
    }

    void EnterSectorKeepingWorldEyeHeight(
        const int newSector,
        const std::vector<Sector>& sectors
    ) {
        if (!IsValidSectorIndex(newSector, sectors)) {
            return;
        }

        const Sector& sector = sectors[newSector];

        const int newFloor = GetFloorFromWorldEyeHeight(
            sector,
            Player::currentEyeHeight
        );

        const float newEyeHeight = GetEyeHeightForFloor(
            sector,
            newFloor
        );

        Player::currentSector = newSector;
        Player::currentFloor = newFloor;
        Player::currentEyeHeight = newEyeHeight;
        Player::position.y = Player::currentEyeHeight;
    }

    int GetOtherPortalSector(const Wall& wall, const int currentSector) {
        if (currentSector == wall.frontSector) {
            return wall.backSector;
        }

        if (currentSector == wall.backSector) {
            return wall.frontSector;
        }

        return -1;
    }

    bool CanStepIntoSector(
        const int currentSector,
        const int newSector,
        const std::vector<Sector>& sectors
    ) {
        if (!IsValidSectorIndex(currentSector, sectors) ||
            !IsValidSectorIndex(newSector, sectors)) {
            return false;
        }

        const Sector& next = sectors[newSector];

        const int newFloor = GetFloorFromWorldEyeHeight(
            next,
            Player::currentEyeHeight
        );

        const float newEyeHeight = GetEyeHeightForFloor(
            next,
            newFloor
        );

        const float heightDifference =
            newEyeHeight - Player::currentEyeHeight;

        if (heightDifference > 0.0f) {
            return heightDifference <= Player::stepSize;
        }

        if (heightDifference < 0.0f) {
            const float fallDistance = -heightDifference;

            if (fallDistance <= Player::stepSize) {
                return true;
            }

            if (fallDistance >= Player::bodySize) {
                return true;
            }

            return false;
        }

        return true;
    }

    int FindCurrentSectorBetweenPortalSides(
        const Wall& wall,
        const std::vector<Sector>& sectors
    ) {
        const Vector2 planarPosition = GetPlanarPosition();

        if (IsValidSectorIndex(wall.frontSector, sectors) &&
            Geometry::IsPointInPolygon(sectors[wall.frontSector].vertices, planarPosition)) {
            return wall.frontSector;
        }

        if (IsValidSectorIndex(wall.backSector, sectors) &&
            Geometry::IsPointInPolygon(sectors[wall.backSector].vertices, planarPosition)) {
            return wall.backSector;
        }

        return Player::currentSector;
    }

    Vector3 GetCameraForward(const float yawDegrees, const float pitchDegrees) {
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

    void RebuildCameraMatrices() {
        Player::cameraForward = GetCameraForward(
            Player::angle,
            Player::pitch
        );

        Player::cameraTarget = {
            Player::position.x + Player::cameraForward.x,
            Player::position.y + Player::cameraForward.y,
            Player::position.z + Player::cameraForward.z
        };

        Player::view = Matrix4::LookAt(
            Player::position,
            Player::cameraTarget,
            {0.0f, 1.0f, 0.0f}
        );

        Player::projection = Matrix4::Perspective(
            90.0f,
            1680.0f / 960.0f,
            0.1f,
            10000.0f
        );
    }

    void UpdateAudioListener() {
        SoundManager::SetListenerPosition(Player::position);
        SoundManager::SetListenerOrientation(Player::cameraForward);
        SoundManager::SetListenerVelocity(Player::velocity);
    }
}

namespace Player {
    void Start(const std::vector<Sector>& sectors) {
        const Vector2 planarPosition = GetPlanarPosition();

        currentSector = MapQueries::FindSectorContainingPoint(
            sectors,
            planarPosition
        );

        if (IsValidSectorIndex(currentSector, sectors)) {
            currentFloor = std::clamp(
                currentFloor,
                0,
                GetSafeFloorCount(sectors[currentSector]) - 1
            );

            currentEyeHeight = GetEyeHeightForFloor(
                sectors[currentSector],
                currentFloor
            );
        }
        else {
            currentSector = 0;
            currentFloor = 0;

            if (!sectors.empty()) {
                currentEyeHeight = GetEyeHeightForFloor(sectors[0], 0);
            }
            else {
                currentEyeHeight = eyeHeight;
            }
        }

        position.y = currentEyeHeight;

        RebuildCameraMatrices();
        UpdateAudioListener();
    }

    void Update(const std::vector<Wall>& walls, const std::vector<Sector>& sectors) {
        Vector2 input = {0.0f, 0.0f};

        if (InputManager::GetKey(SDL_SCANCODE_W)) input.y += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_A)) input.x += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_S)) input.y -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_D)) input.x -= 1.0f;

        if (InputManager::GetKey(SDL_SCANCODE_LSHIFT)) {
            currentSpeed = runningSpeed;
        }
        else {
            currentSpeed = speed;
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_V)) noClip = !noClip;


        angle -= InputManager::GetMouseDelta().x * SENSITIVITY_X;
        pitch -= InputManager::GetMouseDelta().y * SENSITIVITY_Y;

        pitch = std::clamp(pitch, -89.0f, 89.0f);

        if (angle >= 360.0f) {
            angle -= 360.0f;
        }
        else if (angle < 0.0f) {
            angle += 360.0f;
        }

        const float yawRadians =
            angle * std::numbers::pi_v<float> / 180.0f;

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

        Vector2 planarVelocity = GetPlanarVelocity();

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDirection =
                right * input.x + forward * input.y;

            planarVelocity =
                Vector2Math::Normalized(moveDirection) * currentSpeed;
        }
        else {
            planarVelocity *= FRICTION;
        }

        Vector2 planarPosition = GetPlanarPosition();
        planarPosition += planarVelocity * GameTime::deltaTime;

        SetPlanarPosition(planarPosition);
        SetPlanarVelocity(planarVelocity);

        bool touchingPortalThisFrame = false;

        if (IsValidSectorIndex(currentSector, sectors)) {
            for (int iter = 0; iter < COLLISION_ITERATIONS; ++iter) {
                bool collided = false;

                for (int i = 0; i < static_cast<int>(sectors[currentSector].walls.size()); ++i) {
                    const Wall& wall = sectors[currentSector].walls[i];

                    if (wall.floor != currentFloor) {
                        continue;
                    }

                    planarPosition = GetPlanarPosition();
                    planarVelocity = GetPlanarVelocity();

                    const Vector2 closest =
                        ClosestPointOnSegment(wall, planarPosition);

                    const Vector2 delta =
                        planarPosition - closest;

                    const float distSq =
                        Vector2Math::Dot(delta, delta);

                    const float radiusSq =
                        size * size;

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
                        const int newSector =
                            GetOtherPortalSector(wall, currentSector);

                        if (CanStepIntoSector(currentSector, newSector, sectors)) {
                            touchingPortalThisFrame = true;
                            activePortalWallIndex = i;

                            const int sectorOnOtherSide =
                                FindCurrentSectorBetweenPortalSides(wall, sectors);

                            if (sectorOnOtherSide != currentSector) {
                                EnterSectorKeepingWorldEyeHeight(
                                    sectorOnOtherSide,
                                    sectors
                                );
                            }

                            continue;
                        }
                    }

                    const float penetration =
                        size - dist;

                    if (!noClip) {
                        planarPosition += normal * penetration;
                    }

                    const float intoWall =
                        Vector2Math::Dot(normal, planarVelocity);

                    if (intoWall < 0.0f) {
                        planarVelocity -= normal * intoWall;
                    }

                    SetPlanarPosition(planarPosition);
                    SetPlanarVelocity(planarVelocity);

                    collided = true;
                }

                if (!collided) {
                    break;
                }
            }
        }

        planarPosition = GetPlanarPosition();

        const int foundSector =
            MapQueries::FindSectorContainingPoint(sectors, planarPosition);

        if (foundSector != -1 && foundSector != currentSector) {
            EnterSectorKeepingWorldEyeHeight(foundSector, sectors);
        }

        if (IsValidSectorIndex(currentSector, sectors)) {
            const Sector& sector = sectors[currentSector];

            currentFloor = std::clamp(
                currentFloor,
                0,
                GetSafeFloorCount(sector) - 1
            );

            currentEyeHeight = GetEyeHeightForFloor(
                sector,
                currentFloor
            );
        }

        position.y = currentEyeHeight;

        if (!touchingPortalThisFrame) {
            activePortalWallIndex = -1;
        }

        RebuildCameraMatrices();
        UpdateAudioListener();
    }
}