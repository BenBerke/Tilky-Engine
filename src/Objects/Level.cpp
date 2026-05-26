//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Level.hpp"

Entity* Level::GetEntity(const EntityID entityID) {
    for (Entity& entity : entities) {
        if (entity.id == entityID) {
            return &entity;
        }
    }

    return nullptr;
}

const Entity* Level::GetEntity(const EntityID entityID) const {
    for (const Entity& entity : entities) {
        if (entity.id == entityID) {
            return &entity;
        }
    }

    return nullptr;
}

EntityID Level::CreateEntity(const bool uiEntity) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = "Entity";
    entity.attachedLevelId = id;

    entities.push_back(entity);

    const EntityID newId = entity.id;

    if (uiEntity) [[unlikely]] {
        ui_transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_UI_TRANSFORM);
    } else [[likely]] {
        transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_TRANSFORM);
    }

    return newId;
}

void Level::DestroyEntity(const EntityID entityID) {
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
    sphereColliders.Remove(entityID);

    ui_transforms.Remove(entityID);
    ui_sprites.Remove(entityID);
    ui_texts.Remove(entityID);
}

void Level::DestroyEntity(const Entity& entity) {
    DestroyEntity(entity.id);
}