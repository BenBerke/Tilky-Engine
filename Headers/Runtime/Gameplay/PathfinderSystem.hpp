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
/// entities ignore both, Can Drop Off the second), there's no room under the
/// ceiling, it leaves the map, or the body would touch a solid wall or a line
/// that blocks Pathfinders. With a Rigidbody the owner moves by velocity
/// through physics, so other bodies block it as well; a step that physics cut
/// short also counts as blocked. Without one the Transform is moved directly
/// and kept on the floor (air entities keep their height).
///
/// With a Rigidbody, velocity from anything else (knockback from a script or
/// dispatch) is added on top of the walking and fades by Push Friction.
///
/// Only an entity's first active pathfinder moves it. Only runs during Play
/// and Standalone (LevelSystem), after scripts and before physics.
namespace PathfinderSystem {
    // What happened to a pathfinder this frame.
    enum class NoticeKind {
        ARRIVED,
        BLOCKED,
        STUCK,
        STARTED_MOVING,
        STOPPED_MOVING
    };

    // `targetID` is the entity it was following (ARRIVED only), else INVALID_ID.
    using NoticeHandler = std::function<void(ID ownerID, NoticeKind kind, ID targetID)>;

    // A monster's chase step in Doom: 3 tics, of 35 a second. Doom's movecount counts these.
    inline constexpr float CHASE_STEP_SECONDS = 3.0f / 35.0f;

    // Forgets every pathfinder's direction and step. Call once at Play start.
    void Start(Level& level);

    // `deltaTime` is scaled (GameTime::deltaTime), so timeScale 0 holds them.
    // Notices, and the doors they open, come after every pathfinder moved.
    void Update(Level& level, float deltaTime, const NoticeHandler& onNotice = {});

    // Run right after physics: remembers the velocity it left, so a change
    // from it by the next Update is known to be a push.
    void AfterPhysics(Level& level);

    // --- Shared by the Lua API ---------------------------------------------

    // Switches to Position input and walks to `destination`. A different
    // place counts as not arrived, so getting there fires OnPathfinderArrived
    // again. Sending the place it's already heading for changes nothing.
    void SetDestination(ComponentPathfinder& pathfinder, const Vector3& destination);

    // Switches to Entity input and follows `targetID`, aiming at its position
    // + `offset` (world axes). `destination` is updated right away. Arrival
    // as SetDestination: the same target and offset changes nothing.
    void SetTarget(Level& level, ComponentPathfinder& pathfinder, ID targetID, const Vector3& offset);

    // Holds still for `seconds` (0 = carry on now), keeping an air entity's
    // height and any push.
    void Pause(ComponentPathfinder& pathfinder, float seconds);

    // Chooses a direction again at its next update.
    void PickNewDirection(ComponentPathfinder& pathfinder);

    // The unit direction it is walking in, zero when it isn't.
    [[nodiscard]] Vector3 MoveDirection(const ComponentPathfinder& pathfinder);
}

#endif //TILKY_ENGINE_PATHFINDERSYSTEM_HPP
