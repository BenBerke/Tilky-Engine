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

    // Boots the Lua scripting subsystem (opens the sol::state, registers
    // every binding, populates LuaBindingMetadata) if it hasn't already run.
    // Safe to call outside Play mode - it does not load or run any script -
    // so editor-side tooling (e.g. the script editor's autocomplete list in
    // AssetBrowser.cpp) can call this to guarantee binding metadata is
    // available without needing a loaded/playing level.
    bool EnsureScriptingInitialized();

    void RefreshScriptAssets(Level& level);
    ComponentCamera* GetActiveCamera(Level& level);

    const std::vector<ScriptPublicField>* GetPublicFieldsForScript(const std::string& fileName);
    bool ReconcileScriptPublicValues(ComponentScript& script);
    void RefreshScriptAssets(Level& level);
}

#endif //TILKY_ENGINE_LEVELUPDATE_H