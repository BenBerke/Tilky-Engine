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

// =============================================================================
//  Style helpers
// =============================================================================

static void PushDangerStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.15f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.10f, 0.10f, 1.00f));
}
static void PopDangerStyle()  { ImGui::PopStyleColor(3); }

static void PushSuccessStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.45f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.58f, 0.26f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.10f, 0.35f, 0.15f, 1.00f));
}
static void PopSuccessStyle() { ImGui::PopStyleColor(3); }

static void PushAccentStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.36f, 0.62f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.46f, 0.78f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.13f, 0.28f, 0.52f, 1.00f));
}
static void PopAccentStyle()  { ImGui::PopStyleColor(3); }

// Coloured ">> Label" used as a lightweight section heading.
static void SectionHeader(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.00f, 1.00f));
    ImGui::TextUnformatted(">>");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted(label);
}

// Tooltip shown after a short hover. Call immediately after the widget.
static void HoverTooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Button that stretches to fill the available horizontal space.
static bool FullWidthButton(const char* label) {
    return ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
}

// =============================================================================
//  Toast notification system
// =============================================================================

namespace {
    struct Toast {
        char  message[128];
        float remainingSeconds;
        bool  isError;
    };

    static Toast s_toast{};

    void ShowNotification(const char* msg, bool isError = false, float duration = 3.0f) {
        std::strncpy(s_toast.message, msg, sizeof(s_toast.message) - 1);
        s_toast.message[sizeof(s_toast.message) - 1] = '\0';
        s_toast.remainingSeconds = duration;
        s_toast.isError          = isError;
    }

    void DrawNotification(const float dt) {
        if (s_toast.remainingSeconds <= 0.0f) return;
        s_toast.remainingSeconds -= dt;

        const ImGuiViewport* vp     = ImGui::GetMainViewport();
        const float          margin = 16.0f;

        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                   vp->WorkPos.y + vp->WorkSize.y - margin),
            ImGuiCond_Always,
            ImVec2(0.5f, 1.0f)
        );
        ImGui::SetNextWindowBgAlpha(0.88f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration      |
            ImGuiWindowFlags_NoMove            |
            ImGuiWindowFlags_NoSavedSettings   |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav             |
            ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin("##Toast", nullptr, flags);

        const ImVec4 col = s_toast.isError
            ? ImVec4(1.00f, 0.45f, 0.45f, 1.00f)
            : ImVec4(0.55f, 1.00f, 0.65f, 1.00f);

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("  %s  ", s_toast.message);
        ImGui::PopStyleColor();

        ImGui::End();
    }
} // namespace

// =============================================================================
//  RunExporter — unchanged logic, adds toast feedback
// =============================================================================

static bool RunExporter() {
    const fs::path engineBasePath = ProjectManager::GetEngineBasePath();

#ifdef _WIN32
    const fs::path exporterExe  = engineBasePath / "tilky_exporter.exe";
    const fs::path standaloneExe = engineBasePath / "Standalone.exe";
#else
    const fs::path exporterExe  = engineBasePath / "tilky_exporter";
    const fs::path standaloneExe = engineBasePath / "Standalone";
#endif

    const fs::path projectMetadata = ProjectManager::GetProjectFiles();
    const fs::path exportFolder    = ProjectManager::GetDefaultExportFolder();

    auto fail = [](const char* msg, const std::string& detail = "") -> bool {
        spdlog::error("{}{}", msg, detail.empty() ? "" : ": " + detail);
        ShowNotification(msg, /*isError=*/true);
        return false;
    };

    if (!fs::exists(exporterExe))
        return fail("Export failed: exporter not found.");
    if (!fs::exists(standaloneExe))
        return fail("Export failed: Standalone not found.");
    if (!fs::exists(projectMetadata))
        return fail("Export failed: project metadata missing.");

    try { fs::create_directories(exportFolder); }
    catch (const std::exception& e) { return fail("Export failed: can't create output folder.", e.what()); }

#ifdef _WIN32
    std::wstring commandLine =
        L"\"" + exporterExe.wstring()   + L"\" " +
        L"\"" + projectMetadata.wstring() + L"\" " +
        L"\"" + exportFolder.wstring()   + L"\" " +
        L"\"" + standaloneExe.wstring()  + L"\"";

    STARTUPINFOW     startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    spdlog::info("Running exporter to {}", exportFolder.string());

    if (!CreateProcessW(exporterExe.wstring().c_str(), commandLine.data(),
                        nullptr, nullptr, FALSE, 0, nullptr,
                        engineBasePath.wstring().c_str(),
                        &startupInfo, &processInfo))
        return fail("Export failed: could not launch process.");

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    if (exitCode != 0) return fail("Export failed. Check logs.");

#else
    spdlog::info("Running exporter to {}", exportFolder.string());

    const pid_t pid = fork();
    if (pid < 0)  return fail("Export failed: fork error.");

    if (pid == 0) {
        execl(exporterExe.c_str(), exporterExe.c_str(),
              projectMetadata.c_str(), exportFolder.c_str(),
              standaloneExe.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)    return fail("Export failed: wait error.");
    if (!WIFEXITED(status))               return fail("Export failed: abnormal exit.");

    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0)                    return fail("Export failed. Check logs.");
#endif

    spdlog::info("Export completed successfully to {}", exportFolder.string());
    ShowNotification("Export complete!");
    return true;
}

