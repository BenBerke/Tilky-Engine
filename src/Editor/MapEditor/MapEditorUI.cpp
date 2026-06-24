//
// Created by berke on 6/2/2026.
//

#include "../EditorInternal.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Project/ProjectManager.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static bool RunExporter() {
    const fs::path engineBasePath = ProjectManager::GetEngineBasePath();

#ifdef _WIN32
    const fs::path exporterExe = engineBasePath / "tilky_exporter.exe";
    const fs::path standaloneExe = engineBasePath / "Standalone.exe";
#else
    const fs::path exporterExe = engineBasePath / "tilky_exporter";
    const fs::path standaloneExe = engineBasePath / "Standalone";
#endif

    const fs::path projectMetadata = ProjectManager::GetProjectFiles();
    const fs::path exportFolder = ProjectManager::GetDefaultExportFolder();

    if (!fs::exists(exporterExe)) {
        spdlog::error("Exporter executable not found: {}", exporterExe.string());
        return false;
    }

    if (!fs::exists(standaloneExe)) {
        spdlog::error("Standalone executable not found: {}", standaloneExe.string());
        return false;
    }

    if (!fs::exists(projectMetadata)) {
        spdlog::error("Project metadata not found: {}", projectMetadata.string());
        return false;
    }

    try {
        fs::create_directories(exportFolder);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to create export folder '{}': {}", exportFolder.string(), e.what());
        return false;
    }

#ifdef _WIN32
    std::wstring commandLine =
        L"\"" + exporterExe.wstring() + L"\" " +
        L"\"" + projectMetadata.wstring() + L"\" " +
        L"\"" + exportFolder.wstring() + L"\" " +
        L"\"" + standaloneExe.wstring() + L"\"";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};

    spdlog::info("Running exporter to {}", exportFolder.string());

    BOOL success = CreateProcessW(
        exporterExe.wstring().c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        engineBasePath.wstring().c_str(),
        &startupInfo,
        &processInfo
    );

    if (!success) {
        spdlog::error("Failed to run exporter. Windows error: {}", GetLastError());
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    if (exitCode != 0) {
        spdlog::error("Exporter failed with exit code {}", exitCode);
        return false;
    }

#else
    spdlog::info("Running exporter to {}", exportFolder.string());

    const pid_t pid = fork();

    if (pid < 0) {
        spdlog::error("Failed to fork exporter process");
        return false;
    }

    if (pid == 0) {
        execl(
            exporterExe.c_str(),
            exporterExe.c_str(),
            projectMetadata.c_str(),
            exportFolder.c_str(),
            standaloneExe.c_str(),
            static_cast<char*>(nullptr)
        );

        _exit(127);
    }

    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        spdlog::error("Failed while waiting for exporter process");
        return false;
    }

    if (!WIFEXITED(status)) {
        spdlog::error("Exporter process did not exit normally");
        return false;
    }

    const int exitCode = WEXITSTATUS(status);

    if (exitCode != 0) {
        spdlog::error("Exporter failed with exit code {}", exitCode);
        return false;
    }
#endif

    spdlog::info("Export completed successfully to {}", exportFolder.string());
    return true;
}

namespace Editor {
    void RefreshLevelSoundsFromFolder() {
        Level& level = LevelManager::CurrentLevel();
        level.sounds.clear();

        const std::filesystem::path soundsPath = ProjectManager::GetSoundsPath();

        if (!std::filesystem::exists(soundsPath)) {
            std::filesystem::create_directories(soundsPath);
            spdlog::warn("Created missing Sounds folder: {}", soundsPath.string());
            return;
        }

        if (!std::filesystem::is_directory(soundsPath)) {
            spdlog::error("Sounds path is not a directory: {}", soundsPath.string());
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(soundsPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path path = entry.path();

            std::string extension = path.extension().string();

            std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (extension != ".wav") {
                continue;
            }

            Sound sound;
            sound.fileName = path.stem().string();

            level.sounds.push_back(sound);
        }

        std::ranges::sort(level.sounds, [](const Sound& a, const Sound& b) {
            return a.fileName < b.fileName;
        });

        spdlog::info("Refreshed {} level sound(s)", level.sounds.size());
    }
}

namespace {
    constexpr bool DRAGGABLE = false;

