#ifndef TILKY_ENGINE_ENTITY_H
#define TILKY_ENGINE_ENTITY_H

#include <bitset>
#include <string>

#include "EntityTypes.hpp"

#define MAX_COMPONENTS 64
using ComponentMask = std::bitset<MAX_COMPONENTS>;

struct Entity {
    std::string name;
    ID id = static_cast<ID>(-1);
    ID attachedLevelId = static_cast<ID>(-1);

    ComponentMask componentsMask;

    void Start();
    void Update();

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