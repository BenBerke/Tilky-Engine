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
    ComponentStorage<ComponentPlayerSpawn> playerSpawns;
    ComponentStorage<ComponentAudioSource> audioSources;
    ComponentStorage<ComponentScript> scripts;

    ComponentStorage<ComponentUITransform> ui_transforms;
    ComponentStorage<ComponentUISprite> ui_sprites;

    Entity& CreateEntity(bool uiEntity) {
        Entity entity;
        entity.id = nextEntityID++;
        entity.name = "Entity";
        entity.attachedLevelId = id;

        entities.push_back(entity);

        Entity& createdEntity = entities.back();

        if (uiEntity) ui_transforms.Add(createdEntity.id);
        else transforms.Add(createdEntity.id);

        return createdEntity;
    }
    void DestroyEntity(const EntityID id) {
        std::erase_if(entities, [id](const Entity& entity) {
            return entity.id == id;
        });

        transforms.Remove(id);
        sprites.Remove(id);
        decals.Remove(id);
        playerSpawns.Remove(id);
        audioSources.Remove(id);
        scripts.Remove(id);

        ui_transforms.Remove(id);
        ui_sprites.Remove(id);
    }
    void DestroyEntity(const Entity& entity) {
        DestroyEntity(entity.id);
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
    }

    void Update() {
        for (Entity& entity : entities) entity.Update();
    }
};

#endif //TILKY_ENGINE_LEVEL_H