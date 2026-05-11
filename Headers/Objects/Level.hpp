//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVEL_H
#define TILKY_ENGINE_LEVEL_H
#include <vector>
#include <string>

#include "Components.hpp"
#include "Entity.hpp"

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<Entity> entities;

    EntityID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentDecal> decals;
    ComponentStorage<ComponentPlayerSpawn> playerSpawns;

    Entity& CreateEntity() {
        Entity entity;
        entity.id = nextEntityID++;
        entity.name = "Entity";
        entity.attachedLevelId = id;

        entities.push_back(entity);

        Entity& createdEntity = entities.back();

        transforms.Add(createdEntity.id);

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
    }
    void DestroyEntity(const Entity& entity) {
        DestroyEntity(entity.id);
    }
};

#endif //TILKY_ENGINE_LEVEL_H