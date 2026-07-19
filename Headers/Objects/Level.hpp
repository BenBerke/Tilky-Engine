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
    float gravity = 9.8f;
};

// See src/Runtime/Renderer/OpenGL/OpenGLInit.cpp
enum RendererTextureSettings {
    PIXEL_ART_SHIMMERY,
    PIXEL_ART_LESS_MOIRE,
    PIXEL_ART_SMOOTH_DISTANCE,
    REALISTIC_NORMAL,
    RETRO,
    LOW_RES
};
struct RendererSettings {
    RendererTextureSettings textureSetting = PIXEL_ART_SHIMMERY;
};

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<Entity> entities;

    ID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;

    // These are needed so that walls/sectors dont get their IDs mixed up from deletion
    std::unordered_map<ID, int> sectorIDToIndex;
    std::unordered_map<ID, int> wallIDToIndex;

    ID nextSectorID = 0;
    ID nextWallID = 0;

    std::vector<Texture> textures;
    std::vector<Sound> sounds;

    ListenerSettings listenerSettings;
    WorldSettings worldSettings;
    RendererSettings rendererSettings;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentDecal> decals;
    ComponentStorage<ComponentAudioSource> audioSources;
    ComponentStorage<ComponentScript> scripts;
    ComponentStorage<ComponentPlayerController> playerControllers;
    ComponentStorage<ComponentCamera> cameras;
    ColliderStorage colliders;
    ComponentStorage<ComponentRigidbody> rigidbodies;

    ComponentStorage<ComponentUITransform> ui_transforms;
    ComponentStorage<ComponentUISprite> ui_sprites;
    ComponentStorage<ComponentUIText> ui_texts;

    Entity* GetEntity(ID entityID);
    const Entity* GetEntity(ID entityID) const;
    ID CreateEntity(bool uiEntity);
    ID CreateEntity(Entity& entity);
    void DestroyEntity(ID entityID);
    void DestroyEntity(const Entity& entity);
};

#endif // TILKY_ENGINE_LEVEL_H