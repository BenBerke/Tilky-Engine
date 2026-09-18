#ifndef TILKY_ENGINE_ENTITY_H
#define TILKY_ENGINE_ENTITY_H

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "Components.hpp"
#include "EntityTypes.hpp"

#define MAX_COMPONENTS 128
using ComponentMask = std::bitset<MAX_COMPONENTS>;

struct Entity {
    std::string name;
    ID id = static_cast<ID>(-1);
    ID attachedLevelId = static_cast<ID>(-1);

    std::vector<std::string> tags;
    std::vector<uint16_t> tagIds;

    // GameObject-level active state (Lua: GameObject.enabled / GameObject:SetEnabled()).
    // Distinct from any single component's own enabled flag (e.g. ComponentScript::enabled):
    // disabling the entity disables every attached script's effective enabled state
    // without touching each script's own flag, so re-enabling the entity restores
    // each script's individual state exactly as it was. See LevelSystem::Update.
    bool enabled = true;

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

    ComponentScript& AddScript();

    ComponentScript* GetScript(ScriptInstanceID instanceID);

    std::vector<ComponentScript*> GetScripts();

    bool RemoveScript(ScriptInstanceID instanceID);

    bool RemoveAllScripts();
};

#endif