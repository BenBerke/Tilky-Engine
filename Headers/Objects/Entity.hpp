#ifndef TILKY_ENGINE_ENTITY_H
#define TILKY_ENGINE_ENTITY_H

#include <cstdint>
#include <string>

#include "EntityTypes.hpp"

struct Entity {
    std::string name;
    EntityID id = static_cast<EntityID>(-1);
    EntityID attachedLevelId = static_cast<EntityID>(-1);

    template<typename T>
    T* GetComponent();

    template<typename T>
    T* AddComponent();

    template<typename T>
    bool RemoveComponent();

    template<typename T>
    bool HasComponent();
};

#endif