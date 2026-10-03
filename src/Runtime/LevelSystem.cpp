//
// Created by berke on 5/26/2026.
//

/// This script updates everything that will run in the level.
/// Such as renderer, physics and scripts
/// Does not run in the editor

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include <tracy/Tracy.hpp>

#include <optional>
#include <string>
#include <utility>

#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/Runtime/Gameplay/PlayerControllerSystem.hpp"
#include "Headers/Runtime/Scripting/CSharp/CSharpScripting.hpp"

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

    // The controller last reported as unusable, so a broken setup is logged
    // once rather than every frame.
    ID reportedControllerID = INVALID_ENTITY_ID;

    // The controller that ran last frame. When control leaves it, its walking
    // velocity is cleared, or the old player would keep sliding.
    ID previousControllerID = INVALID_ENTITY_ID;

    // See RequestLevelLoad.
    std::optional<std::string> requestedLevel;

    LuaScriptSystem scriptingSystem;
    bool scriptingInitialized = false;

    // Entity-entity contacts PhysicsSystem found this frame, handed to the
    // scripts' OnCollision* / OnTrigger* callbacks. Kept around only to
    // reuse its allocations.
    PhysicsSystem::Contacts frameContacts;

    bool EnsureScriptingInitialized() {
        if (scriptingInitialized) return true;


        if (!scriptingSystem.Initialize()) {
            spdlog::critical("Failed to initialize script system");
            return false;
        }

        scriptingInitialized = true;
        return true;
    }

}

namespace LevelSystem {
    void RefreshScriptAssets(Level& level) {
        if (!EnsureScriptingInitialized()) return;

        scriptingSystem.RefreshScriptAssets(level);
    }

    bool EnsureScriptingInitialized() {
        if (scriptingInitialized) return true;

        if (!scriptingSystem.Initialize()) {
            spdlog::critical("Failed to initialize script system");
            return false;
        }

        scriptingInitialized = true;
        return true;
    }

    const std::vector<ScriptPublicField>* GetPublicFieldsForScript(const std::string& fileName) {
        if (!EnsureScriptingInitialized()) return nullptr;

        return scriptingSystem.GetPublicFieldsForScript(fileName);
    }

    bool ReconcileScriptPublicValues(ComponentScript& script) {
        if (!EnsureScriptingInitialized()) return false;

        return scriptingSystem.ReconcileScriptPublicValues(script);
    }

    bool ReconcileScriptPublicValues(ScriptAttachmentData& script, const std::string& ownerLabel) {
        if (!EnsureScriptingInitialized()) return false;

        return scriptingSystem.ReconcileScriptPublicValues(script, ownerLabel);
    }

    const std::string* GetScriptLoadError(const std::string& fileName) {
        if (!EnsureScriptingInitialized()) return nullptr;

        return scriptingSystem.GetScriptLoadError(fileName);
    }

    ComponentCamera *GetActiveCamera(Level &level) {
        for (ComponentCamera &camera: level.cameras.components)
            if (camera.isActive) return &camera;


        return nullptr;
    }

    void Start(Level &level) {
        ZoneScopedN("LevelSystemStart");
        if (!EnsureScriptingInitialized()) return;

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

        // A level saved with several ticked keeps the first of each.
        if (const ComponentCamera *camera = GetActiveCamera(level)) level.ActivateCamera(camera->ownerID);
        if (const ComponentPlayerController *controller = GetActivePlayerController(level))
            level.ActivatePlayerController(controller->ownerID);
        reportedControllerID = INVALID_ENTITY_ID;
        previousControllerID = INVALID_ENTITY_ID;

        // for (Entity &entity: level.entities) {
        //     entity.Start(); // Currently does nothing, probably should do nothing
        //     let's leave it here in case we need it later
        // }

        for (ComponentTransform &transform: level.transforms.components)
            transform.UpdateObjectSectorAndFloor(level.sectors);

        ComponentCamera *activeCamera = GetActiveCamera(level);
        if (activeCamera == nullptr && !level.cameras.components.empty()) {
            const ID cameraEntityID = level.cameras.components.front().ownerID;
            level.ActivateCamera(cameraEntityID);
            activeCamera = GetActiveCamera(level);

            spdlog::info(
                "No active camera was selected; using camera entity {}",
                cameraEntityID
            );
        }

        if (activeCamera == nullptr) {
            spdlog::error("Level::Start failed: the level has no camera");
            return;
        }

        if (GetActivePlayerController(level) == nullptr) spdlog::info("Level started without an active player controller");

        {
            ZoneScopedN("Scripting System Start");
            scriptingSystem.Start(level);
        }
        // Future level start systems will run here.
    }

