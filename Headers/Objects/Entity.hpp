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

    // Entity-level active state (Lua: Entity.enabled / Entity:SetEnabled()).
    // Distinct from any single component's own enabled flag (e.g. ComponentScript::enabled):
    // disabling the entity disables every attached script's effective enabled state
    // without touching each script's own flag, so re-enabling the entity restores
    // each script's individual state exactly as it was. See LevelSystem::Update.
    bool enabled = true;

    ComponentMask componentsMask;

    void Start();
    void Update();

    // An entity can have several components of a type (except Transform and
    // UITransform). GetComponent returns the first one; the order is the
    // inspector's.
    template<typename T>
    T* GetComponent();

    // All of this entity's components of type T, in order. Only valid until
    // the next component is added or removed.
    template<typename T>
    std::vector<T*> GetComponents();

    // The component with this instance ID, if this entity owns it.
    template<typename T>
    T* GetComponentInstance(ComponentInstanceID instanceID);

    // Adds a new component after the existing ones. Transform and UITransform
    // are one per entity: for those it returns the existing one.
    template<typename T>
    T* AddComponent();

    // Removes every component of type T. False if there was none.
    template<typename T>
    bool RemoveComponent();

    // Removes the one component with this instance ID, if this entity owns it.
    template<typename T>
    bool RemoveComponentInstance(ComponentInstanceID instanceID);

    template<typename T>
    bool HasComponent();

    ComponentScript& AddScript();

    ComponentScript* GetScript(ScriptInstanceID instanceID);

    std::vector<ComponentScript*> GetScripts();

    bool RemoveScript(ScriptInstanceID instanceID);

    bool RemoveAllScripts();
};

#endif