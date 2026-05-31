//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVELMANAGER_H
#define TILKY_ENGINE_LEVELMANAGER_H

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Objects/Level.hpp"

namespace LevelManager {
    extern std::vector<Level> loadedLevels;
    extern int currentLevelIndex;

    Level& CurrentLevel();
    bool HasCurrentLevel();

    void ClearLoadedLevels();

    bool LoadLevelFromFile(const std::filesystem::path& levelFile);
    bool LoadLevelByName(const std::string& levelName);
    bool LoadFirstProjectLevel();
    void TriangulateCurrentLevelSectors();
}

#endif // TILKY_ENGINE_LEVELMANAGER_H