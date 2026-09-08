#include "EditorInternal.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>

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

    nlohmann::json SerializeColor(const Vector3& color) {
        return nlohmann::json::array({
            color.x,
            color.y,
            color.z
        });
    }

    nlohmann::json SerializeColor(const Vector4& color) {
        return nlohmann::json::array({
            color.x,
            color.y,
            color.z,
            color.w
        });
    }

    fs::path GetUserSettingsPath() {
        // This works because the launcher runs the engine version pinned
        // by the current project.
        const std::string version = ProjectManager::GetProjectEngineVersion();

        if (version.empty()) return {};

        return ProjectManager::GetEngineVersionDirectory(version) / "UserSettings.bson";
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

    bool SaveUserSettings() {
        const fs::path settingsPath = GetUserSettingsPath();

        if (settingsPath.empty()) {
            spdlog::error("Cannot save user settings because the current engine version is empty");
            return false;
        }

        try {
            const nlohmann::json settings = {
                {"formatVersion", 1},

                {"colors", {
                    {"normalEntityColor",
                     SerializeColor(normalEntityColor)},

                    {"highlightedEntityColor",
                     SerializeColor(highlightedEntityColor)},

                    {"spriteEntityColor",
                     SerializeColor(spriteEntityColor)},

                    {"normalWallColor",
                     SerializeColor(normalWallColor)},

                    {"highlightedWallColor",
                     SerializeColor(highlightedWallColor)},

                    {"hoveredSectorColor",
                     SerializeColor(hoveredSectorColor)},

                    {"highlightedSectorColor",
                     SerializeColor(highlightedSectorColor)},

                    {"snapIndicatorColor",
                     SerializeColor(snapIndicatorColor)},

                    {"validLineColor",
                     SerializeColor(kValidLineColor)},

                    {"invalidLineColor",
                     SerializeColor(kInvalidLineColor)},

                    {"anchorColor",
                     SerializeColor(kAnchorColor)},

                    {"validFillColor",
                     SerializeColor(kValidFillColor)},

                    {"invalidFillColor",
                     SerializeColor(kInvalidFillColor)},

                    {"normalHandleColor",
                     SerializeColor(normalHandleColor)},

                    {"highlightedHandleColor",
                     SerializeColor(highlightedHandleColor)},

                    {"handleOutlineColor",
                     SerializeColor(handleOutlineColor)},

                    {"themeTextColor",
                     SerializeColor(themeTextColor)},

                    {"gridColor",
                     SerializeColor(gridColor)},

                    {"backgroundColor",
                     SerializeColor(backgroundColor)}
                }}
            };

            fs::create_directories(settingsPath.parent_path());

            const std::vector<std::uint8_t> bson = nlohmann::json::to_bson(settings);

            std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);

            if (!output) {
                spdlog::error("Could not open user settings file for writing: {}", settingsPath.string());
                return false;
            }

            output.write(reinterpret_cast<const char*>(bson.data()), static_cast<std::streamsize>(bson.size()));

            if (!output) {
                spdlog::error("Failed while writing user settings: {}",settingsPath.string());
                return false;
            }

            spdlog::info("User settings saved successfully: {}",settingsPath.string());

            return true;
        }
        catch (const std::exception& error) {
            spdlog::error(
                "Failed to save user settings: {}",
                error.what()
            );
            return false;
        }
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

    // Creates a brand-new, empty level named levelName, saves it to disk, and
    // makes it the current level - the "start from scratch" counterpart to
    // LoadLevel() above. Unlike MapEditorInternal::Save(), which persists
    // whatever level is already current, this always replaces the current level
    // with a fresh one first, so it can't accidentally overwrite an existing
    // level's contents under a new name.
    bool NewLevel(const std::string& levelName) {
        using namespace MapEditorInternal;

        const std::string cleanName = LevelSerialization::CleanLevelName(levelName);

        if (cleanName.empty()) {
            spdlog::warn("Can not create a new level with an empty name");
            return false;
        }

        LevelManager::loadedLevels.assign(1, Level{});
        LevelManager::currentLevelIndex = 0;

        Level& level = LevelManager::CurrentLevel();
        level.name = cleanName;

        LevelSerialization::LevelExtraData extraData;
        extraData.backgroundTextureFileName.clear();

        const fs::path path = LevelSerialization::BuildLevelPath(cleanName);
        std::string errorMessage;

        if (!LevelSerialization::SaveLevelToFile(path, level, &extraData, &errorMessage)) {
            spdlog::critical("{}", errorMessage);
            return false;
        }

        backgroundTextureFileName = extraData.backgroundTextureFileName;
        currentMap = cleanName;

        // Same editor-session reset as LoadLevel(): a brand-new level starts with
        // a clean session too, not whatever was left over from the previous one.
        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;

        sectorBeingCreated.clear();
        pendingSectorParams = PendingSectorParams{};

        editingSector = false;
        selectedSectorID = INVALID_ID;

        actions.clear();

        LevelSystem::RefreshScriptAssets(level);

        spdlog::info("New level created and saved successfully {}", path.string());

        UpdateLevels();

        return true;
    }
}