#include "EditorInternal.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/LevelSystem.hpp"

namespace fs = std::filesystem;

namespace {
    Level& GetOrCreateCurrentLevel() {
        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        return LevelManager::CurrentLevel();
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
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".bson") continue;

                Editor::maps.push_back(entry.path().stem().string());
            }
        }
        catch (const fs::filesystem_error& e) {
            spdlog::critical("Error loading levels {}", e.what());
        }
    }

    bool Save(const std::string& saveTo) {
        Level& level = GetOrCreateCurrentLevel();

        const std::string cleanName = LevelSerialization::CleanLevelName(saveTo);

        if (cleanName.empty()) {
            spdlog::warn("Can not save with an empty name");
            return false;
        }

        level.name = cleanName;

        LevelSerialization::LevelExtraData extraData;
        extraData.backgroundTextureFileName = Editor::backgroundTextureFileName;

        const fs::path path = LevelSerialization::BuildLevelPath(cleanName);
        std::string errorMessage;

        if (!LevelSerialization::SaveLevelToFile(path, level, &extraData, &errorMessage)) {
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

        if (!LevelSerialization::LoadLevelFromFile(path, loadedLevel, &extraData, &errorMessage)) {
            spdlog::critical("{}", errorMessage);
            return false;
        }

        backgroundTextureFileName = extraData.backgroundTextureFileName;
        currentMap = cleanName;

        // Dots are editor-session data scoped to whatever level is on
        // screen = they don't carry over to a different level file, so they get cleared rather than rebuilt.
        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;
        selectedDotID = INVALID_ID;

        sectorBeingCreated.clear();
        pendingSectorParams = PendingSectorParams{};

        editingSector = false;
        selectedSectorID = INVALID_ID;

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
        else LevelManager::loadedLevels[LevelManager::currentLevelIndex] = std::move(loadedLevel);

        //todo check if works in actual game
        LevelSystem::RefreshScriptAssets(LevelManager::CurrentLevel());

        spdlog::info("Level loaded successfully {}", path.string());

        return true;
    }
}