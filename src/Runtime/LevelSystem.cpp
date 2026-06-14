//
// Created by berke on 5/26/2026.
//

/// This script handles the runtime renderer. Does not run in the editor

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include <tracy/Tracy.hpp>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Runtime/ScriptSystem.hpp"

namespace {
    ComponentPlayerController *GetActivePlayerController(Level &level) {
        for (ComponentPlayerController &controller: level.playerControllers.components)
            if (controller.isActive) return &controller;

        return nullptr;
    }

    const ComponentCamera *GetActiveCamera(const Level &level) {
        for (const ComponentCamera &camera: level.cameras.components)
            if (camera.isActive) return &camera;

        return nullptr;
    }

    ComponentTransform *GetActiveCameraTransform(Level &level) {
        const ComponentCamera *camera = GetActiveCamera(level);

        if (camera == nullptr) return nullptr;

        return level.transforms.Get(camera->ownerID);
    }

    ComponentTransform *GetActivePlayerTransform(Level &level) {
        ComponentPlayerController *controller = GetActivePlayerController(level);

        if (controller == nullptr) return nullptr;

        return level.transforms.Get(controller->ownerID);
    }

    void SetActiveCamera(const EntityID entityID, Level &level) {
        bool found = false;

        for (ComponentCamera &camera: level.cameras.components) {
            camera.isActive = camera.ownerID == entityID;

            if (camera.isActive) found = true;
        }

        if (!found) {
            spdlog::warn(
                "Tried to set active camera to entity {}, but that entity has no camera component",
                entityID
            );
        }
    }

    void SetActivePlayerController(const EntityID entityID, Level &level) {
        bool found = false;

        for (ComponentPlayerController &controller: level.playerControllers.components) {
            controller.isActive = controller.ownerID == entityID;

            if (controller.isActive) {
                found = true;
            }
        }

        if (!found)
            spdlog::warn(
                "Tried to set active player controller to entity {}, but that entity has no player controller component",
                entityID
            );
    }

    ComponentPlayerController *activeController;

