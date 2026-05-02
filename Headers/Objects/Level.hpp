//
// Created by berke on 5/2/2026.
//

#ifndef WOLFY_ENGINE_LEVEL_H
#define WOLFY_ENGINE_LEVEL_H
#include <vector>

#include "Components.hpp"

using LevelID = EntityID;

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<EntityID> entities;

    EntityID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentDecal> decals;
    ComponentStorage<ComponentPlayerSpawn> playerSpawns;

    EntityID CreateEntity() {
        const EntityID eid = nextEntityID++;
        entities.push_back(eid);
        return eid;
    }
    void DestroyEntity(const EntityID id) {
        std::erase(entities, id);

        transforms.Remove(id);
        sprites.Remove(id);
        decals.Remove(id);
        playerSpawns.Remove(id);
    }
};

#endif //WOLFY_ENGINE_LEVEL_H