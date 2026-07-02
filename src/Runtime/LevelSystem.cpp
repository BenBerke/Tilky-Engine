//
// Created by berke on 5/26/2026.
//

/// This script handles the runtime renderer. Does not run in the editor

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include <tracy/Tracy.hpp>

#include "../../Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

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

    void SetActiveCamera(const ID entityID, Level &level) {
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

    void SetActivePlayerController(const ID entityID, Level &level) {
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
        if (!ScriptSystem::Initialize()) {
            spdlog::critical("Failed to initialize script system");
            return;
        }

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

        // for (Entity &entity: level.entities) {
        //     entity.Start(); // Currently does nothing
        // }

        for (ComponentTransform &transform: level.transforms.components)
            transform.UpdateObjectSectorAndFloor(level.sectors);

        activeController = GetActivePlayerController(level);

        if (activeController != nullptr) {
            ComponentTransform *playerTransform = level.transforms.Get(activeController->ownerID);
            const ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(activeController->ownerID);

            if (playerTransform != nullptr && playerRigidbody != nullptr) [[likely]] {
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

            } else spdlog::error("Level::Start skipped player controller: entity {} is missing transform or rigidbody", activeController->ownerID);

        } else spdlog::error("Level::Start skipped player controller: entity {} has no transform",activeController->ownerID);

        ScriptSystem::Start(level);

        // Future level start systems will run here.
    }

    void Update(Level &level) {
        // for (Entity &entity: level.entities) {
        //     entity.Update(); // currently does nothing. Probably should do nothing
        // }

        {
            ZoneScopedN("Scripts");
            ScriptSystem::Update(level);
        }

        if (activeController != nullptr && activeController->isActive) {
            const ID ownerID = activeController->ownerID;

            ComponentTransform *playerTransform = level.transforms.Get(ownerID);
            if (!playerTransform) [[unlikely]] {
                spdlog::error("Player controller entity {} has no transform", ownerID);
                return;
            }

            ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(ownerID);
            if (!playerRigidbody) [[unlikely]] {
                spdlog::error("Player controller entity {} has no rigidbody", ownerID);
                return;
            }

            ComponentCollider *playerCollider = level.colliders.Get(ownerID);

            ComponentCamera *activeCamera = GetActiveCamera(level);
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
            //todo sort entities where sphere colliders are in the beggining of the vector to optimize for branch prediction
            ZoneScopedN("Physics");
            constexpr int COLLISION_ITERATIONS = 1;
            const float subDeltaTime = GameTime::deltaTime / static_cast<float>(COLLISION_ITERATIONS);

            for (int i = 0; i < COLLISION_ITERATIONS; i++) {
                for (ComponentRigidbody &r: level.rigidbodies.components) {
                    ComponentTransform *transform = level.transforms.Get(r.ownerID);

                    if (!transform) [[unlikely]] {
                        spdlog::error("Rigidbody entity {} has no transform", r.ownerID);
                        continue;
                    }

                    if (transform->sectorIndex != -1) [[unlikely]]
                        if (transform->relativeHeight > 0.0001f)
                            r.ApplyGravity(level.worldSettings.gravity, subDeltaTime);

                    // Apply Rb's base friction
                    r.ApplyFriction(0, subDeltaTime);
                    r.ApplyAirResistance(0, subDeltaTime);

                    if (!r.velocity.IsZero())
                        transform->AddPosition(r.velocity * subDeltaTime);
                }
                PhysicsSystem::Run(level);
            }

        } // Zone Physics

        {
            ZoneScopedN("Transform setup");
            for (ComponentTransform &transform: level.transforms.components) {
                Entity *owner = level.GetEntity(transform.ownerID);

                if (!owner) [[unlikely]] {
                    spdlog::error("Transform owner {} does not exist", transform.ownerID);
                    continue;
                }

                if (level.rigidbodies.Get(transform.ownerID) == nullptr || !transform.isDirty) continue;
                transform.UpdateObjectSectorAndFloor(level.sectors);
            }
        }

        for (ComponentTransform &transform: level.transforms.components) transform.isDirty = false;
    }

    void Shutdown(Level &level) {
        ScriptSystem::Shutdown();
    }
}
