//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_GAMEFUNCTIONS_HPP
#define TILKY_ENGINE_GAMEFUNCTIONS_HPP

#include "../../Objects/Level.hpp"
#include "../../Objects/RayHit.hpp"

namespace GameFunctions {
    std::optional<RayHit> Raycast(Level &level, Vector3 pos, Vector3 dir, float length, ID ignoredEntity, bool requireCollider);
}

#endif //TILKY_ENGINE_GAMEFUNCTIONS_HPP