    // squareCollider = BOX / AABB
    // circleCollider = SPHERE
    // circleCollider.scale.x = radius
    // squareCollider.scale.xyz = full box size
    bool CircleAABBCollision(Level &level, ComponentCollider &squareCollider, ComponentCollider &circleCollider) {
        if (squareCollider.type != COLLIDERTYPE_BOX || circleCollider.type != COLLIDERTYPE_SPHERE) [[unlikely]] return
                false;
        if (squareCollider.isTrigger || circleCollider.isTrigger) return false;

        ComponentTransform *squareTransform = level.transforms.Get(squareCollider.ownerID);
        ComponentTransform *circleTransform = level.transforms.Get(circleCollider.ownerID);

        if (squareTransform == nullptr || circleTransform == nullptr) [[unlikely]] return false;

        ComponentRigidbody *squareRb = level.rigidbodies.Get(squareCollider.ownerID);
        ComponentRigidbody *circleRb = level.rigidbodies.Get(circleCollider.ownerID);

        const Vector3 squarePos = {
            squareTransform->position.x,
            squareTransform->position.z,
            squareTransform->position.y
        };

        const Vector3 circlePos = {
            circleTransform->position.x,
            circleTransform->position.z,
            circleTransform->position.y
        };

        const Vector3 halfSize = {
            squareCollider.scale.x * 0.5f,
            squareCollider.scale.z * 0.5f,
            squareCollider.scale.y * 0.5f
        };

        const float circleRadius = circleCollider.scale.x;

        const Vector3 boxMin = {
            squarePos.x - halfSize.x,
            squarePos.y - halfSize.y,
            squarePos.z - halfSize.z
        };

        const Vector3 boxMax = {
            squarePos.x + halfSize.x,
            squarePos.y + halfSize.y,
            squarePos.z + halfSize.z
        };

        Vector3 closestPoint;
        closestPoint.x = std::max(boxMin.x, std::min(circlePos.x, boxMax.x));
        closestPoint.y = std::max(boxMin.y, std::min(circlePos.y, boxMax.y));
        closestPoint.z = std::max(boxMin.z, std::min(circlePos.z, boxMax.z));

        const float distanceSquared = Vector3Math::DistanceSquared(circlePos, closestPoint);

        if (distanceSquared >= circleRadius * circleRadius) return false;

        Vector3 pushDirection;
        float overlap = 0.0f;

        if (distanceSquared < 0.000001f) {
            const float distToMinX = std::abs(circlePos.x - boxMin.x);
            const float distToMaxX = std::abs(boxMax.x - circlePos.x);
            const float distToMinY = std::abs(circlePos.y - boxMin.y);
            const float distToMaxY = std::abs(boxMax.y - circlePos.y);
            const float distToMinZ = std::abs(circlePos.z - boxMin.z);
            const float distToMaxZ = std::abs(boxMax.z - circlePos.z);

            float smallestDistance = distToMinX;
            pushDirection = {-1.0f, 0.0f, 0.0f};

            if (distToMaxX < smallestDistance) {
                smallestDistance = distToMaxX;
                pushDirection = {1.0f, 0.0f, 0.0f};
            }

            if (distToMinY < smallestDistance) {
                smallestDistance = distToMinY;
                pushDirection = {0.0f, -1.0f, 0.0f};
            }

            if (distToMaxY < smallestDistance) {
                smallestDistance = distToMaxY;
                pushDirection = {0.0f, 1.0f, 0.0f};
            }

            if (distToMinZ < smallestDistance) {
                smallestDistance = distToMinZ;
                pushDirection = {0.0f, 0.0f, -1.0f};
            }

            if (distToMaxZ < smallestDistance) {
                smallestDistance = distToMaxZ;
                pushDirection = {0.0f, 0.0f, 1.0f};
            }

            overlap = circleRadius + smallestDistance;
        } else {
            const float distance = std::sqrt(distanceSquared);
            overlap = circleRadius - distance;
            pushDirection = (circlePos - closestPoint) / distance;
        }

        float squareWeight = 0.5f;
        float circleWeight = 0.5f;

        const bool squareStatic = squareRb == nullptr || squareRb->isStatic;
        const bool circleStatic = circleRb == nullptr || circleRb->isStatic;

        if (squareStatic && circleStatic) [[unlikely]] return true;

        if (squareStatic) {
            squareWeight = 0.0f;
            circleWeight = 1.0f;
        } else if (circleStatic) {
            squareWeight = 1.0f;
            circleWeight = 0.0f;
        }

        const Vector3 squarePush = pushDirection * -overlap * squareWeight;
        const Vector3 circlePush = pushDirection * overlap * circleWeight;

        bool squareBlockedByWall = false;

        if (squareWeight > 0.0f && squareTransform->sectorIndex >= 0 && squareTransform->sectorIndex < static_cast<int>(
                level.sectors.size())) {
            const Sector &squareSector =
                    level.sectors[squareTransform->sectorIndex];

            const Vector2 candidateSquarePos = {
                squareTransform->position.x + squarePush.x,
                squareTransform->position.y + squarePush.z
            };

            // Horizontal footprint uses collision-space x/z.
            const float squareRadius = std::max(halfSize.x, halfSize.z);

            for (const Wall &wall: squareSector.walls) {
                const Vector2 v = wall.vector;

                const float vDotV = Vector2Math::Dot(v, v);
                if (vDotV <= 0.001f) continue;

                const Vector2 w = candidateSquarePos - wall.start;

                float t = Vector2Math::Dot(w, v) / vDotV;
                t = std::max(0.0f, std::min(1.0f, t));

                const Vector2 closestWallPoint = wall.start + t * v;

                const float wallDistanceSquared = Vector2Math::DistanceSquared(candidateSquarePos, closestWallPoint);

                if (wallDistanceSquared < squareRadius * squareRadius) {
                    squareBlockedByWall = true;
                    break;
                }
            }
        }

        if (squareBlockedByWall) {
            if (!circleStatic) {
                const Vector3 fullCirclePush = pushDirection * overlap;

                circleTransform->AddPosition({
                    fullCirclePush.x,
                    fullCirclePush.z,
                    fullCirclePush.y
                });
            }
        } else {
            squareTransform->AddPosition({
                squarePush.x,
                squarePush.z,
                squarePush.y
            });

            circleTransform->AddPosition({
                circlePush.x,
                circlePush.z,
                circlePush.y
            });
        }

        return true;
    }
}

namespace LevelSystem {
    ComponentCamera *GetActiveCamera(Level &level) {
        for (ComponentCamera &camera: level.cameras.components)
            if (camera.isActive) return &camera;


        return nullptr;
    }

