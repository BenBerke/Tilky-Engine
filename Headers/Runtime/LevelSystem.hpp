//
// Created by berke on 5/26/2026.
//

#ifndef TILKY_ENGINE_LEVELUPDATE_H
#define TILKY_ENGINE_LEVELUPDATE_H

#include "Headers/Objects/Level.hpp"

#include <optional>
#include <string>

/// This only runs during Play or Standalone. Does not run during Realtime Editor
/// Responsible for updating physics, scripts, audio etc.

namespace LevelSystem {
    void Start(Level& level);
    void Update(Level& level);
    void Shutdown(Level& level);

    // Ends the level for a level change: every script gets OnDestroy, but
    // the Lua state (and the Global table) stays for the next Start().
    void StopLevel(Level& level);

    // Game.LoadLevel queues the level here; RuntimeSession switches to it
    // once the current frame has finished. A later request in the same
    // frame replaces an earlier one.
    void RequestLevelLoad(const std::string& levelName);
    std::optional<std::string> TakeRequestedLevel();

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
    // Same for a script owned by something other than an entity (a sector).
    bool ReconcileScriptPublicValues(ScriptAttachmentData& script, const std::string& ownerLabel);
    const std::string* GetScriptLoadError(const std::string& fileName);
    void RefreshScriptAssets(Level& level);
}

#endif //TILKY_ENGINE_LEVELUPDATE_H