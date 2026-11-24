#pragma once

#include <string>
#include <vector>

#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector3.hpp"
#include "../Objects/Wall.hpp"
#include "../Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"

struct Level;
using ID = uint32_t;

namespace Editor {
    extern Vector3 playerStartPos;

    extern std::vector<std::string> maps;
    extern std::string currentMap;

    // Once at engine start, after RuntimeSession::Initialize(): the editors
    // draw in the renderer's window (fonts, ImGui, texture loading).
    bool Initialize();
    // Each time the editor becomes the active mode: resets session state and shows the window.
    void Start();
    void Update();
    // Leaving the editor for Play, which goes on in the same window.
    void Stop();
    // Once at engine exit, before RuntimeSession::Destroy() ends the window.
    void Destroy();

    bool ShutdownRequested();
    // Call before acting on a shutdown. True while a game export is running:
    // the editor has asked the user what to do, so don't close yet.
    bool HoldShutdownForExport();
    bool QuitRequested();
    bool PlayRequested();

    // The runtime editor (3D view) is a panel of the editor, next to the
    // map: its session runs from Start() to Stop(), restarted for each level
    // loaded in between (MapEditorInternal::StartRuntimeView).
    bool LoadLevel(const std::string& levelName);
    // Saves the open level, then switches to levelName on the editor's next
    // frame. What the Levels panel and the Asset Browser use to open a level.
    void OpenLevel(const std::string& levelName);

    // Readies the level in memory for the runtime editor or Play, as loading
    // its just-saved file would, without reading it back from disk.
    void PrepareLevelForRuntime();
    void RefreshLevelSoundsFromFolder();

    void AddWall(const Wall& wall);
    void AddSector(const Sector& sector);

    bool NewLevel(const std::string& levelName);

    // A level file was renamed on disk from oldName to newName (names without
    // ".bson"). Keeps the open level, the project's last open level and the
    // level list pointing at the renamed file, so the next save doesn't
    // recreate the old name.
    void LevelFileRenamed(const std::string& oldName, const std::string& newName);

    // Blocks until an auto save running in the background has written its
    // files. Call before anything that writes, renames, moves or deletes
    // level files, so it can't race the save.
    void WaitForAutoSave();

    SDL_Window* GetWindow();
}
