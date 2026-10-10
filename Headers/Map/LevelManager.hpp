//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVELMANAGER_H
#define TILKY_ENGINE_LEVELMANAGER_H

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Level.hpp"

namespace LevelManager {
    extern std::vector<Level> loadedLevels;
    extern int currentLevelIndex;

    Level& CurrentLevel();
    bool HasCurrentLevel();

    void ClearLoadedLevels();

    // `outExtraData` receives what the file stores beside the Level itself,
    // such as the background texture.
    bool LoadLevelFromFile(const std::filesystem::path& levelFile, LevelSerialization::LevelExtraData* outExtraData = nullptr);
    bool LoadLevelByName(const std::string& levelName);
    bool LoadFirstProjectLevel(LevelSerialization::LevelExtraData* outExtraData = nullptr);
    void TriangulateCurrentLevelSectors();

    void RenameTextureReference(const std::string& oldReference, const std::string& newReference);
    void RenameSoundReference(const std::string& oldReference, const std::string& newReference);
    void RenameScriptReference(const std::string& oldReference, const std::string& newReference);
    void RenameModelReference(const std::string& oldReference, const std::string& newReference);
    // Flipbook components and script fields typed FlipbookAsset.
    void RenameFlipbookReference(const std::string& oldReference, const std::string& newReference);
    // UI Text Font fields.
    void RenameFontReference(const std::string& oldReference, const std::string& newReference);
}

#endif // TILKY_ENGINE_LEVELMANAGER_H