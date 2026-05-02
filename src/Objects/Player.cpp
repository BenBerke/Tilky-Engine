//
// Created by berke on 4/13/2026.
//

#include "../../Headers/Objects/Player.hpp"

#include <algorithm>

#include "../../Headers/Engine/InputManager.hpp"
#include "../../Headers/Engine/GameTime.hpp"

#include "../../Headers/Math/Vector/Vector2Math.hpp"

#include "../../Headers/Math/Geometry/Geometry.h"
#include "../../Headers/Map/MapQueries.hpp"

#define FRICTION .8f
#define TURN_SPEED 90.0f
#define SENSITIVITY_X .5f
#define SENSITIVITY_Y .004f

constexpr int COLLISION_ITERATIONS = 4;

namespace {
    float GetSectorHeight(const Sector &sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    int GetSafeFloorCount(const Sector &sector) {
        return std::max(1, sector.floorCount);
    }

    float GetFloorBaseHeight(const Sector &sector, const int floorIndex) {
        const float sectorHeight = GetSectorHeight(sector);

        return sector.floorHeight + sectorHeight * static_cast<float>(floorIndex);
    }

    float GetEyeHeightForFloor(const Sector &sector, const int floorIndex) {
        return GetFloorBaseHeight(sector, floorIndex) + Player::eyeHeight;
    }

    int GetFloorFromWorldEyeHeight(
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

    Vector2 ClosestPointOnSegment(const Wall& wall, const Vector2& p) {
        if (wall.length <= 0.00001f) {
            return wall.start;
        }

        float distanceAlongWall = Vector2Math::Dot(p - wall.start, wall.dir);

        distanceAlongWall = std::clamp(
            distanceAlongWall,
            0.0f,
            wall.length
        );

        return wall.start + wall.dir * distanceAlongWall;
    }

    int activePortalWallIndex = -1;

    bool IsPortalWall(const Wall &wall) {
        return wall.frontSector != -1 && wall.backSector != -1 && wall.backSector != wall.frontSector;
    }

    bool IsValidSectorIndex(const int index, const std::vector<Sector> &sectors) {
        return index >= 0 && index < static_cast<int>(sectors.size());
    }

    void EnterSectorKeepingWorldEyeHeight(
        const int newSector,
        const std::vector<Sector> &sectors
    ) {
        if (!IsValidSectorIndex(newSector, sectors)) {
            return;
        }

        const Sector &sector = sectors[newSector];

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
    }

    int GetOtherPortalSector(const Wall &wall, const int currentSector) {
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

        // Moving up: only allow normal step height.
        if (heightDifference > 0.0f) {
            return heightDifference <= Player::stepSize;
        }

        // Moving down.
        if (heightDifference < 0.0f) {
            const float fallDistance = -heightDifference;

            // Allow small step-downs.
            if (fallDistance <= Player::stepSize) {
                return true;
            }

            // Allow large drops only if they are large enough to be a real floor drop.
            if (fallDistance >= Player::bodySize) {
                return true;
            }

            // Medium drop: block it, like hitting a floor edge.
            return false;
        }

        return true;
    }

    void OnPortalEnter(const Wall &wall, const std::vector<Sector> &sectors) {
        const int newSector = GetOtherPortalSector(wall, Player::currentSector);

        if (!CanStepIntoSector(Player::currentSector, newSector, sectors)) {
            return;
        }

        EnterSectorKeepingWorldEyeHeight(newSector, sectors);
    }

    int FindCurrentSectorBetweenPortalSides(
        const Wall &wall,
        const std::vector<Sector> &sectors
    ) {
        if (IsValidSectorIndex(wall.frontSector, sectors) &&
            Geometry::IsPointInPolygon(sectors[wall.frontSector].vertices, Player::position)) {
            return wall.frontSector;
        }

        if (IsValidSectorIndex(wall.backSector, sectors) &&
            Geometry::IsPointInPolygon(sectors[wall.backSector].vertices, Player::position)) {
            return wall.backSector;
        }

        return Player::currentSector;
    }
}

namespace Player {
    void Start(const std::vector<Sector> &sectors) {
        currentSector = MapQueries::FindSectorContainingPoint(sectors, position);

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
        } else {
            currentSector = 0;
            currentFloor = 0;

            if (!sectors.empty()) {
                currentEyeHeight = GetEyeHeightForFloor(sectors[0], 0);
            } else {
                currentEyeHeight = eyeHeight;
            }
        }
    }

    void Update(const std::vector<Wall> &walls, const std::vector<Sector> &sectors) {
        Vector2 input = {0.0f, 0.0f};
        if (InputManager::GetKey(SDL_SCANCODE_W)) input.y += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_A)) input.x -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_S)) input.y -= 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_D)) input.x += 1.0f;

        if (InputManager::GetKey(SDL_SCANCODE_Q)) angle -= TURN_SPEED * GameTime::deltaTime;
        if (InputManager::GetKey(SDL_SCANCODE_E)) angle += TURN_SPEED * GameTime::deltaTime;

        if (InputManager::GetKey(SDL_SCANCODE_Z)) camZ += 2.0f * GameTime::deltaTime;
        if (InputManager::GetKey(SDL_SCANCODE_X)) camZ -= 2.0f * GameTime::deltaTime;

        if (InputManager::GetKey(SDL_SCANCODE_LSHIFT)) currentSpeed = runningSpeed;
        else currentSpeed = speed;

        if (InputManager::GetKeyDown(SDL_SCANCODE_V)) noClip = !noClip;

        angle += InputManager::GetMouseDelta().x * SENSITIVITY_X;
        camZ -= InputManager::GetMouseDelta().y * SENSITIVITY_Y;

        camZ = std::clamp(camZ, -.15f, .8f);

        const float angleInRad = angle * M_PI / 180.0f;
        const float s = std::sin(angleInRad);
        const float c = std::cos(angleInRad);

        const Vector2 forward = {s, c};
        const Vector2 right = {c, -s};

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDir = right * input.x + forward * input.y;
            velocity = Vector2Math::Normalized(moveDir) * currentSpeed;
        } else velocity *= FRICTION;


        position += velocity * GameTime::deltaTime;

        bool touchingPortalThisFrame = false;


        for (int iter = 0; iter < COLLISION_ITERATIONS; ++iter) {
            bool collided = false;

            for (int i = 0; i < sectors[currentSector].walls.size(); ++i) {
                const Wall &wall = sectors[currentSector].walls[i];

                if (wall.floor != currentFloor) continue;

                const Vector2 closest = ClosestPointOnSegment(wall, position);
                const Vector2 delta = position - closest;

                const float distSq = Vector2Math::Dot(delta, delta);
                const float radiusSq = size * size;

                if (distSq >= radiusSq) {
                    continue;
                }

                const float dist = std::sqrt(distSq);

                Vector2 normal;
                if (dist > 0.00001f) {
                    normal = delta * (1.0f / dist);
                } else {
                    normal = wall.normal;
                }

                if (IsPortalWall(wall)) {
                    const int newSector = GetOtherPortalSector(wall, currentSector);

                    if (CanStepIntoSector(currentSector, newSector, sectors)) {
                        touchingPortalThisFrame = true;
                        activePortalWallIndex = i;

                        const int sectorOnOtherSide = FindCurrentSectorBetweenPortalSides(wall, sectors);

                        if (sectorOnOtherSide != currentSector) {
                            EnterSectorKeepingWorldEyeHeight(sectorOnOtherSide, sectors);
                        }

                        continue;
                    }

                    // Portal exists, but the next sector is too high.
                    // Do NOT continue. Let it collide like a normal wall.
                }

                const float penetration = size - dist;
                if (!noClip)
                    position += normal * penetration;

                const float intoWall = Vector2Math::Dot(normal, velocity);
                if (intoWall < 0.0f) {
                    velocity -= normal * intoWall;
                }

                collided = true;
            }

            if (!collided) {
                break;
            }
        }

        //Check each sector every frame, might cause lag
        const int foundSector = MapQueries::FindSectorContainingPoint(sectors, position);

        if (foundSector != -1 && foundSector != currentSector) {
            EnterSectorKeepingWorldEyeHeight(foundSector, sectors);
        }

        if (IsValidSectorIndex(currentSector, sectors)) {
            const Sector &sector = sectors[currentSector];

            currentFloor = std::clamp(
                currentFloor,
                0,
                GetSafeFloorCount(sector) - 1
            );

            currentEyeHeight = GetEyeHeightForFloor(sector, currentFloor);
        }

        if (!touchingPortalThisFrame) {
            activePortalWallIndex = -1;
        }
    }
}
