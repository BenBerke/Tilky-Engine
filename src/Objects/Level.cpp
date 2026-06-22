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

ID Level::CreateEntity(Entity& copy) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = copy.name + "Copy";

    if (copy.HasComponent<ComponentTransform>()) {
        ComponentTransform* t  = entity.AddComponent<ComponentTransform>();
        const ComponentTransform* ct = copy.GetComponent<ComponentTransform>();

        t->position       = ct->position;
        t->relativeHeight = ct->relativeHeight;
        t->forward        = ct->forward;
        t->scale          = ct->scale;
        t->sectorIndex    = ct->sectorIndex;
        t->isDirty        = ct->isDirty;
    }

    if (copy.HasComponent<ComponentSprite>()) {
        ComponentSprite* s  = entity.AddComponent<ComponentSprite>();
        const ComponentSprite* cs = copy.GetComponent<ComponentSprite>();

        s->textureIndex = cs->textureIndex;
    }

    if (copy.HasComponent<ComponentDecal>()) {
        ComponentDecal* d  = entity.AddComponent<ComponentDecal>();
        const ComponentDecal* cd = copy.GetComponent<ComponentDecal>();

        d->wallIndex        = cd->wallIndex;
        d->verticalPos      = cd->verticalPos;
        d->horizontalPos    = cd->horizontalPos;
        d->wallNormalOffset = cd->wallNormalOffset;
        d->wallT            = cd->wallT;
        d->baseHeight       = cd->baseHeight;
        d->absHeight        = cd->absHeight;
    }

    if (copy.HasComponent<ComponentAudioSource>()) {
        ComponentAudioSource* a  = entity.AddComponent<ComponentAudioSource>();
        const ComponentAudioSource* ca = copy.GetComponent<ComponentAudioSource>();

        a->name              = ca->name;
        a->soundIndex        = ca->soundIndex;
        a->pitch             = ca->pitch;
        a->gain              = ca->gain;
        a->looping           = ca->looping;
        a->playOnStart       = ca->playOnStart;
        a->referenceDistance = ca->referenceDistance;
        a->maxDistance       = ca->maxDistance;
        a->rollOffFactor     = ca->rollOffFactor;
        a->innerConeAngle    = ca->innerConeAngle;
        a->outerConeAngle    = ca->outerConeAngle;
        a->outerGain         = ca->outerGain;
    }

    if (copy.HasComponent<ComponentScript>()) {
        ComponentScript* s  = entity.AddComponent<ComponentScript>();
        const ComponentScript* cs = copy.GetComponent<ComponentScript>();

        s->fileName = cs->fileName;
        s->enabled  = cs->enabled;
    }

    if (copy.HasComponent<ComponentPlayerController>()) {
        ComponentPlayerController* p  = entity.AddComponent<ComponentPlayerController>();
        const ComponentPlayerController* cp = copy.GetComponent<ComponentPlayerController>();

        p->isActive      = cp->isActive;
        p->speed         = cp->speed;
        p->runningSpeed  = cp->runningSpeed;
        p->jumpPower     = cp->jumpPower;
        p->eyeHeight     = cp->eyeHeight;
        p->friction      = cp->friction;
        p->sensitivityX  = cp->sensitivityX;
        p->sensitivityY  = cp->sensitivityY;
        p->noClip        = cp->noClip;
        // velocity, currentSpeed, currentEyeHeight intentionally left as default (read-only runtime state)
    }

    if (copy.HasComponent<ComponentCamera>()) {
        ComponentCamera* c  = entity.AddComponent<ComponentCamera>();
        const ComponentCamera* cc = copy.GetComponent<ComponentCamera>();

        c->isActive    = cc->isActive;
        c->yaw         = cc->yaw;
        c->pitch       = cc->pitch;
        c->fov         = cc->fov;
        c->aspectRatio = cc->aspectRatio;
        c->nearPlane   = cc->nearPlane;
        c->farPlane    = cc->farPlane;
        // forward, target, view, projection intentionally left as default (runtime derived state)
    }

    if (copy.HasComponent<ComponentCollider>()) {
        ComponentCollider* c  = entity.AddComponent<ComponentCollider>();
        const ComponentCollider* cc = copy.GetComponent<ComponentCollider>();

        c->type      = cc->type;
        c->isActive  = cc->isActive;
        c->isTrigger = cc->isTrigger;
        c->scale     = cc->scale;
        c->stepSize  = cc->stepSize;
    }

    if (copy.HasComponent<ComponentRigidbody>()) {
        ComponentRigidbody* r  = entity.AddComponent<ComponentRigidbody>();
        const ComponentRigidbody* cr = copy.GetComponent<ComponentRigidbody>();

        r->isStatic     = cr->isStatic;
        r->mass         = cr->mass;
        r->gravityScale = cr->gravityScale;
        r->friction     = cr->friction;
        // velocity intentionally left as default (runtime state)
    }

    // UI Components
    if (copy.HasComponent<ComponentUITransform>()) {
        ComponentUITransform* t  = entity.AddComponent<ComponentUITransform>();
        const ComponentUITransform* ct = copy.GetComponent<ComponentUITransform>();

        t->anchorMin        = ct->anchorMin;
        t->anchorMax        = ct->anchorMax;
        t->pivot            = ct->pivot;
        t->position         = ct->position;
        t->scale            = ct->scale;
        t->rotation         = ct->rotation;
        // resolvedPosition, resolvedSize intentionally left as default (runtime derived state)
    }

    if (copy.HasComponent<ComponentUISprite>()) {
        ComponentUISprite* s  = entity.AddComponent<ComponentUISprite>();
        const ComponentUISprite* cs = copy.GetComponent<ComponentUISprite>();

        s->textureIndex = cs->textureIndex;
    }

    if (copy.HasComponent<ComponentUIText>()) {
        ComponentUIText* t  = entity.AddComponent<ComponentUIText>();
        const ComponentUIText* ct = copy.GetComponent<ComponentUIText>();

        t->text = ct->text;
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