    void Start(Level &level) {
        SoundManager::SetListenerGain(level.listenerSettings.masterGain);
        SoundManager::SetListenerDopplerFactor(level.listenerSettings.dopplerFactor);
        SoundManager::SetListenerSpeedOfSound(level.listenerSettings.speedOfSound);
        SoundManager::SetListenerDistanceModel(level.listenerSettings.distanceModel);

        spdlog::info(
            "Level audio settings applied. Gain: {}, Doppler: {}, Speed of sound: {}, Distance model: {}",
            level.listenerSettings.masterGain,
            level.listenerSettings.dopplerFactor,
            level.listenerSettings.speedOfSound,
            static_cast<int>(level.listenerSettings.distanceModel)
        );

        for (const ComponentCamera &cam: level.cameras.components) {
            if (cam.isActive) {
                //todo support multiple cameras
                SetActiveCamera(cam.ownerID, level);
                break;
            }
        }

        for (Entity &entity: level.entities) {
            entity.Start(); // Currently does nothing
        }

        for (ComponentTransform &transform: level.transforms.components) {
            transform.UpdateObjectSectorAndFloor(level.sectors, level.GetEntity(transform.ownerID));
        }

        activeController = GetActivePlayerController(level);

        if (activeController != nullptr) {
            ComponentTransform *playerTransform = level.transforms.Get(activeController->ownerID);
            const ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(activeController->ownerID);

            if (playerTransform != nullptr) [[likely]]{
                ComponentCamera *activeCamera = GetActiveCamera(level);

                if (activeCamera != nullptr) {
                    PlayerControllerSystem::Start(
                        *activeController,
                        *playerTransform,
                        *playerRigidbody,
                        *activeCamera,
                        level.sectors
                    );
                } else spdlog::warn("Level::Start skipped player controller: no active camera");
            } else
                spdlog::error("Level::Start skipped player controller: entity {} has no transform",
                              activeController->ownerID);
        }

        ScriptSystem::Start(level);

        // Future level start systems will run here.
    }

