#include "EditorInternal.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Headers/Objects/Prefab.hpp"
#include "Headers/Editor/EditorStyle.hpp"
#include "Headers/TagRegistry.hpp"
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
}

namespace MapEditorInternal {
    bool LoadProjectSettings()
    {
        const fs::path settingsPath = ProjectManager::GetProjectFolder() / "ProjectSettings.json";

        try {
            if (!fs::exists(settingsPath)) {
                TagRegistry::Clear();
                return true;
            }

            std::ifstream file(settingsPath);

            if (!file.is_open()) {
                spdlog::error("Failed to open project settings: {}",settingsPath.string());
                return false;
            }

            nlohmann::json settings;
            file >> settings;

            const int formatVersion = settings.at("formatVersion").get<int>();

            if (formatVersion != 1) {
                spdlog::error("Unsupported project settings version: {}",formatVersion);
                return false;
            }

            auto tags = settings.at("tagToId").get<TagRegistry::TagMap>();

            // Calculate the minimum safe next ID. This prevents ID collisions
            // if nextTagId is missing or contains an incorrect lower value.
            uint32_t minimumNextId = 0;

            for (const auto& [name, id] : tags)
                minimumNextId = std::max(minimumNextId,static_cast<uint32_t>(id) + 1u);

            const uint32_t savedNextId = settings.value("nextTagId", minimumNextId);

            const uint32_t nextId = std::max(savedNextId, minimumNextId);

            TagRegistry::Load(std::move(tags), nextId);

            return true;
        }
        catch (const std::exception& error) {
            spdlog::error("Failed to load project settings: {}",error.what());
            return false;
        }
    }

    bool SaveProjectSettings() {
        const fs::path settingsPath = ProjectManager::GetProjectFolder() / "ProjectSettings.json";

        try {
            const nlohmann::json settings = {
                {"formatVersion", 1},
                {"tagToId", TagRegistry::GetAll()},
                {"nextTagId", TagRegistry::GetNextId()}
            };

            std::ofstream file(settingsPath, std::ios::trunc);

            if (!file.is_open()) {
                spdlog::error("Failed to open project settings for writing: {}",settingsPath.string());
                return false;
            }

            file << settings.dump(4);

            if (!file.good()) {
                spdlog::error("Failed while writing project settings: {}",settingsPath.string());
                return false;
            }

            return true;
        }
        catch (const std::exception& error) {
            spdlog::error("Failed to save project settings: {}",error.what());
            return false;
        }
    }

