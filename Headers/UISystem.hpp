//
// Created by berke on 5/22/2026.
//

#ifndef TILKY_ENGINE_UISYSTEM_HPP
#define TILKY_ENGINE_UISYSTEM_HPP

#include "Headers/Math/Vector/Vector2.hpp"
#include "Objects/Components.hpp"
#include "Objects/Level.hpp"

namespace UISystem {
    constexpr float DEFAULT_UI_SIZE = 5.0f;

    void UpdateTransform(ComponentUITransform& transform, float screenWidth, float screenHeight);
    void UpdateAllTransforms(Level& level, float screenWidth, float screenHeight);
}

#endif //TILKY_ENGINE_UISYSTEM_HPP