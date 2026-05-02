//
// Created by berke on 4/27/2026.
//

#ifndef WOLFY_ENGINE_OBJECT_H
#define WOLFY_ENGINE_OBJECT_H

#include "../Map/LevelManager.hpp"
#include "Components.hpp"

using EntityID  = uint32_t;

struct Entity {
    EntityID id = -1;
    EntityID attachedLevelId = -1;

    template<typename T>
    T* GetComponent() {
        Level& level = LevelManager::CurrentLevel();

        if constexpr (std::is_same_v<T, ComponentTransform>) return level.transforms.Get(this->id);
        else if constexpr (std::is_same_v<T, ComponentDecal>) return level.decals.Get(this->id);
        else if constexpr (std::is_same_v<T, ComponentSprite>) return level.sprites.Get(this->id);
        else if constexpr (std::is_same_v<T, ComponentPlayerSpawn>) return level.playerSpawns.Get(this->id);

        return nullptr;
    }
    template<typename T>
    T& AddComponent() {
        Level& level = LevelManager::CurrentLevel();

        if constexpr (std::is_same_v<T, ComponentTransform>) return level.transforms.Add(this->id);
        else if constexpr (std::is_same_v<T, ComponentDecal>) return level.decals.Add(this->id);
        else if constexpr (std::is_same_v<T, ComponentSprite>) return level.sprites.Add(this->id);
        else if constexpr (std::is_same_v<T, ComponentPlayerSpawn>) return level.playerSpawns.Add(this->id);

        return nullptr;
    }

    template<typename T>
    T RemoveComponent() {
        Level& level = LevelManager::CurrentLevel();

        if constexpr (std::is_same_v<T, ComponentTransform>) level.transforms.Remove(this->id);
        if constexpr (std::is_same_v<T, ComponentDecal>) level.decals.Remove(this->id);
        if constexpr (std::is_same_v<T, ComponentSprite>) level.sprites.Remove(this->id);
        if constexpr (std::is_same_v<T, ComponentPlayerSpawn>) level.playerSpawns.Remove(this->id);

        return nullptr;
    }

};


#endif //WOLFY_ENGINE_OBJECT_H