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

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/Runtime/EventSystem.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Runtime/Gameplay/PlayerControllerSystem.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Runtime/Gameplay/FlipbookSystem.hpp"
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
    namespace {
        SkySettings startingSky;
    }

    void CaptureStartingSettings(const Level& level) {
        startingSky = level.sky;
        startingSky.spin = 0.0f;
        EventSystem::BeginLevelMessages(level);
    }

    const SkySettings& StartingSky() {
        return startingSky;
    }

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

        // Before the scripts start, so their Start callbacks change and show from here.
        CaptureStartingSettings(level);

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
        if (const ComponentCamera *camera = GetActiveCamera(level)) level.ActivateCamera(*camera);
        if (const ComponentPlayerController *controller = GetActivePlayerController(level))
            level.ActivatePlayerController(*controller);
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
            level.ActivateCamera(level.cameras.components.front());
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

        // After scripts, so a first-frame event finds their functions.
        FlipbookSystem::Start(level);

        // After the transforms' sectors are set, so they are the baseline.
        EventSystem::Start(level, {
            .callFunction = [](Level& lvl, const ID entityID, const std::string& functionName, const ID instigatorID) {
                scriptingSystem.DispatchEntityEvent(lvl, entityID, functionName, instigatorID);
            },
            .destroyEntity = [](const ID entityID) { LuaScriptRuntime::QueueEntityDestroy(entityID); },
            .spawnPrefab = [](Level& lvl, const std::string& prefab) {
                const ID id = Prefab::Instantiate(lvl, prefab);
                if (id != INVALID_ID) LuaScriptRuntime::QueueEntityScriptsStart(id);
                return id;
            },
            .loadLevel = [](const std::string& levelName) {
                // Checked like Game.LoadLevel, so a wrong name says so here.
                std::string errorMessage;
                if (LevelSerialization::FindLevelPath(levelName, &errorMessage).empty()) {
                    spdlog::warn("Load Level dispatch: {}", errorMessage);
                    return;
                }

                RequestLevelLoad(levelName);
            },
        });

        // After the scripts' start callbacks.
        EventSystem::FireLevelStart(level);
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

        {
            // After scripts, so a Play() from this frame's Update shows this frame.
            ZoneScopedN("Flipbooks");
            FlipbookSystem::Update(level, GameTime::deltaTime,
                [&level](const ID entityID, const std::string& functionName, const std::string& frameName) {
                    return scriptingSystem.DispatchEntityEvent(level, entityID, functionName, frameName);
                });
        }

        // Only here, so the sky spins while the game runs and never in the editor.
        level.sky.spin = std::fmod(level.sky.spin + level.sky.rotationSpeed * GameTime::deltaTime, 360.0f);

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
            const ComponentCamera *activeCamera = GetActiveCamera(level);
            // The entity's own camera: the active one if it's on this entity, else its first.
            ComponentCamera *ownCamera = activeCamera != nullptr && activeCamera->ownerID == ownerID
                ? level.cameras.GetInstance(activeCamera->instanceID)
                : level.cameras.Get(ownerID);

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
                    level.sectors
                );

                // Use: a typed ray from the eye along the view.
                const ComponentCamera* viewCamera = ownCamera != nullptr && ownCamera->isActive ? ownCamera : activeCamera;
                if (viewCamera != nullptr && activeController->useKey != SDL_SCANCODE_UNKNOWN &&
                    InputManager::GetKeyDown(activeController->useKey)) {
                    Vector3 eye = playerTransform->position;
                    eye.y += activeController->eyeHeight;

                    GameFunctions::Raycast(level, eye, CameraSystem::GetCameraForwardEngineSpace(viewCamera->yaw, viewCamera->pitch),
                                           activeController->useReach, ownerID, false, activeController->useRayType);
                }
            }
        }

        {
            //todo TILKY_TODO
            // sort entities where sphere colliders are in the beggining of the vector to optimize for branch prediction
            ZoneScopedN("Physics");
            // More steps only on frames where something moves fast for
            // its size; one is enough otherwise.
            const int substeps = PhysicsSystem::SubstepCount(level, GameTime::deltaTime, level.worldSettings.maxPhysicsSubsteps);
            const float subDeltaTime = GameTime::deltaTime / static_cast<float>(substeps);

            frameContacts.Clear();

            for (int i = 0; i < substeps; i++) {
                for (ComponentRigidbody &r: level.rigidbodies.components) {
                    // Only an entity's first rigidbody simulates; the others are inert.
                    if (level.rigidbodies.Get(r.ownerID) != &r) continue;

                    ComponentTransform *transform = level.transforms.Get(r.ownerID);

                    if (!transform) [[unlikely]] {
                        spdlog::error("Rigidbody entity {} has no transform", r.ownerID);
                        continue;
                    }

                    // Only a collider can ground a body, so one without
                    // a collider falls through everything.
                    if (transform->sectorIndex != -1) [[unlikely]]
                        if (!r.isGrounded)
                            r.ApplyGravity(level.worldSettings.gravity, subDeltaTime);

                    // Apply Rb's base friction
                    r.ApplyFriction(0, subDeltaTime);
                    r.ApplyAirResistance(0, subDeltaTime);

                    if (!r.velocity.IsZero()) transform->AddPosition(r.velocity * subDeltaTime);
                }
                PhysicsSystem::Run(level, frameContacts, subDeltaTime);
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
                    // Measured from the base collider's bottom: the collider
                    // is what stands on the floor.
                    const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, transform.ownerID);

                    if (collider == nullptr) continue;

                    const Vector3 colliderBottom = collider->Base(transform);

                    // Sampled under the collider: a sloped floor's height is
                    // only its height at the sector's edge.
                    const Vector2 point = {colliderBottom.x, colliderBottom.z};

                    const Sector& oldSector = level.sectors[oldSectorIndex];
                    const Sector& newSector = level.sectors[newSectorIndex];

                    const float oldHeight =
                        MapQueries::SurfaceHeightAt(oldSector, oldSector.floors[0].floor, point);

                    const float newHeight =
                        MapQueries::SurfaceHeightAt(newSector, newSector.floors[0].floor, point);

                    // Upward stepping and camera smoothing are already detected
                    // from the physical Y change in the renderer.
                    if (newHeight >= oldHeight) continue;

                    const float distanceToStep = colliderBottom.y - newHeight;

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

                    for (ComponentCamera* camera : owner->GetComponents<ComponentCamera>()) {
                        if (!camera->smoothStep) continue;

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

        {
            // After every position and sector change, and before destroys
            // are flushed so a DESTROY dispatch is applied this frame.
            ZoneScopedN("Map Events");
            EventSystem::Update(level, frameContacts);
        }

        for (ComponentTransform &transform: level.transforms.components) transform.isDirty = false;

        // Runs last, after every system above has finished reading this
        // frame's entities/components - see FlushPendingDestroys's
        // declaration comment for why Entity:Destroy() is deferred this
        // far rather than applied inside scriptingSystem.Update() itself.
        scriptingSystem.FlushPendingDestroys(level);
    }

    void Shutdown(Level &level) {
        EventSystem::Stop();
        scriptingSystem.Shutdown();
        scriptingInitialized = false;
        reportedControllerID = INVALID_ENTITY_ID;
        previousControllerID = INVALID_ENTITY_ID;
        requestedLevel.reset();
    }

    void StopLevel(Level &level) {
        EventSystem::Stop();
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