    using namespace Localisation;
    using namespace MapEditorInternal;

    ImGuiDrawFunctions::EntityInspectorState entityInspectorState;

    std::optional<std::string> pendingLevelToLoad;

    // Feature #1: Project Settings is a separate overlay window, toggled
    // from a button anchored to the bottom-right of the editor UI.
    bool projectSettingsOpen = false;

    // Feature #3: "Create Level" opens a name-input modal before anything
    // is actually cleared/switched.
    bool createLevelModalRequested = false;

    Entity* FindEntityById(Level& level, const ID entityId) {
        for (Entity& entity : level.entities) {
            if (entity.id == entityId) {
                return &entity;
            }
        }

        return nullptr;
    }

    void ResetInspectorState() {
        entityInspectorState = {};
        editingComponent = false;
    }

    void DrawDockSpace() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("Dockspace##MainDockspaceHost", nullptr, flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceID = ImGui::GetID("MainDockspace");

        ImGui::DockSpace(
            dockspaceID,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_PassthruCentralNode
        );

        ImGui::End();
    }

    // Feature #8: small thumbnail + index + name, anywhere a texture is
    // listed or picked. Never crashes on a missing/invalid texture - falls
    // back to a flat placeholder box instead.
    void DrawTextureThumbnailRow(const Level& level, const int textureIndex) {
        constexpr float thumbnailSize = 32.0f;

        if (textureIndex < 0 || textureIndex >= static_cast<int>(level.textures.size())) {
            ImGui::Dummy(ImVec2(thumbnailSize, thumbnailSize));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", Get("editor.texture_none").c_str());
            return;
        }

        SDL_Texture* texture = GetEditorTexture(textureIndex);

        if (texture != nullptr) {
            // NOTE: assumes the classic pointer-sized ImTextureID (pre-1.92
            // ImGui texture API). If this project is on a newer ImGui with
            // ImTextureRef/ImTextureData, this cast is the one line to update.
            ImGui::Image(
                reinterpret_cast<ImTextureID>(texture),
                ImVec2(thumbnailSize, thumbnailSize)
            );
        }
        else {
            // Placeholder: see GetEditorTexture() in MapEditorDrawing.cpp /
            // NOTES.md for exactly what renderer access this assumes.
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(thumbnailSize, thumbnailSize));

            ImGui::GetWindowDrawList()->AddRectFilled(
                cursor,
                ImVec2(cursor.x + thumbnailSize, cursor.y + thumbnailSize),
                IM_COL32(90, 90, 90, 255)
            );
            ImGui::GetWindowDrawList()->AddRect(
                cursor,
                ImVec2(cursor.x + thumbnailSize, cursor.y + thumbnailSize),
                IM_COL32(150, 150, 150, 255)
            );
        }

        ImGui::SameLine();
        ImGui::Text("%d: %s", textureIndex, level.textures[textureIndex].fileName.c_str());
    }

    void DrawTextureCategory() {
        const Level& level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_textures").c_str())) {
            EditorTextureCache::RefreshLevelTexturesFromFolder();
        }

        for (int i = 0; i < static_cast<int>(level.textures.size()); i++) {
            ImGui::PushID(i);

            DrawTextureThumbnailRow(level, i);

            ImGui::PopID();
        }

