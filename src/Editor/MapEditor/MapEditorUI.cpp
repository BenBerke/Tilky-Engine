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
    using namespace Localisation;
    using namespace MapEditorInternal;

    ImGuiDrawFunctions::EntityInspectorState entityInspectorState;

    std::optional<std::string> pendingLevelToLoad;

    Entity* FindEntityById(Level& level, const EntityID entityId) {
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
        ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags flags =
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

    void DrawTextureCategory() {
        const Level& level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_textures").c_str())) {
            EditorTextureCache::RefreshLevelTexturesFromFolder();
        }

        for (int i = 0; i < static_cast<int>(level.textures.size()); i++) {
            ImGui::PushID(i);

            ImGui::Text("%d: %s", i, level.textures[i].fileName.c_str());

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

    void DrawMode() {
        if (ImGui::Button(Get("editor.mode").c_str())) {
            const Mode previousMode = currentMode;

            ChangeMode();

            if (previousMode == MODE_SECTOR) {
                FinishSectorSelection();
                creatableSector = false;
            }

            if (currentMode == MODE_SECTOR) {
                sectorBeingCreated.clear();
                creatableSector = false;
            }
        }

        auto GetModeName = [](const int mode) -> std::string {
            switch (mode) {
                case MODE_DOT:
                    return Get("mode.dot");

                case MODE_WALL:
                    return Get("mode.wall");

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

    void DrawSelectedSectorInspector(Level& level) {
        if (!editingSector || currentMode != MODE_SECTOR) {
            return;
        }

        if (selectedSector < 0 || selectedSector >= static_cast<int>(level.sectors.size())) {
            editingSector = false;
            selectedSector = -1;
            return;
        }

        Sector& sector = level.sectors[selectedSector];

        const bool deleteRequested =
            ImGuiDrawFunctions::DrawSectorEditor(sector, &editingSector, selectedSector);

        if (deleteRequested) {
            level.sectors.erase(level.sectors.begin() + selectedSector);

            editingSector = false;
            selectedSector = -1;
        }
    }

    void DrawSelectedWallInspector(Level& level) {
        if (!editingWall || currentMode != MODE_WALL) {
            return;
        }

        if (selectedWall < 0 || selectedWall >= static_cast<int>(level.walls.size())) {
            editingWall = false;
            selectedWall = -1;
            return;
        }

        Wall& wall = level.walls[selectedWall];

        const bool deleteRequested =
            ImGuiDrawFunctions::DrawWallEditor(wall, &editingWall, selectedWall);

        if (deleteRequested) {
            level.walls.erase(level.walls.begin() + selectedWall);

            editingWall = false;
            selectedWall = -1;
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
            ImGuiDrawFunctions::DrawEntityEditor(entity, entityInspectorState, &editingEntity);

        editingComponent = entityInspectorState.editingComponent;

        if (deleteRequested) {
            const EntityID idToDelete = entity.id;

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
                &entityInspectorState.editingComponent
            );

            editingComponent = entityInspectorState.editingComponent;
        }
    }

    void DrawSelectionInspectors(Level& level) {
        DrawSelectedSectorInspector(level);
        DrawSelectedWallInspector(level);
        DrawSelectedEntityInspector(level);
    }
}

namespace MapEditorInternal {
    using namespace Localisation;

    void ChangeMode() {
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
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
        selectedSector = -1;

        editingWall = false;
        selectedWall = -1;

        editingEntity = false;

        creatableSector = false;
        sectorBeingCreated.clear();
        actions.clear();

        spdlog::info("Processing queued level load: {}", levelToLoad);

        Editor::LoadLevel(levelToLoad);

        return true;
    }

    void DrawEditorUI() {
        DrawDockSpace();

        Level& level = LevelManager::CurrentLevel();

        ImGui::Begin(Get("editor.title").c_str());

        DrawMode();

        DrawSelectionInspectors(level);

        if (creatableSector) {
            if (ImGui::Button(Get("editor.create_sector").c_str())) {
                if (sectorBeingCreated.size() >= 3) {
                    if (!SamePoint(sectorBeingCreated.front(), sectorBeingCreated.back())) {
                        sectorBeingCreated.push_back(sectorBeingCreated.front());
                    }

                    FinishSectorSelection();
                    actions.push_back(ACTION_CREATE_SECTOR);

                    creatableSector = false;
                }
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

        ImGuiDrawFunctions::PutSpace(3);

        if (ImGui::Button(Get("editor.shutdown").c_str())) {
            shutdown = true;
            quit = true;
        }

        static char buf[64] = "";

        if (buf[0] == '\0' && !Editor::currentMap.empty()) {
            std::strncpy(buf, Editor::currentMap.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        }

        if (ImGui::InputText(Get("editor.level_name").c_str(), buf, IM_ARRAYSIZE(buf))) {
            Editor::currentMap = buf;
        }

        if (ImGui::Button(Get("editor.switch_to_ui").c_str())) {
            currentState = STATE_UI;
        }

        if (ImGui::Button(Get("editor.export").c_str())) {
            RunExporter();
        }

        ImGui::End();

        DrawLevelsMenu();
        DrawWorldSettings();
    }
}