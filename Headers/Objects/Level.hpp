//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVEL_H
#define TILKY_ENGINE_LEVEL_H

#include <vector>
#include <string>
#include <spdlog/spdlog.h>

#include "Components.hpp"
#include "Entity.hpp"
#include "Loadables.hpp"
#include "../Runtime/PlayerControllerSystem.hpp"
#include "../Runtime/Renderer/OpenGL/RendererTexture.hpp"

struct ListenerSettings {
    float masterGain = 1.0f;
    float dopplerFactor = 1.0f;
    float speedOfSound = 343.3f;
    ALenum distanceModel = AL_INVERSE_DISTANCE_CLAMPED;
};

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<Entity> entities;

    EntityID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;

    std::vector<Texture> textures;
    std::vector<Sound> sounds;

    ListenerSettings listenerSettings;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentDecal> decals;
    ComponentStorage<ComponentAudioSource> audioSources;
    ComponentStorage<ComponentScript> scripts;
    ComponentStorage<ComponentPlayerController> playerControllers;
    ComponentStorage<ComponentCamera> cameras;

    ComponentStorage<ComponentUITransform> ui_transforms;
    ComponentStorage<ComponentUISprite> ui_sprites;
    ComponentStorage<ComponentUIText> ui_texts;

    Entity* GetEntity(const EntityID entityID) {
        for (Entity& entity : entities) {
            if (entity.id == entityID) {
                return &entity;
            }
        }

        return nullptr;
    }

    const Entity* GetEntity(const EntityID entityID) const {
        for (const Entity& entity : entities) {
            if (entity.id == entityID) {
                return &entity;
            }
        }

        return nullptr;
    }

    Entity& CreateEntity(const bool uiEntity) {
        Entity entity;
        entity.id = nextEntityID++;
        entity.name = "Entity";
        entity.attachedLevelId = id;

        entities.push_back(entity);

        Entity& createdEntity = entities.back();

        if (uiEntity) {
            ui_transforms.Add(createdEntity.id);
        }
        else {
            transforms.Add(createdEntity.id);
        }

        return createdEntity;
    }

    void DestroyEntity(const EntityID entityID) {
        std::erase_if(entities, [entityID](const Entity& entity) {
            return entity.id == entityID;
        });

        transforms.Remove(entityID);
        sprites.Remove(entityID);
        decals.Remove(entityID);
        audioSources.Remove(entityID);
        scripts.Remove(entityID);
        playerControllers.Remove(entityID);
        cameras.Remove(entityID);

        ui_transforms.Remove(entityID);
        ui_sprites.Remove(entityID);
        ui_texts.Remove(entityID);
    }

    void DestroyEntity(const Entity& entity) {
        DestroyEntity(entity.id);
    }

    ComponentPlayerController* GetActivePlayerController() {
        for (ComponentPlayerController& controller : playerControllers.components) {
            if (controller.isActive) {
                return &controller;
            }
        }

        return nullptr;
    }

    const ComponentPlayerController* GetActivePlayerController() const {
        for (const ComponentPlayerController& controller : playerControllers.components) {
            if (controller.isActive) {
                return &controller;
            }
        }

        return nullptr;
    }

    ComponentCamera* GetActiveCamera() {
        for (ComponentCamera& camera : cameras.components) {
            if (camera.isActive) {
                return &camera;
            }
        }

        return nullptr;
    }

    const ComponentCamera* GetActiveCamera() const {
        for (const ComponentCamera& camera : cameras.components) {
            if (camera.isActive) {
                return &camera;
            }
        }

        return nullptr;
    }

    ComponentTransform* GetActiveCameraTransform() {
        ComponentCamera* camera = GetActiveCamera();

        if (camera == nullptr) {
            return nullptr;
        }

        return transforms.Get(camera->ownerID);
    }

    ComponentTransform* GetActivePlayerTransform() {
        ComponentPlayerController* controller = GetActivePlayerController();

        if (controller == nullptr) {
            return nullptr;
        }

        return transforms.Get(controller->ownerID);
    }

    void SetActiveCamera(const EntityID entityID) {
        bool found = false;

        for (ComponentCamera& camera : cameras.components) {
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

    void SetActivePlayerController(const EntityID entityID) {
        bool found = false;

        for (ComponentPlayerController& controller : playerControllers.components) {
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

    void Start() {
        SoundManager::SetListenerGain(listenerSettings.masterGain);
        SoundManager::SetListenerDopplerFactor(listenerSettings.dopplerFactor);
        SoundManager::SetListenerSpeedOfSound(listenerSettings.speedOfSound);
        SoundManager::SetListenerDistanceModel(listenerSettings.distanceModel);

        spdlog::info(
            "Level audio settings applied. Gain: {}, Doppler: {}, Speed of sound: {}, Distance model: {}",
            listenerSettings.masterGain,
            listenerSettings.dopplerFactor,
            listenerSettings.speedOfSound,
            static_cast<int>(listenerSettings.distanceModel)
        );

        for (const ComponentCamera& cam : cameras.components) {
            if (cam.isActive) {
                //todo support multiple cameras
                SetActiveCamera(cam.ownerID);
                break;
            }
        }

        ComponentPlayerController* activeController =
            GetActivePlayerController();

        if (activeController != nullptr) {
            ComponentTransform* playerTransform =
                transforms.Get(activeController->ownerID);

            if (playerTransform == nullptr) {
                spdlog::error(
                    "Level::Start skipped player controller: entity {} has no transform",
                    activeController->ownerID
                );
            }
            else {
                ComponentCamera* activeCamera =
                    GetActiveCamera();

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
                        sectors
                    );
                }
            }
        }

        // Future level start systems will run here.
    }

    void Update() {
        for (Entity& entity : entities) {
            entity.Update();
        }

        ComponentPlayerController* activeController =
            GetActivePlayerController();

        if (activeController != nullptr) {
            ComponentTransform* playerTransform =
                transforms.Get(activeController->ownerID);

            if (playerTransform == nullptr) {
                spdlog::error(
                    "Level::Update skipped player controller: entity {} has no transform",
                    activeController->ownerID
                );
            }
            else {
                ComponentCamera* activeCamera =
                    GetActiveCamera();

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
                        walls,
                        sectors
                    );
                }
            }
        }

        // Future systems should still run here.
        // Example:
        // PhysicsSystem::Update(*this);
        // AnimationSystem::Update(*this);
        // TriggerSystem::Update(*this);
    }
};

#endif // TILKY_ENGINE_LEVEL_H