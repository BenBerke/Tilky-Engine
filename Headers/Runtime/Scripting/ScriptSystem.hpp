//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_SCRIPTSYSTEM_HPP
#define TILKY_ENGINE_SCRIPTSYSTEM_HPP

#include "Headers/Objects/Level.hpp"

namespace sol {
    class state;
}

namespace ScriptSystem {
    bool Initialize();
    void Start(Level& level);
    void Update(Level& level);
    void Shutdown();

    void RegisterVectorBindings(sol::state& lua);
    void RegisterComponentBindings(sol::state& lua);
    void RegisterEntityBindings(sol::state& lua);
    void RegisterInputBindings(sol::state& lua);
    void RegisterMathBindings(sol::state& lua);
    void RegisterEditorFunctionBindings(sol::state& lua);
}

#endif //TILKY_ENGINE_SCRIPTSYSTEM_HPP