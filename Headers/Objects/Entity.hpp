#ifndef TILKY_ENGINE_ENTITY_H
#define TILKY_ENGINE_ENTITY_H

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "Components.hpp"
#include "Types.hpp"

#define MAX_COMPONENTS 128
using ComponentMask = std::bitset<MAX_COMPONENTS>;

struct Entity {
    std::string name;
    ID id = static_cast<ID>(-1);
    ID attachedLevelId = static_cast<ID>(-1);

    // TagRegistry IDs; names are looked up with TagRegistry::GetName.
    std::vector<uint16_t> tagIds;

    // Entity-level active state (Lua: Entity.enabled / Entity:SetEnabled()).
    // Distinct from any single component's own enabled flag (e.g. ComponentScript::enabled):
    // a disabled entity is out of the game - no scripts, drawing, physics,
    // rays, flipbooks, sounds or events - without touching each component's
    // own flag, so re-enabling it restores every component exactly as it was.
    // Components see it through their ownerEnabled copy; change it with
    // Level::SetEntityEnabled so they see it at once (a direct write is
    // picked up by Level::SyncEntityEnabledStates the next frame).
    bool enabled = true;

    // Runtime only, not saved: the `enabled` last copied to the components.
    bool appliedEnabled = true;

    std::vector<Event> events;
    std::vector<Dispatch> dispatches;

    ComponentMask componentsMask;

    // Which .prefab this entity is an instance of, if any.
    PrefabLink prefabLink;

    // Persist Between Levels: Game.LoadLevel and the Load Level dispatch keep
    // the entity, with its running scripts, and add it to the next level.
    // When that level has an entity with the same non-empty persistenceKey,
    // that entity (the level copy) is merged into this one and removed: each
    // component whose takeFromLevelCopy is set takes the copy's values,
    // components only the copy has are added. See LevelTransfer.
    bool persistent = false;
    std::string persistenceKey;

    // Runtime only, not saved: the UI layout (UILayouts) this entity was
    // created from while playing, 0 for the level's own entities.
    std::uint64_t layoutID = 0;

    // Runtime only, not saved: the LevelEpoch the entity came into the
    // running level in. A script handle made before then can't mean it:
    // the handle is from an earlier level and this entity wasn't carried.
    std::uint32_t sinceEpoch = 0;

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