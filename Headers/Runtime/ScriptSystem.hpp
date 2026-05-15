//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_SCRIPTSYSTEM_HPP
#define TILKY_ENGINE_SCRIPTSYSTEM_HPP

#include "Headers/Objects/Level.hpp"

namespace ScriptSystem {
    bool Initialize();
    void Start(Level& level);
    void Update(Level& level, float deltaTime);
    void Shutdown();
}

#endif //TILKY_ENGINE_SCRIPTSYSTEM_HPP