    void Update(Level &level) {
        // for (Entity &entity: level.entities) {
        //     entity.Update(); // currently does nothing. Probably should do nothing
        // }

        {
            ZoneScopedN("Scripts");
            scriptingSystem.Update(level);
        }

        {
            // After scripts, so a move started this frame already moves.
            ZoneScopedN("Sector Movement");
            level.UpdateSectorMovement(GameTime::deltaTime);
        }

        // Looked up every frame, so ticking another controller switches to it.
        ComponentPlayerController *activeController = GetActivePlayerController(level);
        const ID activeControllerID = activeController != nullptr ? activeController->ownerID : INVALID_ENTITY_ID;

        if (activeControllerID != previousControllerID) {
            if (ComponentRigidbody *released = level.rigidbodies.Get(previousControllerID)) {
                released->velocity.x = 0.0f;
                released->velocity.z = 0.0f;
            }

            previousControllerID = activeControllerID;
        }

        if (activeController != nullptr) {
            const ID ownerID = activeController->ownerID;

            ComponentTransform *playerTransform = level.transforms.Get(ownerID);
            ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(ownerID);
            ComponentCamera *ownCamera = level.cameras.Get(ownerID);
            const ComponentCamera *activeCamera = GetActiveCamera(level);

            if (playerTransform == nullptr || playerRigidbody == nullptr || (ownCamera == nullptr && activeCamera == nullptr)) [[unlikely]] {
                if (reportedControllerID != ownerID) {
                    spdlog::error(
                        "Player controller entity {} skipped: it needs a transform, a rigidbody and a camera (its own or an active one)",
                        ownerID
                    );
                    reportedControllerID = ownerID;
                }
            }
            else {
                PlayerControllerSystem::Update(
                    *activeController,
                    *playerTransform,
                    ownCamera,
                    activeCamera,
                    *playerRigidbody,
                    level.colliders.Get(ownerID),
                    level.sectors
                );
            }
        }

        {
            //todo TILKY_TODO
            // sort entities where sphere colliders are in the beggining of the vector to optimize for branch prediction
            ZoneScopedN("Physics");
            constexpr int COLLISION_ITERATIONS = 1;
            const float subDeltaTime = GameTime::deltaTime / static_cast<float>(COLLISION_ITERATIONS);

            frameContacts.Clear();

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

                    if (!r.velocity.IsZero()) transform->AddPosition(r.velocity * subDeltaTime);
                }
                PhysicsSystem::Run(level, frameContacts);
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

                if (!transform.isDirty) continue;

                // Every moved entity updates its sector, not only physics
                // bodies: a script moving a plain entity has to show up in
                // Sector:ContainsEntity and OnEntityEnter too.
                const int oldSectorIndex = transform.sectorIndex;
                transform.UpdateObjectSectorAndFloor(level.sectors);

                const int newSectorIndex = transform.sectorIndex;

                // Step-down below is for physics bodies only, and needs a
                // sector on both sides (-1 means outside the map).
                if (level.rigidbodies.Get(transform.ownerID) == nullptr) continue;
                if (oldSectorIndex < 0 || newSectorIndex < 0) continue;

                // UpdateObjectSectorAndFloor() shouldn't change the transform position
                // Doing this is technically slower but it is cleaner
                if (oldSectorIndex != newSectorIndex) {
                    const ComponentCollider* collider =
                        owner->GetComponent<ComponentCollider>();

                    if (collider == nullptr) continue;

                    const float oldHeight =
                        level.sectors[oldSectorIndex]
                            .floors[0].floor.height;

                    const float newHeight =
                        level.sectors[newSectorIndex]
                            .floors[0].floor.height;

                    // Upward stepping and camera smoothing are already detected
                    // from the physical Y change in the renderer.
                    if (newHeight >= oldHeight) continue;

                    ComponentCamera* camera = owner->GetComponent<ComponentCamera>();

                    const float distanceToStep = transform.position.y - newHeight;

                    const bool canStepDown = collider->stepSize > 0.0f &&
                        distanceToStep > Constants::Epsilon &&
                        distanceToStep <= collider->stepSize + Constants::Epsilon;

                    if (!canStepDown) continue;

                    const float previousTransformY = transform.position.y;

                    transform.AddPosition({
                        0.0f,
                        -distanceToStep,
                        0.0f
                    });

                    transform.relativeHeight = 0.0f;

                    if (camera != nullptr && camera->smoothStep) {
                        float currentVisualY =
                            previousTransformY;

                        if (camera->isStepping)
                            currentVisualY =
                                camera->smoothStepStartY + camera->stepOffsetY + (previousTransformY - camera->smoothStepTargetY);

                        camera->smoothStepStartY = currentVisualY;
                        camera->smoothStepTargetY = transform.position.y;

                        camera->stepOffsetY = 0.0f;
                        camera->isStepping = true;
                    }
                }
            }
        }

        {
            // After every position and sector change of the frame.
            ZoneScopedN("Sector Occupancy Events");
            scriptingSystem.DispatchSectorOccupancyEvents(level);
            scriptingSystem.DispatchSectorChangeEvents(level);
        }

        {
            ZoneScopedN("Contact Events");
            scriptingSystem.DispatchContactEvents(level, frameContacts);
        }

        for (ComponentTransform &transform: level.transforms.components) transform.isDirty = false;

        // Runs last, after every system above has finished reading this
        // frame's entities/components - see FlushPendingDestroys's
        // declaration comment for why Entity:Destroy() is deferred this
        // far rather than applied inside scriptingSystem.Update() itself.
        scriptingSystem.FlushPendingDestroys(level);
    }

    void Shutdown(Level &level) {
        scriptingSystem.Shutdown();
        scriptingInitialized = false;
        reportedControllerID = INVALID_ENTITY_ID;
        previousControllerID = INVALID_ENTITY_ID;
        requestedLevel.reset();
    }

    void StopLevel(Level &level) {
        scriptingSystem.Stop(level);
        reportedControllerID = INVALID_ENTITY_ID;
        previousControllerID = INVALID_ENTITY_ID;
    }

    void RequestLevelLoad(const std::string &levelName) {
        requestedLevel = levelName;
    }

    std::optional<std::string> TakeRequestedLevel() {
        return std::exchange(requestedLevel, std::nullopt);
    }
}
