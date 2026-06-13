#include "EditorInternal.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace Editor {
    bool ExportProjectAsGame(const fs::path& exportFolder) {
        try {
            fs::create_directories(exportFolder);

            const fs::path runtimeExe =
                ProjectManager::GetEngineBasePath() / "Tilky_GameRuntime.exe";

            const fs::path outputExe =
                exportFolder / "MyGame.exe";

            fs::copy_file(
                runtimeExe,
                outputExe,
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetProjectFolder(),
                exportFolder / "Project",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetEngineBasePath() / "EngineAssets",
                exportFolder / "EngineAssets",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetEngineBasePath() / "Shaders",
                exportFolder / "Shaders",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            fs::copy(
                ProjectManager::GetEngineBasePath() / "Fonts",
                exportFolder / "Fonts",
                fs::copy_options::recursive |
                fs::copy_options::overwrite_existing
            );

            spdlog::info("Exported game to {}", exportFolder.string());
            return true;
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to export game: {}", e.what());
            return false;
        }
    }
}

namespace {
    Level& GetOrCreateCurrentLevel() {
        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        return LevelManager::CurrentLevel();
    }

    void RebuildPlacedCornersFromWalls(const Level& level) {
        using namespace MapEditorInternal;

        placedCorners.clear();

        for (const Wall& wall : level.walls) {
            if (!CornerExistsAt(wall.start)) {
                placedCorners.push_back(wall.start);
            }

            if (!CornerExistsAt(wall.end)) {
                placedCorners.push_back(wall.end);
            }
        }
    }
}

namespace MapEditorInternal {
    void UpdateLevels() {
        const fs::path levelsPath = ProjectManager::GetLevelsPath();

        try {
            Editor::maps.clear();

            if (!fs::exists(levelsPath) ||
                !fs::is_directory(levelsPath)) {
                fs::create_directories(levelsPath);
                return;
            }

            for (const fs::directory_entry& entry : fs::directory_iterator(levelsPath)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                if (entry.path().extension() != ".bson") {
                    continue;
                }

                Editor::maps.push_back(entry.path().stem().string());
            }
        }
        catch (const fs::filesystem_error& e) {
            spdlog::critical("Error loading levels {}", e.what());
        }
    }

    bool Save(const std::string& saveTo) {
        Level& level = GetOrCreateCurrentLevel();

        EditorTextureCache::RefreshLevelTexturesFromFolder();
        Editor::RefreshLevelSoundsFromFolder();

        const std::string cleanName = LevelSerialization::CleanLevelName(saveTo);

        if (cleanName.empty()) {
            spdlog::warn("Can not save with an empty name");
            return false;
        }

        level.name = cleanName;

        LevelSerialization::LevelExtraData extraData;
        extraData.backgroundTextureIndex = Editor::backgroundTextureIndex;

        const fs::path path = LevelSerialization::BuildLevelPath(cleanName);
        std::string errorMessage;

        if (!LevelSerialization::SaveLevelToFile(
                path,
                level,
                &extraData,
                &errorMessage
            )) {
            spdlog::critical("{}", errorMessage);
            return false;
        }

        spdlog::info("Level saved successfully {}", path.string());

        UpdateLevels();

        return true;
    }
}

namespace Editor {
    bool LoadLevel(const std::string& levelName) {
        using namespace MapEditorInternal;

        const std::string cleanName = LevelSerialization::CleanLevelName(levelName);
        const fs::path path = LevelSerialization::BuildLevelPath(cleanName);

        Level loadedLevel;
        LevelSerialization::LevelExtraData extraData;
        std::string errorMessage;

        if (!LevelSerialization::LoadLevelFromFile(
                path,
                loadedLevel,
                &extraData,
                &errorMessage
            )) {
            spdlog::critical("{}", errorMessage);
            return false;
        }

        backgroundTextureIndex = extraData.backgroundTextureIndex;
        currentMap = cleanName;

        placedCorners.clear();
        sectorBeingCreated.clear();

        editingSector = false;
        selectedSector = -1;
        creatableSector = false;

        editingWall = false;
        selectedWall = -1;

        actions.clear();

        if (LevelManager::loadedLevels.empty()) {
            LevelManager::loadedLevels.push_back(std::move(loadedLevel));
            LevelManager::currentLevelIndex = 0;
        }
        else if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.push_back(std::move(loadedLevel));
            LevelManager::currentLevelIndex =
                static_cast<int>(LevelManager::loadedLevels.size()) - 1;
        }
        else {
            LevelManager::loadedLevels[LevelManager::currentLevelIndex] =
                std::move(loadedLevel);
        }

        Level& activeLevel = LevelManager::CurrentLevel();

        RebuildPlacedCornersFromWalls(activeLevel);

        EditorTextureCache::RefreshLevelTexturesFromFolder();
        RefreshLevelSoundsFromFolder();

        spdlog::info("Level loaded successfully {}", path.string());

        return true;
    }
}
