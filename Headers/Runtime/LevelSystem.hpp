//
// Created by berke on 5/26/2026.
//

#ifndef TILKY_ENGINE_LEVELUPDATE_H
#define TILKY_ENGINE_LEVELUPDATE_H

#include "Headers/Objects/Level.hpp"

/// This only runs during Play or Standalone. Does not run during Realtime Editor
/// Responsible for updating physics, scripts, audio etc.

namespace LevelSystem {
    void Start(Level& level);
    void Update(Level& level);
    void Shutdown(Level& level);

    void RefreshScriptAssets(Level& level);
    ComponentCamera* GetActiveCamera(Level& level);

    const std::vector<ScriptPublicField>* GetPublicFieldsForScript(const std::string& fileName);
    bool ReconcileScriptPublicValues(ComponentScript& script);
    void RefreshScriptAssets(Level& level);
}

#endif //TILKY_ENGINE_LEVELUPDATE_H