    bool LoadUserSettings() {
        const std::string version = ProjectManager::GetProjectEngineVersion();
        if (version.empty()) {
            spdlog::error("Can't find engine version, unable to load engine data");
            return false;
        }
        const fs::path settingsPath = ProjectManager::GetUserSettingsPath();
        if (!fs::exists(settingsPath)) {
            spdlog::error("User settings file doesn't exist: {}. Using defaults.", settingsPath.string());
            return false;
        }
        try {
            std::ifstream input(settingsPath, std::ios::binary);

            if (!input) spdlog::error("Could not open user settings file {}", settingsPath.string());

            const nlohmann::json settings = nlohmann::json::from_bson(input);

            if (!settings.is_object()) {
                spdlog::error("User settings root is not an object");
                return false;
            }

            const int formatVersion = settings.value("formatVersion", 0);

            if (formatVersion != 1) {
                spdlog::error("Unsupported user settings format version: {}", formatVersion);
                return false;
            }
            // A missing or malformed colour keeps its current value, so a file
            // saved before a setting existed still loads everything else.
            static const nlohmann::json NO_COLORS = nlohmann::json::object();
            const auto colorsIt = settings.find("colors");
            const nlohmann::json &colors = colorsIt != settings.end() && colorsIt->is_object() ? *colorsIt : NO_COLORS;

            auto ReadColor = [&](const char *key, float *out, const std::size_t count) {
                const auto it = colors.find(key);
                if (it == colors.end() || !it->is_array() || it->size() < count) return;

                for (std::size_t i = 0; i < count; ++i)
                    if (!(*it)[i].is_number()) return;

                for (std::size_t i = 0; i < count; ++i) out[i] = (*it)[i].get<float>();
            };

            auto LoadColor3 = [&](const char *key, Vector3 &color) {
                float values[3] = {color.x, color.y, color.z};
                ReadColor(key, values, 3);
                color = {values[0], values[1], values[2]};
            };

            auto LoadColor4 = [&](const char *key, Vector4 &color) {
                float values[4] = {color.x, color.y, color.z, color.w};
                ReadColor(key, values, 4);
                color = {values[0], values[1], values[2], values[3]};
            };

            using namespace MapEditorInternal;
            LoadColor3("normalEntityColor", normalEntityColor);
            LoadColor3("highlightedEntityColor", highlightedEntityColor);
            LoadColor3("spriteEntityColor", spriteEntityColor);
            LoadColor3("normalWallColor", normalWallColor);
            LoadColor3("highlightedWallColor", highlightedWallColor);
            LoadColor3("hoveredSectorColor", hoveredSectorColor);
            LoadColor3("highlightedSectorColor", highlightedSectorColor);
            LoadColor3("snapIndicatorColor", snapIndicatorColor);
            LoadColor3("validLineColor", kValidLineColor);
            LoadColor3("invalidLineColor", kInvalidLineColor);
            LoadColor3("anchorColor", kAnchorColor);
            LoadColor4("validFillColor", kValidFillColor);
            LoadColor4("invalidFillColor", kInvalidFillColor);
            LoadColor3("normalHandleColor", normalHandleColor);
            LoadColor3("highlightedHandleColor", highlightedHandleColor);
            LoadColor3("handleOutlineColor", handleOutlineColor);
            LoadColor3("gizmoXColor", gizmoXColor);
            LoadColor3("gizmoYColor", gizmoYColor);
            LoadColor3("gizmoZColor", gizmoZColor);
            LoadColor3("themeTextColor", themeTextColor);
            LoadColor3("gridColor", gridColor);
            LoadColor3("backgroundColor", backgroundColor);

            cameraSpeed = settings.value("cameraSpeed", 1.0f);
            cameraStepSize = settings.value("cameraStepSize", 50.0f);
            runtimeCameraFov = std::clamp(
                settings.value("runtimeCameraFov", DEFAULT_RUNTIME_CAMERA_FOV),
                MIN_RUNTIME_CAMERA_FOV,
                MAX_RUNTIME_CAMERA_FOV
            );
            textureFieldSize = std::clamp(
                settings.value("textureFieldSize", DEFAULT_TEXTURE_FIELD_SIZE),
                MIN_TEXTURE_FIELD_SIZE,
                MAX_TEXTURE_FIELD_SIZE
            );

            hierarchyVisible = settings.value("hierarchyVisible", true);
            editorPanelVisible = settings.value("editorPanelVisible", true);
            worldSettingsVisible = settings.value("worldSettingsVisible", true);
            assetBrowserVisible = settings.value("assetBrowserVisible", true);
            historyVisible = settings.value("historyVisible", false);
            runtimeHistoryVisible = settings.value("runtimeHistoryVisible", false);
            entityMarkerMode = static_cast<EntityMarkerMode>(std::clamp(
                settings.value("entityMarkerMode", static_cast<int>(EntityMarkerMode::Always)),
                0,
                static_cast<int>(EntityMarkerMode::Count) - 1
            ));

            // A binding missing from the file, or a slot that isn't a valid
            // scancode, keeps its current (default) key. 0 is an empty slot.
            if (const auto keysIt = settings.find("keyBindings"); keysIt != settings.end() && keysIt->is_object()) {
                for (int bind = 0; bind < KEYBIND_COUNT; ++bind) {
                    const auto it = keysIt->find(KEY_BIND_INFO[bind].saveName);
                    if (it == keysIt->end() || !it->is_array()) continue;

                    for (int slot = 0; slot < KEYBIND_SLOTS && slot < static_cast<int>(it->size()); ++slot) {
                        const nlohmann::json& value = (*it)[slot];
                        if (!value.is_number_integer()) continue;

                        const int code = value.get<int>();
                        if (code >= SDL_SCANCODE_UNKNOWN && code < SDL_SCANCODE_COUNT) keyBindings[bind][slot] = static_cast<SDL_Scancode>(code);
                    }
                }
            }

            // Same for the combos, four slots each.
            if (const auto combosIt = settings.find("comboBindings"); combosIt != settings.end() && combosIt->is_object()) {
                for (int combo = 0; combo < COMBO_COUNT; ++combo) {
                    const auto it = combosIt->find(COMBO_INFO[combo].saveName);
                    if (it == combosIt->end() || !it->is_array()) continue;

                    for (int slot = 0; slot < COMBO_SLOTS && slot < static_cast<int>(it->size()); ++slot) {
                        const nlohmann::json& value = (*it)[slot];
                        if (!value.is_number_integer()) continue;

                        const int code = value.get<int>();
                        if (code >= SDL_SCANCODE_UNKNOWN && code < SDL_SCANCODE_COUNT) comboBindings[combo][slot] = static_cast<SDL_Scancode>(code);
                    }
                }
            }

            // A missing or non-number step keeps its default.
            if (const auto stepsIt = settings.find("runtimeSteps"); stepsIt != settings.end() && stepsIt->is_object()) {
                for (int step = 0; step < RT_STEP_COUNT; ++step) {
                    const RuntimeStepInfo& info = RUNTIME_STEP_INFO[step];
                    const auto it = stepsIt->find(info.saveName);
                    if (it == stepsIt->end() || !it->is_number()) continue;

                    runtimeSteps[step] = std::clamp(it->get<float>(), info.min, info.max);
                }
            }

            // An id no design has (or none, from older settings files) gets the default.
            EditorStyle::Apply(settings.value("editorDesign", std::string(EditorStyle::DEFAULT_DESIGN)));
        }
        catch (std::exception& e) {
            spdlog::error("Error while loading user settings {}", e.what());
            return false;
        }

        return true;
    }

