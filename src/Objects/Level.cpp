//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Level.hpp"

Entity* Level::GetEntity(const ID entityID) {
    for (Entity& entity : entities) if (entity.id == entityID) return &entity;
    return nullptr;
}

const Entity* Level::GetEntity(const ID entityID) const {
    for (const Entity& entity : entities) if (entity.id == entityID) return &entity;
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
    }
    else [[likely]] {
        transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_TRANSFORM);
    }

    return newId;
}

// Create entity through copy-paste
// Whenever a component is added or changed, this has to be updated too
// It would be good if we switch to C++26 and have reflections
ID Level::CreateEntity(Entity& copy) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = copy.name + "Copy";

    if (copy.HasComponent<ComponentTransform>()) {
        auto *s = entity.AddComponent<ComponentTransform>();
        const ComponentTransform *cs = copy.GetComponent<ComponentTransform>();

        s->position = cs->position;
        s->relativeHeight = cs->relativeHeight;
        s->forward = cs->forward;
        s->scale = cs->scale;
        s->sectorIndex = cs->sectorIndex;
        s->isDirty = cs->isDirty;
        s->rotation = cs->rotation;
    }

    if (copy.HasComponent<ComponentSprite>()) {
        auto *s = entity.AddComponent<ComponentSprite>();
        const ComponentSprite *cs = copy.GetComponent<ComponentSprite>();

        s->textureFileNames = cs->textureFileNames;
        s->sideCount = cs->sideCount;
        s->isStatic = cs->isStatic;
    }

    if (copy.HasComponent<ComponentAudioSource>()) {
        auto *s = entity.AddComponent<ComponentAudioSource>();
        const ComponentAudioSource *ca = copy.GetComponent<ComponentAudioSource>();

        s->name = ca->name;
        s->soundFileName = ca->soundFileName;
        s->pitch = ca->pitch;
        s->gain = ca->gain;
        s->looping = ca->looping;
        s->playOnStart = ca->playOnStart;
        s->referenceDistance = ca->referenceDistance;
        s->maxDistance = ca->maxDistance;
        s->rollOffFactor = ca->rollOffFactor;
        s->innerConeAngle = ca->innerConeAngle;
        s->outerConeAngle = ca->outerConeAngle;
        s->outerGain = ca->outerGain;
    }

    if (copy.HasComponent<ComponentScript>()) {
        auto *s = entity.AddComponent<ComponentScript>();
        const ComponentScript *cs = copy.GetComponent<ComponentScript>();

        s->fileName = cs->fileName;
        s->enabled = cs->enabled;
    }

    if (copy.HasComponent<ComponentPlayerController>()) {
        auto *s = entity.AddComponent<ComponentPlayerController>();
        const ComponentPlayerController *cs = copy.GetComponent<ComponentPlayerController>();

        s->isActive = cs->isActive;
        s->speed = cs->speed;
        s->runningSpeed = cs->runningSpeed;
        s->jumpPower = cs->jumpPower;
        s->eyeHeight = cs->eyeHeight;
        s->friction = cs->friction;
        s->sensitivityX = cs->sensitivityX;
        s->sensitivityY = cs->sensitivityY;
        s->noClip = cs->noClip;
        // velocity, currentSpeed, currentEyeHeight intentionally left as default (read-only runtime state)
    }

    if (copy.HasComponent<ComponentCamera>()) {
        auto *s = entity.AddComponent<ComponentCamera>();
        const ComponentCamera *cs = copy.GetComponent<ComponentCamera>();

        s->isActive = cs->isActive;
        s->yaw = cs->yaw;
        s->pitch = cs->pitch;
        s->fov = cs->fov;
        s->aspectRatio = cs->aspectRatio;
        s->nearPlane = cs->nearPlane;
        s->farPlane = cs->farPlane;
        // forward, target, view, projection intentionally left as default (runtime derived state)
    }

    if (copy.HasComponent<ComponentCollider>()) {
        auto *s = entity.AddComponent<ComponentCollider>();
        const ComponentCollider *cs = copy.GetComponent<ComponentCollider>();

        s->type = cs->type;
        s->isActive = cs->isActive;
        s->isTrigger = cs->isTrigger;
        s->scale = cs->scale;
        s->stepSize = cs->stepSize;
    }

    if (copy.HasComponent<ComponentRigidbody>()) {
        auto *s = entity.AddComponent<ComponentRigidbody>();
        const ComponentRigidbody *cs = copy.GetComponent<ComponentRigidbody>();

        s->isStatic = cs->isStatic;
        s->mass = cs->mass;
        s->gravityScale = cs->gravityScale;
        s->friction = cs->friction;
        // velocity intentionally left as default (runtime state)
    }

    // UI Components
    if (copy.HasComponent<ComponentUITransform>()) {
        auto *s = entity.AddComponent<ComponentUITransform>();
        const ComponentUITransform *cs = copy.GetComponent<ComponentUITransform>();

        s->anchorMin = cs->anchorMin;
        s->anchorMax = cs->anchorMax;
        s->pivot = cs->pivot;
        s->position = cs->position;
        s->scale = cs->scale;
        s->rotation = cs->rotation;
        // resolvedPosition, resolvedSize intentionally left as default (runtime derived state)
    }

    if (copy.HasComponent<ComponentUISprite>()) {
        auto *s = entity.AddComponent<ComponentUISprite>();
        const ComponentUISprite *cs = copy.GetComponent<ComponentUISprite>();

        s->texture = cs->texture;
    }

    if (copy.HasComponent<ComponentUIText>()) {
        auto *s = entity.AddComponent<ComponentUIText>();
        const ComponentUIText *cs = copy.GetComponent<ComponentUIText>();

        s->text = cs->text;
    }

    entities.push_back(entity);

    return entity.id;
}

void Level::DestroyEntity(const ID entityID) {
    if (entityID == INVALID_ID) return;

    for (Sector& sector : sectors) std::erase(sector.entitiesInside, entityID);

    colliders.Remove(entityID);
    rigidbodies.Remove(entityID);

    sprites.Remove(entityID);
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