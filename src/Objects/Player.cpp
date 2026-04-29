//
// Created by berke on 4/13/2026.
//

#include "../../Headers/Objects/Player.hpp"

#include <algorithm>

#include "../../Headers/Engine/InputManager.hpp"
#include "../../Headers/Engine/GameTime.hpp"

#include "../../Headers/Math/Vector/Vector2Math.hpp"

#define FRICTION .8f
#define TURN_SPEED 90.0f
#define SENSITIVITY_X .5f
#define SENSITIVITY_Y .004f

constexpr int COLLISION_ITERATIONS = 4;

namespace {
    bool IsPointInPolygon(const std::vector<Vector2>& polygon, const Vector2 p) {
        bool inside = false;
        const size_t n = polygon.size();

        // Loop through every edge of the polygon
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            // Check if the point's Y-coordinate is between the edge's Y-coordinates
            bool isBetweenY = (polygon[i].y > p.y) != (polygon[j].y > p.y);

            // If it is, calculate the X-coordinate of the intersection of the edge and the ray
            // then check if the point's X-coordinate is to the left of that intersection
            if (isBetweenY &&
                (p.x < (polygon[j].x - polygon[i].x) * (p.y - polygon[i].y) / (polygon[j].y - polygon[i].y) + polygon[i].x)) {
                inside = !inside; // Toggle the state
                }
        }

        return inside;
    }
    Vector2 ClosestPointOnSegment(const Wall& wall, const Vector2& p) {
        if (wall.lengthSq <= 0.00001f) {
            return wall.start;
        }

        float t = Vector2Math::Dot((p - wall.start), wall.dir) / wall.lengthSq;
        t = std::clamp(t, 0.0f, 1.0f);

        return wall.start + wall.dir * t;
    }

    int activePortalWallIndex = -1;

    bool IsPortalWall(const Wall& wall) {
        return wall.frontSector != -1 && wall.backSector != -1 && wall.backSector != wall.frontSector;
    }

    bool IsValidSectorIndex(const int index, const std::vector<Sector>& sectors) {
        return index >= 0 && index < static_cast<int>(sectors.size());
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

        const float currentFloorHeight = sectors[currentSector].floorHeight;
        const float newFloorHeight = sectors[newSector].floorHeight;

        const float stepUpAmount = newFloorHeight - currentFloorHeight;

        return stepUpAmount <= Player::stepSize;
    }

    void OnPortalEnter(const Wall& wall, const std::vector<Sector>& sectors) {
        const int newSector = GetOtherPortalSector(wall, Player::currentSector);

        if (!CanStepIntoSector(Player::currentSector, newSector, sectors)) {
            return;
        }

        Player::currentSector = newSector;
    }

    int FindCurrentSectorBetweenPortalSides(
        const Wall &wall,
        const std::vector<Sector> &sectors
    ) {
        if (IsValidSectorIndex(wall.frontSector, sectors) &&
            IsPointInPolygon(sectors[wall.frontSector].vertices, Player::position)) {
            return wall.frontSector;
        }

        if (IsValidSectorIndex(wall.backSector, sectors) &&
            IsPointInPolygon(sectors[wall.backSector].vertices, Player::position)) {
            return wall.backSector;
        }

        return Player::currentSector;
    }

    float PolygonAreaAbs(const std::vector<Vector2>& polygon) {
        float area = 0.0f;

        for (int i = 0; i < static_cast<int>(polygon.size()); ++i) {
            const int next = (i + 1) % static_cast<int>(polygon.size());

            area += polygon[i].x * polygon[next].y;
            area -= polygon[next].x * polygon[i].y;
        }

        return std::abs(area) * 0.5f;
    }
}

namespace Player {
    int FindCurrentSector(const std::vector<Sector>& sectors) {
        int bestSector = -1;
        float bestArea = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(sectors.size()); ++i) {
            if (!IsPointInPolygon(sectors[i].vertices, position)) {
                continue;
            }

            const float area = PolygonAreaAbs(sectors[i].vertices);

            if (area < bestArea) {
                bestArea = area;
                bestSector = i;
            }
        }

        if (bestSector != -1) {
            currentSector = bestSector;
        }

        return bestSector;
    }

    void Start(const std::vector<Sector>& sectors) {
        currentSector = FindCurrentSector(sectors);
        currentEyeHeight = currentSector < sectors.size() ? sectors[currentSector].floorHeight + eyeHeight : eyeHeight;

    }

    void Update(const std::vector<Wall>& walls, const std::vector<Sector>& sectors) {
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

        //Check each sector every frame, might cause lag
        currentSector = FindCurrentSector(sectors);
        float sectorCeilHeight = sectors[currentSector].ceilingHeight;
        float sectorFloorHeight = sectors[currentSector].floorHeight;

        float targetWorldEyeHeight = eyeHeight;

        if (sectorCeilHeight < currentEyeHeight) {
            targetWorldEyeHeight = sectorCeilHeight + eyeHeight;
            currentFloor = 1;
        }
        else {
            targetWorldEyeHeight = currentSector < sectors.size() ? sectors[currentSector].floorHeight + eyeHeight : eyeHeight;
            currentFloor = 0;
        }

        currentEyeHeight = targetWorldEyeHeight;

        const float angleInRad = angle * M_PI / 180.0f;
        const float s = std::sin(angleInRad);
        const float c = std::cos(angleInRad);

        const Vector2 forward = {s, c};
        const Vector2 right = {c, -s};

        if (input.x != 0.0f || input.y != 0.0f) {
            const Vector2 moveDir = right * input.x + forward * input.y;
            velocity = Vector2Math::Normalized(moveDir) * currentSpeed;
        } else {
            velocity *= FRICTION;
        }

        position += velocity * GameTime::deltaTime;

        bool touchingPortalThisFrame = false;

        for (int iter = 0; iter < COLLISION_ITERATIONS; ++iter) {
            bool collided = false;

            for (int i = 0; i < static_cast<int>(walls.size()); ++i) {
                const Wall& wall = walls[i];

                if (wall.floor != currentFloor) continue;

                const Vector2 closest = ClosestPointOnSegment(wall, position);
                const Vector2 delta = position - closest;

                const float distSq = Vector2Math::Dot(delta, delta);
                const float radiusSq = size * size;

                if (distSq >= radiusSq) {
                    continue;
                }

                float dist = std::sqrt(distSq);

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

                        currentSector = FindCurrentSectorBetweenPortalSides(wall, sectors);

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

        if (!touchingPortalThisFrame) {
            activePortalWallIndex = -1;
        }
    }
}