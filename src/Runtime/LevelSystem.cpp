//
// Created by berke on 5/26/2026.
//

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include "tracy/Tracy.hpp"

namespace {
    ComponentPlayerController* GetActivePlayerController(Level& level)  {
        for (ComponentPlayerController& controller : level. playerControllers.components) {
            if (controller.isActive) {
                return &controller;
            }
        }

        return nullptr;
    }

    const ComponentCamera* GetActiveCamera(const Level& level) {
        for (const ComponentCamera& camera : level.cameras.components) {
            if (camera.isActive) {
                return &camera;
            }
        }

        return nullptr;
    }

    ComponentTransform* GetActiveCameraTransform(Level& level) {
        const ComponentCamera* camera = GetActiveCamera(level);

        if (camera == nullptr) {
            return nullptr;
        }

        return level.transforms.Get(camera->ownerID);
    }

    ComponentTransform* GetActivePlayerTransform(Level& level) {
        ComponentPlayerController* controller = GetActivePlayerController(level);

        if (controller == nullptr) {
            return nullptr;
        }

        return level.transforms.Get(controller->ownerID);
    }

    void SetActiveCamera(const EntityID entityID, Level& level) {
        bool found = false;

        for (ComponentCamera& camera : level.cameras.components) {
            camera.isActive = camera.ownerID == entityID;

            if (camera.isActive) {
                found = true;
            }
        }

        if (!found) {
            spdlog::warn(
                "Tried to set active camera to entity {}, but that entity has no camera component",
                entityID
            );
        }
    }

    void SetActivePlayerController(const EntityID entityID, Level& level) {
        bool found = false;

        for (ComponentPlayerController& controller : level.playerControllers.components) {
            controller.isActive = controller.ownerID == entityID;

            if (controller.isActive) {
                found = true;
            }
        }

        if (!found) {
            spdlog::warn(
                "Tried to set active player controller to entity {}, but that entity has no player controller component",
                entityID
            );
        }
    }

    ComponentPlayerController* activeController;
}

namespace LevelSystem {
    ComponentCamera* GetActiveCamera(Level& level) {
        for (ComponentCamera& camera : level.cameras.components) {
            if (camera.isActive) {
                return &camera;
            }
        }

        return nullptr;
    }

    void Start(Level& level) {
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

        for (const ComponentCamera& cam : level.cameras.components) {
            if (cam.isActive) {
                //todo support multiple cameras
                SetActiveCamera(cam.ownerID, level);
                break;
            }
        }

        for (Entity& entity : level.entities) {
            entity.Start();
        }

        activeController = GetActivePlayerController(level);



        if (activeController != nullptr) {
            ComponentTransform* playerTransform =
                level.transforms.Get(activeController->ownerID);

            if (playerTransform == nullptr) {
                spdlog::error(
                    "Level::Start skipped player controller: entity {} has no transform",
                    activeController->ownerID
                );
            }
            else {
                ComponentCamera* activeCamera =
                    GetActiveCamera(level);

                if (activeCamera == nullptr) {
                    spdlog::warn(
                        "Level::Start skipped player controller: no active camera"
                    );
                }
                else {
                    PlayerControllerSystem::Start(
                        *activeController,
                        *playerTransform,
                        *activeCamera,
                        level.sectors
                    );
                }
            }
        }

        // Future level start systems will run here.
    }
    void Update(Level& level) {
        for (Entity& entity : level.entities) {
            entity.Update();
        }

        if (activeController != nullptr) {
            ComponentTransform* playerTransform = level.transforms.Get(activeController->ownerID);

            if (playerTransform == nullptr) {
                spdlog::error(
                    "Level::Update skipped player controller: entity {} has no transform",
                    activeController->ownerID
                );
            }
            else {
                ComponentCamera* activeCamera =
                    GetActiveCamera(level);

                if (activeCamera == nullptr) {
                    spdlog::warn(
                        "Level::Update skipped player controller: no active camera"
                    );
                }
                else {
                    PlayerControllerSystem::Update(
                        *activeController,
                        *playerTransform,
                        *activeCamera,
                        level.walls,
                        level.sectors
                    );
                }
            }
        }

        {
            //todo make a world setting
            constexpr float friction = 9.8f;
            ZoneScopedN("Physics");
            for (ComponentRigidbody& r : level.rigidbodies.components) {
                // make sure to remove the include
                if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) r.AddForce({1.0f, 0.0f, 0.0f});


                if (r.velocity.IsZero()) continue;

                ComponentTransform* transform = level.transforms.Get(r.ownerID);

                transform->AddPosition(r.velocity);
                r.ApplyFriction(friction);
                r.ApplyAirResistance(friction);

                if (transform->position.z - transform->scale.y * .5f >= level.sectors[transform->sectorIndex].floorHeight)
                r.ApplyGravity(friction);
            }
        }

