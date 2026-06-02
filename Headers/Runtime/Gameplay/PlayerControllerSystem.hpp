//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_PLAYER_H
#define TILKY_ENGINE_PLAYER_H

#include <vector>

#include "../../Objects/Components.hpp"
#include "../../Objects/Wall.hpp"
#include "../../Objects/Sector.hpp"

#include "Headers/Math/Vector/Vector2.hpp"

namespace PlayerControllerSystem {
    inline Vector2 GetPlanarPosition(const ComponentTransform& transform) {
        return {
            transform.position.x,
            transform.position.z
        };
    }

    inline void SetPlanarPosition(
        ComponentTransform& transform,
        const Vector2& planarPosition
    ) {
        transform.position.x = planarPosition.x;
        transform.position.z = planarPosition.y;
    }

    inline Vector2 GetPlanarVelocity(
        const ComponentPlayerController& controller
    ) {
        return {
            controller.velocity.x,
            controller.velocity.z
        };
    }

    inline void SetPlanarVelocity(
        ComponentPlayerController& controller,
        const Vector2& planarVelocity
    ) {
        controller.velocity.x = planarVelocity.x;
        controller.velocity.y = 0.0f;
        controller.velocity.z = planarVelocity.y;
    }

    void Start(
        ComponentPlayerController &controller,
        ComponentTransform &playerTransform,
        const ComponentRigidbody &rigidbody,
        const ComponentCamera &camera,
        const std::vector<Sector> &sectors
    );

    void Update(
        ComponentPlayerController &controller,
        const ComponentTransform &playerTransform,
        ComponentCamera &camera,
        ComponentRigidbody &rigidbody,
        ComponentCollider &sphereCollider,
        const std::vector<Sector> &sectors
    );
}

#endif // TILKY_ENGINE_PLAYER_H