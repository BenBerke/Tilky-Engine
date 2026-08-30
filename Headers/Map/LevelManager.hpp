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

    void RenameTextureReference(const std::string& oldReference, const std::string& newReference);
    void RenameSoundReference(const std::string& oldReference, const std::string& newReference);
    void RenameScriptReference(const std::string& oldReference, const std::string& newReference);
}

#endif // TILKY_ENGINE_LEVELMANAGER_H