// =============================================================================
//  Editor::RefreshLevelSoundsFromFolder — unchanged
// =============================================================================

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
            if (!entry.is_regular_file()) continue;

            const std::filesystem::path path = entry.path();
            std::string ext = path.extension().string();

            std::ranges::transform(ext, ext.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (ext != ".wav") continue;

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

// =============================================================================
//  Anonymous namespace — all private UI state and drawing
// =============================================================================

namespace {
    constexpr bool DRAGGABLE = false;

    using namespace Localisation;
    using namespace MapEditorInternal;

    ImGuiDrawFunctions::EntityInspectorState entityInspectorState;

    std::optional<std::string> pendingLevelToLoad;

    // Feature #1: Project Settings overlay
    bool projectSettingsOpen       = false;

    // Feature #3: Create Level modal gate
    bool createLevelModalRequested = false;

    // Confirmation guards
    bool        shutdownConfirmOpen    = false;
    bool        deleteLevelConfirmOpen = false;
    std::string deleteLevelPending;

    // Hierarchy search
    static char hierarchySearchBuf[64] = "";

    // Unsaved-changes flag — set on any edit, cleared on Save / new level
    bool hasUnsavedChanges = false;

    // =========================================================================
    //  Utility
    // =========================================================================

    Entity* FindEntityById(Level& level, const ID entityId) {
        for (Entity& entity : level.entities)
            if (entity.id == entityId) return &entity;
        return nullptr;
    }

    void ResetInspectorState() {
        entityInspectorState = {};
        editingComponent     = false;
    }

    // =========================================================================
    //  CreateNewLevel — defined early so DrawCreateLevelModal can call it
    // =========================================================================

    void CreateNewLevel(const std::string& levelName) {
        if (!Editor::currentMap.empty()) Save(Editor::currentMap);

        Level& level  = LevelManager::CurrentLevel();
        level         = Level{};
        level.name    = levelName;

        level.nextEntityID = 1;
        level.nextSectorID = 0;
        level.nextWallID   = 0;

        sectorBeingCreated.clear();
        pendingSectorParams = PendingSectorParams{};

        dots.clear();
        dotIDToIndex.clear();
        nextDotID     = 0;
        selectedDotID = INVALID_ID;

        editingSector    = false;
        selectedSectorID = INVALID_ID;

        editingEntity = false;
        ResetInspectorState();

        actions.clear();

        Editor::currentMap = levelName;

        // Re-scan the shared asset folders (mirrors what Editor::Start() does).
        EditorTextureCache::RefreshLevelTexturesFromFolder();
        Editor::RefreshLevelSoundsFromFolder();

        MapQueries::RebuildSectorRuntimeLinks(level);

        hasUnsavedChanges = false;

        spdlog::info("Created new level: {}", levelName);
    }

    // Small rounded count badge rendered inline (SameLine after the header).
    void DrawCountBadge(const int count) {
        if (count <= 0) return;
        char buf[16];
        std::snprintf(buf, sizeof(buf), " %d ", count);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.38f, 0.60f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.38f, 0.60f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f, 0.38f, 0.60f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.92f, 1.00f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(4.0f, 1.0f));
        ImGui::SmallButton(buf);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }

    // =========================================================================
    //  DrawDockSpace
    // =========================================================================

    void DrawDockSpace() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDocking          |
            ImGuiWindowFlags_NoTitleBar          |
            ImGuiWindowFlags_NoCollapse          |
            ImGuiWindowFlags_NoResize            |
            ImGuiWindowFlags_NoMove              |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus          |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0.0f, 0.0f));

        ImGui::Begin("Dockspace##MainDockspaceHost", nullptr, flags);
        ImGui::PopStyleVar(3);

        ImGui::DockSpace(
            ImGui::GetID("MainDockspace"),
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_PassthruCentralNode
        );

        ImGui::End();
    }

    // =========================================================================
    //  Texture thumbnail row
    // =========================================================================

    void DrawTextureThumbnailRow(const Level& level, const int textureIndex) {
        constexpr float thumb = 32.0f;

        if (textureIndex < 0 || textureIndex >= static_cast<int>(level.textures.size())) {
            ImGui::Dummy(ImVec2(thumb, thumb));
            ImGui::SameLine();
            ImGui::TextDisabled("(none)");
            return;
        }

        SDL_Texture* texture = GetEditorTexture(textureIndex);

        if (texture) {
            ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(thumb, thumb));
        }
        else {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(thumb, thumb));

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(cursor, ImVec2(cursor.x + thumb, cursor.y + thumb), IM_COL32(90, 90, 90, 255));
            dl->AddRect(      cursor, ImVec2(cursor.x + thumb, cursor.y + thumb), IM_COL32(150, 150, 150, 255));
        }

        ImGui::SameLine(0.0f, 8.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (thumb - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::Text("[%d]  %s", textureIndex, level.textures[textureIndex].fileName.c_str());
    }

    // =========================================================================
    //  Asset panels
    // =========================================================================

    void DrawTextureCategory() {
        const Level& level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_textures").c_str())) {
            EditorTextureCache::RefreshLevelTexturesFromFolder();
            ShowNotification("Textures refreshed.");
        }
        HoverTooltip("Scan the project Textures folder and reload the list.");

        ImGui::Spacing();

        if (level.textures.empty()) {
            ImGui::TextDisabled("No textures found — add files to the Textures folder.");
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

        for (int i = 0; i < static_cast<int>(level.textures.size()); i++) {
            ImGui::PushID(i);
            DrawTextureThumbnailRow(level, i);
            ImGui::PopID();
        }

        ImGui::PopStyleVar();
    }

    void DrawSoundCategory() {
        const Level& level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_sounds").c_str())) {
            Editor::RefreshLevelSoundsFromFolder();
            ShowNotification("Sounds refreshed.");
        }
        HoverTooltip("Scan the project Sounds folder for .wav files.");

        ImGui::Spacing();

        if (level.sounds.empty()) {
            ImGui::TextDisabled("No sounds found — add .wav files to the Sounds folder.");
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));

        for (int i = 0; i < static_cast<int>(level.sounds.size()); i++) {
            ImGui::PushID(i);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.90f, 0.55f, 1.00f));
            ImGui::TextUnformatted("*");
            ImGui::PopStyleColor();

            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text("[%d]  %s", i, level.sounds[i].fileName.c_str());

            ImGui::PopID();
        }

        ImGui::PopStyleVar();
    }

    // =========================================================================
    //  World Settings panel
    // =========================================================================

    void DrawWorldSettings() {
        Level&            level    = LevelManager::CurrentLevel();
        ListenerSettings& settings = level.listenerSettings;

        ImGui::Begin(Get("editor.world_settings").c_str());

        // ---- Audio --------------------------------------------------------
        SectionHeader(Get("settings.audio.title").c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SliderFloat(
            Get("settings.audio.master_gain").c_str(),
            &settings.masterGain, 0.0f, 2.0f, "%.2f"
        );
        HoverTooltip("Overall audio volume for this level. 1.0 = neutral.");

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Get("settings.audio.global_physics_header").c_str());
        ImGui::Spacing();

        if (ImGui::DragFloat(
            Get("settings.audio.doppler_factor").c_str(),
            &settings.dopplerFactor, 0.01f, 0.0f, 10.0f, "%.2f"
        )) {
            // SoundManager::SetListenerDopplerFactor(settings.dopplerFactor);
        }
        HoverTooltip("Scales the Doppler effect. 0 = disabled, 1 = physically accurate.");

        if (ImGui::InputFloat(
            Get("settings.audio.speed_of_sound").c_str(),
            &settings.speedOfSound, 1.0f, 50.0f, "%.1f"
        )) {
            if (settings.speedOfSound < 1.0f) {
                settings.speedOfSound = 1.0f;
                ShowNotification("Speed of sound clamped to 1.", /*isError=*/true);
            }
            // SoundManager::SetListenerSpeedOfSound(settings.speedOfSound);
        }
        HoverTooltip("Speed of sound (units/s). Real-world air ~343 m/s.  Clamped >= 1.");

        ImGui::Spacing();

        // Distance model combo
        {
            const std::array<std::string, 5> modelLabels = {
                Get("settings.audio.distance_model.inverse"),
                Get("settings.audio.distance_model.inverse_clamped"),
                Get("settings.audio.distance_model.linear"),
                Get("settings.audio.distance_model.linear_clamped"),
                Get("settings.audio.distance_model.none")
            };

            const char* models[] = {
                modelLabels[0].c_str(), modelLabels[1].c_str(),
                modelLabels[2].c_str(), modelLabels[3].c_str(),
                modelLabels[4].c_str()
            };

            const ALenum alModels[] = {
                AL_INVERSE_DISTANCE,
                AL_INVERSE_DISTANCE_CLAMPED,
                AL_LINEAR_DISTANCE,
                AL_LINEAR_DISTANCE_CLAMPED,
                AL_NONE
            };

            auto DistanceModelToIndex = [](ALenum model) -> int {
                switch (model) {
                    case AL_INVERSE_DISTANCE:         return 0;
                    case AL_INVERSE_DISTANCE_CLAMPED: return 1;
                    case AL_LINEAR_DISTANCE:          return 2;
                    case AL_LINEAR_DISTANCE_CLAMPED:  return 3;
                    case AL_NONE:                     return 4;
                    default:                          return 1;
                }
            };

            int currentModel = DistanceModelToIndex(settings.distanceModel);

            if (ImGui::Combo(
                Get("settings.audio.distance_model").c_str(),
                &currentModel, models, IM_ARRAYSIZE(models)
            )) {
                settings.distanceModel = alModels[currentModel];
                // SoundManager::SetListenerDistanceModel(settings.distanceModel);
            }

            HoverTooltip(
                "OpenAL distance attenuation model.\n"
                "Inverse Clamped is the standard choice for most games.\n"
                "None = no distance attenuation at all."
            );
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Physics ------------------------------------------------------
        SectionHeader(Get("settings.physics.title").c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::InputFloat(
            Get("settings.physics.gravity").c_str(),
            &level.worldSettings.gravity, 0.1f, 1.0f, "%.2f"
        );
        HoverTooltip("Gravitational acceleration (units/s^2).  Negative = downward.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Rendering ----------------------------------------------------
        SectionHeader("Rendering");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::InputInt(Get("editor.background_texture").c_str(), &Editor::backgroundTextureIndex);
        HoverTooltip("Texture index used for the skybox / background.  -1 = none.");

        ImGui::Spacing();

        // Thumbnail of the current background texture
        DrawTextureThumbnailRow(LevelManager::CurrentLevel(), Editor::backgroundTextureIndex);

        ImGui::End();
    }

    // =========================================================================
    //  Level list — drawn as a scrollable ChildWindow inside the Project
    //  Settings panel so it doesn't spawn its own floating window.
    // =========================================================================

    void DrawLevelsChildPanel() {
        const float childHeight = std::min(
            static_cast<float>(Editor::maps.size()) * 28.0f + 8.0f,
            180.0f
        );

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 1.00f));
        ImGui::BeginChild("##LevelsList", ImVec2(0.0f, childHeight), /*border=*/true);

        if (Editor::maps.empty()) {
            ImGui::TextDisabled("  No levels found.");
        }

        for (int i = 0; i < static_cast<int>(Editor::maps.size()); ++i) {
            ImGui::PushID(i);

            const std::string& name      = Editor::maps[i];
            const bool         isCurrent = (Editor::currentMap == name);

            // Accent the active level name
            if (isCurrent)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.00f, 1.00f));

            const float available   = ImGui::GetContentRegionAvail().x;
            const float buttonWidth = 52.0f;

            ImGui::AlignTextToFramePadding();
            ImGui::Text("  %s%s", isCurrent ? "* " : "  ", name.c_str());

            if (isCurrent) ImGui::PopStyleColor();

            ImGui::SameLine(available - buttonWidth * 2.0f - 4.0f);

            PushAccentStyle();
            if (ImGui::Button("Load", ImVec2(buttonWidth, 0.0f))) {
                if (!Editor::currentMap.empty()) Save(Editor::currentMap);
                QueueLevelLoad(name);
                spdlog::info("Queued level load: {}", name);
            }
            PopAccentStyle();
            HoverTooltip("Save the current level and load this one.");

            ImGui::SameLine();

            PushDangerStyle();
            if (ImGui::Button("Del", ImVec2(buttonWidth, 0.0f))) {
                deleteLevelPending     = name;
                deleteLevelConfirmOpen = true;
            }
            PopDangerStyle();
            HoverTooltip("Delete this level file from disk. Cannot be undone.");

            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Delete confirmation modal
        if (deleteLevelConfirmOpen) {
            ImGui::OpenPopup("##DeleteLevelConfirm");
            deleteLevelConfirmOpen = false;
        }

        if (ImGui::BeginPopupModal("##DeleteLevelConfirm", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete  \"%s\"?", deleteLevelPending.c_str());
            ImGui::TextDisabled("This cannot be undone.");
            ImGui::Spacing();

            PushDangerStyle();
            if (ImGui::Button("Delete", ImVec2(90.0f, 0.0f))) {
                const std::filesystem::path path =
                    ProjectManager::GetLevelsPath() / (deleteLevelPending + ".bson");

                try {
                    if (std::filesystem::remove(path)) {
                        spdlog::info("Deleted level: {}", path.string());
                        if (Editor::currentMap == deleteLevelPending) Editor::currentMap = "";
                        UpdateLevels();
                        ShowNotification("Level deleted.");
                    }
                    else {
                        spdlog::error("File not found: {}", path.string());
                        ShowNotification("Delete failed: file not found.", /*isError=*/true);
                    }
                }
                catch (const std::filesystem::filesystem_error& e) {
                    spdlog::error("Failed to delete level: {}", e.what());
                    ShowNotification("Delete failed. Check logs.", /*isError=*/true);
                }

                deleteLevelPending.clear();
                ImGui::CloseCurrentPopup();
            }
            PopDangerStyle();

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
                deleteLevelPending.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // =========================================================================
    //  Create Level modal
    // =========================================================================

    void DrawCreateLevelModal() {
        static char newLevelNameBuf[64] = "";
        constexpr const char* kPopupID  = "Create Level";

        if (createLevelModalRequested) {
            ImGui::OpenPopup(kPopupID);
            newLevelNameBuf[0]        = '\0';
            createLevelModalRequested = false;
        }

        if (ImGui::BeginPopupModal(kPopupID, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Get("editor.create_level_prompt").c_str());
            ImGui::Spacing();

            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##NewLevelName", newLevelNameBuf, IM_ARRAYSIZE(newLevelNameBuf));

            const bool nameValid = newLevelNameBuf[0] != '\0';

            if (!nameValid) {
                ImGui::SameLine();
                ImGui::TextDisabled(" Name required");
            }

            ImGui::Spacing();
            ImGui::BeginDisabled(!nameValid);

            PushAccentStyle();
            if (ImGui::Button(Get("editor.create").c_str(), ImVec2(100.0f, 0.0f))) {
                CreateNewLevel(newLevelNameBuf);
                UpdateLevels();
                ShowNotification("Level created.");
                hasUnsavedChanges = false;
                ImGui::CloseCurrentPopup();
            }
            PopAccentStyle();

            ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::Button(Get("editor.cancel").c_str(), ImVec2(80.0f, 0.0f)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    // =========================================================================
    //  Project Settings window (Feature #1)
    // =========================================================================

    void DrawProjectSettingsWindow() {
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);

        const bool windowVisible =
                ImGui::Begin(Get("editor.project_settings").c_str(), &projectSettingsOpen);

        if (windowVisible) {
            // ---- Current level name -------------------------------------------
            SectionHeader("Current Level");
            ImGui::Separator();
            ImGui::Spacing();

            static char levelNameBuf[64] = "";
            static std::string lastSyncedMap;

            if (lastSyncedMap != Editor::currentMap) {
                std::strncpy(levelNameBuf, Editor::currentMap.c_str(), sizeof(levelNameBuf) - 1);
                levelNameBuf[sizeof(levelNameBuf) - 1] = '\0';
                lastSyncedMap = Editor::currentMap;
            }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

            if (ImGui::InputText(
                ("##" + Get("editor.level_name")).c_str(),
                levelNameBuf,
                IM_ARRAYSIZE(levelNameBuf)
            )) {
                Editor::currentMap = levelNameBuf;
                lastSyncedMap = Editor::currentMap;
                hasUnsavedChanges = true;
            }

            HoverTooltip("Name used when saving this level to disk.");

            ImGui::Spacing();

            if (FullWidthButton(Get("editor.create_level").c_str())) {
                createLevelModalRequested = true;
            }

            HoverTooltip("Create a new blank level (saves the current one first).");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Level list ---------------------------------------------------
            SectionHeader("All Levels");
            ImGui::Separator();
            ImGui::Spacing();

            DrawLevelsChildPanel();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Build --------------------------------------------------------
            SectionHeader("Build");
            ImGui::Separator();
            ImGui::Spacing();

            PushSuccessStyle();

            if (FullWidthButton(Get("editor.export").c_str())) {
                RunExporter();
            }

            PopSuccessStyle();

            HoverTooltip("Package the project into a standalone executable.");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Application --------------------------------------------------
            SectionHeader("Application");
            ImGui::Separator();
            ImGui::Spacing();

            PushDangerStyle();

            if (FullWidthButton(Get("editor.shutdown").c_str())) {
                shutdownConfirmOpen = true;
            }

            PopDangerStyle();

            HoverTooltip("Exit the editor. Unsaved changes will be lost.");
        }

        ImGui::End();

        // ---- Shutdown confirmation modal --------------------------------------
        if (shutdownConfirmOpen) {
            ImGui::OpenPopup("##ShutdownConfirm");
            shutdownConfirmOpen = false;
        }

        if (ImGui::BeginPopupModal(
            "##ShutdownConfirm",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        )) {
            ImGui::Text("Exit the editor?");

            if (hasUnsavedChanges) {
                ImGui::Spacing();
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(1.00f, 0.65f, 0.30f, 1.00f)
                );
                ImGui::Text("You have unsaved changes!");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            PushDangerStyle();

            if (ImGui::Button("Exit", ImVec2(80.0f, 0.0f))) {
                MapEditorInternal::shutdown = true;
                quit = true;
                ImGui::CloseCurrentPopup();
            }

            PopDangerStyle();

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        DrawCreateLevelModal();
    }

    // =========================================================================
    //  Project Settings floating button (bottom-right anchor)
    // =========================================================================

    void DrawProjectSettingsButton() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float      margin   = 12.0f;

        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin,
                   viewport->WorkPos.y + viewport->WorkSize.y - margin),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f)
        );

        ImGui::SetNextWindowBgAlpha(0.0f);

        constexpr ImGuiWindowFlags overlayFlags =
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_NoMove             |
            ImGuiWindowFlags_NoSavedSettings    |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin("##ProjectSettingsButtonOverlay", nullptr, overlayFlags);

        const bool wasOpen = projectSettingsOpen;

        if (wasOpen) {
            PushAccentStyle();
        }

        if (ImGui::Button(
            wasOpen ? "[ Project Settings ]" : Get("editor.project_settings").c_str(),
            ImVec2(180.0f, 0.0f)
        )) {
            projectSettingsOpen = !projectSettingsOpen;
        }

        if (wasOpen) {
            PopAccentStyle();
        }

        ImGui::End();

        if (projectSettingsOpen) {
            DrawProjectSettingsWindow();
        }
    }

    // =========================================================================
    //  Hierarchy panel (Feature #10) — search, type-coloured icons, counts,
    //  deferred deletion, copy-ID context menu
    // =========================================================================

    void DrawHierarchyPanel(Level& level) {
        ImGui::Begin(Get("editor.hierarchy").c_str());

        // Search bar
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
        ImGui::InputText("##HierarchySearch", hierarchySearchBuf, sizeof(hierarchySearchBuf));
        HoverTooltip("Filter hierarchy by name or ID.");

        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::SmallButton("x")) hierarchySearchBuf[0] = '\0';
        HoverTooltip("Clear filter.");

        const std::string searchLower = [&] {
            std::string s = hierarchySearchBuf;
            std::ranges::transform(s, s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }();
        const bool filtering = !searchLower.empty();

        ImGui::Spacing();

        // Helper: case-insensitive substring match for filtering
        auto matches = [&](const std::string& label) -> bool {
            if (!filtering) return true;
            std::string lower = label;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lower.find(searchLower) != std::string::npos;
        };

        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);

        // ---- Sectors ------------------------------------------------------
        {
            const bool open = ImGui::CollapsingHeader(
                Get("editor.hierarchy.sectors").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen
            );
            DrawCountBadge(static_cast<int>(level.sectors.size()));

            if (open) {
                ImGui::Indent();

                ID sectorPendingDelete = INVALID_ID;

                for (const Sector& sector : level.sectors) {
                    const std::string label = "Sector #" + std::to_string(sector.id);
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(sector.id));

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.85f, 1.00f, 1.00f));
                    ImGui::TextUnformatted("[S]");
                    ImGui::PopStyleColor();

                    ImGui::SameLine(0.0f, 6.0f);

                    const bool selected = (selectedSectorID == sector.id);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        selectedSectorID = sector.id;
                        editingSector    = true;
                        currentMode      = MODE_SECTOR;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            sectorPendingDelete = sector.id;
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();

                if (sectorPendingDelete != INVALID_ID) {
                    DeleteSector(sectorPendingDelete);
                    hasUnsavedChanges = true;
                }
            }
        }

        // ---- Entities -----------------------------------------------------
        {
            const bool open = ImGui::CollapsingHeader(
                Get("editor.hierarchy.entities").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen
            );
            DrawCountBadge(static_cast<int>(level.entities.size()));

            if (open) {
                ImGui::Indent();

                ID entityPendingDelete = INVALID_ID;

                for (const Entity& entity : level.entities) {
                    const std::string label =
                        entity.name + "  (#" + std::to_string(entity.id) + ")";
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(entity.id));

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 1.00f, 0.70f, 1.00f));
                    ImGui::TextUnformatted("[E]");
                    ImGui::PopStyleColor();

                    ImGui::SameLine(0.0f, 6.0f);

                    const bool selected = editingEntity && (selectedEntity.id == entity.id);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        selectedEntity = entity;
                        editingEntity  = true;
                        currentMode    = MODE_ENTITY;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            entityPendingDelete = entity.id;

                        ImGui::Separator();

                        if (ImGui::MenuItem("Copy ID")) {
                            char buf[32];
                            std::snprintf(buf, sizeof(buf), "%u",
                                static_cast<unsigned>(entity.id));
                            ImGui::SetClipboardText(buf);
                            ShowNotification("Entity ID copied to clipboard.");
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();

                if (entityPendingDelete != INVALID_ID) {
                    if (editingEntity && selectedEntity.id == entityPendingDelete) {
                        editingEntity = false;
                        ResetInspectorState();
                    }
                    level.DestroyEntity(entityPendingDelete);
                    hasUnsavedChanges = true;
                }
            }
        }

        // ---- Dots ---------------------------------------------------------
        {
            const bool open = ImGui::CollapsingHeader(
                Get("editor.hierarchy.dots").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen
            );
            DrawCountBadge(static_cast<int>(dots.size()));

            if (open) {
                ImGui::Indent();

                ID dotPendingDelete = INVALID_ID;

                for (const Dot& dot : dots) {
                    const std::string label =
                        "Dot #" + std::to_string(dot.id) +
                        "  (" + std::to_string(static_cast<int>(dot.position.x)) +
                        ", " + std::to_string(static_cast<int>(dot.position.y)) + ")";
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(dot.id));

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.90f, 0.50f, 1.00f));
                    ImGui::TextUnformatted("[D]");
                    ImGui::PopStyleColor();

                    ImGui::SameLine(0.0f, 6.0f);

                    const bool selected = (selectedDotID == dot.id);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        selectedDotID = dot.id;
                        currentMode   = MODE_DOT;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            dotPendingDelete = dot.id;
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();

                if (dotPendingDelete != INVALID_ID)
                    RemoveDot(dotPendingDelete);
            }
        }

        ImGui::PopStyleVar();
        ImGui::End();
    }

    // =========================================================================
    //  Mode switcher — three inline toggle buttons
    // =========================================================================

    void DrawMode() {
        struct ModeEntry { int id; const char* label; };
        constexpr ModeEntry modes[] = {
            { MODE_DOT,    "Dot Mode"    },
            { MODE_SECTOR, "Sector Mode" },
            { MODE_ENTITY, "Entity Mode" },
        };

        const float buttonWidth = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;

        for (int i = 0; i < 3; ++i) {
            const bool active = (currentMode == modes[i].id);

            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.36f, 0.62f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.46f, 0.78f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.22f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.32f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.60f, 0.68f, 0.82f, 1.00f));
            }

            if (ImGui::Button(modes[i].label, ImVec2(buttonWidth, 0.0f)))
                if (currentMode != modes[i].id) ChangeMode();

            ImGui::PopStyleColor(3);
            if (i < 2) ImGui::SameLine(0.0f, 4.0f);
        }

        // In-progress sector chain reminder
        if (currentMode == MODE_SECTOR && !sectorBeingCreated.empty()) {
            const int n = static_cast<int>(sectorBeingCreated.size());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.80f, 0.30f, 1.00f));
            ImGui::Text("  Placing sector... (%d dot%s)", n, n == 1 ? "" : "s");
            ImGui::TextDisabled("  Right-click to close or cancel.");
            ImGui::PopStyleColor();
        }
    }

    // =========================================================================
    //  Selection inspectors
    // =========================================================================

    void DrawSelectedSectorInspector(Level& level) {
        if (!editingSector || currentMode != MODE_SECTOR) return;

        const auto it = level.sectorIDToIndex.find(selectedSectorID);
        if (it == level.sectorIDToIndex.end()) {
            selectedSectorID = INVALID_ID;
            editingSector    = false;
            return;
        }

        Sector& sector = level.sectors[it->second];

        if (ImGuiDrawFunctions::DrawSectorEditor(sector, &editingSector, it->second, DRAGGABLE)) {
            DeleteSector(selectedSectorID);
            hasUnsavedChanges = true;
        }
    }

    void DrawSelectedEntityInspector(Level& level) {
        if (!editingEntity || currentMode != MODE_ENTITY) return;

        Entity* entityPtr = FindEntityById(level, selectedEntity.id);
        if (!entityPtr) {
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
            editingEntity       = false;
            ResetInspectorState();
            level.DestroyEntity(idToDelete);
            hasUnsavedChanges = true;
            return;
        }

        if (!editingEntity) { ResetInspectorState(); return; }

        if (entityInspectorState.editingComponent &&
            entityInspectorState.selectedComponent != -1)
        {
            ImGuiDrawFunctions::DrawComponentEditor(
                entity, entityInspectorState,
                &entityInspectorState.editingComponent, DRAGGABLE
            );
            editingComponent = entityInspectorState.editingComponent;
        }
    }

    // Wall Mode is gone (feature #4).
    void DrawSelectionInspectors(Level& level) {
        DrawSelectedSectorInspector(level);
        DrawSelectedEntityInspector(level);
    }

} // anonymous namespace

