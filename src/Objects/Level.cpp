//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Level.hpp"

Entity* Level::GetEntity(const ID entityID) {
    for (Entity& entity : entities) {
        if (entity.id == entityID) {
            return &entity;
        }
    }

    return nullptr;
}

const Entity* Level::GetEntity(const ID entityID) const {
    for (const Entity& entity : entities) {
        if (entity.id == entityID) {
            return &entity;
        }
    }

    return nullptr;
}

ID Level::CreateEntity(const bool uiEntity) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = "Entity";
    entity.attachedLevelId = id;

    entities.push_back(entity);

    const ID newId = entity.id;

    if (uiEntity) [[unlikely]] {
        ui_transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_UI_TRANSFORM);
    } else [[likely]] {
        transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_TRANSFORM);
    }

    return newId;
}

ID Level::CreateEntity(Entity& copy) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = copy.name + "Copy";

    if (copy.HasComponent<ComponentTransform>())
        entity.AddComponent<ComponentTransform>();

    if (copy.HasComponent<ComponentSprite>())
        entity.AddComponent<ComponentSprite>();

    if (copy.HasComponent<ComponentDecal>())
        entity.AddComponent<ComponentDecal>();

    if (copy.HasComponent<ComponentAudioSource>())
        entity.AddComponent<ComponentAudioSource>();

    if (copy.HasComponent<ComponentScript>())
        entity.AddComponent<ComponentScript>();

    if (copy.HasComponent<ComponentPlayerController>())
        entity.AddComponent<ComponentPlayerController>();

    if (copy.HasComponent<ComponentCamera>())
        entity.AddComponent<ComponentCamera>();

    if (copy.HasComponent<ComponentCollider>())
        entity.AddComponent<ComponentCollider>();

    if (copy.HasComponent<ComponentRigidbody>())
        entity.AddComponent<ComponentRigidbody>();

    // UI Components
    if (copy.HasComponent<ComponentUITransform>())
        entity.AddComponent<ComponentUITransform>();

    if (copy.HasComponent<ComponentUISprite>())
        entity.AddComponent<ComponentUISprite>();

    if (copy.HasComponent<ComponentUIText>())
        entity.AddComponent<ComponentUIText>();


    entities.push_back(entity);

    return entity.id;
}

void Level::DestroyEntity(const ID entityID) {
    if (entityID == INVALID_ID) return;

    for (Sector& sector : sectors) std::erase(sector.entitiesInside, entityID);

    colliders.Remove(entityID);
    rigidbodies.Remove(entityID);

    sprites.Remove(entityID);
    decals.Remove(entityID);
    audioSources.Remove(entityID);
    scripts.Remove(entityID);
    playerControllers.Remove(entityID);
    cameras.Remove(entityID);
    transforms.Remove(entityID);

    // UI components.
    ui_sprites.Remove(entityID);
    ui_texts.Remove(entityID);
    ui_transforms.Remove(entityID);

    std::erase_if(entities, [entityID](const Entity& entity) {
        return entity.id == entityID;
    });
}

void Level::DestroyEntity(const Entity& entity) {
    DestroyEntity(entity.id);
}