//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_RUNTIMEEDITOR_HPP
#define TILKY_ENGINE_RUNTIMEEDITOR_HPP

#include "Headers/Objects/Level.hpp"

namespace RuntimeEditor {
    void Start(Level& level);
    void Update(Level& level);
    void Shutdown(Level& level);
}

#endif //TILKY_ENGINE_RUNTIMEEDITOR_HPP