    void Update(Level &level) {
        // for (Entity &entity: level.entities) {
        //     entity.Update(); // currently does nothing
        // }

        {
            ZoneScopedN("Transform setup");
            for (ComponentTransform& transform : level.transforms.components) {
                Entity* owner = level.GetEntity(transform.ownerID);

                if (!owner) [[unlikely]] {
                    spdlog::error("Transform owner {} does not exist", transform.ownerID);
                    continue;
                }

                transform.UpdateObjectSectorAndFloor(level.sectors, owner);
            }
        }

        if (activeController != nullptr && activeController->isActive) {
            const EntityID ownerID = activeController->ownerID;

            ComponentTransform* playerTransform = level.transforms.Get(ownerID);
            if (!playerTransform) [[unlikely]] {
                spdlog::error("Player controller entity {} has no transform", ownerID);
                return;
            }

            ComponentRigidbody* playerRigidbody = level.rigidbodies.Get(ownerID);
            if (!playerRigidbody) [[unlikely]] {
                spdlog::error("Player controller entity {} has no rigidbody", ownerID);
                return;
            }

            ComponentCollider* playerCollider = level.colliders.Get(ownerID);

            ComponentCamera* activeCamera = GetActiveCamera(level);
            if (!activeCamera) [[unlikely]] {
                spdlog::warn("LevelSystem::Update skipped player controller: no active camera");
                return;
            }

            PlayerControllerSystem::Update(
                *activeController,
                *playerTransform,
                *activeCamera,
                *playerRigidbody,
                playerCollider,
                level.sectors
            );
        }

        {
            ZoneScopedN("Scripts");
            ScriptSystem::Update(level);
        }


        {
            ZoneScopedN("Physics");

            for (ComponentRigidbody& r : level.rigidbodies.components) {
                ComponentTransform* transform = level.transforms.Get(r.ownerID);

                if (!transform) [[unlikely]] {
                    spdlog::error("Rigidbody entity {} has no transform", r.ownerID);
                    continue;
                }

                if (transform->position.z > 0.0f) r.ApplyGravity(level.worldSettings.gravity);

                if (transform->position.z <= 0.0f) {
                    transform->position.z = 0.0f;

                    if (r.velocity.z < 0.0f) r.velocity.z = 0.0f;
                }

                if (r.velocity.IsZero()) continue;

                transform->AddPosition(r.velocity * GameTime::deltaTime);

                r.ApplyFriction(0); // Applies rigidbody's base friction
                r.ApplyAirResistance(0);
            }
        }

        {
            //todo sort entities where sphere colliders are in the beggining of the vector to optimize for branch prediction
            ZoneScopedN("Collision");

            //todo make this a world setting
            constexpr int COLLISION_ITERATIONS = 4;
            for (int i = 0; i < COLLISION_ITERATIONS; i++) {
                for (ComponentCollider &selfCollider: level.colliders.components) {
                    if (!selfCollider.isActive) continue;
                    if (selfCollider.isTrigger) continue;

                    ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);

                    if (selfTransform == nullptr) [[unlikely]] continue;
                    if (!selfTransform->isDirty) [[unlikely]] continue;

                    if (selfTransform->sectorIndex < 0 || selfTransform->sectorIndex >= static_cast<int>(level.sectors.
                            size()))
                        continue;

                    Sector &sector = level.sectors[selfTransform->sectorIndex];

                    ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);
                    if (selfRb == nullptr) continue; // collision checks should only happen on entities with rigidbodies

                    Entity *selfEntity = level.GetEntity(selfCollider.ownerID);
                    if (selfEntity == nullptr) [[unlikely]] continue;

                    std::vector<Entity *> allEntities;
                    allEntities.insert(allEntities.end(), sector.entitiesInside.begin(), sector.entitiesInside.end());
                    //Neighboring sectors.
                    //Can be potentially commented-out in exchange for better performance but potential glitches between sectors
                    for (const Sector *nSector: sector.neighbors) {
                        if (!nSector) [[unlikely]] continue;
                        allEntities.insert(allEntities.end(), nSector->entitiesInside.begin(),
                                           nSector->entitiesInside.end());
                    }

                    const Vector3 selfSize = selfCollider.scale;

                    const Vector3 selfCollisionPos = {
                        selfTransform->position.x,
                        selfTransform->position.z,
                        selfTransform->position.y
                    };

                    const Vector3 selfCollisionSize = {
                        selfCollider.scale.x,
                        selfCollider.scale.z,
                        selfCollider.scale.y
                    };

                    for (Entity *otherEntity: allEntities) {
                        if (otherEntity == nullptr) [[unlikely]] continue;
                        if (otherEntity == selfEntity) continue;

                        if (!otherEntity->HasComponent<ComponentCollider>()) continue;

                        auto *otherCollider = otherEntity->GetComponent<ComponentCollider>();

                        if (!otherCollider->isActive) continue;

                        if (otherCollider->isTrigger) continue;

                        auto *otherTransform = otherEntity->GetComponent<ComponentTransform>();
                        if (otherTransform == nullptr) [[unlikely]] continue;

                        const Vector3 otherSize = otherCollider->scale;

                        const Vector3 otherCollisionPos = {
                            otherTransform->position.x,
                            otherTransform->position.z,
                            otherTransform->position.y
                        };

                        const Vector3 otherCollisionSize = {
                            otherCollider->scale.x,
                            otherCollider->scale.z,
                            otherCollider->scale.y
                        };

                        if (selfCollider.type == COLLIDERTYPE_SPHERE && otherCollider->type == COLLIDERTYPE_SPHERE) {
                            const float minDistance = otherSize.x + selfSize.x;

                            const float distanceSquared =
                                    Vector3Math::DistanceSquared(otherCollisionPos, selfCollisionPos);

                            if (distanceSquared < minDistance * minDistance) [[unlikely]] {
                                ComponentRigidbody *otherRb = nullptr;
                                if (otherEntity->HasComponent<ComponentRigidbody>())
                                    otherRb = otherEntity->GetComponent<ComponentRigidbody>();

                                Vector3 delta = selfCollisionPos - otherCollisionPos;
                                float fixedDistanceSquared = distanceSquared;

                                if (fixedDistanceSquared < 0.000001f) {
                                    delta = {1.0f, 0.0f, 0.0f};
                                    fixedDistanceSquared = 1.0f;
                                }

                                const float distance = std::sqrt(fixedDistanceSquared);
                                const float overlap = minDistance - distance;

                                Vector3 pushDirection = delta / distance;

                                float selfWeight = 0.5f;
                                float otherWeight = 0.5f;

                                const bool aStatic = selfRb->isStatic;
                                const bool bStatic = (otherRb == nullptr) || otherRb->isStatic;

                                if (bStatic && aStatic) [[unlikely]] continue;

                                if (aStatic) {
                                    selfWeight = 0.0f;
                                    otherWeight = 1.0f;
                                } else if (bStatic) {
                                    selfWeight = 1.0f;
                                    otherWeight = 0.0f;
                                }

                                // Collision-space push:
                                // x = world X
                                // y = height
                                // z = world depth
                                Vector3 selfPush = pushDirection * overlap * selfWeight;
                                Vector3 otherPush = pushDirection * -overlap * otherWeight;

                                bool otherBlockedByWall = false;

                                if (otherTransform->sectorIndex >= 0 &&
                                    otherTransform->sectorIndex < static_cast<int>(level.sectors.size())) {
                                    const Sector &otherSector = level.sectors[otherTransform->sectorIndex];

                                    // Convert collision-space push back to engine horizontal x/y.
                                    const Vector2 candidateOtherPos = {
                                        otherTransform->position.x + otherPush.x,
                                        otherTransform->position.y + otherPush.z
                                    };

                                    const float otherRadius = otherCollider->scale.x;

                                    for (const Wall &wall: otherSector.walls) {
                                        const Vector2 v = wall.vector;

                                        const float vDotV = Vector2Math::Dot(v, v);
                                        if (vDotV <= 0.001f) continue;

                                        const Vector2 w = candidateOtherPos - wall.start;

                                        float t = Vector2Math::Dot(w, v) / vDotV;
                                        t = std::max(0.0f, std::min(1.0f, t));

                                        const Vector2 closestPoint = wall.start + t * v;

                                        const float wallDistanceSquared =
                                                Vector2Math::DistanceSquared(candidateOtherPos, closestPoint);

                                        if (wallDistanceSquared < otherRadius * otherRadius) {
                                            otherBlockedByWall = true;
                                            break;
                                        }
                                    }
                                }

                                if (otherBlockedByWall) {
                                    const Vector3 fullPush = pushDirection * overlap;

                                    selfTransform->AddPosition({
                                        fullPush.x,
                                        fullPush.z,
                                        fullPush.y
                                    });
                                } else {
                                    selfTransform->AddPosition({
                                        selfPush.x,
                                        selfPush.z,
                                        selfPush.y
                                    });

                                    otherTransform->AddPosition({
                                        otherPush.x,
                                        otherPush.z,
                                        otherPush.y
                                    });
                                }
                            }
                        } else if (selfCollider.type == COLLIDERTYPE_SPHERE && otherCollider->type == COLLIDERTYPE_BOX)
                            CircleAABBCollision(level, *otherCollider, selfCollider);

                        else if (selfCollider.type == COLLIDERTYPE_BOX && otherCollider->type == COLLIDERTYPE_SPHERE)
                            CircleAABBCollision(level, selfCollider, *otherCollider);

                        else if (selfCollider.type == COLLIDERTYPE_BOX && otherCollider->type == COLLIDERTYPE_BOX) {
                            const Vector3 selfHalfSize = selfCollisionSize * 0.5f;
                            const Vector3 otherHalfSize = otherCollisionSize * 0.5f;

                            const Vector3 delta = selfCollisionPos - otherCollisionPos;

                            const float overlapX =
                                    selfHalfSize.x + otherHalfSize.x - std::abs(delta.x);

                            const float overlapY =
                                    selfHalfSize.y + otherHalfSize.y - std::abs(delta.y);

                            const float overlapZ =
                                    selfHalfSize.z + otherHalfSize.z - std::abs(delta.z);

                            if (overlapX > 0.0f &&
                                overlapY > 0.0f &&
                                overlapZ > 0.0f) {
                                ComponentRigidbody *otherRb = nullptr;
                                if (otherEntity->HasComponent<ComponentRigidbody>())
                                    otherRb = otherEntity->GetComponent<ComponentRigidbody>();

                                Vector3 pushDirection;
                                float overlap;

                                if (overlapX <= overlapY && overlapX <= overlapZ) {
                                    overlap = overlapX;
                                    pushDirection = {
                                        delta.x >= 0.0f ? 1.0f : -1.0f,
                                        0.0f,
                                        0.0f
                                    };
                                } else if (overlapY <= overlapX && overlapY <= overlapZ) {
                                    overlap = overlapY;
                                    pushDirection = {
                                        0.0f,
                                        delta.y >= 0.0f ? 1.0f : -1.0f,
                                        0.0f
                                    };
                                } else {
                                    overlap = overlapZ;
                                    pushDirection = {
                                        0.0f,
                                        0.0f,
                                        delta.z >= 0.0f ? 1.0f : -1.0f
                                    };
                                }

                                float selfWeight = 0.5f;
                                float otherWeight = 0.5f;

                                const bool aStatic = selfRb == nullptr || selfRb->isStatic;
                                const bool bStatic = otherRb == nullptr || otherRb->isStatic;

                                if (aStatic && bStatic) [[unlikely]] continue;

                                if (aStatic) {
                                    selfWeight = 0.0f;
                                    otherWeight = 1.0f;
                                } else if (bStatic) {
                                    selfWeight = 1.0f;
                                    otherWeight = 0.0f;
                                }

                                // Collision-space push.
                                Vector3 selfPush = pushDirection * overlap * selfWeight;
                                Vector3 otherPush = pushDirection * -overlap * otherWeight;

                                auto IsBoxBlockedByWall = [&level](
                                    ComponentTransform *transform,
                                    const Vector3 &push,
                                    const Vector3 &halfSize
                                ) -> bool {
                                    if (transform == nullptr) [[unlikely]] return false;

                                    // Collision-space:
                                    // push.x = world X
                                    // push.y = height
                                    // push.z = world depth
                                    //
                                    // So if x and z are zero, the push is vertical only.
                                    if (std::abs(push.x) < 0.000001f &&
                                        std::abs(push.z) < 0.000001f) {
                                        return false;
                                    }

                                    if (transform->sectorIndex < 0 ||
                                        transform->sectorIndex >= static_cast<int>(level.sectors.size())) {
                                        return false;
                                    }

                                    const Sector &sector = level.sectors[transform->sectorIndex];

                                    const Vector2 candidatePos = {
                                        transform->position.x + push.x,
                                        transform->position.y + push.z
                                    };

                                    // Horizontal footprint uses collision-space x/z.
                                    const float radius = std::max(halfSize.x, halfSize.z);

                                    for (const Wall &wall: sector.walls) {
                                        const Vector2 v = wall.vector;

                                        const float vDotV = Vector2Math::Dot(v, v);
                                        if (vDotV <= 0.001f) continue;

                                        const Vector2 w = candidatePos - wall.start;

                                        float t = Vector2Math::Dot(w, v) / vDotV;
                                        t = std::max(0.0f, std::min(1.0f, t));

                                        const Vector2 closestWallPoint = wall.start + t * v;

                                        const float wallDistanceSquared =
                                                Vector2Math::DistanceSquared(candidatePos, closestWallPoint);

                                        if (wallDistanceSquared < radius * radius) {
                                            return true;
                                        }
                                    }

                                    return false;
                                };

                                const bool selfBlockedByWall =
                                        selfWeight > 0.0f &&
                                        IsBoxBlockedByWall(selfTransform, selfPush, selfHalfSize);

                                const bool otherBlockedByWall =
                                        otherWeight > 0.0f &&
                                        IsBoxBlockedByWall(otherTransform, otherPush, otherHalfSize);

                                if (selfBlockedByWall && otherBlockedByWall) {
                                    continue;
                                }

                                if (selfBlockedByWall) {
                                    if (!bStatic) {
                                        const Vector3 fullOtherPush = pushDirection * -overlap;

                                        otherTransform->AddPosition({
                                            fullOtherPush.x,
                                            fullOtherPush.z,
                                            fullOtherPush.y
                                        });
                                    }
                                } else if (otherBlockedByWall) {
                                    if (!aStatic) {
                                        const Vector3 fullSelfPush = pushDirection * overlap;

                                        selfTransform->AddPosition({
                                            fullSelfPush.x,
                                            fullSelfPush.z,
                                            fullSelfPush.y
                                        });
                                    }
                                } else {
                                    selfTransform->AddPosition({
                                        selfPush.x,
                                        selfPush.z,
                                        selfPush.y
                                    });

                                    otherTransform->AddPosition({
                                        otherPush.x,
                                        otherPush.z,
                                        otherPush.y
                                    });
                                }
                            }
                        }
                    }
                    auto GetSectorFloorWorldHeight = [](const Sector &sector, int floorIndex) -> float {
                        const int clampedFloor = std::clamp(
                            floorIndex,
                            0,
                            std::max(1, sector.floorCount) - 1
                        );

                        const float sectorHeight = sector.ceilingHeight - sector.floorHeight;

                        return sector.floorHeight + sectorHeight * static_cast<float>(clampedFloor);
                    };

                    auto GetColliderRequiredGap = [](const ComponentCollider &collider) -> float {
                        if (collider.type == COLLIDERTYPE_SPHERE) {
                            return collider.scale.x; // radius
                        }

                        if (collider.type == COLLIDERTYPE_BOX) {
                            return collider.scale.y;
                        }

                        return 0.0f;
                    };

                    auto CanStepIntoSector = [&level, &GetSectorFloorWorldHeight, &GetColliderRequiredGap](
                        const ComponentTransform &transform,
                        const ComponentCollider &collider,
                        int currentSectorIndex,
                        int targetSectorIndex
                    ) -> bool {
                        if (currentSectorIndex < 0 ||
                            currentSectorIndex >= static_cast<int>(level.sectors.size()) ||
                            targetSectorIndex < 0 ||
                            targetSectorIndex >= static_cast<int>(level.sectors.size())) {
                            return false;
                        }

                        const Sector &currentSector = level.sectors[currentSectorIndex];
                        const Sector &targetSector = level.sectors[targetSectorIndex];

                        const float currentFloorHeight =
                                GetSectorFloorWorldHeight(currentSector, transform.floor);

                        const float targetFloorHeight =
                                GetSectorFloorWorldHeight(targetSector, transform.floor);

                        const float targetGap =
                                targetSector.ceilingHeight - targetSector.floorHeight;

                        const float requiredGap =
                                GetColliderRequiredGap(collider);

                        if (targetGap < requiredGap) {
                            return false;
                        }

                        const float stepHeight = targetFloorHeight - currentFloorHeight;

                        // Going down should always be allowed as long as the target sector has enough vertical gap.
                        if (stepHeight <= 0.0f) {
                            return true;
                        }

                        // Going up requires step size + current local height.
                        return stepHeight <= collider.stepSize + transform.position.z;
                    };

                    auto FindPortalTransition = [&level, selfTransform, selfRb](
                        const Vector2 &pointOnWall,
                        const float probeDistance,
                        int &outFromSector,
                        int &outTargetSector
                    ) -> bool {
                        outFromSector = -1;
                        outTargetSector = -1;

                        if (selfTransform == nullptr || selfRb == nullptr) [[unlikely]] {
                            return false;
                        }

                        const Vector2 currentPos = {
                            selfTransform->position.x,
                            selfTransform->position.y
                        };

                        const Vector2 velocity = {
                            selfRb->velocity.x,
                            selfRb->velocity.y
                        };

                        const float velocityLengthSquared = Vector2Math::Dot(velocity, velocity);

                        if (velocityLengthSquared <= 0.000001f) {
                            return false;
                        }

                        const Vector2 moveDir =
                                velocity / std::sqrt(velocityLengthSquared);

                        // Approximate where the object was before physics moved it this frame.
                        const Vector2 previousPos =
                                currentPos - velocity * GameTime::deltaTime;

                        outFromSector = MapQueries::FindSectorContainingPoint(
                            level.sectors,
                            previousPos
                        );

                        if (outFromSector < 0) {
                            outFromSector = selfTransform->sectorIndex;
                        }

                        if (outFromSector < 0 ||
                            outFromSector >= static_cast<int>(level.sectors.size())) {
                            return false;
                        }

                        // If the center is already in another sector, use that.
                        const int sectorAtCurrentPos = MapQueries::FindSectorContainingPoint(
                            level.sectors,
                            currentPos
                        );

                        if (sectorAtCurrentPos >= 0 && sectorAtCurrentPos != outFromSector) {
                            outTargetSector = sectorAtCurrentPos;
                            return true;
                        }

                        const float distances[] = {
                            0.05f,
                            0.25f,
                            probeDistance,
                            probeDistance * 2.0f
                        };

                        for (const float distance: distances) {
                            const Vector2 probePoint =
                                    pointOnWall + moveDir * distance;

                            const int sectorIndex = MapQueries::FindSectorContainingPoint(
                                level.sectors,
                                probePoint
                            );

                            if (sectorIndex >= 0 && sectorIndex != outFromSector) {
                                outTargetSector = sectorIndex;
                                return true;
                            }
                        }

                        return false;
                    };

                    // Wall collision
                    for (const Wall &wall: sector.walls) {
                        const Vector2 selfVector2 = {
                            selfTransform->position.x,
                            selfTransform->position.y
                        };

                        const Vector2 v = wall.vector;

                        const float vDotV = Vector2Math::Dot(v, v);
                        if (vDotV <= 0.001f) continue;

                        if (selfCollider.type == COLLIDERTYPE_SPHERE) {
                            const Vector2 w = selfVector2 - wall.start;

                            float t = Vector2Math::Dot(w, v) / vDotV;
                            t = std::max(0.0f, std::min(1.0f, t));

                            const Vector2 closestPoint = wall.start + t * v;

                            const Vector2 delta = selfVector2 - closestPoint;
                            const float distanceSquared = Vector2Math::Dot(delta, delta);

                            const float radius = selfSize.x;

                            if (distanceSquared >= radius * radius) continue;

                            const float distance = std::sqrt(distanceSquared);
                            const float overlap = radius - distance;

                            Vector2 pushDirection;

                            if (distance > 0.0001f) {
                                pushDirection = delta / distance;
                            } else {
                                const Vector2 wallDir = Vector2Math::Normalized(wall.vector);
                                pushDirection = {-wallDir.y, wallDir.x};
                            }

                            int fromSectorIndex = -1;
                            int targetSectorIndex = -1;

                            const bool foundPortalTransition =
                                    FindPortalTransition(
                                        closestPoint,
                                        radius + 0.05f,
                                        fromSectorIndex,
                                        targetSectorIndex
                                    );

                            if (foundPortalTransition &&
                                CanStepIntoSector(
                                    *selfTransform,
                                    selfCollider,
                                    fromSectorIndex,
                                    targetSectorIndex
                                )) {
                                continue;
                            }

                            const Vector2 push = pushDirection * overlap;

                            selfTransform->AddPosition({push.x, push.y, 0.0f});

                            const float velocityIntoWall =
                                    selfRb->velocity.x * pushDirection.x +
                                    selfRb->velocity.y * pushDirection.y;

                            if (velocityIntoWall < 0.0f) {
                                selfRb->velocity.x -= pushDirection.x * velocityIntoWall;
                                selfRb->velocity.y -= pushDirection.y * velocityIntoWall;
                            }
                        } else if (selfCollider.type == COLLIDERTYPE_BOX) {
                            const Vector2 halfSize = {
                                selfSize.x * 0.5f,
                                selfSize.y * 0.5f
                            };

                            const float wallLength = std::sqrt(vDotV);
                            if (wallLength <= 0.001f) continue;

                            const Vector2 wallDir = v / wallLength;

                            Vector2 wallNormal = {
                                -wallDir.y,
                                wallDir.x
                            };

                            const Vector2 wallToBox = selfVector2 - wall.start;

                            float normalDistance = Vector2Math::Dot(wallToBox, wallNormal);

                            if (normalDistance < 0.0f) {
                                wallNormal = wallNormal * -1.0f;
                                normalDistance = -normalDistance;
                            }

                            const float boxExtentAlongNormal =
                                    std::abs(wallNormal.x) * halfSize.x +
                                    std::abs(wallNormal.y) * halfSize.y;

                            const float boxExtentAlongWall =
                                    std::abs(wallDir.x) * halfSize.x +
                                    std::abs(wallDir.y) * halfSize.y;

                            const float boxCenterAlongWall =
                                    Vector2Math::Dot(wallToBox, wallDir);

                            if (boxCenterAlongWall < -boxExtentAlongWall ||
                                boxCenterAlongWall > wallLength + boxExtentAlongWall) {
                                continue;
                            }

                            if (normalDistance >= boxExtentAlongNormal) {
                                continue;
                            }

                            const float overlap = boxExtentAlongNormal - normalDistance;

                            const Vector2 pushDirection = wallNormal;

                            const float clampedAlongWall = std::clamp(
                                boxCenterAlongWall,
                                0.0f,
                                wallLength
                            );

                            const Vector2 closestPoint =
                                    wall.start + wallDir * clampedAlongWall;

                            const float boxProbeDistance =
                                    std::max(halfSize.x, halfSize.y) + 0.05f;

                            int fromSectorIndex = -1;
                            int targetSectorIndex = -1;

                            const bool foundPortalTransition =
                                    FindPortalTransition(
                                        closestPoint,
                                        boxProbeDistance,
                                        fromSectorIndex,
                                        targetSectorIndex
                                    );

                            if (foundPortalTransition &&
                                CanStepIntoSector(
                                    *selfTransform,
                                    selfCollider,
                                    fromSectorIndex,
                                    targetSectorIndex
                                )) {
                                continue;
                            }

                            const Vector2 push = pushDirection * overlap;

                            selfTransform->AddPosition({push.x, push.y, 0.0f});

                            const float velocityIntoWall =
                                    selfRb->velocity.x * pushDirection.x +
                                    selfRb->velocity.y * pushDirection.y;

                            if (velocityIntoWall < 0.0f) {
                                selfRb->velocity.x -= pushDirection.x * velocityIntoWall;
                                selfRb->velocity.y -= pushDirection.y * velocityIntoWall;
                            }
                        }
                    }
                }
            }
        } // Zone Collision

        for (ComponentTransform &transform: level.transforms.components) {
            transform.isDirty = false;
        }

        // Future systems should still run here.
        // Example:
        // PhysicsSystem::Update(*this);
        // AnimationSystem::Update(*this);
        // TriggerSystem::Update(*this);
    }
}
