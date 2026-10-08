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

    void Start();
    void Update();
    void Destroy();

    bool ShutdownRequested();
    bool QuitRequested();
    bool PlayRequested();
    bool SwitchToRuntimeEditorRequested();
    bool LoadLevel(const std::string& levelName);
    void RefreshLevelSoundsFromFolder();

    void AddWall(const Wall& wall);
    void AddSector(const Sector& sector);

    bool NewLevel(const std::string& levelName);

    // A level file was renamed on disk from oldName to newName (names without
    // ".bson"). Keeps the open level, the project's last open level and the
    // level list pointing at the renamed file, so the next save doesn't
    // recreate the old name.
    void LevelFileRenamed(const std::string& oldName, const std::string& newName);

    SDL_Window* GetWindow();
}