        ImGuiDrawFunctions::PutSpace(2);
    }

    void DrawSoundCategory() {
        const Level& level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_sounds").c_str())) {
            Editor::RefreshLevelSoundsFromFolder();
        }

        for (int i = 0; i < static_cast<int>(level.sounds.size()); i++) {
            ImGui::PushID(i);

            ImGui::Text("%d: %s", i, level.sounds[i].fileName.c_str());

            ImGui::PopID();
        }

        ImGuiDrawFunctions::PutSpace(2);
    }

    // World Settings stays exactly as it was: it's already correctly
    // level-specific (feature #1 says not to mix it with Project Settings),
    // so nothing here needs to change.
    void DrawWorldSettings() {
        Level& level = LevelManager::CurrentLevel();
        ListenerSettings& settings = level.listenerSettings;

        ImGui::Begin(Get("editor.world_settings").c_str());

        ImGui::Text(Get("settings.audio.title").c_str());

        ImGui::SliderFloat(
            Get("settings.audio.master_gain").c_str(),
            &settings.masterGain,
            0.0f,
            2.0f
        );

        ImGui::Separator();
        ImGui::TextDisabled("%s", Get("settings.audio.global_physics_header").c_str());

        if (ImGui::DragFloat(
            Get("settings.audio.doppler_factor").c_str(),
            &settings.dopplerFactor,
            0.01f,
            0.0f,
            10.0f
        )) {
            // SoundManager::SetListenerDopplerFactor(settings.dopplerFactor);
        }

        if (ImGui::InputFloat(
            Get("settings.audio.speed_of_sound").c_str(),
            &settings.speedOfSound
        )) {
            settings.speedOfSound = std::max(1.0f, settings.speedOfSound);
            // SoundManager::SetListenerSpeedOfSound(settings.speedOfSound);
        }

        const std::array<std::string, 5> modelLabels = {
            Get("settings.audio.distance_model.inverse"),
            Get("settings.audio.distance_model.inverse_clamped"),
            Get("settings.audio.distance_model.linear"),
            Get("settings.audio.distance_model.linear_clamped"),
            Get("settings.audio.distance_model.none")
        };

        const char* models[] = {
            modelLabels[0].c_str(),
            modelLabels[1].c_str(),
            modelLabels[2].c_str(),
            modelLabels[3].c_str(),
            modelLabels[4].c_str()
        };

        const ALenum alModels[] = {
            AL_INVERSE_DISTANCE,
            AL_INVERSE_DISTANCE_CLAMPED,
            AL_LINEAR_DISTANCE,
            AL_LINEAR_DISTANCE_CLAMPED,
            AL_NONE
        };

        auto DistanceModelToIndex = [](const ALenum model) -> int {
            switch (model) {
                case AL_INVERSE_DISTANCE:
                    return 0;

                case AL_INVERSE_DISTANCE_CLAMPED:
                    return 1;

                case AL_LINEAR_DISTANCE:
                    return 2;

                case AL_LINEAR_DISTANCE_CLAMPED:
                    return 3;

                case AL_NONE:
                    return 4;

                default:
                    return 1;
            }
        };

        int currentModel = DistanceModelToIndex(settings.distanceModel);

        if (ImGui::Combo(
            Get("settings.audio.distance_model").c_str(),
            &currentModel,
            models,
            IM_ARRAYSIZE(models)
        )) {
            settings.distanceModel = alModels[currentModel];
            // SoundManager::SetListenerDistanceModel(settings.distanceModel);
        }

        ImGuiDrawFunctions::PutSpace(5);
        ImGui::Text(Get("settings.physics.title").c_str());
        ImGui::InputFloat(Get("settings.physics.gravity").c_str(), &level.worldSettings.gravity);

        ImGuiDrawFunctions::PutSpace(5);

        ImGui::InputInt(Get("editor.background_texture").c_str(), &Editor::backgroundTextureIndex);

        ImGui::End();
    }

    void DrawLevelsMenu() {
        ImGui::Begin(Get("levels.title").c_str());

        for (int i = 0; i < static_cast<int>(Editor::maps.size()); ++i) {
            ImGui::PushID(i);

            std::string cleanName = Editor::maps[i];

            ImGui::Text("%s", cleanName.c_str());

            if (ImGui::Button(Get("levels.load").c_str())) {
                if (!Editor::currentMap.empty()) {
                    Save(Editor::currentMap);
                }

                QueueLevelLoad(cleanName);

                spdlog::info("Queued level load: {}", cleanName);
            }

            ImGui::SameLine();

            if (ImGui::Button(Get("levels.delete").c_str())) {
                const std::filesystem::path path =
                    ProjectManager::GetLevelsPath() / (cleanName + ".bson");

                try {
                    if (std::filesystem::remove(path)) {
                        spdlog::info("Deleted level: {}", path.string());

                        if (Editor::currentMap == cleanName) {
                            Editor::currentMap = "";
                        }

                        UpdateLevels();
                    }
                    else {
                        spdlog::error("Failed to delete level, file may not exist: {}", path.string());
                    }
                }
                catch (const std::filesystem::filesystem_error& e) {
                    spdlog::error("Failed to delete level: {}", e.what());
                }
            }

            ImGui::PopID();
        }

        ImGui::End();
    }

    // Feature #3: "Create Level". Saves the current level with the existing
    // Save() function (if a level is currently open), then resets
    // LevelManager::CurrentLevel() to a genuinely blank Level and switches
    // the editor onto it.
    //
    // NOTE: this resets CurrentLevel() in place via `level = Level{}` rather
    // than touching LevelManager::loadedLevels directly, since
    // LevelManager.hpp wasn't part of the pasted source and relying on
    // Level's own default constructor is the only way to guarantee every
    // field/pool starts genuinely empty without guessing at the struct's
    // exact contents. If LevelManager exposes its own dedicated "create a
    // new level" API, prefer that instead. See NOTES.md.
    void CreateNewLevel(const std::string& levelName) {
        if (!Editor::currentMap.empty()) {
            Save(Editor::currentMap);
        }

        Level& level = LevelManager::CurrentLevel();

        level = Level{};
        level.name = levelName;

        level.nextEntityID = 1;
        level.nextSectorID = 0;
        level.nextWallID = 0;

        sectorBeingCreated.clear();
        pendingSectorParams = PendingSectorParams{};

        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;
        selectedDotID = INVALID_ID;

        editingSector = false;
        selectedSectorID = INVALID_ID;

        editingEntity = false;
        ResetInspectorState();

        actions.clear();

        Editor::currentMap = levelName;

        // Textures/sounds are folder-driven project assets, not user-authored
        // level content (feature #3: "do not touch project-wide assets...
        // unless the current code clearly stores them per-level" - here they
        // are Level fields, but they're populated by scanning a shared
        // folder). Re-scanning mirrors exactly what Editor::Start() already
        // does for a freshly-created empty level.
        EditorTextureCache::RefreshLevelTexturesFromFolder();
        Editor::RefreshLevelSoundsFromFolder();

        MapQueries::RebuildSectorRuntimeLinks(level);

        spdlog::info("Created new level: {}", levelName);
    }

    void DrawCreateLevelModal() {
        static char newLevelNameBuf[64] = "";

        constexpr const char* kCreateLevelPopupID = "Create Level";

        if (createLevelModalRequested) {
            ImGui::OpenPopup(kCreateLevelPopupID);
            newLevelNameBuf[0] = '\0';
            createLevelModalRequested = false;
        }

        if (ImGui::BeginPopupModal(kCreateLevelPopupID, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Get("editor.create_level_prompt").c_str());
            ImGui::InputText("##NewLevelName", newLevelNameBuf, IM_ARRAYSIZE(newLevelNameBuf));

            const bool nameValid = newLevelNameBuf[0] != '\0';

            ImGui::BeginDisabled(!nameValid);

            if (ImGui::Button(Get("editor.create").c_str())) {
                CreateNewLevel(newLevelNameBuf);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            if (ImGui::Button(Get("editor.cancel").c_str())) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // Feature #1: everything project-level lives here now - level
    // name/management, Create Level, export, and shutdown/close. Nothing
    // World-Settings-specific (level-specific) is mixed in here.
    void DrawProjectSettingsWindow() {
        ImGui::Begin(Get("editor.project_settings").c_str(), &projectSettingsOpen);

        static char levelNameBuf[64] = "";

        if (levelNameBuf[0] == '\0' && !Editor::currentMap.empty()) {
            std::strncpy(levelNameBuf, Editor::currentMap.c_str(), sizeof(levelNameBuf) - 1);
            levelNameBuf[sizeof(levelNameBuf) - 1] = '\0';
        }

        if (ImGui::InputText(Get("editor.level_name").c_str(), levelNameBuf, IM_ARRAYSIZE(levelNameBuf))) {
            Editor::currentMap = levelNameBuf;
        }

        if (ImGui::Button(Get("editor.create_level").c_str())) {
            createLevelModalRequested = true;
        }

        ImGuiDrawFunctions::PutSpace(2);

        DrawLevelsMenu();

        ImGuiDrawFunctions::PutSpace(2);

        if (ImGui::Button(Get("editor.export").c_str())) {
            RunExporter();
        }

        ImGuiDrawFunctions::PutSpace(3);

        if (ImGui::Button(Get("editor.shutdown").c_str())) {
            MapEditorInternal::shutdown = true;
            quit = true;
        }

        ImGui::End();

        DrawCreateLevelModal();
    }

    void DrawProjectSettingsButton() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        constexpr float margin = 12.0f;

        const ImVec2 anchor(
            viewport->WorkPos.x + viewport->WorkSize.x - margin,
            viewport->WorkPos.y + viewport->WorkSize.y - margin
        );

        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);

        constexpr ImGuiWindowFlags overlayFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin("##ProjectSettingsButtonOverlay", nullptr, overlayFlags);

        if (ImGui::Button(Get("editor.project_settings").c_str(), ImVec2(180.0f, 0.0f))) {
            projectSettingsOpen = !projectSettingsOpen;
        }

        ImGui::End();

        if (projectSettingsOpen) {
            DrawProjectSettingsWindow();
        }
    }

    // Feature #10: hierarchy/outliner panel listing entities, sectors, and
    // dots. Deletions are deferred until after each loop to avoid mutating
    // the containers mid-iteration.
    void DrawHierarchyPanel(Level& level) {
        ImGui::Begin(Get("editor.hierarchy").c_str());

        if (ImGui::CollapsingHeader(Get("editor.hierarchy.sectors").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ID sectorPendingDelete = INVALID_ID;

            for (const Sector& sector : level.sectors) {
                ImGui::PushID(static_cast<int>(sector.id));

                const std::string label = "Sector #" + std::to_string(sector.id);
                const bool isSelected = (selectedSectorID == sector.id);

                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedSectorID = sector.id;
                    editingSector = true;
                    currentMode = MODE_SECTOR;
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(Get("editor.delete").c_str())) {
                        sectorPendingDelete = sector.id;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            if (sectorPendingDelete != INVALID_ID) {
                DeleteSector(sectorPendingDelete);
            }
        }

        if (ImGui::CollapsingHeader(Get("editor.hierarchy.entities").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ID entityPendingDelete = INVALID_ID;

            for (const Entity& entity : level.entities) {
                ImGui::PushID(static_cast<int>(entity.id));

                const std::string label = entity.name + " (#" + std::to_string(entity.id) + ")";
                const bool isSelected = editingEntity && (selectedEntity.id == entity.id);

                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedEntity = entity;
                    editingEntity = true;
                    currentMode = MODE_ENTITY;
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(Get("editor.delete").c_str())) {
                        entityPendingDelete = entity.id;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            if (entityPendingDelete != INVALID_ID) {
                if (editingEntity && selectedEntity.id == entityPendingDelete) {
                    editingEntity = false;
                    ResetInspectorState();
                }

                level.DestroyEntity(entityPendingDelete);
            }
        }

        if (ImGui::CollapsingHeader(Get("editor.hierarchy.dots").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ID dotPendingDelete = INVALID_ID;

            for (const Dot& dot : dots) {
                ImGui::PushID(static_cast<int>(dot.id));

                const std::string label =
                    "Dot #" + std::to_string(dot.id) +
                    " (" + std::to_string(static_cast<int>(dot.position.x)) +
                    ", " + std::to_string(static_cast<int>(dot.position.y)) + ")";

                const bool isSelected = (selectedDotID == dot.id);

                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedDotID = dot.id;
                    currentMode = MODE_DOT;
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(Get("editor.delete").c_str())) {
                        dotPendingDelete = dot.id;
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            if (dotPendingDelete != INVALID_ID) {
                RemoveDot(dotPendingDelete);
            }
        }

        ImGui::End();
    }

    void DrawMode() {
        if (ImGui::Button(Get("editor.mode").c_str())) {
            ChangeMode();
        }

        auto GetModeName = [](const int mode) -> std::string {
            switch (mode) {
                case MODE_DOT:
                    return Get("mode.dot");

                case MODE_SECTOR:
                    return Get("mode.sector");

                case MODE_ENTITY:
                    return Get("mode.entity");

                default:
                    return Get("mode.unknown");
            }
        };

        const std::string modeName = GetModeName(currentMode);
        ImGui::Text("%s", modeName.c_str());
    }

    // Feature #11: looks the sector up by stable ID through
    // level.sectorIDToIndex every frame (the exact pattern requested),
    // rather than persisting a raw vector index. Deletion now routes through
    // DeleteSector() so wall references / ID maps get cleaned up correctly.
    void DrawSelectedSectorInspector(Level& level) {
        if (!editingSector || currentMode != MODE_SECTOR) return;

        const auto it = level.sectorIDToIndex.find(selectedSectorID);

        if (it == level.sectorIDToIndex.end()) {
            selectedSectorID = INVALID_ID;
            editingSector = false;
            return;
        }

        Sector& sector = level.sectors[it->second];

        const bool deleteRequested =
            ImGuiDrawFunctions::DrawSectorEditor(sector, &editingSector, it->second, DRAGGABLE);

        if (deleteRequested) {
            DeleteSector(selectedSectorID);
        }
    }

    void DrawSelectedEntityInspector(Level& level) {
        if (!editingEntity || currentMode != MODE_ENTITY) {
            return;
        }

        Entity* entityPtr = FindEntityById(level, selectedEntity.id);

        if (entityPtr == nullptr) {
            editingEntity = false;
            ResetInspectorState();
            return;
        }

        Entity& entity = *entityPtr;

        const bool deleteRequested =
            ImGuiDrawFunctions::DrawEntityEditor(entity, entityInspectorState, &editingEntity, DRAGGABLE);

        editingComponent = entityInspectorState.editingComponent;

        if (deleteRequested) {
            const ID idToDelete = entity.id;

            editingEntity = false;
            ResetInspectorState();

            level.DestroyEntity(idToDelete);
            return;
        }

        if (!editingEntity) {
            ResetInspectorState();
            return;
        }

        if (entityInspectorState.editingComponent && entityInspectorState.selectedComponent != -1) {
            ImGuiDrawFunctions::DrawComponentEditor(
                entity,
                entityInspectorState,
                &entityInspectorState.editingComponent, DRAGGABLE
            );

            editingComponent = entityInspectorState.editingComponent;
        }
    }

    // Wall Mode is gone (feature #4), so there is no more user-facing wall
    // inspector - DrawSelectedWallInspector has been removed along with the
    // editingWall/selectedWall state it depended on.
    void DrawSelectionInspectors(Level& level) {
        DrawSelectedSectorInspector(level);
        DrawSelectedEntityInspector(level);
    }
}

namespace MapEditorInternal {
    using namespace Localisation;

    void ChangeMode() {
        const Mode previousMode = currentMode;

        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);

        // Centralized here so leaving Sector Mode behaves the same way
        // whether triggered by the UI button or the Q hotkey (previously
        // only the UI button cleaned up the in-progress chain).
        if (previousMode == MODE_SECTOR) {
            CancelSectorChain();
        }
    }

    // Feature #2: light/white theme toggle. StyleColorsLight() alone leaves
    // disabled text, selected states, and borders low-contrast, so the
    // trouble spots are explicitly overridden on top of it. Dark mode stays
    // exactly as before (ImGui::StyleColorsDark()).
    void ApplyEditorTheme(const EditorTheme theme) {
        if (theme == THEME_DARK) {
            ImGui::StyleColorsDark();
            return;
        }

        ImGui::StyleColorsLight();

        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text]                 = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.45f, 0.47f, 1.00f);
        colors[ImGuiCol_WindowBg]             = ImVec4(0.94f, 0.94f, 0.95f, 1.00f);
        colors[ImGuiCol_ChildBg]              = ImVec4(0.97f, 0.97f, 0.98f, 1.00f);
        colors[ImGuiCol_PopupBg]              = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
        colors[ImGuiCol_Border]               = ImVec4(0.55f, 0.55f, 0.58f, 0.60f);
        colors[ImGuiCol_FrameBg]              = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.78f, 0.84f, 0.97f, 1.00f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.68f, 0.78f, 0.97f, 1.00f);
        colors[ImGuiCol_TitleBg]              = ImVec4(0.85f, 0.85f, 0.87f, 1.00f);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.70f, 0.78f, 0.95f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.90f, 0.90f, 0.91f, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.90f, 0.90f, 0.91f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.65f, 0.65f, 0.68f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.45f, 0.48f, 1.00f);
        colors[ImGuiCol_CheckMark]            = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.30f, 0.55f, 0.90f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
        colors[ImGuiCol_Button]               = ImVec4(0.82f, 0.82f, 0.85f, 1.00f);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.72f, 0.80f, 0.96f, 1.00f);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.60f, 0.72f, 0.95f, 1.00f);
        colors[ImGuiCol_Header]               = ImVec4(0.75f, 0.80f, 0.93f, 1.00f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.68f, 0.76f, 0.95f, 1.00f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.58f, 0.70f, 0.95f, 1.00f);
        colors[ImGuiCol_Separator]            = ImVec4(0.60f, 0.60f, 0.63f, 0.60f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.40f, 0.55f, 0.85f, 0.78f);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.30f, 0.50f, 0.85f, 1.00f);
        colors[ImGuiCol_ResizeGrip]           = ImVec4(0.60f, 0.60f, 0.63f, 0.40f);
        colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.40f, 0.55f, 0.85f, 0.65f);
        colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.30f, 0.50f, 0.85f, 0.90f);
        colors[ImGuiCol_Tab]                  = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
        colors[ImGuiCol_TabHovered]           = ImVec4(0.70f, 0.78f, 0.96f, 1.00f);
        colors[ImGuiCol_TabActive]            = ImVec4(0.72f, 0.80f, 0.97f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.85f, 0.85f, 0.87f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.80f, 0.84f, 0.93f, 1.00f);
        colors[ImGuiCol_DockingPreview]       = ImVec4(0.30f, 0.50f, 0.85f, 0.55f);
        colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        colors[ImGuiCol_PlotLines]            = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_PlotHistogram]        = ImVec4(0.80f, 0.55f, 0.10f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.30f, 0.55f, 0.90f, 0.35f);
        colors[ImGuiCol_DragDropTarget]       = ImVec4(0.90f, 0.70f, 0.10f, 0.90f);
        colors[ImGuiCol_NavHighlight]         = ImVec4(0.30f, 0.55f, 0.90f, 0.80f);
    }

    void QueueLevelLoad(const std::string& levelName) {
        pendingLevelToLoad = levelName;
        spdlog::info("Queued level load: {}", levelName);
    }

    bool ProcessPendingLevelLoad() {
        if (!pendingLevelToLoad.has_value()) {
            return false;
        }

        const std::string levelToLoad = *pendingLevelToLoad;
        pendingLevelToLoad.reset();

        ResetInspectorState();

        editingSector = false;
        selectedSectorID = INVALID_ID;

        editingEntity = false;

        sectorBeingCreated.clear();
        pendingSectorParams = PendingSectorParams{};
        actions.clear();

        // Dots are editor-session data tied to whatever level you're
        // looking at (see NOTES.md) - they don't carry over to a different
        // level file.
        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;
        selectedDotID = INVALID_ID;

        spdlog::info("Processing queued level load: {}", levelToLoad);

        Editor::LoadLevel(levelToLoad);

        return true;
    }

    void DrawEditorUI() {
        DrawDockSpace();

        Level& level = LevelManager::CurrentLevel();

        ImGui::Begin(Get("editor.title").c_str());

        DrawMode();

        ImGuiDrawFunctions::PutSpace(1);

        bool lightModeActive = (currentTheme == THEME_LIGHT);

        if (ImGui::Checkbox(Get("editor.light_mode").c_str(), &lightModeActive)) {
            currentTheme = lightModeActive ? THEME_LIGHT : THEME_DARK;
            ApplyEditorTheme(currentTheme);
        }

        ImGui::SameLine();
        ImGui::Checkbox(Get("editor.texture_view_mode").c_str(), &textureViewMode);

        ImGuiDrawFunctions::PutSpace(2);

        DrawSelectionInspectors(level);

        ImGuiDrawFunctions::PutSpace(2);

        if (currentMode == MODE_SECTOR) {
            ImGui::InputInt("Wall Texture Index", &wallTextureIndex);
            DrawTextureThumbnailRow(level, wallTextureIndex);

            ImGui::InputInt("Ceiling Texture Index", &ceilTextureIndex);
            DrawTextureThumbnailRow(level, ceilTextureIndex);

            ImGui::InputInt("Floor Texture Index", &floorTextureIndex);
            DrawTextureThumbnailRow(level, floorTextureIndex);

            ImGui::Separator();

            ImGui::InputFloat("Floor Height", &floorHeight, 1.0f, 10.0f, "%.2f");
            ImGui::InputFloat("Ceiling Height", &ceilHeight, 1.0f, 10.0f, "%.2f");
            ImGui::InputFloat("Light Value", &lightValue, 1.0f, 10.0f, "%.2f");

            ImGui::Separator();
            ImGui::ColorEdit3("Wall Color", &wallColor.x);
            ImGui::ColorEdit3("Ceiling Color", &ceilColor.x);
            ImGui::ColorEdit3("Floor Color", &floorColor.x);

            if (!sectorBeingCreated.empty()) {
                ImGui::TextDisabled("%s", Get("editor.sector_chain_in_progress").c_str());
            }
        }

        ImGuiDrawFunctions::PutSpace(2);

        DrawSoundCategory();

        ImGuiDrawFunctions::PutSpace(2);

        DrawTextureCategory();

        ImGui::InputInt(Get("editor.floor").c_str(), &currentFloor);

        if (ImGui::Button(Get("editor.save").c_str())) {
            Save(Editor::currentMap);
        }

        if (ImGui::Button(Get("editor.runtime_editor").c_str())) {
            if (Save(Editor::currentMap)) {
                SDL_Log("%s", Editor::currentMap.c_str());
                switchToRuntime = true;
            }
        }

        ImGuiDrawFunctions::PutSpace(1);

        if (ImGui::Button(Get("editor.save_and_play").c_str())) {
            if (Save(Editor::currentMap)) {
                SDL_Log("%s", Editor::currentMap.c_str());
                quit = true;
                play = true;
            }
        }

        if (ImGui::Button(Get("editor.save_and_quit").c_str())) {
            if (Save(Editor::currentMap)) {
                SDL_Log("%s", Editor::currentMap.c_str());
                quit = true;
            }
        }

        if (ImGui::Button(Get("editor.switch_to_ui").c_str())) {
            currentState = STATE_UI;
        }

        ImGui::End();

        DrawWorldSettings();
        DrawHierarchyPanel(level);
        DrawProjectSettingsButton();
    }
}