// =============================================================================
//  MapEditorInternal — public interface
// =============================================================================

namespace MapEditorInternal {
    using namespace Localisation;

    void ChangeMode() {
        const Mode previousMode = currentMode;
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);

        // Centralized so the Q hotkey and the UI button both clean up the chain.
        if (previousMode == MODE_SECTOR) CancelSectorChain();
    }

    // Feature #2: polished light theme (unchanged colour values from the
    // "pretty" reference).
    void ApplyEditorTheme(const EditorTheme theme) {
        if (theme == THEME_DARK) { ImGui::StyleColorsDark(); return; }

        ImGui::StyleColorsLight();

        ImGuiStyle& style  = ImGui::GetStyle();
        ImVec4*     colors = style.Colors;

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
        if (!pendingLevelToLoad.has_value()) return false;

        const std::string levelToLoad = *pendingLevelToLoad;
        pendingLevelToLoad.reset();

        ResetInspectorState();

        editingSector    = false;
        selectedSectorID = INVALID_ID;
        editingEntity    = false;

        sectorBeingCreated.clear();
        pendingSectorParams = PendingSectorParams{};
        actions.clear();

        dots.clear();
        dotIDToIndex.clear();
        nextDotID     = 0;
        selectedDotID = INVALID_ID;

        hasUnsavedChanges = false;

        spdlog::info("Processing queued level load: {}", levelToLoad);
        Editor::LoadLevel(levelToLoad);

        return true;
    }

    // =========================================================================
    //  DrawEditorUI — main panel
    // =========================================================================

    void DrawEditorUI() {
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);

        DrawDockSpace();

        Level& level = LevelManager::CurrentLevel();

        // Append * to the title bar when the level has unsaved changes
        std::string panelTitle = Get("editor.title");
        if (hasUnsavedChanges) panelTitle += "  *";

        ImGui::Begin(panelTitle.c_str());

        // ---- Mode ---------------------------------------------------------
        SectionHeader("Mode");
        ImGui::Spacing();
        DrawMode();
        ImGui::Spacing();

        // ---- Theme / view toggles ----------------------------------------
        ImGui::Separator();
        ImGui::Spacing();

        bool lightModeActive = (currentTheme == THEME_LIGHT);
        if (ImGui::Checkbox(Get("editor.light_mode").c_str(), &lightModeActive)) {
            currentTheme = lightModeActive ? THEME_LIGHT : THEME_DARK;
            ApplyEditorTheme(currentTheme);
        }
        HoverTooltip("Toggle dark / light theme.");

        ImGui::SameLine(0.0f, 16.0f);

        ImGui::Checkbox(Get("editor.texture_view_mode").c_str(), &textureViewMode);
        HoverTooltip("Overlay textures in the map viewport.");

        // ---- Sector creation params (only visible in Sector mode) ---------
        // ---- Sector creation params (only visible in Sector mode) ---------
        if (currentMode == MODE_SECTOR) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            SectionHeader("New Sector Properties");
            ImGui::Separator();
            ImGui::Spacing();

            // Texture indices with inline thumbnail previews
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("Wall Tex", &wallTextureIndex)) hasUnsavedChanges = true;
            ImGui::SameLine(0.0f, 8.0f);
            DrawTextureThumbnailRow(level, wallTextureIndex);

            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("Ceil Tex", &ceilTextureIndex)) hasUnsavedChanges = true;
            ImGui::SameLine(0.0f, 8.0f);
            DrawTextureThumbnailRow(level, ceilTextureIndex);

            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("Floor Tex", &floorTextureIndex)) hasUnsavedChanges = true;
            ImGui::SameLine(0.0f, 8.0f);
            DrawTextureThumbnailRow(level, floorTextureIndex);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float iw = 110.0f;
            ImGui::SetNextItemWidth(iw);
            if (ImGui::InputFloat("Floor H",   &floorHeight, 1.0f, 10.0f, "%.2f")) hasUnsavedChanges = true;
            HoverTooltip("Floor plane height for new sectors.");

            ImGui::SetNextItemWidth(iw);
            if (ImGui::InputFloat("Ceil H",    &ceilHeight,  1.0f, 10.0f, "%.2f")) hasUnsavedChanges = true;
            HoverTooltip("Ceiling plane height for new sectors.");

            ImGui::SetNextItemWidth(iw);
            if (ImGui::InputFloat("Light",     &lightValue,  1.0f, 10.0f, "%.2f")) hasUnsavedChanges = true;
            HoverTooltip("Sector light level (0 = fully dark, 1 = full bright).");

            ImGui::Spacing();
            ImGui::ColorEdit3("Wall Color",  &wallColor.x);
            ImGui::ColorEdit3("Ceil Color",  &ceilColor.x);
            ImGui::ColorEdit3("Floor Color", &floorColor.x);
            ImGui::Spacing();

            // FIX: keep pendingSectorParams in sync with the visible fields.
            // Without this, pendingSectorParams stays at its default-constructed
            // (zeroed) value from CreateNewLevel()/ProcessPendingLevelLoad(),
            // which yields ceilingHeight <= floorHeight on every new sector.
            pendingSectorParams.wallTextureIndex  = wallTextureIndex;
            pendingSectorParams.ceilTextureIndex  = ceilTextureIndex;
            pendingSectorParams.floorTextureIndex = floorTextureIndex;
            pendingSectorParams.floorHeight       = floorHeight;
            pendingSectorParams.ceilHeight        = ceilHeight;
            pendingSectorParams.lightValue        = lightValue;
            pendingSectorParams.wallColor         = wallColor;
            pendingSectorParams.ceilColor         = ceilColor;
            pendingSectorParams.floorColor        = floorColor;
        }

        // ---- Inspector ----------------------------------------------------
        ImGui::Separator();
        ImGui::Spacing();
        DrawSelectionInspectors(level);
        ImGui::Spacing();

        // ---- Assets (collapsible) -----------------------------------------
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Sounds")) {
            ImGui::Spacing();
            DrawSoundCategory();
            ImGui::Spacing();
        }

        if (ImGui::CollapsingHeader("Textures")) {
            ImGui::Spacing();
            DrawTextureCategory();
            ImGui::Spacing();
        }

        // ---- Floor selector -----------------------------------------------
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt(Get("editor.floor").c_str(), &currentFloor);
        HoverTooltip("Active editor floor / Z-layer.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Action buttons -----------------------------------------------
        SectionHeader("Actions");
        ImGui::Spacing();

        PushAccentStyle();
        if (FullWidthButton(Get("editor.save").c_str())) {
            if (Save(Editor::currentMap)) {
                hasUnsavedChanges = false;
                ShowNotification("Level saved.");
            }
            else {
                ShowNotification("Save failed. Check logs.", /*isError=*/true);
            }
        }
        PopAccentStyle();
        HoverTooltip("Save the current level to disk.");

        ImGui::Spacing();

        if (FullWidthButton(Get("editor.runtime_editor").c_str())) {
            if (Save(Editor::currentMap)) {
                hasUnsavedChanges = false;
                SDL_Log("%s", Editor::currentMap.c_str());
                switchToRuntime = true;
            }
        }
        HoverTooltip("Save and switch to the runtime editor view.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        PushSuccessStyle();
        if (FullWidthButton(Get("editor.save_and_play").c_str())) {
            if (Save(Editor::currentMap)) {
                hasUnsavedChanges = false;
                SDL_Log("%s", Editor::currentMap.c_str());
                quit = true;
                play = true;
            }
        }
        PopSuccessStyle();
        HoverTooltip("Save and launch the game in play mode.");

        ImGui::Spacing();

        if (FullWidthButton(Get("editor.save_and_quit").c_str())) {
            if (Save(Editor::currentMap)) {
                hasUnsavedChanges = false;
                SDL_Log("%s", Editor::currentMap.c_str());
                quit = true;
            }
        }
        HoverTooltip("Save and exit the editor.");

        ImGui::Spacing();

        if (FullWidthButton(Get("editor.switch_to_ui").c_str()))
            currentState = STATE_UI;

        HoverTooltip("Switch to the UI editor state.");

        ImGui::End();

        // ---- Other panels -------------------------------------------------
        DrawWorldSettings();
        DrawHierarchyPanel(level);
        DrawProjectSettingsButton();

        // ---- Toast notification -------------------------------------------
        DrawNotification(dt);
    }

} // namespace MapEditorInternal