    // {"pan_up": [26, 0], ...} - each binding's slots as scancodes, 0 = empty.
    nlohmann::json SerializeKeyBindings() {
        nlohmann::json bindings = nlohmann::json::object();

        for (int bind = 0; bind < KEYBIND_COUNT; ++bind) {
            nlohmann::json slots = nlohmann::json::array();
            for (const SDL_Scancode key : keyBindings[bind]) slots.push_back(static_cast<int>(key));

            bindings[KEY_BIND_INFO[bind].saveName] = std::move(slots);
        }

        return bindings;
    }

    // {"undo": [224, 29, 228, 29], ...} - set A then set B, 0 = empty.
    nlohmann::json SerializeComboBindings() {
        nlohmann::json bindings = nlohmann::json::object();

        for (int combo = 0; combo < COMBO_COUNT; ++combo) {
            nlohmann::json slots = nlohmann::json::array();
            for (const SDL_Scancode key : comboBindings[combo]) slots.push_back(static_cast<int>(key));

            bindings[COMBO_INFO[combo].saveName] = std::move(slots);
        }

        return bindings;
    }

    // {"height": 1.0, ...}
    nlohmann::json SerializeRuntimeSteps() {
        nlohmann::json steps = nlohmann::json::object();

        for (int step = 0; step < RT_STEP_COUNT; ++step) steps[RUNTIME_STEP_INFO[step].saveName] = runtimeSteps[step];

        return steps;
    }

