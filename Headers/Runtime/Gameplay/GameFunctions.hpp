//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_GAMEFUNCTIONS_HPP
#define TILKY_ENGINE_GAMEFUNCTIONS_HPP

#include "../../Objects/Level.hpp"
#include "../../Objects/RayHit.hpp"

namespace GameFunctions {
    std::optional<RayHit> Raycast(Level &level, Vector3 pos, const Vector3 &dir, float length, ID ignoredEntity, bool requireCollider);

    // True when nothing solid is between `from` and `to`: walls, floors and
    // ceilings block it, entities don't. One ray, however many entities are
    // in the way.
    bool LineOfSight(Level &level, const Vector3 &from, const Vector3 &to);

    // Raycast that only hits walls, floors and ceilings - entities are passed through.
    std::optional<RayHit> RaycastGeometry(Level &level, Vector3 pos, const Vector3 &dir, float length);
}

#endif //TILKY_ENGINE_GAMEFUNCTIONS_HPP