//
// Created by berke on 5/26/2026.
//

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include "tracy/Tracy.hpp"

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

        for (ComponentTransform& transform : level.transforms.components) {
            transform.UpdateObjectSectorAndFloor(level.sectors, level.GetEntity(transform.ownerID));
        }

        activeController = GetActivePlayerController(level);

        if (activeController != nullptr) {
            ComponentTransform *playerTransform = level.transforms.Get(activeController->ownerID);
            ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(activeController->ownerID);

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
            } else spdlog::error("Level::Start skipped player controller: entity {} has no transform",
                                 activeController->ownerID);
        }

        // Future level start systems will run here.
    }

    void Update(Level &level) {
        for (Entity &entity: level.entities) {
            entity.Update(); // currently does nothing
        }

        {
            ZoneScopedN("Transform setup");
            for (ComponentTransform& transform : level.transforms.components) {
                transform.UpdateObjectSectorAndFloor(level.sectors, level.GetEntity(transform.ownerID));
            }
        }


        if (activeController != nullptr && activeController->isActive) {
            ComponentTransform *playerTransform = level.transforms.Get(activeController->ownerID);
            ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(activeController->ownerID);
            ComponentSphereCollider *playerSphereCollider = level.sphereColliders.Get(activeController->ownerID);

            if (playerTransform != nullptr) [[likely]]{
                ComponentCamera *activeCamera = GetActiveCamera(level);

                if (activeCamera != nullptr) {
                    PlayerControllerSystem::Update(
                        *activeController,
                        *playerTransform,
                        *activeCamera,
                        *playerRigidbody,
                        *playerSphereCollider,
                        level.sectors
                    );
                } else spdlog::warn("Level::Update skipped player controller: no active camera");
            } else spdlog::error("Level::Update skipped player controller: entity {} has no transform",
                                 activeController->ownerID);
        }

        {
            //todo make a world setting
            constexpr float friction = 9.8f;

            ZoneScopedN("Physics");

            for (ComponentRigidbody &r: level.rigidbodies.components) {
                ComponentTransform *transform = level.transforms.Get(r.ownerID);

                if (transform->position.z > 0) r.ApplyGravity(friction);

                if (transform->position.z <= 0.0f) {
                    transform->position.z = 0.0f;

                    if (r.velocity.z < 0.0f)
                        r.velocity.z = 0.0f;
                }

                if (r.velocity.IsZero()) continue;

                transform->AddPosition(r.velocity * GameTime::deltaTime);
                r.ApplyFriction(friction);
                r.ApplyAirResistance(friction);
            }
        }

        {
            ZoneScopedN("Collision");

            //todo make this a world setting
            constexpr int COLLISION_ITERATIONS = 4;
            for (int i = 0; i < COLLISION_ITERATIONS; i++) {
                for (const ComponentSphereCollider &selfCollider: level.sphereColliders.components) {
                    if (!selfCollider.isActive) continue;

                    ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);

                    if (selfTransform == nullptr) [[unlikely]]continue;

                    if (selfTransform->sectorIndex < 0 || selfTransform->sectorIndex >= static_cast<int>(level.sectors.size()))
                        continue;

                    Sector& sector = level.sectors[selfTransform->sectorIndex];
                    if (!selfTransform->isDirty) [[unlikely]] continue;

                    ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);
                    if (selfRb == nullptr) continue; // collision checks should only happen on entities with rigidbides

                    std::vector<Entity*> allEntities;
                    allEntities.insert(allEntities.end(), sector.entitiesInside.begin(), sector.entitiesInside.end());
                    //Neighboring sectors.
                    //Can be potentially commented-out in exchange for better performance but potential glitches between sectors
                    for (const Sector *nSector: sector.neighbors) {
                        if (!nSector) [[unlikely]] continue;
                        allEntities.insert(allEntities.end(), nSector->entitiesInside.begin(),
                                           nSector->entitiesInside.end());
                    }

                    Vector3 selfPos = selfTransform->position;
                    const Vector2 selfVector2 = (Vector2){selfTransform->position.x, selfTransform->position.y};

                    const float selfSize = selfCollider.size;

                    for (Entity* otherEntity : allEntities) {;
                        if (otherEntity->id == level.GetEntity(selfCollider.ownerID)->id) continue;

                        if (!otherEntity->HasComponent<ComponentSphereCollider>()) continue;

                        auto *otherCollider = otherEntity->GetComponent<ComponentSphereCollider>();

                        if (!otherCollider->isActive) continue;

                        auto *otherTransform = otherEntity->GetComponent<ComponentTransform>();

                        Vector3 otherPos = otherTransform->position;

                        const float otherSize = otherCollider->size;
                        const float minDistance = otherSize + selfSize;

                        if (Vector3Math::DistanceSquared(otherPos, selfPos) < minDistance * minDistance) [[unlikely]] {
                            ComponentRigidbody *otherRb = nullptr;
                            if (otherEntity->HasComponent<ComponentRigidbody>())
                                otherRb = otherEntity->GetComponent<ComponentRigidbody>();

                            const float overlap = selfSize + otherSize - Vector3Math::Distance(otherPos, selfPos);

                            Vector3 pushDirection = Vector3Math::Normalized(otherPos - selfPos);

                            //todo implement mass
                            float selfWeight = 0.5f;
                            float otherWeight = 0.5f;

                            const bool aStatic = selfRb->isStatic; // Can not reach here if selfRb is null
                            const bool bStatic = (otherRb == nullptr) || otherRb->isStatic;

                            if (bStatic && aStatic) [[unlikely]] return;

                            if (aStatic) {
                                selfWeight = 0.0f;
                                otherWeight = 1.0f;
                            } else if (bStatic) {
                                selfWeight = 1.0f;
                                otherWeight = 0.0f;
                            }

                            Vector3 selfPush =
                                    (Vector3){pushDirection.x, pushDirection.y, pushDirection.z} * overlap * selfWeight;
                            Vector3 otherPush =
                                    (Vector3){pushDirection.x, pushDirection.y, pushDirection.z} * overlap * otherWeight
                                    * -1;

                          //  selfTransform->AddPosition(selfPush);
                           // otherTransform->AddPosition(otherPush);

                            spdlog::info("Entity {} collided with Entity {}",
                                         level.entities[selfCollider.ownerID].name, otherEntity->name);
                        }
                    }

                    // Wall collision
                    for (const Wall &wall: sector.walls) {
                        const Vector2 w = selfVector2 - wall.start;
                        const Vector2 v = wall.vector;

                        const float vDotV = Vector2Math::Dot(v, v);
                        if (vDotV <= 0.001f) continue;

                        float t = Vector2Math::Dot(w, v) / vDotV;
                        t = std::max(0.0f, std::min(1.0f, t));

                        Vector2 closestPoint = wall.start + t * v;

                        const float distanceSquared = Vector2Math::DistanceSquared(closestPoint, selfVector2);

                        if (distanceSquared < selfSize * selfSize) [[unlikely]] {
                            const float overlap = std::sqrt(distanceSquared);

                            const Vector2 pushDirection = closestPoint - selfVector2;
                            const Vector2 push = pushDirection * overlap;

                            //selfTransform->AddPosition((Vector3){push.x, push.y, 0.0f});
                            spdlog::info("Entity {} collided with a wall", level.entities[selfCollider.ownerID].name);
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
