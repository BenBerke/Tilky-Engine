//
// Created by berke on 10/26/2026.
//

#ifndef TILKY_ENGINE_EVENTSYSTEM_HPP
#define TILKY_ENGINE_EVENTSYSTEM_HPP

#include <functional>
#include <string>

#include "Headers/Objects/Event.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"

struct Level;

namespace EventSystem {
    // What dispatches need from the scripting side, set by Start.
    struct Hooks {
        // Calls `functionName(instigator)` on every enabled script of the entity.
        std::function<void(Level& level, ID entityID, const std::string& functionName, ID instigatorID)> callFunction;
        // Destroys the entity at the end of the frame.
        std::function<void(ID entityID)> destroyEntity;
    };

    // Rebuild after anything that adds/removes walls, sectors, entities or
    // their events/dispatches (level load, topology rebuild, undo).
    void CreateRegistry(const Level& level);

    // Builds the registry, resets every event's triggerCount and takes the
    // level's current state (positions, sectors, enabled flags) as the
    // baseline, so nothing fires for how the level starts.
    void Start(Level& level, Hooks hooks);

    // Advances wall color fades, then diffs this frame against the last and
    // fires every event that happened: line cross/collide, sector
    // enter/exit, entity collide/change sector/enable/disable. Run after
    // physics and the transform sector update.
    void Update(Level& level, const PhysicsSystem::Contacts& contacts);

    void Stop();

    // Acts on the wall/sector/entity the dispatch is attached to (`ownerKind`,
    // `ownerID`). `instigatorID` is the entity that caused the event
    // (INVALID_ID if none), passed to CALL_FUNCTION.
    void ExecuteDispatch(Level& level, const Dispatch& dispatch, EventOwnerKind ownerKind, ID ownerID, ID instigatorID);

    // Checks the trigger limit, the tag filter against the instigator's tags,
    // the condition and the chance, then runs every dispatch with the event's tag.
    void TriggerEvent(Level& level, Event& event, ID instigatorID);
}

#endif //TILKY_ENGINE_EVENTSYSTEM_HPP
