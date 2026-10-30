//
// Created by berke on 6/15/2026.
//

#ifndef TILKY_ENGINE_PHYSICSSYSTEM_HPP
#define TILKY_ENGINE_PHYSICSSYSTEM_HPP

#include <vector>

#include "Headers/Objects/Types.hpp"

struct Level;
struct Vector3;
struct ComponentCollider;
struct ComponentTransform;
namespace PhysicsSystem {
    // Two entities whose colliders were resolved against each other.
    // Always stored with a < b, so one contact is never listed in both orders.
    struct CollisionPair {
        ID a = INVALID_ENTITY_ID;
        ID b = INVALID_ENTITY_ID;

        bool operator==(const CollisionPair&) const = default;
    };

    // An entity's solid collider pushed out of a wall.
    struct WallContact {
        ID entity = INVALID_ENTITY_ID;
        ID wall = INVALID_ID;

        bool operator==(const WallContact&) const = default;
    };

    struct Contacts {
        // Solid colliders that were pushed apart.
        std::vector<CollisionPair> collisions;
        // A trigger collider overlapping any other collider (trigger or not).
        // Nothing is pushed apart for these.
        std::vector<CollisionPair> triggers;
        // Not deduplicated either - an entity can hit the same wall with
        // several colliders or spans.
        std::vector<WallContact> walls;

        void Clear() {
            collisions.clear();
            triggers.clear();
            walls.clear();
        }
    };

    // Resolves every moving body after it moved by `deltaTime` worth of
    // velocity. Appends every contact found during this run to `contacts`.
    // Not deduplicated - the caller may run physics several times per frame.
    void Run(Level& level, Contacts& contacts, float deltaTime);

    // How many sub-steps (at most `maxSubsteps`) this frame needs so that
    // no body moves more than a fraction of its own size per step. Faster
    // than that, a body could get past a wall's line in one step and be
    // pushed out on the far side.
    [[nodiscard]] int SubstepCount(const Level& level, float deltaTime, int maxSubsteps);

    // The collider that stands for the entity's bottom: its active, solid
    // collider with the lowest bottom. It owns relativeHeight and the step
    // down between sectors. Null when the entity has none.
    const ComponentCollider* FindBaseCollider(const Level& level, ID ownerID);

    // True when the collider's shape, as physics sees it, touches or overlaps
    // the sphere. Its active and trigger flags aren't checked.
    [[nodiscard]] bool ColliderOverlapsSphere(const ComponentTransform& transform, const ComponentCollider& collider,
                                              const Vector3& center, float radius);
}

#endif //TILKY_ENGINE_PHYSICSSYSTEM_HPP
