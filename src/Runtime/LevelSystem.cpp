//
// Created by berke on 5/26/2026.
//

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include "tracy/Tracy.hpp"

namespace {
    ComponentPlayerController* GetActivePlayerController(Level& level)  {
        for (ComponentPlayerController& controller : level. playerControllers.components)
            if (controller.isActive) return &controller;

        return nullptr;
    }

    const ComponentCamera* GetActiveCamera(const Level& level) {
        for (const ComponentCamera& camera : level.cameras.components)
            if (camera.isActive) return &camera;

        return nullptr;
    }

    ComponentTransform* GetActiveCameraTransform(Level& level) {
        const ComponentCamera* camera = GetActiveCamera(level);

        if (camera == nullptr) return nullptr;

        return level.transforms.Get(camera->ownerID);
    }

    ComponentTransform* GetActivePlayerTransform(Level& level) {
        ComponentPlayerController* controller = GetActivePlayerController(level);

        if (controller == nullptr) return nullptr;

        return level.transforms.Get(controller->ownerID);
    }

    void SetActiveCamera(const EntityID entityID, Level& level) {
        bool found = false;

        for (ComponentCamera& camera : level.cameras.components) {
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

    void SetActivePlayerController(const EntityID entityID, Level& level) {
        bool found = false;

        for (ComponentPlayerController& controller : level.playerControllers.components) {
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

    ComponentPlayerController* activeController;
}

namespace LevelSystem {
    ComponentCamera* GetActiveCamera(Level& level) {
        for (ComponentCamera& camera : level.cameras.components)
            if (camera.isActive) return &camera;



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
            ComponentTransform* playerTransform = level.transforms.Get(activeController->ownerID);
            ComponentRigidbody* playerRigidboy = level.rigidbodies.Get(activeController->ownerID);

            if (playerTransform == nullptr)
                spdlog::error(
                    "Level::Start skipped player controller: entity {} has no transform", activeController->ownerID);
            else {
                ComponentCamera* activeCamera = GetActiveCamera(level);

                if (activeCamera == nullptr)
                    spdlog::warn("Level::Start skipped player controller: no active camera");
                else {
                    PlayerControllerSystem::Start(
                        *activeController,
                        *playerTransform,
                        *playerRigidboy,
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
            ComponentRigidbody* playerRigidbody = level.rigidbodies.Get(activeController->ownerID);
            ComponentSphereCollider* playerSphereCollider = level.sphereColliders.Get(activeController->ownerID);

            if (playerTransform == nullptr) {
                spdlog::error(
                    "Level::Update skipped player controller: entity {} has no transform",
                    activeController->ownerID
                );
            }
            else {
                ComponentCamera* activeCamera = GetActiveCamera(level);

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
                        *playerRigidbody,
                        *playerSphereCollider,
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
                ComponentTransform* transform = level.transforms.Get(r.ownerID);

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
            
        } // Zone Collision

        for (ComponentTransform& transform : level.transforms.components) {
            transform.isDirty = false;
        }

        // Future systems should still run here.
        // Example:
        // PhysicsSystem::Update(*this);
        // AnimationSystem::Update(*this);
        // TriggerSystem::Update(*this);
    }
}