    bool SaveUserSettings() {
        const fs::path settingsPath = ProjectManager::GetUserSettingsPath();

        if (settingsPath.empty()) {
            spdlog::error("Cannot save user settings because the current engine version is empty");
            return false;
        }

        try {
            const nlohmann::json settings = {
                // When changing this, dont forget to change it in Editor.cpp LoadUserSettings() as well
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

                    {"gizmoXColor",
                     SerializeColor(gizmoXColor)},

                    {"gizmoYColor",
                     SerializeColor(gizmoYColor)},

                    {"gizmoZColor",
                     SerializeColor(gizmoZColor)},

                    {"themeTextColor",
                     SerializeColor(themeTextColor)},

                    {"gridColor",
                     SerializeColor(gridColor)},

                    {"backgroundColor",
                     SerializeColor(backgroundColor)}
                }},
                   {"cameraSpeed", cameraSpeed},
                {"cameraStepSize", cameraStepSize},
                {"runtimeCameraFov", runtimeCameraFov},
                {"textureFieldSize", textureFieldSize},
                {"editorDesign", EditorStyle::ActiveDesign()},
                {"hierarchyVisible", hierarchyVisible},
                {"editorPanelVisible", editorPanelVisible},
                {"worldSettingsVisible", worldSettingsVisible},
                {"assetBrowserVisible", assetBrowserVisible},
                {"historyVisible", historyVisible},
                {"runtimeHistoryVisible", runtimeHistoryVisible},
                {"entityMarkerMode", static_cast<int>(entityMarkerMode)},
                {"keyBindings", SerializeKeyBindings()},
                {"comboBindings", SerializeComboBindings()},
                {"runtimeSteps", SerializeRuntimeSteps()}
            };

            spdlog::info("Saving user settings to: '{}'", settingsPath.string());
            spdlog::info("Absolute settings path: '{}'", fs::absolute(settingsPath).string());

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

    void UpdateLevels() {
        Editor::maps.clear();

        for (const fs::path& levelFile : LevelSerialization::ListLevelFiles()) {
            const std::string levelName = levelFile.stem().string();

            if (std::ranges::find(Editor::maps, levelName) != Editor::maps.end()) {
                spdlog::error("More than one level is called \"{}\" (one is {}); rename all but one", levelName, levelFile.string());
                continue;
            }

            Editor::maps.push_back(levelName);
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

        // Saved override lists must match what the instances hold now.
        Prefab::RefreshAllOverrides(level);

        LevelSerialization::LevelExtraData extraData;

        std::string errorMessage;
        const fs::path path = LevelSerialization::ResolveLevelSavePath(cleanName, &errorMessage);

        if (path.empty() || !LevelSerialization::SaveLevelToFile(path, level, &extraData, &errorMessage)) {
            spdlog::critical("Failed to save level to file {}", errorMessage);
            return false;
        }

        if (!SaveProjectSettings()) {
            spdlog::critical("Failed to save project settings {}", errorMessage);
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
        Level loadedLevel;
        LevelSerialization::LevelExtraData extraData;
        std::string errorMessage;

        const fs::path path = LevelSerialization::FindLevelPath(cleanName, &errorMessage);

        if (path.empty() || !LevelSerialization::LoadLevelFromFile(path, loadedLevel, &extraData, &errorMessage)) {
            spdlog::critical("{}", errorMessage);
            return false;
        }

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

        ClearUndoHistory();

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

        //todo TILKYTODO check if works in actual game
        LevelSystem::RefreshScriptAssets(LevelManager::CurrentLevel());

        spdlog::info("Level loaded successfully {}", path.string());

        return true;
    }

    void LevelFileRenamed(const std::string& oldName, const std::string& newName) {
        if (oldName == newName) return;

        if (currentMap == oldName) {
            currentMap = newName;
            if (LevelManager::HasCurrentLevel()) LevelManager::CurrentLevel().name = newName;
        }

        if (ProjectManager::GetLastOpenLevelName() == oldName) ProjectManager::SetLastOpenLevelName(newName);
        if (ProjectManager::GetStartLevelName() == oldName) ProjectManager::SetStartLevelName(newName);

        MapEditorInternal::UpdateLevels();
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

        std::string errorMessage;
        const fs::path path = LevelSerialization::ResolveLevelSavePath(cleanName, &errorMessage);

        if (path.empty() || !LevelSerialization::SaveLevelToFile(path, level, &extraData, &errorMessage)) {
            spdlog::critical("{}", errorMessage);
            return false;
        }

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

        ClearUndoHistory();

        LevelSystem::RefreshScriptAssets(level);

        spdlog::info("New level created and saved successfully {}", path.string());

        UpdateLevels();

        return true;
    }
}