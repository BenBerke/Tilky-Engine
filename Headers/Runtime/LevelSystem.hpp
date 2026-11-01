//
// Created by berke on 5/26/2026.
//

#ifndef TILKY_ENGINE_LEVELUPDATE_H
#define TILKY_ENGINE_LEVELUPDATE_H

#include "Headers/Objects/Level.hpp"

#include <optional>
#include <string>
#include <unordered_set>

#include "Headers/Runtime/LevelTransfer.hpp"

/// This only runs during Play or Standalone. Does not run during Realtime Editor
/// Responsible for updating physics, scripts, audio etc.

namespace LevelSystem {
    void Start(Level& level);

    // Remembers what Game.Sky.Reset and Game.MessageBox.Reset go back to, and
    // clears the messages on screen. Start runs it first, before the scripts.
    void CaptureStartingSettings(const Level& level);

    // The level's sky as CaptureStartingSettings found it.
    const SkySettings& StartingSky();
    void Update(Level& level);
    void Shutdown(Level& level);

    // Ends the level for a level change: every script gets OnDestroy except
    // those of `keepEntities` (the ones LevelTransfer carries), but the Lua
    // state (and the Global table) stays for ContinueLevel().
    void StopLevel(Level& level, const std::unordered_set<ID>& keepEntities);

    // Start, for the level a switch made (LevelTransfer::Merge): the carried
    // entities' scripts carry on and get OnLevelLoaded, their events keep
    // their trigger counts and don't fire On Start again.
    void ContinueLevel(Level& level, const LevelTransfer::Result& transfer);

    // Game.LoadLevel queues the level here; RuntimeSession switches to it
    // once the current frame has finished. A later request in the same
    // frame replaces an earlier one.
    void RequestLevelLoad(const std::string& levelName);
    std::optional<std::string> TakeRequestedLevel();

    // Game.Quit. Once the current frame has finished, the exported game
    // closes and the editor stops Play. Cleared by Shutdown.
    void RequestQuit();
    [[nodiscard]] bool QuitRequested();

    // Game.Pause and the Pause dispatch: timeScale 0, cursor unlocked and shown.
    void PauseGame();
    // The Resume dispatch: timeScale 1, cursor as the level's World Settings
    // start it (Lock Cursor / Hide Cursor on Level Start).
    void ResumeGame(const Level& level);

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