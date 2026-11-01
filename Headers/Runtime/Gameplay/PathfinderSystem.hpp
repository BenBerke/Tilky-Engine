//
// Created by berke on 11/1/2026.
//

#ifndef TILKY_ENGINE_PATHFINDERSYSTEM_HPP
#define TILKY_ENGINE_PATHFINDERSYSTEM_HPP

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

    // `deltaTime` is scaled (GameTime::deltaTime), so timeScale 0 holds them.
    void Update(Level& level, float deltaTime);
}

#endif //TILKY_ENGINE_PATHFINDERSYSTEM_HPP
