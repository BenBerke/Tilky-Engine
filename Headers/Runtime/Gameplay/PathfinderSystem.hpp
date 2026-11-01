//
// Created by berke on 11/1/2026.
//

#ifndef TILKY_ENGINE_PATHFINDERSYSTEM_HPP
#define TILKY_ENGINE_PATHFINDERSYSTEM_HPP

#include <functional>

#include "Headers/Objects/Level.hpp"

/// Moves every ComponentPathfinder's owner toward its destination with
/// Doom's chase logic (P_NewChaseDir / P_TryWalk): no baked paths, just one
/// of 8 compass directions held for a random number of chase steps, and a
/// new one whenever the current one is blocked.
///
/// A step is blocked when the floor ahead is more than the collider's
/// stepSize above the feet, more than stepSize below them (a drop-off; air
/// entities ignore both), there's no room under the ceiling, or it leaves
/// the map. With a Rigidbody the owner moves by velocity through physics, so
/// walls and other bodies block it as well; a step that physics cut short
/// also counts as blocked. Without one the Transform is moved directly and
/// kept on the floor (air entities keep their height).
///
/// Only an entity's first active pathfinder moves it. Only runs during Play
/// and Standalone (LevelSystem), after scripts and before physics.
namespace PathfinderSystem {
    // Doom runs a monster's chase every 3 tics (35 tics a second); Doom's
    // movecount counts these, so a direction is held for 0..15 of them.
    inline constexpr float CHASE_STEP_SECONDS = 3.0f / 35.0f;

    // Forgets every pathfinder's direction and step. Call once at Play start.
    void Start(Level& level);

    // Called with the owner's ID each time a pathfinder arrives (gets within
    // reach of its destination, or within range when ranged, after not being).
    using ArrivalDispatcher = std::function<void(ID ownerID)>;

    // `deltaTime` is scaled (GameTime::deltaTime), so timeScale 0 holds them.
    // Arrivals are reported through `onArrived` after every pathfinder moved.
    void Update(Level& level, float deltaTime, const ArrivalDispatcher& onArrived = {});

    // --- Shared by the Lua API ---------------------------------------------

    // Switches to Position input and walks to `destination`. Counts as not
    // arrived, so arriving (even right away) fires OnPathfinderArrived again.
    void SetDestination(ComponentPathfinder& pathfinder, const Vector3& destination);

    // Switches to Entity input and follows `targetID`, aiming at its position
    // + `offset` (world axes). `destination` is updated right away. Counts as
    // not arrived, like SetDestination.
    void SetTarget(Level& level, ComponentPathfinder& pathfinder, ID targetID, const Vector3& offset);
}

#endif //TILKY_ENGINE_PATHFINDERSYSTEM_HPP