        {
            ZoneScopedN("Collision");

            auto ResolveCollision = [](ComponentTransform& aTrans, const ComponentSphereCollider& aCol,
                              ComponentTransform& bTrans, const ComponentSphereCollider& bCol)
            {
                const Vector3 delta = aTrans.position - bTrans.position;
                const float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;

                if (distanceSq == 0.0f) {
                    aTrans.position.x += 0.1f;
                    return;
                }

                const float distance = std::sqrt(distanceSq);
                const float overlap = (aCol.size + bCol.size) - distance;
                const Vector3 normal = { delta.x / distance, delta.y / distance, delta.z / distance };

                float aWeight = 0.5f;
                float bWeight = 0.5f;

             //   if (aCol.isStatic) { aWeight = 0.0f; bWeight = 1.0f; }
              //  else if (bCol.isStatic) { aWeight = 1.0f; bWeight = 0.0f; }

              //  if (aCol.isStatic && bCol.isStatic) return;

                float ax = normal.x * overlap * aWeight;
                float ay = normal.y * overlap * aWeight;
                float az = normal.z * overlap * aWeight;

                float bx = -normal.x * overlap * bWeight;
                float by = -normal.y * overlap * bWeight;
                float bz = -normal.z * overlap * bWeight;

                aTrans.AddPosition({ax, ay, az});
                bTrans.AddPosition({bx, by, bz});
            };

            constexpr int COLLISION_ITERATIONS = 4;
            for (int i = 0; i < COLLISION_ITERATIONS; ++i) {
                for (const ComponentSphereCollider &selfCollider: level.sphereColliders.components) {
                    ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);
                    Vector3 selfPos = selfTransform->position;
                    const float selfSize = selfCollider.size;
                    const float selfHalfSize = selfSize * .5f;

                    if (!selfTransform->isDirty) [[unlikely]] continue;

                    Sector &sector = level.sectors[selfTransform->sectorIndex];

                    for (const uint32_t entityId: sector.entitiesInside) {
                        ComponentTransform *otherTransform = level.transforms.Get(entityId);
                        const ComponentSphereCollider *otherCollider = level.sphereColliders.Get(entityId);

                        if (!otherCollider || !otherCollider->isActive) continue;

                        if (Vector3Math::DistanceSquared(otherTransform->position, selfPos) <
                            (otherCollider->size + selfSize) * (otherCollider->size + selfSize)) {
                            ResolveCollision(*selfTransform, selfCollider, *otherTransform, *otherCollider);
                        }
                    }

                    //Neighboring sectors.
                    //Can be potentially commented-out in exchange for better performance but potential glitches between sectors
                    for (const Sector *nSector: sector.neighbors) {
                        if (!nSector) [[unlikely]] continue;
                        for (const uint32_t entityId: nSector->entitiesInside) {
                            ComponentTransform *otherTransform = level.transforms.Get(entityId);
                            ComponentSphereCollider *otherCollider = level.sphereColliders.Get(entityId);

                            if (!otherCollider || !otherCollider->isActive) continue;

                            if (Vector3Math::DistanceSquared(otherTransform->position, selfPos) <
                                (otherCollider->size + selfSize) * (otherCollider->size + selfSize)) {
                                ResolveCollision(*selfTransform, selfCollider, *otherTransform, *otherCollider);
                            }
                        }
                    }

                    // Wall collision
                    for (const Wall& wall : sector.walls) {
                        if (wall.floor != selfTransform->floor) continue;

                        const Vector2 selfPosVector2 = (Vector2){selfPos.x, selfPos.y};

                        const Vector2 w = selfPosVector2 - wall.start;
                        const float unClampedT = Vector2Math::Dot(w, wall.vector) / (wall.length * wall.length);

                        const float t = std::max(std::min(unClampedT, 1.0f), .0f);

                        const Vector2 closestPoint = wall.start + t * wall.vector;

                        const float distSq = Vector2Math::DistanceSquared(selfPosVector2, closestPoint);

                        if (distSq < selfHalfSize * selfHalfSize) {
                            const float currentDistance = std::sqrt(distSq);
                            const float overlapDistance = selfHalfSize - currentDistance;

                            Vector2 pushDirection = { 0.0f, 0.0f };

                            if (currentDistance > 0.001f) [[likely]] {
                                pushDirection.x = (selfPosVector2.x - closestPoint.x) / currentDistance;
                                pushDirection.y = (selfPosVector2.y - closestPoint.y) / currentDistance;
                            } else [[unlikely]] {
                                pushDirection.x = -wall.vector.y / wall.length;
                                pushDirection.y = wall.vector.x / wall.length;
                            }

                            float x = pushDirection.x * overlapDistance;
                            float y = pushDirection.y * overlapDistance;

                            selfTransform->AddPosition({x, y, 0});
                        }
                    }

                    //todo Fix player controller collision bug

                } // Collision loop
            }
        } // Zone Collision

        for (ComponentTransform& transform : level.transforms.components) {
          //  transform.isDirty = false;
        }

        // Future systems should still run here.
        // Example:
        // PhysicsSystem::Update(*this);
        // AnimationSystem::Update(*this);
        // TriggerSystem::Update(*this);
    }
}
