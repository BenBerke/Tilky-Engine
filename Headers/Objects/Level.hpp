//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVEL_H
#define TILKY_ENGINE_LEVEL_H

#include <AL/al.h>
#include <vector>
#include <string>
#include <spdlog/spdlog.h>

#include "Components.hpp"
#include "Entity.hpp"
#include "Loadables.hpp"
#include "../Runtime/Gameplay/PlayerControllerSystem.hpp"
#include "../Runtime/Renderer/OpenGL/RendererTexture.hpp"

struct ListenerSettings {
    float masterGain = 1.0f;
    float dopplerFactor = 1.0f;
    float speedOfSound = 343.3f;
    ALenum distanceModel = AL_INVERSE_DISTANCE_CLAMPED;
};

struct WorldSettings {
    float gravity;
};

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<Entity> entities;

    EntityID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;

    std::unordered_map<ID, int> sectorIDToIndex;
    std::unordered_map<ID, int> wallIDToIndex;

    ID nextSectorID = 0;
    ID nextWallID = 0;

    std::vector<Texture> textures;
    std::vector<Sound> sounds;

    ListenerSettings listenerSettings;
    WorldSettings worldSettings;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentDecal> decals;
    ComponentStorage<ComponentAudioSource> audioSources;
    ComponentStorage<ComponentScript> scripts;
    ComponentStorage<ComponentPlayerController> playerControllers;
    ComponentStorage<ComponentCamera> cameras;
    ComponentStorage<ComponentCollider> colliders;
    ComponentStorage<ComponentRigidbody> rigidbodies;

    ComponentStorage<ComponentUITransform> ui_transforms;
    ComponentStorage<ComponentUISprite> ui_sprites;
    ComponentStorage<ComponentUIText> ui_texts;

    Entity* GetEntity(EntityID entityID);
    const Entity* GetEntity(EntityID entityID) const;
    EntityID CreateEntity(bool uiEntity);
    void DestroyEntity(EntityID entityID);
    void DestroyEntity(const Entity& entity);
};

#endif // TILKY_ENGINE_LEVEL_H