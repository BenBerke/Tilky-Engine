//
// Created by berke on 6/15/2026.
//

#ifndef TILKY_ENGINE_PHYSICSSYSTEM_HPP
#define TILKY_ENGINE_PHYSICSSYSTEM_HPP

#include <vector>

#include "Headers/Objects/EntityTypes.hpp"

struct Level;
namespace PhysicsSystem {
    // Two entities whose colliders were resolved against each other.
    // Always stored with a < b, so one contact is never listed in both orders.
    struct CollisionPair {
        ID a = INVALID_ENTITY_ID;
        ID b = INVALID_ENTITY_ID;

        bool operator==(const CollisionPair&) const = default;
    };

    struct Contacts {
        // Solid colliders that were pushed apart.
        std::vector<CollisionPair> collisions;
        // A trigger collider overlapping any other collider (trigger or not).
        // Nothing is pushed apart for these.
        std::vector<CollisionPair> triggers;

        void Clear() {
            collisions.clear();
            triggers.clear();
        }
    };

    // Appends every entity-entity contact found during this run to
    // `contacts`. Not deduplicated - the caller may run physics several
    // times per frame.
    void Run(Level& level, Contacts& contacts);
}

#endif //TILKY_ENGINE_PHYSICSSYSTEM_HPP
