//
// Created by berke on 6/2/2026.
//

#include "Headers/Runtime/RuntimeSession.hpp"
#include "../EditorInternal.hpp"
#include "../TagExplorer.hpp"
#include "../Comments.hpp"
#include "../ObjExport.hpp"
#include "../CommandPalette.hpp"

#include <mutex>

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Editor/AssetBrowser.hpp"
#include "External/tracy/profiler/src/profiler/IconsFontAwesome6.h"
#include "Headers/Editor/EditorStyle.hpp"
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/TagRegistry.hpp"
#include "Headers/GlobalEventVariables.hpp"

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
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.10f, 0.10f, 1.00f));
}

static void PopDangerStyle() { ImGui::PopStyleColor(3); }

static void PushSuccessStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.58f, 0.26f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.35f, 0.15f, 1.00f));
}

static void PopSuccessStyle() { ImGui::PopStyleColor(3); }

static void PushAccentStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::AccentShade(-0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::Accent());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorStyle::AccentShade(-0.30f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
}

static void PopAccentStyle() { ImGui::PopStyleColor(4); }

// One button of a pick-one group (modes, drawing tools): the selected one in
// the accent colour, the others in the theme's normal button colours.
static void PushToggleStyle(const bool selected) {
    if (selected) {
        PushAccentStyle();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
}

static void PopToggleStyle() { ImGui::PopStyleColor(4); }

// Accent-barred label used as a lightweight section heading.
static void SectionHeader(const char *label) {
    // A short accent bar in front of the label, in the theme's accent colour.
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(start.x, start.y + 1.0f),
        ImVec2(start.x + 3.0f, start.y + height - 1.0f),
        ImGui::GetColorU32(ImGuiCol_CheckMark),
        1.5f
    );

    ImGui::Indent(10.0f);
    ImGui::TextUnformatted(label);
    ImGui::Unindent(10.0f);
}

// Grey help text that wraps to the panel instead of running off its edge.
static void HintText(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// Tooltip shown after a short hover. Call immediately after the widget.
static void HoverTooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Button that stretches to fill the available horizontal space.
static bool FullWidthButton(const char *label) {
    return ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
}

// =============================================================================
//  Toast notification system
// =============================================================================

namespace {
    // Shared with the runtime editor (ImGuiDrawFunctions.cpp).
    using ImGuiDrawFunctions::ShowNotification;
    using ImGuiDrawFunctions::DrawNotification;

    constexpr Vector3 defaultWallColor = {205, 205, 205};
    constexpr Vector3 defaultSnapIndicator = {220, 255, 255};
    constexpr Vector3 defaultNormalEntityColor = {190, 190, 190};
    constexpr Vector3 defaultHighlightedEntityColor = {20, 20, 220};
    constexpr Vector3 defaultSpriteEntityColor = {100, 200, 100};

    constexpr Vector3 defaultNormalWallColor = {205, 205, 205};
    constexpr Vector3 defaultPortalWallColor = {150, 150, 150};
    constexpr Vector3 defaultHighlightedWallColor = {80, 220, 255};

    constexpr Vector3 defaultLinedefColor = {135, 206, 250};
    constexpr Vector3 defaultLinedefEndpointColor = {135, 206, 250};

    constexpr Vector3 defaultHoveredSectorColor = {250, 250, 100};
    constexpr Vector3 defaultHighlightedSectorColor = {40, 40, 220};

    constexpr Vector3 defaultEventLinkColor = {255, 0, 0};

    constexpr Vector3 defaultKValidLineColor = {255, 220, 80};
    constexpr Vector3 defaultKInvalidLineColor = {230, 70, 70};
    constexpr Vector3 defaultKAnchorColor = {80, 220, 255};
    constexpr Vector4 defaultKValidFillColor = {1.0f, 0.863f, 0.314f, 0.28f};
    constexpr Vector4 defaultKInvalidFillColor = {0.90f, 0.27f, 0.27f, 0.28f};

    constexpr Vector3 defaultThemeTextColor = {255, 255, 255};
    constexpr Vector4 defaultGridColor = {1.0f, 1.0f, 1.0f, 0.5f};

    constexpr Vector3 defaultBackgroundColor = {45, 45, 45};

    constexpr Vector3 defaultNormalHandleColor = {80, 220, 255};
    constexpr Vector3 defaultHighlightedHandleColor = {255, 255, 255};
    constexpr Vector3 defaultHandleOutlineColor = {20, 20, 20};

    constexpr Vector3 defaultGizmoXColor = {230, 60, 60};
    constexpr Vector3 defaultGizmoYColor = {80, 200, 80};
    constexpr Vector3 defaultGizmoZColor = {60, 110, 240};

    constexpr Uint8 defaultFilterdAlpha = 60;
} // namespace

// =============================================================================
//  RunExporter - exports the game into `exportFolder`, with toast feedback
// =============================================================================

static bool RunExporter(const fs::path& exportFolder) {
    const fs::path engineBasePath = ProjectManager::GetEngineBasePath();

#ifdef _WIN32
    const fs::path exporterExe = engineBasePath / "tilky_exporter.exe";
    const fs::path standaloneExe = engineBasePath / "Standalone.exe";
#else
    const fs::path exporterExe = engineBasePath / "tilky_exporter";
    const fs::path standaloneExe = engineBasePath / "Standalone";
#endif

    const fs::path projectMetadata = ProjectManager::GetProjectFiles();

    auto fail = [](const char *logMsg, const char *userMsg, const std::string &detail = "") -> bool {
        spdlog::error("{}{}", logMsg, detail.empty() ? "" : ": " + detail);
        ShowNotification(userMsg, /*isError=*/true);
        return false;
    };

    if (!fs::exists(exporterExe))
        return fail("Export failed: exporter not found.",
                    Localisation::Get("editor.notification.export_failed_exporter_not_found").c_str());
    if (!fs::exists(standaloneExe))
        return fail("Export failed: Standalone not found.",
                    Localisation::Get("editor.notification.export_failed_standalone_not_found").c_str());
    if (!fs::exists(projectMetadata))
        return fail("Export failed: project metadata missing.",
                    Localisation::Get("editor.notification.export_failed_project_metadata_missing").c_str());

    try { fs::create_directories(exportFolder); } catch (const std::exception &e) {
        return fail("Export failed: can't create output folder.",
                    Localisation::Get("editor.notification.export_failed_create_output_folder").c_str(), e.what());
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

    if (!CreateProcessW(exporterExe.wstring().c_str(), commandLine.data(),
                        nullptr, nullptr, FALSE, 0, nullptr,
                        engineBasePath.wstring().c_str(),
                        &startupInfo, &processInfo))
        return fail("Export failed: could not launch process.",
                    Localisation::Get("editor.notification.export_failed_launch_process").c_str());

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    if (exitCode != 0) return fail("Export failed. Check logs.",
                                   Localisation::Get("editor.notification.export_failed_check_logs").c_str());

#else
    spdlog::info("Running exporter to {}", exportFolder.string());

    const pid_t pid = fork();
    if (pid < 0) return fail("Export failed: fork error.",
                             Localisation::Get("editor.notification.export_failed_fork_error").c_str());

    if (pid == 0) {
        execl(exporterExe.c_str(), exporterExe.c_str(),
              projectMetadata.c_str(), exportFolder.c_str(),
              standaloneExe.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return fail("Export failed: wait error.",
                                                  Localisation::Get("editor.notification.export_failed_wait_error").
                                                  c_str());
    if (!WIFEXITED(status)) return fail("Export failed: abnormal exit.",
                                        Localisation::Get("editor.notification.export_failed_abnormal_exit").c_str());

    const int exitCode = WEXITSTATUS(status);
    if (exitCode != 0) return fail("Export failed. Check logs.",
                                   Localisation::Get("editor.notification.export_failed_check_logs").c_str());
#endif

    spdlog::info("Export completed successfully to {}", exportFolder.string());
    ShowNotification(Localisation::Get("editor.notification.export_complete").c_str());
    return true;
}

// =============================================================================
//  Hierarchy helpers shared with the UI Editor (declared in EditorInternal.hpp)
// =============================================================================

namespace MapEditorInternal {
    // Small rounded count badge rendered inline (SameLine after the header).
    void DrawCountBadge(const int count) {
        if (count <= 0) return;
        char buf[16];
        std::snprintf(buf, sizeof(buf), " %d ", count);

        ImVec4 badge = EditorStyle::Accent();
        badge.w = 0.55f;

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, badge);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, badge);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, badge);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
        ImGui::SmallButton(buf);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }

    void DrawHierarchyTypeIcon(const char *icon, const char *fallback, const ImVec4 &color) {
        // Pastel colours read well on the dark theme but wash out on the
        // light one, so darken them when the window background is light.
        const ImVec4 &background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const bool lightTheme = background.x + background.y + background.z > 1.5f;
        const ImVec4 shown = lightTheme
                                 ? ImVec4(color.x * 0.55f, color.y * 0.55f, color.z * 0.55f, color.w)
                                 : color;

        if (editorIconFont == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, shown);
            ImGui::TextUnformatted(fallback);
            ImGui::PopStyleColor();
            return;
        }

        const float slotWidth = ImGui::GetFontSize() * 1.2f;
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 start = ImGui::GetCursorScreenPos();

        ImGui::PushFont(editorIconFont, ImGui::GetFontSize() * 0.8f);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(start.x + (slotWidth - iconSize.x) * 0.5f, start.y + (lineHeight - iconSize.y) * 0.5f),
            ImGui::GetColorU32(shown),
            icon
        );
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(slotWidth, lineHeight));
    }

    bool ClearSearchButton(const char *id) {
        ImGui::PushID(id);

        const float size = ImGui::GetFrameHeight();
        bool pressed;

        if (editorIconFont != nullptr) {
            ImGui::PushFont(editorIconFont, ImGui::GetFontSize() * 0.75f);
            pressed = ImGui::Button(ICON_FA_XMARK, ImVec2(size, size));
            ImGui::PopFont();
        }
        else pressed = ImGui::Button("x", ImVec2(size, size));

        ImGui::PopID();
        return pressed;
    }

    // Makes the hierarchy row just drawn draggable onto a matching
    // Entity/Wall/Sector script field. The row's Selectable only fires on a
    // click-release over itself, so dragging a row away never selects it
    // (and never swaps the inspector the user is dropping into).
    void HierarchyRowDragSource(const char *payloadType, const ID id, const std::string &label) {
        if (!ImGui::BeginDragDropSource()) return;

        const LevelObjectDragPayload payload{id, false};
        ImGui::SetDragDropPayload(payloadType, &payload, sizeof(payload));

        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }

    std::string SectorHierarchyLabel(const Sector &sector) {
        if (sector.name.empty())
            return Localisation::Get("editor.hierarchy.sector") + " #" + std::to_string(sector.id);
        return sector.name + "  (#" + std::to_string(sector.id) + ")";
    }

    std::string LinedefHierarchyLabel(const Linedef &linedef) {
        if (linedef.name.empty()) return "Linedef #" + std::to_string(linedef.id);
        return linedef.name + " (#" + std::to_string(linedef.id) + ")";
    }

    std::string WallHierarchyLabel(const Wall &wall) {
        if (wall.name.empty()) return "Wall #" + std::to_string(wall.id);
        return wall.name + "  (#" + std::to_string(wall.id) + ")";
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
    InspectorEditTracker entityInspectorEdit;
    GeometryInspectorEditTracker sectorInspectorEdit;
    GeometryInspectorEditTracker wallInspectorEdit;
    LinedefInspectorEditTracker linedefInspectorEdit;

    std::optional<std::string> pendingLevelToLoad;

    bool projectSettingsOpen = false;
    bool userSettingsOpen = false;

    bool layoutMenuOpen = false;
    bool engineSettingsOpen = false;

    bool createLevelModalRequested = false;

    // Confirmation guards
    bool shutdownConfirmOpen = false;
    bool deleteLevelConfirmOpen = false;
    std::string deleteLevelPending;

    // Hierarchy search
    char hierarchySearchBuf[64] = "";

    // Unsaved-changes flag — set on any edit, cleared on Save / new level.
    // Other files set it through MapEditorInternal::MarkUnsavedChanges().

    // Set when the wall inspector moves an endpoint, cleared once the
    // topology rebuild it needs has run. Drag-style widgets report a
    // change every frame they're held, and that rebuild re-derives every
    // sector in the level, so it waits for the widget to be released
    // rather than running per frame.
    bool wallGeometryDirtyFromInspector = false;

    // =========================================================================
    //  Utility
    // =========================================================================

    Entity *FindEntityById(Level &level, const ID entityId) {
        for (Entity &entity: level.entities)
            if (entity.id == entityId) return &entity;
        return nullptr;
    }

    void ResetInspectorState() {
        entityInspectorState = {};
    }

    // =========================================================================
    //  CreateNewLevel — defined early so DrawCreateLevelModal can call it
    // =========================================================================

    void CreateNewLevel(const std::string &levelName) {
        if (!Editor::currentMap.empty()) Save(Editor::currentMap);

        Level &level = LevelManager::CurrentLevel();
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

        ClearWallSelection();
        ClearLinedefSelection();
        ClearSectorSelection();
        ClearEntitySelection();

        ResetInspectorState();

        ClearUndoHistory();

        Editor::currentMap = levelName;

        MapQueries::RebuildSectorRuntimeLinks(level);

        hasUnsavedChanges = false;

        spdlog::info("Created new level: {}", levelName);
    }


    // =========================================================================
    //  Prefab placement
    // =========================================================================

    // A world prefab dropped on the canvas: a new instance at `worldPoint`,
    // standing on the floor of the sector there (or at 0 outside any sector).
    void PlacePrefab(const std::string& reference, const Vector2 worldPoint) {
        Level& level = LevelManager::CurrentLevel();

        const Level* prefab = Prefab::Get(reference);
        if (prefab == nullptr) return;

        if (prefab->ui_transforms.Has(Prefab::RootOf(*prefab).id)) {
            spdlog::warn("{} is a UI prefab - drop it in the UI editor instead", reference);
            return;
        }

        EntitiesSnapshot before = CaptureEntitiesSnapshot();

        const ID id = Prefab::Instantiate(level, reference);
        if (id == INVALID_ID) return;

        PushEntitiesUndo(std::move(before), "editor.history.place_entity");

        const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, worldPoint);
        const float floorHeight = sectorIndex >= 0 && !level.sectors[sectorIndex].floors.empty()
            ? level.sectors[sectorIndex].floors.front().floor.height
            : 0.0f;

        if (ComponentTransform* transform = level.transforms.Get(id))
            transform->SetPosition({worldPoint.x, floorHeight, worldPoint.y});

        currentMode = MODE_ENTITY;
        SelectEntity(id);
        hasUnsavedChanges = true;
    }

    // =========================================================================
    //  DrawDockSpace
    // =========================================================================

    void DrawDockSpace() {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags flags =
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

        const ImGuiID dockspaceID = ImGui::GetID("MainDockspace");
        ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        // A prefab dropped on the canvas (the dockspace's empty middle) is
        // placed under the cursor.
        std::string droppedPrefab;
        if (AcceptCanvasAssetDrop(dockspaceID, AssetKind::Prefab, droppedPrefab))
            PlacePrefab(droppedPrefab, ScreenToWorld(InputManager::GetMousePosition(), cameraPos));

        // A texture dropped on a wall, sector or sprite is applied to it.
        std::string droppedTexture;
        if (AcceptCanvasAssetDrop(dockspaceID, AssetKind::Texture, droppedTexture))
            DropTextureOnCanvas(droppedTexture, ScreenToWorld(InputManager::GetMousePosition(), cameraPos));

        ImGui::End();
    }

    // =========================================================================
    //  World Settings panel
    // =========================================================================

    // =========================================================================
    //  Event variables - World Settings (level) and Project Settings (global)
    // =========================================================================

    struct EventVariableRow {
        EventVariableID id;
        std::string name;
        float value;
    };

    // What a variable list does to its table. Rename/create return false on
    // an empty or taken name.
    struct EventVariableListActions {
        std::function<bool(EventVariableID, const std::string &)> rename;
        std::function<void(EventVariableID, float)> setValue;
        std::function<void()> valueCommitted; // after a value edit is finished
        std::function<void(EventVariableID)> remove;
        std::function<bool(const std::string &)> create;
    };

    // One row per variable (name, starting value, ID, Delete) plus an empty
    // row for adding one. `nameBuffers`/`newNameBuffer` belong to the caller so
    // the level and global lists keep their own in-progress edits.
    void DrawEventVariableList(const char *id, std::vector<EventVariableRow> rows,
                               std::unordered_map<EventVariableID, std::array<char, 128> > &nameBuffers,
                               std::array<char, 128> &newNameBuffer, const EventVariableListActions &actions) {
        std::ranges::sort(rows, {}, &EventVariableRow::id);

        const auto copyName = [](std::array<char, 128> &buffer, const std::string &name) {
            std::strncpy(buffer.data(), name.c_str(), buffer.size() - 1);
            buffer[buffer.size() - 1] = '\0';
        };

        // Seed buffers for new rows, never overwrite one being edited; drop
        // buffers of variables that are gone (deleted, or another level).
        for (const EventVariableRow &row: rows)
            if (!nameBuffers.contains(row.id)) copyName(nameBuffers[row.id], row.name);

        std::erase_if(nameBuffers, [&](const auto &entry) {
            return std::ranges::none_of(rows, [&](const EventVariableRow &row) { return row.id == entry.first; });
        });

        // Deferred so the rows aren't changed while they're drawn.
        std::optional<EventVariableID> pendingDeletion;

        ImGui::PushID(id);

        const float childHeight = std::min(
            static_cast<float>(rows.size()) * ImGui::GetFrameHeightWithSpacing() +
            ImGui::GetStyle().WindowPadding.y * 2.0f,
            180.0f
        );

        ImVec4 listBackground = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        listBackground.w *= 0.45f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, listBackground);
        ImGui::BeginChild("##VariableList", ImVec2(0.0f, std::max(childHeight, 32.0f)), /*border=*/true);

        if (rows.empty()) ImGui::TextDisabled("%s", Get("variables.none_found").c_str());

        for (const EventVariableRow &row: rows) {
            ImGui::PushID(static_cast<int>(row.id));

            auto &buffer = nameBuffers[row.id];

            constexpr float valueWidth = 70.0f;
            constexpr float idWidth = 40.0f;
            constexpr float deleteWidth = 56.0f;
            const float available = ImGui::GetContentRegionAvail().x;

            ImGui::SetNextItemWidth(std::max(40.0f, available - valueWidth - idWidth - deleteWidth - 18.0f));
            ImGui::InputText("##Name", buffer.data(), buffer.size());
            HoverTooltip(Get("variables.tooltip.name").c_str());

            // Commits on Enter or losing focus, never per keystroke.
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const std::string newName = buffer.data();

                if (newName.empty()) {
                    copyName(buffer, row.name);
                    ShowNotification(Get("variables.notification.empty_name").c_str(), /*isError=*/true);
                } else if (newName != row.name && !actions.rename(row.id, newName)) {
                    copyName(buffer, row.name);
                    ShowNotification(Get("variables.notification.duplicate_name").c_str(), /*isError=*/true);
                }
            }

            ImGui::SameLine();
            float value = row.value;
            ImGui::SetNextItemWidth(valueWidth);
            if (ImGui::InputFloat("##Value", &value, 0.0f, 0.0f, "%.2f")) actions.setValue(row.id, value);
            if (ImGui::IsItemDeactivatedAfterEdit() && actions.valueCommitted) actions.valueCommitted();
            HoverTooltip(Get("variables.tooltip.value").c_str());

            ImGui::SameLine();
            ImGui::TextDisabled("#%u", row.id & ~GLOBAL_EVENT_VARIABLE_BIT);

            ImGui::SameLine();
            PushDangerStyle();
            if (ImGui::SmallButton(Get("common.delete").c_str())) pendingDeletion = row.id;
            PopDangerStyle();
            HoverTooltip(Get("variables.tooltip.delete").c_str());

            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Always-available empty row for adding another variable.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##NewName", Get("variables.new_placeholder").c_str(),
                                 newNameBuffer.data(), newNameBuffer.size());

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string enteredName = newNameBuffer.data();

            if (!enteredName.empty()) {
                if (actions.create(enteredName)) newNameBuffer[0] = '\0';
                else ShowNotification(Get("variables.notification.duplicate_name").c_str(), /*isError=*/true);
            }
        }

        if (pendingDeletion) {
            actions.remove(*pendingDeletion);
            nameBuffers.erase(*pendingDeletion);
        }

        ImGui::PopID();
    }

    // The current level's own variables. Saved with the level.
    void DrawLevelEventVariables(Level &level) {
        static std::unordered_map<EventVariableID, std::array<char, 128> > nameBuffers;
        static std::array<char, 128> newNameBuffer{};

        std::vector<EventVariableRow> rows;
        for (const auto &[id, name]: level.worldSettings.eventVariableNames) {
            const auto value = level.worldSettings.eventVariables.find(id);
            rows.push_back({id, name, value != level.worldSettings.eventVariables.end() ? value->second : 0.0f});
        }

        DrawEventVariableList("LevelVariables", std::move(rows), nameBuffers, newNameBuffer, {
            .rename = [&](const EventVariableID id, const std::string &name) {
                if (!level.RenameEventVariable(id, name)) return false;
                hasUnsavedChanges = true;
                return true;
            },
            .setValue = [&](const EventVariableID id, const float value) {
                level.worldSettings.eventVariables[id] = value;
                hasUnsavedChanges = true;
            },
            .valueCommitted = {},
            .remove = [&](const EventVariableID id) {
                level.DeleteEventVariable(id);
                hasUnsavedChanges = true;
            },
            .create = [&](const std::string &name) {
                if (!level.CreateEventVariable(name)) return false;
                hasUnsavedChanges = true;
                return true;
            },
        });
    }

    // A global variable's name can't be one a loaded level already uses.
    bool IsNameUsedByLoadedLevel(const std::string &name) {
        return std::ranges::any_of(LevelManager::loadedLevels, [&](const Level &level) {
            return level.worldSettings.eventVariableIdsByName.contains(name);
        });
    }

    void SaveProjectSettingsOrNotify() {
        hasUnsavedChanges = true;
        if (!SaveProjectSettings())
            ShowNotification(Get("tags.notification.save_failed").c_str(), /*isError=*/true);
    }

    // The project's global variables. Saved to ProjectSettings.json.
    void DrawGlobalEventVariables() {
        static std::unordered_map<EventVariableID, std::array<char, 128> > nameBuffers;
        static std::array<char, 128> newNameBuffer{};

        std::vector<EventVariableRow> rows;
        for (const auto &[id, definition]: GlobalEventVariables::GetAll())
            rows.push_back({id, definition.name, definition.defaultValue});

        DrawEventVariableList("GlobalVariables", std::move(rows), nameBuffers, newNameBuffer, {
            .rename = [](const EventVariableID id, const std::string &name) {
                if (IsNameUsedByLoadedLevel(name) || !GlobalEventVariables::Rename(id, name)) return false;
                SaveProjectSettingsOrNotify();
                return true;
            },
            .setValue = [](const EventVariableID id, const float value) {
                GlobalEventVariables::SetDefaultValue(id, value);
            },
            .valueCommitted = [] { SaveProjectSettingsOrNotify(); },
            .remove = [](const EventVariableID id) {
                GlobalEventVariables::Delete(id);
                SaveProjectSettingsOrNotify();
            },
            .create = [](const std::string &name) {
                if (IsNameUsedByLoadedLevel(name) || !GlobalEventVariables::Create(name)) return false;
                SaveProjectSettingsOrNotify();
                return true;
            },
        });
    }

    // ── World Settings layout ───────────────────────────────────────────────
    // Each group is a collapsible header, and its fields sit in a two-column
    // table like the inspectors': labels on the left, fields filling the rest.

    bool SettingsSection(const char *titleKey, const char *id, const bool defaultOpen = true,
                         const char *tooltipKey = nullptr) {
        ImGui::Spacing();
        const std::string label = Get(titleKey) + "###" + id;
        const bool open = ImGui::CollapsingHeader(label.c_str(), defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (tooltipKey != nullptr) HoverTooltip(Get(tooltipKey).c_str());
        if (open) ImGui::Spacing();
        return open;
    }

    // Call EndSettingsTable only when this returns true.
    bool BeginSettingsTable(const char *id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;

        const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 90.0f, 170.0f);
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void EndSettingsTable() { ImGui::EndTable(); }

    // A new row: the label (`tooltip` on hover, or the full label if the
    // column clips it), then the value column with the next item filling it.
    // `nested` indents the label under the row above (a setting it enables).
    void SettingRow(const std::string &label, const char *tooltip = nullptr, const bool nested = false) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (nested) ImGui::Indent(12.0f);
        ImGui::TextUnformatted(label.c_str());
        if (nested) ImGui::Unindent(12.0f);

        if (tooltip != nullptr) HoverTooltip(tooltip);
        else if (ImGui::CalcTextSize(label.c_str()).x > ImGui::GetColumnWidth()) HoverTooltip(label.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // A small grey heading between two tables of one section.
    void SettingsSubheading(const char *text) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::SeparatorText(text);
        ImGui::PopStyleColor();
    }

    // Stored 0..255, edited as 0..1 like sector light.
    bool ColorField255(const char *id, Vector3 &color) {
        float normalized[3] = {color.x / 255.0f, color.y / 255.0f, color.z / 255.0f};

        if (!ImGui::ColorEdit3(id, normalized,
                               ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB |
                               ImGuiColorEditFlags_PickerHueWheel))
            return false;

        color = {normalized[0] * 255.0f, normalized[1] * 255.0f, normalized[2] * 255.0f};
        return true;
    }

    // A combo over `keys` (localisation keys), `current` indexing them.
    template <size_t N>
    bool KeyCombo(const char *id, int &current, const char *const (&keys)[N]) {
        bool changed = false;
        if (ImGui::BeginCombo(id, Get(keys[current]).c_str())) {
            for (int i = 0; i < static_cast<int>(N); ++i) {
                if (ImGui::Selectable(Get(keys[i]).c_str(), i == current)) {
                    current = i;
                    changed = true;
                }
                if (i == current) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // ── Message Box ──

    // The Show Message box: font, place, background, text effects, fades, stacking.
    void DrawMessageStyleSettings(MessageStyle &style) {
        const auto row = [](const char *labelKey, const char *tooltipKey = nullptr, const bool nested = false) {
            const std::string tooltip = tooltipKey != nullptr ? Get(tooltipKey) : std::string{};
            SettingRow(Get(labelKey), tooltipKey != nullptr ? tooltip.c_str() : nullptr, nested);
        };
        const auto tooltip = [](const char *key) { HoverTooltip(Get(key).c_str()); };

        // ---- Text ----
        if (BeginSettingsTable("##MessageText")) {
            row("settings.message.font", "settings.message.tooltip.font");
            ImGui::PushID("Font");
            DrawAssetField("", style.font, AssetKind::Font);
            ImGui::PopID();
            tooltip("settings.message.tooltip.font");

            row("settings.message.font_size", "settings.message.tooltip.font_size");
            if (ImGui::InputFloat("##FontSize", &style.fontSize, 1.0f, 4.0f, "%.0f"))
                style.fontSize = std::max(1.0f, style.fontSize);
            tooltip("settings.message.tooltip.font_size");

            static constexpr const char *alignKeys[] = {
                "settings.message.align.left", "settings.message.align.center", "settings.message.align.right"
            };
            row("settings.message.align", "settings.message.tooltip.align");
            int align = static_cast<int>(style.align);
            if (KeyCombo("##Align", align, alignKeys)) style.align = static_cast<MessageTextAlign>(align);
            tooltip("settings.message.tooltip.align");

            row("settings.message.wrap_width", "settings.message.tooltip.wrap_width");
            if (ImGui::InputFloat("##WrapWidth", &style.wrapWidth, 10.0f, 50.0f, "%.0f"))
                style.wrapWidth = std::max(0.0f, style.wrapWidth);
            tooltip("settings.message.tooltip.wrap_width");

            EndSettingsTable();
        }

        // ---- Position ----
        SettingsSubheading(Get("settings.message.group.position").c_str());
        if (BeginSettingsTable("##MessagePosition")) {
            static constexpr const char *anchorKeys[] = {
                "settings.message.anchor.top_left", "settings.message.anchor.top_center", "settings.message.anchor.top_right",
                "settings.message.anchor.middle_left", "settings.message.anchor.center", "settings.message.anchor.middle_right",
                "settings.message.anchor.bottom_left", "settings.message.anchor.bottom_center", "settings.message.anchor.bottom_right"
            };
            row("settings.message.anchor", "settings.message.tooltip.anchor");
            int anchor = static_cast<int>(style.anchor);
            if (KeyCombo("##Anchor", anchor, anchorKeys)) style.anchor = static_cast<MessageAnchor>(anchor);
            tooltip("settings.message.tooltip.anchor");

            row("settings.message.offset", "settings.message.tooltip.offset");
            ImGui::DragFloat2("##Offset", &style.offset.x, 1.0f, 0.0f, 0.0f, "%.0f");
            tooltip("settings.message.tooltip.offset");

            EndSettingsTable();
        }

        // ---- Background, shadow, outline ----
        SettingsSubheading(Get("settings.message.group.look").c_str());
        if (BeginSettingsTable("##MessageLook")) {
            row("settings.message.background", "settings.message.tooltip.background");
            ImGui::Checkbox("##Background", &style.background);
            tooltip("settings.message.tooltip.background");

            ImGui::BeginDisabled(!style.background);
            row("settings.message.color", "settings.message.tooltip.background_color", true);
            ImGui::ColorEdit4("##BackgroundColor", &style.backgroundColor.x);
            tooltip("settings.message.tooltip.background_color");

            row("settings.message.background_texture", "settings.message.tooltip.background_texture", true);
            ImGui::PushID("BackgroundTexture");
            DrawAssetField("", style.backgroundTexture, AssetKind::Texture, 32.0f);
            ImGui::PopID();
            tooltip("settings.message.tooltip.background_texture");

            row("settings.message.padding", "settings.message.tooltip.padding", true);
            ImGui::DragFloat2("##Padding", &style.padding.x, 0.5f, 0.0f, 1000.0f, "%.0f");
            tooltip("settings.message.tooltip.padding");
            ImGui::EndDisabled();

            row("settings.message.shadow", "settings.message.tooltip.shadow");
            ImGui::Checkbox("##Shadow", &style.shadow);
            tooltip("settings.message.tooltip.shadow");

            ImGui::BeginDisabled(!style.shadow);
            row("settings.message.color", nullptr, true);
            ImGui::ColorEdit4("##ShadowColor", &style.shadowColor.x);

            row("settings.message.offset", "settings.message.tooltip.shadow_offset", true);
            ImGui::DragFloat2("##ShadowOffset", &style.shadowOffset.x, 0.25f, 0.0f, 0.0f, "%.1f");
            tooltip("settings.message.tooltip.shadow_offset");
            ImGui::EndDisabled();

            row("settings.message.outline", "settings.message.tooltip.outline");
            ImGui::Checkbox("##Outline", &style.outline);
            tooltip("settings.message.tooltip.outline");

            ImGui::BeginDisabled(!style.outline);
            row("settings.message.color", nullptr, true);
            ImGui::ColorEdit4("##OutlineColor", &style.outlineColor.x);

            row("settings.message.outline_thickness", nullptr, true);
            if (ImGui::InputFloat("##OutlineThickness", &style.outlineThickness, 0.5f, 1.0f, "%.1f"))
                style.outlineThickness = std::max(0.0f, style.outlineThickness);
            ImGui::EndDisabled();

            EndSettingsTable();
        }

        // ---- Fades ----
        SettingsSubheading(Get("settings.message.group.timing").c_str());
        if (BeginSettingsTable("##MessageTiming")) {
            row("settings.message.fade_in", "settings.message.tooltip.fade");
            if (ImGui::InputFloat("##FadeIn", &style.fadeIn, 0.05f, 0.25f, "%.2f s"))
                style.fadeIn = std::max(0.0f, style.fadeIn);
            tooltip("settings.message.tooltip.fade");

            row("settings.message.fade_out", "settings.message.tooltip.fade");
            if (ImGui::InputFloat("##FadeOut", &style.fadeOut, 0.05f, 0.25f, "%.2f s"))
                style.fadeOut = std::max(0.0f, style.fadeOut);
            tooltip("settings.message.tooltip.fade");

            EndSettingsTable();
        }

        // ---- Stacking ----
        SettingsSubheading(Get("settings.message.group.stacking").c_str());
        if (BeginSettingsTable("##MessageStacking")) {
            row("settings.message.stack", "settings.message.tooltip.stack");
            ImGui::Checkbox("##Stack", &style.stack);
            tooltip("settings.message.tooltip.stack");

            ImGui::BeginDisabled(!style.stack);
            row("settings.message.max_messages", "settings.message.tooltip.max_messages", true);
            if (ImGui::InputInt("##MaxMessages", &style.maxMessages))
                style.maxMessages = std::max(1, style.maxMessages);
            tooltip("settings.message.tooltip.max_messages");

            row("settings.message.spacing", "settings.message.tooltip.spacing", true);
            ImGui::InputFloat("##Spacing", &style.spacing, 1.0f, 4.0f, "%.0f");
            tooltip("settings.message.tooltip.spacing");
            ImGui::EndDisabled();

            EndSettingsTable();
        }
    }

    // ── Sections ──

    void DrawPhysicsSettings(Level &level) {
        if (!BeginSettingsTable("##Physics")) return;

        const std::string tooltip = Get("settings.physics.tooltip.gravity");
        SettingRow(Get("settings.physics.gravity"), tooltip.c_str());
        ImGui::InputFloat("##Gravity", &level.worldSettings.gravity, 0.1f, 1.0f, "%.2f");
        HoverTooltip(tooltip.c_str());

        const std::string substepsTooltip = Get("settings.physics.tooltip.max_substeps");
        SettingRow(Get("settings.physics.max_substeps"), substepsTooltip.c_str());
        if (ImGui::InputInt("##MaxPhysicsSubsteps", &level.worldSettings.maxPhysicsSubsteps))
            level.worldSettings.maxPhysicsSubsteps = std::clamp(level.worldSettings.maxPhysicsSubsteps, 1, 64);
        HoverTooltip(substepsTooltip.c_str());

        EndSettingsTable();
    }

    void DrawAudioSettings(Level &level) {
        ListenerSettings &settings = level.listenerSettings;

        if (BeginSettingsTable("##Audio")) {
            const std::string tooltip = Get("settings.audio.tooltip.master_gain");
            SettingRow(Get("settings.audio.master_gain"), tooltip.c_str());
            ImGui::SliderFloat("##MasterGain", &settings.masterGain, 0.0f, 2.0f, "%.2f");
            HoverTooltip(tooltip.c_str());

            EndSettingsTable();
        }

        SettingsSubheading(Get("settings.audio.global_physics_header").c_str());
        if (!BeginSettingsTable("##AudioPhysics")) return;

        {
            const std::string tooltip = Get("settings.audio.tooltip.doppler_factor");
            SettingRow(Get("settings.audio.doppler_factor"), tooltip.c_str());
            ImGui::DragFloat("##DopplerFactor", &settings.dopplerFactor, 0.01f, 0.0f, 10.0f, "%.2f");
            HoverTooltip(tooltip.c_str());
        }

        {
            const std::string tooltip = Get("settings.audio.tooltip.speed_of_sound");
            SettingRow(Get("settings.audio.speed_of_sound"), tooltip.c_str());
            if (ImGui::InputFloat("##SpeedOfSound", &settings.speedOfSound, 1.0f, 50.0f, "%.1f") &&
                settings.speedOfSound < 1.0f) {
                settings.speedOfSound = 1.0f;
                ShowNotification(Get("settings.audio.notification.speed_of_sound_clamped").c_str(), /*isError=*/true);
            }
            HoverTooltip(tooltip.c_str());
        }

        {
            static constexpr const char *modelKeys[] = {
                "settings.audio.distance_model.inverse",
                "settings.audio.distance_model.inverse_clamped",
                "settings.audio.distance_model.linear",
                "settings.audio.distance_model.linear_clamped",
                "settings.audio.distance_model.none"
            };
            static constexpr ALenum alModels[] = {
                AL_INVERSE_DISTANCE,
                AL_INVERSE_DISTANCE_CLAMPED,
                AL_LINEAR_DISTANCE,
                AL_LINEAR_DISTANCE_CLAMPED,
                AL_NONE
            };

            // An unknown model shows as Inverse Clamped, the default.
            int current = 1;
            for (int i = 0; i < static_cast<int>(std::size(alModels)); ++i)
                if (alModels[i] == settings.distanceModel) current = i;

            const std::string tooltip = Get("settings.audio.tooltip.distance_model");
            SettingRow(Get("settings.audio.distance_model"), tooltip.c_str());
            if (KeyCombo("##DistanceModel", current, modelKeys)) settings.distanceModel = alModels[current];
            HoverTooltip(tooltip.c_str());
        }

        EndSettingsTable();
    }

    void DrawRenderingSettings(Level &level) {
        if (!BeginSettingsTable("##Rendering")) return;

        {
            RendererTextureSettings &textureSetting = level.rendererSettings.textureSetting;

            struct TexturePresetOption {
                RendererTextureSettings value;
                const char *labelKey;
                const char *tooltipKey;
            };

            static constexpr TexturePresetOption presets[] = {
                {PIXEL_ART_SHIMMERY, "settings.rendering.texture_preset.pixel_art_shimmery",
                 "settings.rendering.tooltip.texture_preset.pixel_art_shimmery"},
                {PIXEL_ART_LESS_MOIRE, "settings.rendering.texture_preset.pixel_art_less_moire",
                 "settings.rendering.tooltip.texture_preset.pixel_art_less_moire"},
                {PIXEL_ART_SMOOTH_DISTANCE, "settings.rendering.texture_preset.pixel_art_smooth_distance",
                 "settings.rendering.tooltip.texture_preset.pixel_art_smooth_distance"},
                {REALISTIC_NORMAL, "settings.rendering.texture_preset.realistic_normal",
                 "settings.rendering.tooltip.texture_preset.realistic_normal"},
                {RETRO, "settings.rendering.texture_preset.retro", "settings.rendering.tooltip.texture_preset.retro"},
                {LOW_RES, "settings.rendering.texture_preset.low_res", "settings.rendering.tooltip.texture_preset.low_res"},
            };

            const TexturePresetOption *selectedPreset = &presets[0];
            for (const TexturePresetOption &preset: presets)
                if (preset.value == textureSetting) selectedPreset = &preset;

            const std::string tooltip = Get("settings.rendering.tooltip.texture_preset");
            SettingRow(Get("settings.rendering.texture_preset"), tooltip.c_str());

            if (ImGui::BeginCombo("##TexturePreset", Get(selectedPreset->labelKey).c_str())) {
                for (const TexturePresetOption &preset: presets) {
                    const bool selected = preset.value == textureSetting;

                    if (ImGui::Selectable(Get(preset.labelKey).c_str(), selected)) {
                        textureSetting = preset.value;
                        selectedPreset = &preset;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Get(preset.tooltipKey).c_str());
                    if (selected) ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
            HoverTooltip(tooltip.c_str());

            // What the picked preset does, under its combo.
            HintText(Get(selectedPreset->tooltipKey).c_str());
        }

        {
            const std::string tooltip = Get("settings.rendering.tooltip.wall_contrast");
            SettingRow(Get("settings.rendering.wall_contrast"), tooltip.c_str());
            if (ImGui::SliderFloat("##WallContrast", &level.rendererSettings.wallContrast, 0.0f, MAX_WALL_CONTRAST, "%.0f"))
                hasUnsavedChanges = true;
            HoverTooltip(tooltip.c_str());
        }

        EndSettingsTable();
    }

    void DrawSkySettings(Level &level) {
        if (!BeginSettingsTable("##Sky")) return;

        SkySettings &sky = level.sky;

        const SkyModeInfo *selectedMode = FindSkyMode(sky.mode);
        if (selectedMode == nullptr) selectedMode = &SKY_MODES[0];

        SettingRow(Get("settings.rendering.sky.mode"), Get(selectedMode->tooltipKey).c_str());
        if (ImGui::BeginCombo("##SkyMode", Get(selectedMode->labelKey).c_str())) {
            for (const SkyModeInfo &mode : SKY_MODES) {
                const bool selected = mode.mode == sky.mode;

                if (ImGui::Selectable(Get(mode.labelKey).c_str(), selected)) sky.mode = mode.mode;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Get(mode.tooltipKey).c_str());
                if (selected) ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }
        HoverTooltip(Get(selectedMode->tooltipKey).c_str());

        if (sky.mode == SkyMode::Cubemap) {
            const std::string tooltip = Get("settings.rendering.tooltip.sky.faces");
            for (const SkyFaceInfo &face : SKY_FACES) {
                SettingRow(Get(face.labelKey), tooltip.c_str());
                ImGui::PushID(face.name);
                DrawAssetField("", sky.Face(face.face), AssetKind::Texture, 32.0f);
                ImGui::PopID();
                HoverTooltip(tooltip.c_str());
            }
        }
        else {
            const std::string tooltip = Get("settings.rendering.tooltip.sky.texture");
            SettingRow(Get("settings.rendering.sky.texture"), tooltip.c_str());
            ImGui::PushID("SkyTexture");
            DrawAssetField("", sky.texture, AssetKind::Texture, 32.0f);
            ImGui::PopID();
            HoverTooltip(tooltip.c_str());
        }

        const auto numberRow = [](const char *labelKey, const char *tooltipKey, const char *id, float &value,
                                  const float step, const float fastStep, const char *format) {
            const std::string tooltip = Get(tooltipKey);
            SettingRow(Get(labelKey), tooltip.c_str());
            ImGui::InputFloat(id, &value, step, fastStep, format);
            HoverTooltip(tooltip.c_str());
        };

        numberRow("settings.rendering.sky.rotation", "settings.rendering.tooltip.sky.rotation", "##SkyRotation",
                  sky.rotation, 1.0f, 15.0f, "%.1f");
        numberRow("settings.rendering.sky.rotation_speed", "settings.rendering.tooltip.sky.rotation_speed",
                  "##SkyRotationSpeed", sky.rotationSpeed, 0.5f, 5.0f, "%.2f");
        numberRow("settings.rendering.sky.horizon_offset", "settings.rendering.tooltip.sky.horizon_offset",
                  "##SkyHorizonOffset", sky.horizonOffset, 1.0f, 5.0f, "%.1f");

        const auto colorRow = [](const char *labelKey, const char *tooltipKey, const char *id, Vector3 &color) {
            const std::string tooltip = Get(tooltipKey);
            SettingRow(Get(labelKey), tooltip.c_str());
            ColorField255(id, color);
            HoverTooltip(tooltip.c_str());
        };

        colorRow("settings.rendering.sky.tint", "settings.rendering.tooltip.sky.tint", "##SkyTint", sky.tint);
        colorRow("settings.rendering.sky.fallback_color", "settings.rendering.tooltip.sky.fallback_color",
                 "##SkyFallbackColor", sky.fallbackColor);

        EndSettingsTable();
    }

    void DrawWorldSettings() {
        Level &level = LevelManager::CurrentLevel();

        ImGui::Begin(Get("editor.world_settings").c_str());

        if (SettingsSection("settings.physics.title", "Physics")) DrawPhysicsSettings(level);
        if (SettingsSection("settings.audio.title", "Audio")) DrawAudioSettings(level);
        if (SettingsSection("settings.rendering.title", "Rendering")) DrawRenderingSettings(level);
        if (SettingsSection("settings.rendering.sky.title", "Sky")) DrawSkySettings(level);

        if (SettingsSection("settings.message.title", "MessageBox", false, "settings.message.tooltip"))
            DrawMessageStyleSettings(level.worldSettings.messageStyle);

        if (SettingsSection("settings.variables.title", "Variables", true, "settings.variables.tooltip"))
            DrawLevelEventVariables(level);

        // Its Events and Dispatches are collapsible too: indented under it.
        if (SettingsSection("settings.level_events.title", "LevelEvents", true, "settings.level_events.tooltip")) {
            ImGui::Indent(8.0f);
            ImGuiDrawFunctions::DrawLevelEventSections(level, DRAGGABLE);
            ImGui::Unindent(8.0f);
        }

        ImGui::End();
    }

    // =========================================================================
    //  Level list — drawn as a scrollable ChildWindow inside the Project
    //  Settings panel so it doesn't spawn its own floating window.
    // =========================================================================

    // Which level the standalone game opens first. Saved to project.tilky as
    // soon as it is picked.
    void DrawStartLevelCombo() {
        const std::string startLevel = ProjectManager::GetStartLevelName();
        const bool missing = !startLevel.empty() && std::ranges::find(Editor::maps, startLevel) == Editor::maps.end();

        const std::string preview = startLevel.empty()
            ? Get("editor.project.start_level.first_found")
            : missing ? startLevel + " " + Get("editor.project.start_level.missing") : startLevel;

        const auto pick = [](const std::string& levelName) {
            if (!ProjectManager::SetStartLevelName(levelName))
                ShowNotification(Get("editor.project.start_level_failed").c_str(), /*isError=*/true);
        };

        if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.65f, 0.30f, 1.00f));

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        const bool comboOpen = ImGui::BeginCombo("##StartLevel", preview.c_str());

        if (missing) ImGui::PopStyleColor();

        if (comboOpen) {
            if (ImGui::Selectable(Get("editor.project.start_level.first_found").c_str(), startLevel.empty()) && !startLevel.empty())
                pick("");

            for (int i = 0; i < static_cast<int>(Editor::maps.size()); ++i) {
                ImGui::PushID(i);
                const std::string& name = Editor::maps[i];
                if (ImGui::Selectable(name.c_str(), name == startLevel) && name != startLevel) pick(name);
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        HoverTooltip(Get(missing ? "editor.tooltip.project.start_level_missing" : "editor.tooltip.project.start_level").c_str());
    }

    void DrawLevelsChildPanel() {
        const float childHeight = std::min(
            static_cast<float>(Editor::maps.size()) * ImGui::GetFrameHeightWithSpacing() +
            ImGui::GetStyle().WindowPadding.y * 2.0f,
            180.0f
        );

        ImVec4 listBackground = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        listBackground.w *= 0.45f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, listBackground);
        ImGui::BeginChild("##LevelsList", ImVec2(0.0f, childHeight), /*border=*/true);

        if (Editor::maps.empty()) {
            ImGui::TextDisabled("%s", Get("levels.none_found").c_str());
        }

        // Name column stretches and clips; the buttons keep their own column,
        // so a long level name can no longer run underneath them.
        if (!Editor::maps.empty() && ImGui::BeginTable("##LevelsTable", 2, ImGuiTableFlags_None)) {
            constexpr float buttonWidth = 52.0f;

            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed,
                                    buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);

            for (int i = 0; i < static_cast<int>(Editor::maps.size()); ++i) {
                ImGui::PushID(i);

                const std::string &name = Editor::maps[i];
                const bool isCurrent = (Editor::currentMap == name);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                // Accent the active level name
                if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));

                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s%s", isCurrent ? "* " : "  ", name.c_str());

                if (isCurrent) ImGui::PopStyleColor();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", name.c_str());

                ImGui::TableNextColumn();

                PushAccentStyle();
                if (ImGui::Button(Get("levels.load_short").c_str(), ImVec2(buttonWidth, 0.0f))) {
                    Editor::OpenLevel(name);
                }
                PopAccentStyle();
                HoverTooltip(Get("levels.tooltip.load").c_str());

                ImGui::SameLine();

                PushDangerStyle();
                if (ImGui::Button(Get("levels.delete_short").c_str(), ImVec2(buttonWidth, 0.0f))) {
                    deleteLevelPending = name;
                    deleteLevelConfirmOpen = true;
                }
                PopDangerStyle();
                HoverTooltip(Get("levels.tooltip.delete").c_str());

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Delete confirmation modal
        if (deleteLevelConfirmOpen) {
            ImGui::OpenPopup("##DeleteLevelConfirm");
            deleteLevelConfirmOpen = false;
        }

        if (ImGui::BeginPopupModal("##DeleteLevelConfirm", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(Get("levels.delete_confirm").c_str(), deleteLevelPending.c_str());
            ImGui::TextDisabled("%s", Get("common.cannot_be_undone").c_str());
            ImGui::Spacing();

            PushDangerStyle();
            if (ImGui::Button(Get("common.delete").c_str(), ImVec2(90.0f, 0.0f))) {
                Editor::WaitForAutoSave();
                const std::filesystem::path path = LevelSerialization::FindLevelPath(deleteLevelPending);

                try {
                    if (!path.empty() && std::filesystem::remove(path)) {
                        spdlog::info("Deleted level: {}", path.string());
                        if (Editor::currentMap == deleteLevelPending) Editor::currentMap = "";
                        UpdateLevels();
                        ShowNotification(Get("levels.notification.deleted").c_str());
                    }
                    else {
                        spdlog::error("File not found: {}", path.string());
                        ShowNotification(Get("levels.notification.delete_failed_file_not_found").c_str(), /*isError=*/
                                         true);
                    }
                } catch (const std::filesystem::filesystem_error &e) {
                    spdlog::error("Failed to delete level: {}", e.what());
                    ShowNotification(Get("levels.notification.delete_failed_check_logs").c_str(), /*isError=*/true);
                }

                deleteLevelPending.clear();
                ImGui::CloseCurrentPopup();
            }
            PopDangerStyle();

            ImGui::SameLine();

            if (ImGui::Button(Get("common.cancel").c_str(), ImVec2(90.0f, 0.0f))) {
                deleteLevelPending.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // =========================================================================
    //  Project Tags — Project Settings child panel
    // =========================================================================
    // TagRegistry is the authoritative owner of project tags (name <-> stable
    // ID); this panel only drives it through its public API (Create / Rename /
    // Delete / Find) - it never mutates a tag's ID or touches the underlying
    // map directly. Deleting a project tag here also strips its ID from every
    // sector, wall and entity of every loaded level, so none of their tagIds
    // vectors are left referencing a tag the registry no longer knows about.

    // Removes every assignment of `tagId` from every sector, wall and entity
    // of every loaded level. Called once a project tag is actually deleted,
    // so nothing is left pointing at an ID the registry no longer has.
    void RemoveTagFromAllLoadedObjects(const TagId tagId) {
        for (Level &level: LevelManager::loadedLevels) {
            for (Sector &sector: level.sectors) std::erase(sector.tagIds, tagId);
            for (Wall &wall: level.walls) std::erase(wall.tagIds, tagId);
            for (Entity &entity: level.entities) std::erase(entity.tagIds, tagId);
        }
    }

    // One list per tag kind: the normal tags, then the event tags.
    void DrawTagsSection(const TagRegistry::TagKind kind) {
        const int kindIndex = kind == TagRegistry::TagKind::EVENT ? 1 : 0;

        // Per-tag rename buffers, persisted across frames (like
        // levelNameBuf below) so an in-progress edit survives frame
        // boundaries. Keyed by TagId rather than name since renaming must
        // not change which row a buffer belongs to. One set per list, or
        // each list would drop the other's buffers every frame.
        static std::unordered_map<TagId, std::array<char, 128> > tagNameBuffersByKind[2];
        static char newTagNameBufByKind[2][128] = {"", ""};
        auto &tagNameBuffers = tagNameBuffersByKind[kindIndex];
        char (&newTagNameBuf)[128] = newTagNameBufByKind[kindIndex];

        ImGui::PushID(kindIndex);

        // Stable, ID-ordered snapshot for this frame, so rows don't jump
        // around as tags are added/removed.
        const std::vector<std::pair<std::string, TagId> > sortedTags = TagRegistry::GetAllOfKind(kind);

        // Seed a buffer for any tag we haven't seen yet; never overwrite an
        // existing one, or an in-progress edit would be stomped every frame.
        for (const auto &[name, id]: sortedTags) {
            if (tagNameBuffers.contains(id)) continue;

            auto &buffer = tagNameBuffers[id];
            std::strncpy(buffer.data(), name.c_str(), buffer.size() - 1);
            buffer[buffer.size() - 1] = '\0';
        }

        // Drop buffers for tags that no longer exist (deleted elsewhere, or
        // a different project got loaded).
        for (auto it = tagNameBuffers.begin(); it != tagNameBuffers.end();) {
            const bool stillExists = std::ranges::any_of(
                sortedTags, [&](const auto &entry) { return entry.second == it->first; });

            if (stillExists) ++it;
            else it = tagNameBuffers.erase(it);
        }

        // Deferred so Delete() doesn't invalidate `sortedTags`/`tags` while
        // the row loop below is still iterating over them.
        std::optional<std::pair<std::string, TagId> > tagPendingDeletion;

        const float childHeight = std::min(
            static_cast<float>(sortedTags.size()) * ImGui::GetFrameHeightWithSpacing() +
            ImGui::GetStyle().WindowPadding.y * 2.0f,
            180.0f
        );

        ImVec4 listBackground = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        listBackground.w *= 0.45f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, listBackground);
        ImGui::BeginChild("##TagsList", ImVec2(0.0f, std::max(childHeight, 32.0f)), /*border=*/true);

        if (sortedTags.empty())
            ImGui::TextDisabled("%s", Get("tags.none_found").c_str());

        for (const auto &[name, id]: sortedTags) {
            ImGui::PushID(static_cast<int>(id));

            auto &buffer = tagNameBuffers[id];

            constexpr float idWidth = 56.0f;
            constexpr float deleteWidth = 56.0f;
            const float available = ImGui::GetContentRegionAvail().x;

            ImGui::SetNextItemWidth(std::max(40.0f, available - idWidth - deleteWidth - 12.0f));
            ImGui::InputText("##TagName", buffer.data(), buffer.size());

            // Commits on Enter or on losing focus after an edit - never on
            // every keystroke, so typing a name doesn't spam the registry
            // with partial tags.
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const std::string newName = buffer.data();

                if (newName.empty()) {
                    std::strncpy(buffer.data(), name.c_str(), buffer.size() - 1);
                    buffer[buffer.size() - 1] = '\0';
                    ShowNotification(Get("tags.notification.empty_name").c_str(), /*isError=*/true);
                } else if (newName != name) {
                    if (TagRegistry::Rename(name, newName)) {
                        hasUnsavedChanges = true;
                        if (!SaveProjectSettings())
                            ShowNotification(Get("tags.notification.save_failed").c_str(), /*isError=*/true);
                    } else {
                        std::strncpy(buffer.data(), name.c_str(), buffer.size() - 1);
                        buffer[buffer.size() - 1] = '\0';
                        ShowNotification(Get("tags.notification.duplicate_name").c_str(), /*isError=*/true);
                    }
                }
            }

            ImGui::SameLine();
            ImGui::TextDisabled("#%u", id);

            ImGui::SameLine();
            PushDangerStyle();
            if (ImGui::SmallButton(Get("common.delete").c_str())) tagPendingDeletion = {name, id};
            PopDangerStyle();
            HoverTooltip(Get("tags.tooltip.delete").c_str());

            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Always-available empty row for creating another tag.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##NewTagName",
                                 Get(kindIndex == 1 ? "tags.new_event_placeholder" : "tags.new_placeholder").c_str(),
                                 newTagNameBuf, sizeof(newTagNameBuf));

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string enteredName = newTagNameBuf;

            if (!enteredName.empty()) {
                if (TagRegistry::Create(enteredName, kind)) {
                    newTagNameBuf[0] = '\0';
                    hasUnsavedChanges = true;
                    if (!SaveProjectSettings())
                        ShowNotification(Get("tags.notification.save_failed").c_str(), /*isError=*/true);
                } else {
                    ShowNotification(Get("tags.notification.duplicate_name").c_str(), /*isError=*/true);
                }
            }
        }

        if (tagPendingDeletion) {
            const auto &[deletedName, deletedId] = *tagPendingDeletion;

            TagRegistry::Delete(deletedName);
            RemoveTagFromAllLoadedObjects(deletedId);
            tagNameBuffers.erase(deletedId);

            hasUnsavedChanges = true;
            if (!SaveProjectSettings())
                ShowNotification(Get("tags.notification.save_failed").c_str(), /*isError=*/true);
        }

        ImGui::PopID();
    }

    // =========================================================================
    //  Create Level modal
    // =========================================================================

    void DrawCreateLevelModal() {
        static char newLevelNameBuf[64] = "";
        const std::string popupID = Get("editor.create_level") + "##CreateLevelPopup";
        const char *kPopupID = popupID.c_str();

        if (createLevelModalRequested) {
            ImGui::OpenPopup(kPopupID);
            newLevelNameBuf[0] = '\0';
            createLevelModalRequested = false;
        }

        if (ImGui::BeginPopupModal(kPopupID, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Get("editor.create_level_prompt").c_str());
            ImGui::Spacing();

            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##NewLevelName", newLevelNameBuf, IM_ARRAYSIZE(newLevelNameBuf));

            // Level names are unique across the whole Assets folder, and
            // Editor::maps already lists every one of them.
            const bool nameEmpty = newLevelNameBuf[0] == '\0';
            const bool nameTaken = !nameEmpty && std::ranges::find(Editor::maps, LevelSerialization::CleanLevelName(newLevelNameBuf)) != Editor::maps.end();
            const bool nameValid = !nameEmpty && !nameTaken;

            if (nameEmpty) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Get("editor.name_required").c_str());
            }
            else if (nameTaken) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Get("editor.level_name_taken").c_str());
            }

            ImGui::Spacing();
            ImGui::BeginDisabled(!nameValid);

            PushAccentStyle();
            if (ImGui::Button(Get("editor.create").c_str(), ImVec2(100.0f, 0.0f))) {
                CreateNewLevel(newLevelNameBuf);
                UpdateLevels();
                ShowNotification(Get("levels.notification.created").c_str());
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
    //  User Settings window
    // =========================================================================

    void DrawUserSettingsWindow() {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);

        const bool windowVisible = ImGui::Begin(Get("editor.user_settings").c_str(),&userSettingsOpen);

        if (windowVisible) {
            constexpr ImGuiColorEditFlags color3Flags =
                    ImGuiColorEditFlags_Uint8 |
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_NoLabel |
                    ImGuiColorEditFlags_PickerHueBar;

            constexpr ImGuiColorEditFlags color4Flags =
                    color3Flags |
                    ImGuiColorEditFlags_AlphaBar |
                    ImGuiColorEditFlags_AlphaPreviewHalf;

            const std::string &defaultLabel = Get("editor.user_settings.default");

            const float resetColumnWidth =
                    ImGui::CalcTextSize(defaultLabel.c_str()).x +
                    ImGui::GetStyle().FramePadding.x * 2.0f + 6.0f;

            constexpr float colorColumnWidth = 36.0f;

            const ImGuiTableFlags tableFlags =
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_PadOuterX |
                    ImGuiTableFlags_NoSavedSettings;

            const auto DrawColor3Setting = [&](const char *key,Vector3 &color,const Vector3 &defaultColor) {
                ImGui::PushID(key);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Get(key).c_str());

                ImGui::TableSetColumnIndex(1);

                float normalizedColor[3] = {
                    color.x / 255.0f,
                    color.y / 255.0f,
                    color.z / 255.0f
                };

                ImGui::SetNextItemWidth(colorColumnWidth);

                if (ImGui::ColorEdit3("##Color",normalizedColor,color3Flags)) {
                    color = {
                        normalizedColor[0] * 255.0f,
                        normalizedColor[1] * 255.0f,
                        normalizedColor[2] * 255.0f
                    };
                }

                ImGui::TableSetColumnIndex(2);

                if (ImGui::SmallButton(defaultLabel.c_str())) color = defaultColor;

                ImGui::PopID();
            };

            const auto DrawColor4Setting = [&](const char *key,Vector4 &color,const Vector4 &defaultColor) {
                ImGui::PushID(key);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Get(key).c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(colorColumnWidth);

                ImGui::ColorEdit4("##Color",&color.x,color4Flags);

                ImGui::TableSetColumnIndex(2);

                if (ImGui::SmallButton(defaultLabel.c_str()))
                    color = defaultColor;

                ImGui::PopID();
            };

            const auto DrawGroup = [&](const char *titleKey,const char *tableID,const auto &drawRows) {
                if (!ImGui::CollapsingHeader(Get(titleKey).c_str(),ImGuiTreeNodeFlags_DefaultOpen)) return;

                if (ImGui::BeginTable(tableID, 3, tableFlags)) {
                    ImGui::TableSetupColumn(
                        "##Setting",
                        ImGuiTableColumnFlags_WidthStretch
                    );

                    ImGui::TableSetupColumn(
                        "##Color",
                        ImGuiTableColumnFlags_WidthFixed,
                        colorColumnWidth
                    );

                    ImGui::TableSetupColumn(
                        "##Default",
                        ImGuiTableColumnFlags_WidthFixed,
                        resetColumnWidth
                    );

                    drawRows();
                    ImGui::EndTable();
                }

                ImGui::Spacing();
            };

            constexpr float keyColumnWidth = 100.0f;

            // Set by DrawKeySelect, so a binding left listening when its
            // header gets collapsed stops listening (see the end of the window).
            bool keyBindRowDrawn = false;

            // One key slot: the bound key's name on a button. Clicking it
            // listens for the next key press, which becomes the binding;
            // Escape or a second click cancels. Right click empties the slot.
            const auto DrawKeySlot = [&](const int slot, SDL_Scancode* original, const float width) {
                ImGui::PushID(slot);

                const bool listening = keyBindBeingListened == original;

                if (listening) {
                    const SDL_Scancode pressed = InputManager::GetAnyKeyDown();

                    if (pressed == SDL_SCANCODE_ESCAPE) keyBindBeingListened = nullptr;
                    else if (pressed != SDL_SCANCODE_UNKNOWN) {
                        *original = pressed;
                        keyBindBeingListened = nullptr;
                    }
                }

                const std::string buttonLabel = (listening
                    ? Get("editor.user_settings.key_listening")
                    : *original == SDL_SCANCODE_UNKNOWN
                        ? Get("editor.user_settings.key_none")
                        : InputManager::ScancodeToString(*original)) + "##Key";

                if (listening) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

                if (ImGui::Button(buttonLabel.c_str(), ImVec2(width, 0.0f)))
                    keyBindBeingListened = listening ? nullptr : original;

                if (listening) ImGui::PopStyleColor();

                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    *original = SDL_SCANCODE_UNKNOWN;
                    if (listening) keyBindBeingListened = nullptr;
                }

                ImGui::PopID();
            };

            constexpr float comboKeyColumnWidth = 84.0f;
            constexpr float comboSetGap = 12.0f; // extra space between set A and set B

            // One row of a combo table: label, set A's two keys, a gap, set
            // B's two keys, and Default, which resets all four.
            const auto DrawComboSelect = [&](const EditorCombo combo) {
                keyBindRowDrawn = true;

                const EditorComboInfo& info = COMBO_INFO[combo];

                ImGui::PushID(info.saveName);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Get(info.labelKey).c_str());

                // Columns 1-2 set A, 3 the gap, 4-5 set B.
                for (int slot = 0; slot < COMBO_SLOTS; ++slot) {
                    ImGui::TableSetColumnIndex(slot < 2 ? 1 + slot : 2 + slot);
                    DrawKeySlot(slot, &comboBindings[combo][slot], comboKeyColumnWidth);
                }

                ImGui::TableSetColumnIndex(6);

                if (ImGui::SmallButton(defaultLabel.c_str())) {
                    for (int slot = 0; slot < COMBO_SLOTS; ++slot) {
                        if (keyBindBeingListened == &comboBindings[combo][slot]) keyBindBeingListened = nullptr;
                        comboBindings[combo][slot] = info.defaults[slot];
                    }
                }

                ImGui::PopID();
            };

            // One row of the key bindings table: label, primary, alternate,
            // and Default, which resets both.
            const auto DrawKeySelect = [&](const EditorKeyBind bind) {
                keyBindRowDrawn = true;

                const EditorKeyBindInfo& info = KEY_BIND_INFO[bind];

                ImGui::PushID(info.saveName);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Get(info.labelKey).c_str());

                for (int slot = 0; slot < KEYBIND_SLOTS; ++slot) {
                    ImGui::TableSetColumnIndex(1 + slot);
                    DrawKeySlot(slot, &keyBindings[bind][slot], keyColumnWidth);
                }

                ImGui::TableSetColumnIndex(1 + KEYBIND_SLOTS);

                if (ImGui::SmallButton(defaultLabel.c_str())) {
                    for (int slot = 0; slot < KEYBIND_SLOTS; ++slot) {
                        if (keyBindBeingListened == &keyBindings[bind][slot]) keyBindBeingListened = nullptr;
                        keyBindings[bind][slot] = info.defaults[slot];
                    }
                }

                ImGui::PopID();
            };

            SectionHeader(Get("editor.user_settings.colors").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            DrawGroup(
                "editor.user_settings.group.entities",
                "##EntityColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.normal_entity_color",
                        normalEntityColor,
                        defaultNormalEntityColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.highlighted_entity_color",
                        highlightedEntityColor,
                        defaultHighlightedEntityColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.sprite_entity_color",
                        spriteEntityColor,
                        defaultSpriteEntityColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.gizmo_x_color",
                        gizmoXColor,
                        defaultGizmoXColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.gizmo_y_color",
                        gizmoYColor,
                        defaultGizmoYColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.gizmo_z_color",
                        gizmoZColor,
                        defaultGizmoZColor
                    );
                }
            );

            DrawGroup(
                "editor.user_settings.group.walls",
                "##WallColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.normal_wall_color",
                        normalWallColor,
                        defaultNormalWallColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.portal_wall_color",
                        portalWallColor,
                        defaultPortalWallColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.highlighted_wall_color",
                        highlightedWallColor,
                        defaultHighlightedWallColor
                    );
                }
            );

            DrawGroup(
                "editor.user_settings.group.linedefs",
                "##LinedefColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.linedef_color",
                        linedefColor,
                        defaultLinedefColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.linedef_endpoint_color",
                        linedefEndpointColor,
                        defaultLinedefEndpointColor
                    );
                }
            );

            DrawGroup(
                "editor.user_settings.group.events",
                "##EventColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.event_link_color",
                        eventLinkColor,
                        defaultEventLinkColor
                    );
                }
            );

            DrawGroup(
                "editor.user_settings.group.sectors",
                "##SectorColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.hovered_sector_color",
                        hoveredSectorColor,
                        defaultHoveredSectorColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.highlighted_sector_color",
                        highlightedSectorColor,
                        defaultHighlightedSectorColor
                    );
                }
            );

            DrawGroup(
                "editor.user_settings.group.geometry",
                "##GeometryColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.snap_indicator_color",
                        snapIndicatorColor,
                        defaultSnapIndicator
                    );

                    DrawColor3Setting(
                        "editor.user_settings.valid_line_color",
                        kValidLineColor,
                        defaultKValidLineColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.invalid_line_color",
                        kInvalidLineColor,
                        defaultKInvalidLineColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.anchor_color",
                        kAnchorColor,
                        defaultKAnchorColor
                    );

                    DrawColor4Setting(
                        "editor.user_settings.valid_fill_color",
                        kValidFillColor,
                        defaultKValidFillColor
                    );

                    DrawColor4Setting(
                        "editor.user_settings.invalid_fill_color",
                        kInvalidFillColor,
                        defaultKInvalidFillColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.handle_color",
                        normalHandleColor,
                        defaultNormalHandleColor);

                    DrawColor3Setting(
                        "editor.user_settings.highlighted_handle_color",
                        highlightedHandleColor,
                        defaultHighlightedHandleColor);

                    DrawColor3Setting(
                        "editor.user_settings.handle_outline_color",
                        handleOutlineColor,
                        defaultHandleOutlineColor);

                }
            );

            DrawGroup(
                "editor.user_settings.group.theme",
                "##ThemeColorSettings",
                [&] {
                    DrawColor3Setting(
                        "editor.user_settings.theme_text_color",
                        themeTextColor,
                        defaultThemeTextColor
                    );

                    DrawColor4Setting(
                        "editor.user_settings.grid_color",
                        gridColor,
                        defaultGridColor
                    );

                    DrawColor3Setting(
                        "editor.user_settings.background_color",
                        backgroundColor,
                        defaultBackgroundColor
                    );
                }
            );

            SectionHeader(Get("editor.user_settings.editor_settings").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SliderInt(Get("editor.user_settings.filtered_alpha").c_str(), &filteredAlpha, 0, 255);

            ImGui::SliderFloat(Get("editor.user_settings.event_link_thickness").c_str(), &eventLinkThickness,
                               MIN_EVENT_LINK_THICKNESS, MAX_EVENT_LINK_THICKNESS, "%.1f");
            HoverTooltip(Get("editor.tooltip.user_settings.event_link_thickness").c_str());

            ImGui::SliderFloat(Get("editor.user_settings.linedef_thickness").c_str(), &linedefThickness,
                               MIN_LINEDEF_THICKNESS, MAX_LINEDEF_THICKNESS, "%.1f");
            HoverTooltip(Get("editor.tooltip.user_settings.linedef_thickness").c_str());

            ImGui::SliderInt(Get("editor.user_settings.linedef_endpoint_size").c_str(), &linedefEndpointSize,
                             MIN_LINEDEF_ENDPOINT_SIZE, MAX_LINEDEF_ENDPOINT_SIZE);
            HoverTooltip(Get("editor.tooltip.user_settings.linedef_endpoint_size").c_str());

            ImGui::Separator();
            ImGui::Spacing();

            // Every design in EditorStyle::Designs() is listed here; picking
            // one applies it straight away, and Save below keeps it.
            const EditorStyle::Design *activeDesign = EditorStyle::FindDesign(EditorStyle::ActiveDesign());

            if (ImGui::BeginCombo(Get("editor.user_settings.ui_design").c_str(),
                                  activeDesign != nullptr ? Get(activeDesign->labelKey).c_str() : "")) {
                for (const EditorStyle::Design &design : EditorStyle::Designs()) {
                    const bool selected = &design == activeDesign;

                    if (ImGui::Selectable(Get(design.labelKey).c_str(), selected)) EditorStyle::Apply(design.id);
                    if (selected) ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
            HoverTooltip(Get("editor.tooltip.user_settings.ui_design").c_str());

            ImGui::SliderFloat(Get("editor.user_settings.texture_field_size").c_str(), &textureFieldSize,
                               MIN_TEXTURE_FIELD_SIZE, MAX_TEXTURE_FIELD_SIZE, "%.0f");
            HoverTooltip(Get("editor.tooltip.user_settings.texture_field_size").c_str());

            SectionHeader(Get("editor.user_settings.editor_controls").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SliderFloat(Get("editor.user_settings.camera_speed").c_str(), &cameraSpeed, MIN_CAM_SPEED, MAX_CAM_SPEED);
            ImGui::SliderFloat(Get("editor.user_settings.camera_step_size").c_str(), &cameraStepSize, MIN_STEP_SIZE, MAX_STEP_SIZE);
            ImGui::SliderFloat(Get("editor.user_settings.runtime_camera_fov").c_str(), &runtimeCameraFov,
                               MIN_RUNTIME_CAMERA_FOV, MAX_RUNTIME_CAMERA_FOV, "%.0f");
            HoverTooltip(Get("editor.tooltip.user_settings.runtime_camera_fov").c_str());
            ImGui::Checkbox(Get("editor.user_settings.remember_camera_position").c_str(), &rememberCameraPosition);
            HoverTooltip(Get("editor.tooltip.user_settings.remember_camera_position").c_str());

            // PushSuccessStyle();
            // if (ImGui::Button(Get("editor.user_settings.save").c_str())) SaveUserSettings();
            // PopSuccessStyle();

            ImGui::Spacing();

            const bool keyBindingsOpen = ImGui::CollapsingHeader(Get("editor.user_settings.key_bindings").c_str());
            HoverTooltip(Get("editor.tooltip.user_settings.key_bindings").c_str());

            if (keyBindingsOpen) {
                // KEY_BIND_INFO keeps each group's rows together; a new
                // group starts a new heading and table.
                int bind = 0;

                while (bind < KEYBIND_COUNT) {
                    const char* groupKey = KEY_BIND_INFO[bind].groupKey;

                    ImGui::SeparatorText(Get(groupKey).c_str());

                    const bool tableOpen = ImGui::BeginTable(groupKey, 2 + KEYBIND_SLOTS, tableFlags);

                    if (tableOpen) {
                        ImGui::TableSetupColumn("##Setting", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("##Primary", ImGuiTableColumnFlags_WidthFixed, keyColumnWidth);
                        ImGui::TableSetupColumn("##Alternate", ImGuiTableColumnFlags_WidthFixed, keyColumnWidth);
                        ImGui::TableSetupColumn("##Default", ImGuiTableColumnFlags_WidthFixed, resetColumnWidth);
                    }

                    for (; bind < KEYBIND_COUNT && std::strcmp(KEY_BIND_INFO[bind].groupKey, groupKey) == 0; ++bind)
                        if (tableOpen) DrawKeySelect(static_cast<EditorKeyBind>(bind));

                    if (tableOpen) ImGui::EndTable();
                }

                // The combos, grouped the same way.
                int combo = 0;

                while (combo < COMBO_COUNT) {
                    const char* groupKey = COMBO_INFO[combo].groupKey;

                    ImGui::SeparatorText(Get(groupKey).c_str());

                    const bool tableOpen = ImGui::BeginTable(groupKey, 7, tableFlags);

                    if (tableOpen) {
                        ImGui::TableSetupColumn("##Setting", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("##A1", ImGuiTableColumnFlags_WidthFixed, comboKeyColumnWidth);
                        ImGui::TableSetupColumn("##A2", ImGuiTableColumnFlags_WidthFixed, comboKeyColumnWidth);
                        ImGui::TableSetupColumn("##Gap", ImGuiTableColumnFlags_WidthFixed, comboSetGap);
                        ImGui::TableSetupColumn("##B1", ImGuiTableColumnFlags_WidthFixed, comboKeyColumnWidth);
                        ImGui::TableSetupColumn("##B2", ImGuiTableColumnFlags_WidthFixed, comboKeyColumnWidth);
                        ImGui::TableSetupColumn("##Default", ImGuiTableColumnFlags_WidthFixed, resetColumnWidth);
                    }

                    for (; combo < COMBO_COUNT && std::strcmp(COMBO_INFO[combo].groupKey, groupKey) == 0; ++combo)
                        if (tableOpen) DrawComboSelect(static_cast<EditorCombo>(combo));

                    if (tableOpen) ImGui::EndTable();
                }
            }

            if (!keyBindRowDrawn) keyBindBeingListened = nullptr;
        }
        // Hidden (collapsed or docked away) while listening would leave the
        // shortcuts off with nothing on screen to finish the binding.
        else keyBindBeingListened = nullptr;

        ImGui::End();

        SaveUserSettings();
    }

    // =========================================================================
    //  Layout Menu Window
    // =========================================================================

    void DrawLayoutMenu() {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);

        const bool windowVisible = ImGui::Begin(Get("editor.layout_menu").c_str(), &layoutMenuOpen);

        if (windowVisible) {
            ImGui::Checkbox(Get("editor.layout_menu.hierarchy_visible").c_str(), &hierarchyVisible);
            ImGui::Checkbox(Get("editor.layout_menu.world_settings_visible").c_str(), &worldSettingsVisible);
            ImGui::Checkbox(Get("editor.layout_menu.editor_panel_visible").c_str(), &editorPanelVisible);
            ImGui::Checkbox(Get("editor.layout_menu.asset_browser_visible").c_str(), &assetBrowserVisible);
            ImGui::Checkbox(Get("editor.layout_menu.history_visible").c_str(), &historyVisible);
            ImGui::Checkbox(Get("editor.layout_menu.tag_explorer_visible").c_str(), &tagExplorerVisible);
            ImGui::Checkbox(Get("editor.layout_menu.comments_visible").c_str(), &commentsVisible);
        }

        ImGui::End();
    }

    // =========================================================================
    //  Project Settings window
    // =========================================================================

    void DrawProjectSettingsWindow() {
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);

        const bool windowVisible = ImGui::Begin(Get("editor.project_settings").c_str(), &projectSettingsOpen);

        if (windowVisible) {
            // ---- Current level name -------------------------------------------
            SectionHeader(Get("editor.project.current_level").c_str());
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

            if (ImGui::InputText(("##" + Get("editor.level_name")).c_str(), levelNameBuf, IM_ARRAYSIZE(levelNameBuf))) {
                Editor::currentMap = levelNameBuf;
                lastSyncedMap = Editor::currentMap;
                hasUnsavedChanges = true;
            }

            HoverTooltip(Get("editor.tooltip.level_name").c_str());

            ImGui::Spacing();

            if (FullWidthButton(Get("editor.create_level").c_str())) createLevelModalRequested = true;

            HoverTooltip(Get("editor.tooltip.create_level").c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Start level ----------------------------------------------------
            SectionHeader(Get("editor.project.start_level").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            DrawStartLevelCombo();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Level list ---------------------------------------------------
            SectionHeader(Get("editor.project.all_levels").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            DrawLevelsChildPanel();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Tags -----------------------------------------------------------
            SectionHeader(Get("editor.project.tags").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            DrawTagsSection(TagRegistry::TagKind::NORMAL);

            ImGui::Spacing();
            SectionHeader(Get("editor.project.event_tags").c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", Get("editor.project.event_tags_hint").c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            DrawTagsSection(TagRegistry::TagKind::EVENT);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Global event variables -------------------------------------
            SectionHeader(Get("editor.project.variables").c_str());
            HoverTooltip(Get("editor.tooltip.project.variables").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            DrawGlobalEventVariables();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- UI ---------------------------------------------------------------
            SectionHeader(Get("editor.project.ui").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Saved to project.tilky when the edit is finished, not every keystroke.
            static float referenceHeightEdit = ProjectManager::DEFAULT_UI_REFERENCE_HEIGHT;
            static bool editingReferenceHeight = false;
            if (!editingReferenceHeight) referenceHeightEdit = ProjectManager::GetUIReferenceHeight();

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat(Get("editor.project.ui_reference_height").c_str(), &referenceHeightEdit, 0.0f, 0.0f, "%.0f");
            editingReferenceHeight = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit() && !ProjectManager::SetUIReferenceHeight(referenceHeightEdit))
                ShowNotification(Get("editor.project.ui_reference_height_failed").c_str(), /*isError=*/true);
            HoverTooltip(Get("editor.tooltip.project.ui_reference_height").c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- Textures ---------------------------------------------------------
            SectionHeader(Get("editor.project.textures").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Saved to project.tilky when the edit is finished, not every keystroke.
            static float texelsPerUnitEdit = ProjectManager::DEFAULT_TEXELS_PER_UNIT;
            static bool editingTexelsPerUnit = false;
            if (!editingTexelsPerUnit) texelsPerUnitEdit = ProjectManager::GetTexelsPerUnit();

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat(Get("editor.project.texels_per_unit").c_str(), &texelsPerUnitEdit, 0.0f, 0.0f, "%.2f");
            editingTexelsPerUnit = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit() && !ProjectManager::SetTexelsPerUnit(texelsPerUnitEdit))
                ShowNotification(Get("editor.project.texels_per_unit_failed").c_str(), /*isError=*/true);
            HoverTooltip(Get("editor.tooltip.project.texels_per_unit").c_str());


            //todo TILKYTODO make an editor settings menu
        }

        ImGui::End();

        DrawCreateLevelModal();
    }

    // Asks before closing the editor. Opened from Project > Shutdown.
    void DrawShutdownConfirmModal() {
        if (shutdownConfirmOpen) {
            ImGui::OpenPopup("##ShutdownConfirm");
            shutdownConfirmOpen = false;
        }

        if (ImGui::BeginPopupModal("##ShutdownConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Get("editor.shutdown_confirm").c_str());

            if (hasUnsavedChanges) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.65f, 0.30f, 1.00f));
                ImGui::Text("%s", Get("editor.unsaved_changes_warning").c_str());
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            PushDangerStyle();

            if (ImGui::Button(Get("common.exit").c_str(), ImVec2(80.0f, 0.0f))) {
                shutdown = true;
                quit = true;
                ImGui::CloseCurrentPopup();
            }

            PopDangerStyle();

            ImGui::SameLine();

            if (ImGui::Button(Get("common.cancel").c_str(), ImVec2(80.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // =========================================================================
    //  Level actions - shared by the top menu bar and the toolbar
    // =========================================================================

    void SaveLevelFromEditor() {
        if (Save(Editor::currentMap)) {
            hasUnsavedChanges = false;
            ShowNotification(Get("levels.notification.saved").c_str());
        } else ShowNotification(Get("levels.notification.save_failed_check_logs").c_str(), /*isError=*/true);
    }

    void SaveAndPlayFromEditor() {
        if (Save(Editor::currentMap)) {
            hasUnsavedChanges = false;
            SDL_Log("%s", Editor::currentMap.c_str());
            CancelActiveDrawing(); // don't leave a half-drawn shape pending while playing
            quit = true;
            play = true;
        }
    }

    // Puts the runtime editor camera over the 2D mouse position, at eye
    // height above the floor there. Its height is left alone outside every
    // sector, and its rotation always is.
    void MoveRuntimeCameraToMouse() {
        Level& level = LevelManager::CurrentLevel();

        const Vector2 mouseWorld = ScreenToWorld(InputManager::GetMousePosition(), cameraPos);
        Vector3& position = level.runtimeCamPos;

        position.x = mouseWorld.x;
        position.z = mouseWorld.y;

        const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, mouseWorld);
        if (sectorIndex < 0) return;

        const Sector& sector = level.sectors[sectorIndex];
        const int floorIndex = MapQueries::FindFloorAtHeight(sector, mouseWorld, position.y);
        if (floorIndex < 0) return;

        position.y = MapQueries::SurfaceHeightAt(sector, sector.floors[floorIndex].floor, mouseWorld) +
                     ComponentPlayerController{}.eyeHeight;
    }

    void OpenRuntimeEditorFromEditor() {
        if (!rememberCameraPosition) MoveRuntimeCameraToMouse();

        if (Save(Editor::currentMap)) {
            hasUnsavedChanges = false;
            SDL_Log("%s", Editor::currentMap.c_str());
            CancelActiveDrawing(); // don't carry a half-drawn shape into the Runtime Editor
            switchToRuntime = true;
        }
    }

    void SaveAndQuitFromEditor() {
        if (Save(Editor::currentMap)) {
            hasUnsavedChanges = false;
            SDL_Log("%s", Editor::currentMap.c_str());
            CancelActiveDrawing();
            quit = true;
        }
    }

    // =========================================================================
    //  Top menu bar: Project | Project Settings | User Settings
    // =========================================================================

    // The game window's swap interval, saved with the user settings as soon
    // as it changes. Takes effect on the next Play / runtime editor session.
    void DrawVSyncCombo() {
        struct VSyncOption {
            int mode;
            const char* labelKey;
        };
        static constexpr VSyncOption options[] = {
            {1, "editor.engine_settings.vsync.on"},
            {-1, "editor.engine_settings.vsync.adaptive"},
            {0, "editor.engine_settings.vsync.off"},
        };

        const int current = RuntimeSession::GetVSyncMode();
        const char* currentLabelKey = options[0].labelKey;
        for (const VSyncOption& option : options)
            if (option.mode == current) currentLabelKey = option.labelKey;

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo(Get("editor.engine_settings.vsync").c_str(), Get(currentLabelKey).c_str())) {
            for (const VSyncOption& option : options) {
                const bool selected = option.mode == current;
                if (ImGui::Selectable(Get(option.labelKey).c_str(), selected) && !selected) {
                    RuntimeSession::SetVSyncMode(option.mode);
                    SaveUserSettings();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        HoverTooltip(Get("editor.tooltip.engine_settings.vsync").c_str());
    }

    void DrawEngineSettingsWindow() {
        ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);

        const bool windowVisible = ImGui::Begin(Get("editor.menu.engine_settings").c_str(), &engineSettingsOpen);

        if (windowVisible) {
            DrawVSyncCombo();

            ImGui::Spacing();

            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputDouble(Get("editor.engine_settings.auto_save_in_seconds").c_str(), &autoSaveInSeconds, 1, 10, "%.0f")) {
                autoSaveInSeconds = std::max(0.0, autoSaveInSeconds);
                SaveUserSettings();
            }
            HoverTooltip(Get("editor.tooltip.engine_settings.auto_save_in_seconds").c_str());
        }

        ImGui::End();
    }

    // ---- Export Game: the folder comes from the system's folder picker, which
    // answers on its own thread like the OBJ one below.
    std::mutex gameExportMutex;
    std::optional<fs::path> pendingGameExportFolder;

    void SDLCALL GameExportFolderPicked(void*, const char* const* fileList, int) {
        if (fileList == nullptr || fileList[0] == nullptr) return; // error or cancelled

        std::lock_guard lock(gameExportMutex);
        pendingGameExportFolder = fs::path(fileList[0]);
    }

    void ShowGameExportDialog() {
        const std::string defaultPath = ProjectManager::GetProjectFolder().parent_path().string();
        SDL_ShowOpenFolderDialog(&GameExportFolderPicked, nullptr, window, defaultPath.c_str(), false);
    }

    // A folder an earlier export made: the exporter clears it before writing,
    // so only those (and empty folders) are safe to export into directly.
    bool IsEmptyOrPreviousExport(const fs::path& folder) {
        std::error_code ec;
        if (!fs::exists(folder, ec)) return true;
        if (fs::is_empty(folder, ec)) return true;

        return fs::exists(folder / "project.tilky", ec) && fs::is_directory(folder / "Assets", ec);
    }

    // The exporter deletes whatever is in its folder, so a picked folder with
    // other things in it gets a "<project name>" folder made inside instead.
    // That one has to be empty or an earlier export too.
    void ProcessPendingGameExport() {
        std::optional<fs::path> picked;
        {
            std::lock_guard lock(gameExportMutex);
            picked.swap(pendingGameExportFolder);
        }

        if (!picked.has_value()) return;

        fs::path destination = *picked;

        if (!IsEmptyOrPreviousExport(destination)) {
            std::string projectName = ProjectManager::GetProjectName();
            if (projectName.empty()) projectName = ProjectManager::GetProjectFolder().filename().string();
            if (projectName.empty()) projectName = "Game";

            destination /= projectName;

            if (!IsEmptyOrPreviousExport(destination)) {
                spdlog::error("Export failed: {} already exists and isn't an earlier export.", destination.string());
                ShowNotification((Get("editor.notification.export_failed_folder_in_use") + "\n" + destination.string()).c_str(), true);
                return;
            }
        }

        RunExporter(destination);
    }

    // ---- Export to OBJ: SDL's save dialog answers on its own thread, so
    // the picked path waits here for the next frame.
    std::mutex objExportMutex;
    std::optional<fs::path> pendingObjExportPath;

    void SDLCALL ObjExportPathPicked(void*, const char* const* fileList, int) {
        if (fileList == nullptr || fileList[0] == nullptr) return; // error or cancelled

        std::lock_guard lock(objExportMutex);
        pendingObjExportPath = fs::path(fileList[0]);
    }

    void ShowObjExportDialog() {
        static constexpr SDL_DialogFileFilter FILTERS[] = {{"Wavefront OBJ", "obj"}};

        const std::string defaultPath = (ProjectManager::GetProjectFolder() / (LevelManager::CurrentLevel().name + ".obj")).string();
        SDL_ShowSaveFileDialog(&ObjExportPathPicked, nullptr, window, FILTERS, 1, defaultPath.c_str());
    }

    // The selected sectors (with the walls around them) when there are any,
    // else the whole level.
    void ProcessPendingObjExport() {
        std::optional<fs::path> path;
        {
            std::lock_guard lock(objExportMutex);
            path.swap(pendingObjExportPath);
        }

        if (!path.has_value()) return;

        const ObjExport::Result result = ObjExport::Export(LevelManager::CurrentLevel(), selectedSectors, *path);

        if (!result.ok) {
            spdlog::error("OBJ export failed: {}", result.error);
            ShowNotification((Get("editor.obj_export.failed") + "\n" + result.error).c_str(), true);
            return;
        }

        char message[256];
        std::snprintf(message, sizeof(message), Get("editor.obj_export.done").c_str(), result.faceCount, result.textureCount);
        ShowNotification(message);
    }

    void DrawMainMenuBar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu(Get("editor.menu.project").c_str())) {
                if (ImGui::MenuItem(Get("editor.save").c_str())) SaveLevelFromEditor();
                if (ImGui::MenuItem(Get("editor.save_and_quit").c_str())) SaveAndQuitFromEditor();
                if (ImGui::MenuItem(Get("editor.export").c_str())) ShowGameExportDialog();
                if (ImGui::MenuItem(Get("editor.obj_export").c_str())) ShowObjExportDialog();
                HoverTooltip(Get("editor.tooltip.obj_export").c_str());

                ImGui::Separator();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
                if (ImGui::MenuItem(Get("editor.shutdown").c_str())) shutdownConfirmOpen = true;
                ImGui::PopStyleColor();

                ImGui::EndMenu();
            }

            // Plain items, not menus: a click opens or closes the window.
            if (ImGui::MenuItem(Get("editor.project_settings").c_str(), nullptr, projectSettingsOpen))
                projectSettingsOpen = !projectSettingsOpen;

            if (ImGui::MenuItem(Get("editor.user_settings").c_str(), nullptr, userSettingsOpen))
                userSettingsOpen = !userSettingsOpen;

            if (ImGui::MenuItem(Get("editor.layout_menu").c_str(), nullptr, layoutMenuOpen))
                layoutMenuOpen = !layoutMenuOpen;

            if (ImGui::MenuItem(Get("editor.menu.engine_settings").c_str(), nullptr, engineSettingsOpen))
                engineSettingsOpen = !engineSettingsOpen;

            ImGui::EndMainMenuBar();
        }

        if (projectSettingsOpen) DrawProjectSettingsWindow();
        if (userSettingsOpen) DrawUserSettingsWindow();
        else keyBindBeingListened = nullptr;
        if (layoutMenuOpen) DrawLayoutMenu();
        if (engineSettingsOpen) DrawEngineSettingsWindow();

        DrawShutdownConfirmModal();
    }

    // Bottom bar styled like the main menu bar, showing the mouse position in
    // level coordinates. Submitted before the dockspace so it shrinks the
    // viewport's work area instead of covering docked panels.
    void DrawStatusBar() {
        constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_MenuBar;

        const float height = ImGui::GetFrameHeight();
        if (ImGui::BeginViewportSideBar("##MapEditorStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags)) {
            if (ImGui::BeginMenuBar()) {
                const Vector2 mouseWorld = ScreenToWorld(InputManager::GetMousePosition(), cameraPos);

                char text[64];
                std::snprintf(text, sizeof(text), "X: %.2f   Y: %.2f", mouseWorld.x, mouseWorld.y);
                ImGui::TextUnformatted(text);

                ImGui::EndMenuBar();
            }
        }

        ImGui::End();
    }

    // =========================================================================
    //  Toolbar: square icon buttons, centred just below the menu bar
    // =========================================================================

    // One square button. Shows `icon` (a Font Awesome glyph) when the icon
    // font loaded, otherwise `fallback`; the full name and what it does go
    // in the tooltip.
    bool ToolbarButton(const char *id, const char *icon, const char *fallback, const std::string &name,
                       const std::string &tooltip) {
        constexpr float BUTTON_SIZE = 32.0f;

        ImGui::PushID(id);

        if (editorIconFont != nullptr) ImGui::PushFont(editorIconFont, 0.0f);
        const bool pressed = ImGui::Button(editorIconFont != nullptr ? icon : fallback, ImVec2(BUTTON_SIZE, BUTTON_SIZE));
        if (editorIconFont != nullptr) ImGui::PopFont();

        HoverTooltip((name + "\n" + tooltip).c_str());

        ImGui::PopID();
        return pressed;
    }

    void DrawToolbar() {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        constexpr float margin = 6.0f;

        // WorkPos is already below the main menu bar.
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + margin),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.0f)
        );

        ImGui::SetNextWindowBgAlpha(0.0f);

        constexpr ImGuiWindowFlags overlayFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::Begin("##EditorToolbarOverlay", nullptr, overlayFlags);

        if (ToolbarButton("save", ICON_FA_FLOPPY_DISK, "S", Get("editor.save"), Get("editor.tooltip.save")))
            SaveLevelFromEditor();

        ImGui::SameLine();

        PushSuccessStyle();
        const bool playPressed = ToolbarButton("save_and_play", ICON_FA_PLAY, ">", Get("editor.save_and_play"),
                                               FormatKeyBinds(Get("editor.tooltip.save_and_play")));
        PopSuccessStyle();
        if (playPressed) SaveAndPlayFromEditor();

        ImGui::SameLine();

        if (ToolbarButton("runtime_editor", ICON_FA_CUBE, "RT", Get("editor.runtime_editor"),
                          Get("editor.tooltip.runtime_editor")))
            OpenRuntimeEditorFromEditor();

        ImGui::SameLine();

        if (ToolbarButton("edit_ui", ICON_FA_DISPLAY, "UI", Get("editor.switch_to_ui"), Get("editor.tooltip.switch_to_ui")))
            currentState = STATE_UI;

        ImGui::SameLine();

        // The paint selection brush, lit while on; its radius beside it.
        const bool paintWasOn = paintSelectTool;
        if (paintWasOn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ToolbarButton("paint_select", ICON_FA_PAINTBRUSH, "PS", Get("editor.paint_select"), FormatKeyBinds(Get("editor.tooltip.paint_select"))))
            paintSelectTool = !paintSelectTool;
        if (paintWasOn) ImGui::PopStyleColor();

        if (paintSelectTool) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderFloat("##PaintSelectRadius", &paintSelectRadius, MIN_PAINT_SELECT_RADIUS, MAX_PAINT_SELECT_RADIUS, Get("editor.paint_select.radius").c_str());
        }

        ImGui::End();
    }
} // anonymous namespace

namespace MapEditorInternal {
    void OpenRuntimeEditor() {
        OpenRuntimeEditorFromEditor();
    }

    void HandleEditorShortcuts(const bool keyboardBlockedByImgui) {
        if (keyboardBlockedByImgui) return;

        if (GetComboDown(COMBO_COMMAND_PALETTE)) CommandPalette::Open();

        if (GetComboDown(COMBO_SAVE)) SaveLevelFromEditor();
        if (GetKeyBindDown(KEYBIND_TOGGLE_TEXTURE_VIEW))
            textureViewMode = static_cast<TextureViewMode>(
                (static_cast<int>(textureViewMode) + 1) % static_cast<int>(TextureViewMode::Count));

        if (GetKeyBindDown(KEYBIND_TOGGLE_UI_EDITOR)) currentState = STATE_UI;
        else if (GetKeyBindDown(KEYBIND_TOGGLE_RUNTIME_EDITOR)) OpenRuntimeEditorFromEditor();
        else if (GetKeyBindDown(KEYBIND_SAVE_AND_PLAY)) SaveAndPlayFromEditor(); // same as the toolbar's Save & Play
    }
} // namespace MapEditorInternal

namespace {

    // =========================================================================
    //  Hierarchy panel — search, type-coloured icons, counts,
    //  deferred deletion, copy-ID context menu
    // =========================================================================

    void DrawHierarchyPanel(Level &level) {
        ImGui::Begin(Get("editor.hierarchy").c_str());

        // Search bar
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4.0f);
        ImGui::InputText("##HierarchySearch", hierarchySearchBuf, sizeof(hierarchySearchBuf));
        HoverTooltip(Get("editor.hierarchy.tooltip.filter").c_str());

        ImGui::SameLine(0.0f, 4.0f);
        if (ClearSearchButton("ClearHierarchySearch")) hierarchySearchBuf[0] = '\0';
        HoverTooltip(Get("editor.hierarchy.tooltip.clear_filter").c_str());

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
        auto matches = [&](const std::string &label) -> bool {
            if (!filtering) return true;
            std::string lower = label;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lower.find(searchLower) != std::string::npos;
        };

        // Multi-selection notice. The highlighted rows already show *what*
        // is selected; this says what editing does with it, because an
        // inspector field silently writing to five sectors is the kind of
        // thing that needs saying once rather than being discovered.
        {
            const size_t sectorCount = selectedSectors.size();
            const size_t wallCount = selectedWalls.size();
            const size_t entityCount = selectedEntities.size();

            const char *plural = nullptr;
            size_t count = 0;

            if (currentMode == MODE_SECTOR && sectorCount > 1) {
                plural = "sectors";
                count = sectorCount;
            } else if (currentMode == MODE_GEOMETRY && linedefMode && selectedLinedefs.size() > 1) {
                plural = "linedefs";
                count = selectedLinedefs.size();
            } else if (currentMode == MODE_GEOMETRY && wallCount > 1) {
                plural = "walls";
                count = wallCount;
            } else if (currentMode == MODE_ENTITY && entityCount > 1) {
                plural = "entities";
                count = entityCount;
            }

            if (plural != nullptr) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.85f, 0.55f, 1.00f));
                ImGui::TextWrapped("%d %s selected - inspector edits apply to all of them.",
                                   static_cast<int>(count), plural);
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
        }

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

                for (const Sector &sector: level.sectors) {
                    const std::string label = SectorHierarchyLabel(sector);
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(sector.id));

                    DrawHierarchyTypeIcon(ICON_FA_DRAW_POLYGON, "[S]", ImVec4(0.70f, 0.85f, 1.00f, 1.00f));

                    ImGui::SameLine(0.0f, 6.0f);

                    const auto selectedIt = std::find(selectedSectors.begin(), selectedSectors.end(), sector.id);

                    const bool selected = selectedIt != selectedSectors.end();
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        currentMode = MODE_SECTOR;

                        // Routed through the shared selection helpers, the
                        // same way the wall list below already is, rather
                        // than assigning selectedSectorID/selectedSectors by
                        // hand - so a hierarchy click and a canvas click
                        // leave identical state.
                        //
                        // One behaviour change falls out of this: Shift no
                        // longer needs the inspector to already be open to
                        // range-select, only a previous primary, which is
                        // what you'd expect after a Ctrl-click.
                        if (GetKeyBind(KEYBIND_RANGE_SELECT))
                            ExtendSectorSelectionTo(sector.id);
                        else if (GetKeyBind(KEYBIND_MULTI_SELECT))
                            ToggleSectorSelection(sector.id);
                        else SelectSector(sector.id);

                        // The hierarchy is the one place that opens the
                        // inspector on a plain click; the canvas does not.
                        editingSector = selectedSectorID != INVALID_ID;
                    }

                    HierarchyRowDragSource(SECTOR_REF_PAYLOAD, sector.id, label);

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            sectorPendingDelete = sector.id;
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();

                if (sectorPendingDelete != INVALID_ID) {
                    GeometrySnapshot before = CaptureGeometryUndoSnapshot();
                    DeleteSector(sectorPendingDelete);
                    PushGeometryUndoSnapshot(std::move(before), "editor.history.delete_sectors");
                    hasUnsavedChanges = true;
                }
            }
        }

        // ---- Walls --------------------------------------------------------
        {
            const bool open = ImGui::CollapsingHeader(
                "Walls",
                ImGuiTreeNodeFlags_DefaultOpen
            );
            DrawCountBadge(static_cast<int>(level.walls.size()));

            if (open) {
                ImGui::Indent();

                ID wallPendingDelete = INVALID_ID;

                for (const Wall &wall: level.walls) {
                    const std::string label = WallHierarchyLabel(wall);
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(wall.id));

                    DrawHierarchyTypeIcon(ICON_FA_GRIP_LINES_VERTICAL, "[W]", ImVec4(1.00f, 0.65f, 0.65f, 1.00f));

                    ImGui::SameLine(0.0f, 6.0f);

                    const auto selectedIt = std::find(selectedWalls.begin(), selectedWalls.end(), wall.id);

                    const bool selected = selectedIt != selectedWalls.end();
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        // Clicking a wall in the hierarchy is the same
                        // gesture as clicking it on the canvas: switch to
                        // Geometry Mode and open the wall inspector.
                        if (currentMode != MODE_GEOMETRY) {
                            const Mode previousMode = currentMode;
                            currentMode = MODE_GEOMETRY;

                            if (previousMode == MODE_SECTOR) CancelActiveDrawing();
                        }

                        // Routed through the same selection helpers the
                        // canvas uses (rather than assigning the three
                        // pieces of selection state by hand, which is what
                        // the sector and entity lists used to do) so the
                        // hierarchy can't leave the selection in a shape
                        // the canvas considers impossible - a primary wall
                        // that isn't in selectedWalls would show handles
                        // for a wall the inspector isn't editing.
                        // Shift takes the ID range from the previous
                        // primary, Ctrl toggles, a plain click replaces -
                        // and both modifier keys count, matching the canvas.
                        if (GetKeyBind(KEYBIND_RANGE_SELECT))
                            ExtendWallSelectionTo(wall.id);
                        else if (GetKeyBind(KEYBIND_MULTI_SELECT))
                            ToggleWallSelection(wall.id);
                        else SelectWall(wall.id);
                    }

                    HierarchyRowDragSource(WALL_REF_PAYLOAD, wall.id, label);

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            wallPendingDelete = wall.id;
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();

                if (wallPendingDelete != INVALID_ID) {
                    DeleteWall(wallPendingDelete);
                    hasUnsavedChanges = true;
                }
            }
        }

        // ---- Linedefs -----------------------------------------------------
        {
            const bool open = ImGui::CollapsingHeader(
                "Linedefs",
                ImGuiTreeNodeFlags_DefaultOpen
            );
            DrawCountBadge(static_cast<int>(level.linedefs.size()));

            if (open) {
                ImGui::Indent();

                ID linedefPendingDelete = INVALID_ID;

                for (const Linedef &linedef: level.linedefs) {
                    const std::string label = LinedefHierarchyLabel(linedef);
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(linedef.id));

                    DrawHierarchyTypeIcon(ICON_FA_GRIP_LINES_VERTICAL, "[L]", ImVec4(0.53f, 0.81f, 0.98f, 1.00f));

                    ImGui::SameLine(0.0f, 6.0f);

                    const bool selected = std::ranges::find(selectedLinedefs, linedef.id) != selectedLinedefs.end();
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        // Like a wall row: into Geometry Mode (and Linedef
                        // Mode), then the same selection helpers the canvas uses.
                        if (currentMode != MODE_GEOMETRY) {
                            const Mode previousMode = currentMode;
                            currentMode = MODE_GEOMETRY;

                            if (previousMode == MODE_SECTOR) CancelActiveDrawing();
                        }

                        SetLinedefMode(true);

                        if (GetKeyBind(KEYBIND_RANGE_SELECT))
                            ExtendLinedefSelectionTo(linedef.id);
                        else if (GetKeyBind(KEYBIND_MULTI_SELECT))
                            ToggleLinedefSelection(linedef.id);
                        else SelectLinedef(linedef.id);
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            linedefPendingDelete = linedef.id;
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();

                if (linedefPendingDelete != INVALID_ID) {
                    // Through the selection, so it is one undo step like Delete.
                    const std::vector<ID> keptSelection = selectedLinedefs;

                    selectedLinedefs = {linedefPendingDelete};
                    DeleteSelectedLinedefs();

                    for (const ID linedefID : keptSelection)
                        if (linedefID != linedefPendingDelete) selectedLinedefs.push_back(linedefID);

                    selectedLinedefID = selectedLinedefs.empty() ? INVALID_ID : selectedLinedefs.front();
                }
            }
        }

        // ---- Entities / UI Entities --------------------------------------
        // Both live in level.entities; an entity carrying a UI transform is
        // what the UI Editor edits, so it gets its own section here. Clicking
        // one still opens the regular entity inspector - a UI entity is a
        // normal entity (scripts, tags, ...), the UI Editor just owns its
        // on-screen layout.
        auto drawEntitySection = [&](const std::string &header, const bool uiEntities,
                                     const char *icon, const char *tag, const ImVec4 &tagColor) {
            int count = 0;
            for (Entity &entity: level.entities)
                if (entity.HasComponent<ComponentUITransform>() == uiEntities) ++count;

            const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            DrawCountBadge(count);

            if (open) {
                ImGui::Indent();

                ID entityPendingDelete = INVALID_ID;

                for (Entity &entity: level.entities) {
                    if (entity.HasComponent<ComponentUITransform>() != uiEntities) continue;

                    const std::string label = entity.name + "  (#" + std::to_string(entity.id) + ")";
                    if (!matches(label)) continue;

                    ImGui::PushID(static_cast<int>(entity.id));

                    DrawHierarchyTypeIcon(icon, tag, tagColor);

                    ImGui::SameLine(0.0f, 6.0f);

                    const auto selectedIt = std::find(selectedEntities.begin(), selectedEntities.end(), entity.id);

                    const bool selected = selectedIt != selectedEntities.end();
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        currentMode = MODE_ENTITY;

                        // Same routing as the sector and wall lists above.
                        if (GetKeyBind(KEYBIND_RANGE_SELECT))
                            ExtendEntitySelectionTo(entity.id);
                        else if (GetKeyBind(KEYBIND_MULTI_SELECT))
                            ToggleEntitySelection(entity.id);
                        else SelectEntity(entity.id);

                        editingEntity = !selectedEntities.empty();
                    }

                    HierarchyRowDragSource(ENTITY_REF_PAYLOAD, entity.id, label);

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(Get("editor.delete").c_str()))
                            entityPendingDelete = entity.id;

                        ImGui::Separator();

                        if (ImGui::MenuItem(Get("common.copy_id").c_str())) {
                            char buf[32];
                            std::snprintf(buf, sizeof(buf), "%u",
                                          static_cast<unsigned>(entity.id));
                            ImGui::SetClipboardText(buf);
                            ShowNotification(Get("editor.notification.entity_id_copied").c_str());
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
                    if (selectedUIEntityID == entityPendingDelete) selectedUIEntityID = INVALID_ID;
                    EntitiesSnapshot before = CaptureEntitiesSnapshot();
                    level.DestroyEntity(entityPendingDelete);
                    PushEntitiesUndo(std::move(before), "editor.history.delete_entities");
                    hasUnsavedChanges = true;
                }
            }
        };

        drawEntitySection(Get("editor.hierarchy.entities"), false, ICON_FA_CUBE, "[E]", ImVec4(0.70f, 1.00f, 0.70f, 1.00f));
        drawEntitySection(Get("editor.hierarchy.ui_entities"), true, ICON_FA_DISPLAY, "[UI]", ImVec4(0.95f, 0.80f, 1.00f, 1.00f));

        // Dots deliberately have no hierarchy section: they are internal
        // snap anchors, not user-facing objects (see EditorInternal.hpp).
        // Walls, above, are what Geometry Mode edits.

        ImGui::PopStyleVar();
        ImGui::End();
    }

    // =========================================================================
    //  Mode switcher — three inline toggle buttons
    // =========================================================================

    void DrawMode() {
        const float buttonWidth = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;

        // Geometry / Wall Edit Mode button (replaced the Dot Mode button)
        {
            const bool active = (currentMode == MODE_GEOMETRY);

            PushToggleStyle(active);

            if (ImGui::Button(Get("mode.geometry").c_str(), ImVec2(buttonWidth, 0.0f))) {
                if (currentMode != MODE_GEOMETRY) {
                    const Mode previousMode = currentMode;
                    currentMode = MODE_GEOMETRY;

                    if (previousMode == MODE_SECTOR) CancelActiveDrawing();
                }
            }

            PopToggleStyle();
        }

        ImGui::SameLine(0.0f, 4.0f);

        // Sector Mode button
        {
            const bool active = (currentMode == MODE_SECTOR);

            PushToggleStyle(active);

            if (ImGui::Button(Get("mode.sector").c_str(), ImVec2(buttonWidth, 0.0f)))
                if (currentMode != MODE_SECTOR) currentMode = MODE_SECTOR;

            PopToggleStyle();
        }

        ImGui::SameLine(0.0f, 4.0f);

        // Entity Mode button
        {
            const bool active = (currentMode == MODE_ENTITY);

            PushToggleStyle(active);

            if (ImGui::Button(Get("mode.entity").c_str(), ImVec2(buttonWidth, 0.0f))) {
                if (currentMode != MODE_ENTITY) {
                    const Mode previousMode = currentMode;
                    currentMode = MODE_ENTITY;

                    if (previousMode == MODE_SECTOR) CancelActiveDrawing();
                }
            }

            PopToggleStyle();
        }

        // In-progress sector chain reminder
        if (currentMode == MODE_SECTOR && !sectorBeingCreated.empty()) {
            const int n = static_cast<int>(sectorBeingCreated.size());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.80f, 0.30f, 1.00f));

            const std::string placingText = (n == 1)
                                                ? Get("editor.sector_chain_placing_singular")
                                                : Get("editor.sector_chain_placing_plural");

            ImGui::Text(placingText.c_str(), n);
            HintText(Get("editor.sector_chain_hint").c_str());
            ImGui::PopStyleColor();
        }
    }

    // =========================================================================
    //  Multi-edit — fanning one inspector edit out across a selection
    // =========================================================================
    //
    // The inspectors below still edit exactly one object: the primary. What
    // changes with a multi-selection is what happens either side of the
    // draw call - the primary's editable fields are snapshotted before,
    // diffed after, and only the fields that *actually changed* are copied
    // onto the rest of the selection. Fields the user didn't touch keep
    // their per-object values, so selecting five sectors and typing a
    // ceiling height doesn't also flatten their textures to match.
    //
    // Doing it here rather than inside ImGuiDrawFunctions keeps those
    // inspectors completely unaware of multi-selection, which matters
    // because the Runtime Editor shares them.
    //
    // Deliberately NOT propagated, and why:
    //   - Sector vertices/innerLoops and wall start/end. Copying one wall's
    //     coordinates onto four others would stack them. Wall coordinate
    //     edits already go through MoveSharedEndpoint below, which is the
    //     correct shared-corner move.
    //   - Wall frontSector/backSector: per-wall topology links. Writing one
    //     wall's links onto its neighbours corrupts the map.
    //   - Sector floor count (Add/Remove Floor). Per-floor *fields* fan out,
    //     but resizing another sector's floor stack silently discards its
    //     heights and textures.
    //   - Entity name, and component add/remove.

    // Componentwise, because the vector types don't promise an operator==.
    // Exact inequality is deliberate rather than an epsilon compare: these
    // compare a value against the same value one frame later, and the only
    // thing that can have written it in between is an ImGui widget storing
    // an exact float.
    [[nodiscard]] bool ValueChanged(const Vector2 &a, const Vector2 &b) {
        return a.x != b.x || a.y != b.y;
    }

    [[nodiscard]] bool ValueChanged(const Vector3 &a, const Vector3 &b) {
        return a.x != b.x || a.y != b.y || a.z != b.z;
    }

    [[nodiscard]] bool ValueChanged(const Vector4 &a, const Vector4 &b) {
        return a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w;
    }

    struct SectorEditSnapshot {
        decltype(Sector::floors) floors;
        Vector3 light{};
    };

    struct WallEditSnapshot {
        Vector4 color{};
        WallSurface top;
        WallSurface bottom;
        WallSurface middle;
        bool middleHasCollision = true;
        bool middleIgnoredByRaycast = false;
    };

    struct EntityTransformSnapshot {
        bool valid = false;
        Vector3 position{};
        Quaternion rotation{};
        Vector3 scale{};
    };

    [[nodiscard]] SectorEditSnapshot CaptureSectorEditSnapshot(const Sector &sector) {
        SectorEditSnapshot snapshot;
        snapshot.floors = sector.floors;
        snapshot.light = sector.light;
        return snapshot;
    }

    bool PropagateSectorEdits(Level &level,
                              const SectorEditSnapshot &before,
                              const Sector &primary,
                              const std::vector<ID> &selection,
                              const ID primaryID) {
        if (selection.size() <= 1) return false;

        // Add/Remove Floor on the primary: per-floor fields stop fanning out
        // for this frame rather than trying to line up two different floor
        // stacks against each other. Lighting below still applies.
        const bool floorCountMatches = before.floors.size() == primary.floors.size();

        // Generic over the floor/ceiling surface type so this file never has
        // to name it - both sides are the same type, whatever Sector.hpp
        // calls it.
        const auto applySurface = [](const auto &surfaceBefore,
                                     const auto &surfaceAfter,
                                     auto &target,
                                     bool &heightWritten) {
            bool wrote = false;

            if (surfaceAfter.height != surfaceBefore.height) {
                target.height = surfaceAfter.height;
                heightWritten = true;
                wrote = true;
            }

            if (surfaceAfter.slopeDirection != surfaceBefore.slopeDirection) {
                target.slopeDirection = surfaceAfter.slopeDirection;
                wrote = true;
            }

            if (surfaceAfter.slopeStrength != surfaceBefore.slopeStrength) {
                target.slopeStrength = surfaceAfter.slopeStrength;
                wrote = true;
            }

            if (surfaceAfter.texture != surfaceBefore.texture) {
                target.texture = surfaceAfter.texture;
                wrote = true;
            }

            if (ValueChanged(surfaceBefore.textureOffset, surfaceAfter.textureOffset)) {
                target.textureOffset = surfaceAfter.textureOffset;
                wrote = true;
            }

            if (ValueChanged(surfaceBefore.textureScale, surfaceAfter.textureScale)) {
                target.textureScale = surfaceAfter.textureScale;
                wrote = true;
            }

            if (surfaceAfter.flipTextureX != surfaceBefore.flipTextureX) {
                target.flipTextureX = surfaceAfter.flipTextureX;
                wrote = true;
            }

            if (surfaceAfter.flipTextureY != surfaceBefore.flipTextureY) {
                target.flipTextureY = surfaceAfter.flipTextureY;
                wrote = true;
            }

            if (ValueChanged(surfaceBefore.color, surfaceAfter.color)) {
                target.color = surfaceAfter.color;
                wrote = true;
            }

            return wrote;
        };

        const bool lightChanged = ValueChanged(before.light, primary.light);

        bool changedAnything = false;

        for (const ID sectorID: selection) {
            if (sectorID == primaryID) continue;

            const auto it = level.sectorIDToIndex.find(sectorID);
            if (it == level.sectorIDToIndex.end()) continue;

            Sector &target = level.sectors[it->second];

            if (lightChanged) {
                target.light = primary.light;
                changedAnything = true;
            }

            if (!floorCountMatches) continue;

            // Floor N of the primary maps to floor N of the target. A sector
            // with fewer floors than the primary simply doesn't receive the
            // edits for the floors it doesn't have.
            const size_t floorCount = std::min(primary.floors.size(), target.floors.size());

            for (size_t floorIndex = 0; floorIndex < floorCount; ++floorIndex) {
                const auto &floorBefore = before.floors[floorIndex];
                const auto &floorAfter = primary.floors[floorIndex];
                auto &floorTarget = target.floors[floorIndex];

                bool heightWritten = false;

                const bool wroteFloor =
                        applySurface(floorBefore.floor, floorAfter.floor, floorTarget.floor, heightWritten);

                const bool wroteCeiling =
                        applySurface(floorBefore.ceiling, floorAfter.ceiling, floorTarget.ceiling, heightWritten);

                if (wroteFloor || wroteCeiling) changedAnything = true;

                // A height copied from a sector with a different stack can
                // land inverted (floor above its own ceiling). Push the
                // ceiling up rather than refusing the edit - refusing would
                // leave the selection half-updated with nothing on screen
                // explaining why.
                if (heightWritten && floorTarget.floor.height >= floorTarget.ceiling.height)
                    floorTarget.ceiling.height = floorTarget.floor.height + MIN_ROOM_HEIGHT;
            }
        }

        return changedAnything;
    }

    [[nodiscard]] WallEditSnapshot CaptureWallEditSnapshot(const Wall &wall) {
        WallEditSnapshot snapshot;
        snapshot.color = wall.color;
        snapshot.top = wall.top;
        snapshot.bottom = wall.bottom;
        snapshot.middle = wall.middle;
        snapshot.middleHasCollision = wall.middleHasCollision;
        snapshot.middleIgnoredByRaycast = wall.middleIgnoredByRaycast;
        return snapshot;
    }

    // Field by field, so an edit to the Top texture's offset doesn't also
    // copy the primary's texture onto every selected wall.
    bool ApplyWallSurfaceChange(const WallSurface &before, const WallSurface &after, WallSurface &target) {
        bool wrote = false;

        if (after.texture != before.texture) {
            target.texture = after.texture;
            wrote = true;
        }

        if (ValueChanged(before.textureOffset, after.textureOffset)) {
            target.textureOffset = after.textureOffset;
            wrote = true;
        }

        if (ValueChanged(before.textureScale, after.textureScale)) {
            target.textureScale = after.textureScale;
            wrote = true;
        }

        if (after.flipTextureX != before.flipTextureX) {
            target.flipTextureX = after.flipTextureX;
            wrote = true;
        }

        if (after.flipTextureY != before.flipTextureY) {
            target.flipTextureY = after.flipTextureY;
            wrote = true;
        }

        if (after.anchor != before.anchor) {
            target.anchor = after.anchor;
            wrote = true;
        }

        return wrote;
    }

    bool PropagateWallEdits(Level &level,
                            const WallEditSnapshot &before,
                            const Wall &primary,
                            const std::vector<ID> &selection,
                            const ID primaryID) {
        if (selection.size() <= 1) return false;

        const bool colorChanged = ValueChanged(before.color, primary.color);

        bool changedAnything = false;

        for (const ID wallID: selection) {
            if (wallID == primaryID) continue;

            const auto it = level.wallIDToIndex.find(wallID);
            if (it == level.wallIDToIndex.end()) continue;

            Wall &target = level.walls[it->second];

            if (colorChanged) {
                target.color = primary.color;
                changedAnything = true;
            }

            if (ApplyWallSurfaceChange(before.top, primary.top, target.top)) changedAnything = true;
            if (ApplyWallSurfaceChange(before.bottom, primary.bottom, target.bottom)) changedAnything = true;
            if (ApplyWallSurfaceChange(before.middle, primary.middle, target.middle)) changedAnything = true;

            if (primary.middleHasCollision != before.middleHasCollision) {
                target.middleHasCollision = primary.middleHasCollision;
                changedAnything = true;
            }

            if (primary.middleIgnoredByRaycast != before.middleIgnoredByRaycast) {
                target.middleIgnoredByRaycast = primary.middleIgnoredByRaycast;
                changedAnything = true;
            }
        }

        return changedAnything;
    }

    // Entities: ComponentTransform only for now. Every other component's
    // inspector is hand-written per type, so fanning those out means either a
    // switch that has to grow with each new component or a generic field
    // walk - and the centralised per-component field metadata added for the
    // Lua/JSON refactor is exactly the table that walk should use, which
    // would cover every component at once including ones that don't exist
    // yet.
    //
    // Position fans out as a DELTA, not an absolute: copying the primary's
    // position onto the selection would stack every selected entity on one
    // point. Rotation and scale fan out absolutely, which is what "make them
    // all face the same way / all this big" means.

    [[nodiscard]] EntityTransformSnapshot CaptureEntityTransformSnapshot(Level &level, const ID entityID) {
        EntityTransformSnapshot snapshot;

        const ComponentTransform *transform = level.transforms.Get(entityID);
        if (transform == nullptr) return snapshot;

        snapshot.valid = true;
        snapshot.position = transform->position;
        snapshot.rotation = transform->rotation;
        snapshot.scale = transform->scale;
        return snapshot;
    }

    bool PropagateEntityTransformEdits(Level &level,
                                       const EntityTransformSnapshot &before,
                                       const std::vector<ID> &selection,
                                       const ID primaryID) {
        if (!before.valid || selection.size() <= 1) return false;

        const ComponentTransform *primary = level.transforms.Get(primaryID);
        if (primary == nullptr) return false;

        const Vector3 positionDelta = {
            primary->position.x - before.position.x,
            primary->position.y - before.position.y,
            primary->position.z - before.position.z
        };

        const bool moved = positionDelta.x != 0.0f || positionDelta.y != 0.0f || positionDelta.z != 0.0f;

        // Rotation compared through Euler degrees rather than raw quaternion
        // components: the inspector round-trips it through ToEulerDegrees/
        // FromEulerDegrees anyway, so this compares the values the user
        // actually sees and typed.
        const bool rotated = ValueChanged(before.rotation.ToEulerDegrees(), primary->rotation.ToEulerDegrees());

        const bool scaled = ValueChanged(before.scale, primary->scale);

        if (!moved && !rotated && !scaled) return false;

        // Cached before the loop, because writing through level.transforms
        // below could otherwise invalidate the pointer mid-iteration.
        const Quaternion primaryRotation = primary->rotation;
        const Vector3 primaryScale = primary->scale;

        for (const ID entityID: selection) {
            if (entityID == primaryID) continue;

            ComponentTransform *target = level.transforms.Get(entityID);
            if (target == nullptr) continue;

            if (moved)
                target->SetPosition({
                    target->position.x + positionDelta.x,
                    target->position.y + positionDelta.y,
                    target->position.z + positionDelta.z
                });

            if (rotated) target->rotation = primaryRotation;
            if (scaled) target->scale = primaryScale;

            target->isDirty = true;
        }

        return true;
    }

    // =========================================================================
    //  Selection inspectors
    // =========================================================================

    // Fills the Editor Panel's New Sector Properties from an existing sector,
    // so the next sector drawn matches it. Only the bottom floor is copied
    // (new sectors have one), and the wall values come from the first wall
    // in level.walls that borders the sector. DrawEditorPanel() pushes these
    // into pendingSectorParams.
    void CopySectorToNewSectorProperties(const Level &level, const Sector &sector) {
        if (!sector.floors.empty()) {
            const SectorFloor &bottomFloor = sector.floors.front();

            floorHeight = bottomFloor.floor.height;
            floorColor = bottomFloor.floor.color;
            floorTexture = bottomFloor.floor.texture;

            ceilHeight = bottomFloor.ceiling.height;
            ceilColor = bottomFloor.ceiling.color;
            ceilTexture = bottomFloor.ceiling.texture;
        }

        lightValue = sector.light;

        const auto firstWall = std::ranges::find_if(level.walls, [&sector](const Wall &wall) {
            return wall.frontSector == sector.id || wall.backSector == sector.id;
        });

        if (firstWall != level.walls.end()) {
            wallTopTexture = firstWall->top.texture;
            wallBottomTexture = firstWall->bottom.texture;
            wallColor = firstWall->color;
        }

        hasUnsavedChanges = true;
        ShowNotification(Get("editor.notification.sector_properties_copied").c_str());
    }

    void DrawSelectedSectorInspector(Level &level) {
        if (!editingSector || currentMode != MODE_SECTOR) return;

        const auto it = level.sectorIDToIndex.find(selectedSectorID);
        if (it == level.sectorIDToIndex.end()) {
            selectedSectorID = INVALID_ID;
            editingSector = false;
            return;
        }

        Sector &sector = level.sectors[it->second];

        // Snapshot before / propagate after - see the multi-edit block above.
        const bool multiEdit = selectedSectors.size() > 1;

        const SectorEditSnapshot beforeEdit =
                multiEdit ? CaptureSectorEditSnapshot(sector) : SectorEditSnapshot{};

        // Parent -> child propagation runs after multi-edit, so each edited
        // sector in the selection passes its own change on to its children.
        const auto parentsBeforeEdit = level.SnapshotParentSectorValues();

        bool copyRequested = false;
        ImGuiDrawFunctions::MakeDoorRequest makeDoorRequest;

        BeginSectorInspectorEdit(sectorInspectorEdit, sector);

        if (ImGuiDrawFunctions::DrawSectorEditor(sector, &editingSector, DRAGGABLE, &copyRequested, &makeDoorRequest)) {
            // Delete acts on the whole selection, the way the Delete key
            // already does.
            const std::vector<ID> sectorsToDelete =
                    selectedSectors.empty() ? std::vector<ID>{selectedSectorID} : selectedSectors;

            ClearSectorSelection();

            GeometrySnapshot before = CaptureGeometryUndoSnapshot();
            for (const ID sectorID: sectorsToDelete) DeleteSector(sectorID);
            PushGeometryUndoSnapshot(std::move(before), "editor.history.delete_sectors", static_cast<int>(sectorsToDelete.size()));

            hasUnsavedChanges = true;
            return;
        }

        // Before the propagation below, so the step also holds the rest of
        // the selection and the children as they were.
        RecordGeometryInspectorEdit(level, sectorInspectorEdit, "editor.history.edit_sectors");

        if (multiEdit && PropagateSectorEdits(level, beforeEdit, sector, selectedSectors, selectedSectorID))
            hasUnsavedChanges = true;

        level.PropagateSnapshottedSectorChanges(parentsBeforeEdit);

        if (copyRequested) CopySectorToNewSectorProperties(level, sector);

        // Its own undo step, after the inspector's edit is recorded and
        // passed on, so neither the selection nor the children get the door.
        if (makeDoorRequest.floorIndex >= 0) {
            const std::string tagName = MakeDoor(level, sector.id, makeDoorRequest.floorIndex, makeDoorRequest.lip, makeDoorRequest.wait);
            if (!tagName.empty()) ShowNotification((Get("sector.make_door_done") + " " + tagName).c_str(), false);
        }
    }

    // Geometry Mode's inspector. This is the same
    // ImGuiDrawFunctions::DrawWallEditor panel Dot Mode used to open, just
    // reached from the new mode's selection - there is deliberately no
    // second wall editor.
    void DrawSelectedWallInspector(Level &level) {
        if (!editingWall || currentMode != MODE_GEOMETRY) return;

        const auto it = level.wallIDToIndex.find(selectedWallID);
        if (it == level.wallIDToIndex.end()) {
            selectedWallID = INVALID_ID;
            editingWall = false;
            return;
        }

        Wall &wall = level.walls[it->second];

        // Typing a coordinate here is a geometry edit exactly like
        // dragging the wall on the canvas, so it has to go through the
        // same shared-endpoint move - otherwise editing a coordinate
        // silently tears the corner away from every other wall attached
        // to it - and trigger the same topology rebuild.
        const Vector2 startBeforeEdit = wall.start;
        const Vector2 endBeforeEdit = wall.end;

        // Appearance/texture fields fan out across the selection; the
        // coordinates just above do not, because they go through the
        // shared-endpoint move below instead.
        const bool multiEdit = selectedWalls.size() > 1;

        const WallEditSnapshot appearanceBeforeEdit =
                multiEdit ? CaptureWallEditSnapshot(wall) : WallEditSnapshot{};

        BeginWallInspectorEdit(wallInspectorEdit, wall);

        const bool deleteRequested =
                ImGuiDrawFunctions::DrawWallEditor(wall, &editingWall, DRAGGABLE);

        if (deleteRequested) {
            // DeleteSelectedWalls already handles the whole selection as one
            // undoable operation with a single topology rebuild, which is
            // exactly what a multi-selection wants - and it falls back to
            // the single primary when only one wall is selected.
            DeleteSelectedWalls();
            hasUnsavedChanges = true;
            return;
        }

        const Vector2 startAfterEdit = wall.start;
        const Vector2 endAfterEdit = wall.end;

        const bool startMoved = !SamePoint(startBeforeEdit, startAfterEdit);
        const bool endMoved = !SamePoint(endBeforeEdit, endAfterEdit);

        // Before the propagation and the shared-endpoint move below, so the
        // step also holds the rest of the selection and every wall joined to
        // a moved corner as they were.
        RecordGeometryInspectorEdit(level, wallInspectorEdit,
                                    startMoved || endMoved ? "editor.history.edit_wall_points" : "editor.history.edit_walls");

        if (multiEdit &&
            PropagateWallEdits(level, appearanceBeforeEdit, wall, selectedWalls, selectedWallID))
            hasUnsavedChanges = true;

        if (startMoved || endMoved) {
            // Put the edited endpoints back first, so MoveSharedEndpoint
            // sees this wall in the same state as everything else joined
            // to that corner. Then move them all together.
            wall.start = startBeforeEdit;
            wall.end = endBeforeEdit;

            if (startMoved) MoveSharedEndpoint(startBeforeEdit, startAfterEdit);
            if (endMoved) MoveSharedEndpoint(endBeforeEdit, endAfterEdit);

            wallGeometryDirtyFromInspector = true;
            hasUnsavedChanges = true;
        }

        if (wallGeometryDirtyFromInspector && !ImGui::IsAnyItemActive()) {
            wallGeometryDirtyFromInspector = false;

            // A coordinate edit moves points, it doesn't change which
            // faces exist - so the sectors MoveSharedEndpoint already
            // moved are kept as they are rather than re-derived.
            RebuildGeometryAfterMove();
        }
    }

    void DrawSelectedEntityInspector(Level &level) {
        if (!editingEntity || currentMode != MODE_ENTITY) return;

        // A canvas drag keeps the inspector on the entity it was showing
        // before the press, so the dragged entity can be dropped onto one of
        // its script fields - see GetCanvasDragPinnedEntity.
        const ID pinnedID = GetCanvasDragPinnedEntity();
        const bool pinnedByCanvasDrag = pinnedID != INVALID_ID;

        Entity *entityPtr = FindEntityById(level, pinnedByCanvasDrag ? pinnedID : selectedEntity.id);
        if (!entityPtr) {
            editingEntity = false;
            ResetInspectorState();
            return;
        }

        Entity &entity = *entityPtr;

        // Transform edits fan out across the selection: position as a delta,
        // rotation and scale absolutely. Captured here and propagated after
        // the component editor below, because that is where the transform
        // fields actually live. Not while pinned: the inspected entity is
        // then not the primary of selectedEntities.
        const bool multiEdit = selectedEntities.size() > 1 && !pinnedByCanvasDrag;

        const EntityTransformSnapshot transformBeforeEdit =
                multiEdit ? CaptureEntityTransformSnapshot(level, entity.id) : EntityTransformSnapshot{};

        {
            using WallSnapStatus = ImGuiDrawFunctions::EntityInspectorState::WallSnapStatus;

            const WallSnap::Snap* snap = WallSnap::Find(entity.id);
            entityInspectorState.wallSnapStatus = WallSnap::IsPicking() ? WallSnapStatus::Picking
                                                : snap != nullptr ? WallSnapStatus::Snapped
                                                : WallSnapStatus::Off;
            if (snap != nullptr) entityInspectorState.wallSnapGap = snap->gap;
        }

        BeginInspectorEdit(level, entityInspectorEdit, entity.id);

        const bool deleteRequested =
                ImGuiDrawFunctions::DrawEntityEditor(entity, entityInspectorState, &editingEntity, DRAGGABLE);

        if (deleteRequested) {
            const std::vector<ID> entitiesToDelete =
                    selectedEntities.empty() ? std::vector<ID>{entity.id} : selectedEntities;

            ClearEntitySelection();
            ResetInspectorState();

            EntitiesSnapshot before = CaptureEntitiesSnapshot();
            for (const ID entityID: entitiesToDelete) level.DestroyEntity(entityID);
            PushEntitiesUndo(std::move(before), "editor.history.delete_entities", static_cast<int>(entitiesToDelete.size()));

            hasUnsavedChanges = true;
            return;
        }

        if (!editingEntity) {
            RecordInspectorEdit(level, entityInspectorEdit);
            ResetInspectorState();
            return;
        }

        // Before the propagation below, so the step also holds the rest of
        // the selection as it was.
        RecordInspectorEdit(level, entityInspectorEdit, multiEdit ? selectedEntities : std::vector<ID>{});

        if (multiEdit &&
            PropagateEntityTransformEdits(level, transformBeforeEdit, selectedEntities, entity.id))
            hasUnsavedChanges = true;

        // After the propagation above, which would otherwise pass the
        // primary's snap on to the others as a move.
        if (entityInspectorState.snapToFloorRequested) {
            entityInspectorState.snapToFloorRequested = false;

            EntityTransformUndo before;
            int snapped = 0;

            for (const ID entityID : multiEdit ? selectedEntities : std::vector<ID>{entity.id}) {
                ComponentTransform* transform = level.transforms.Get(entityID);
                if (transform == nullptr) continue;

                const Vector3 position = transform->position;
                if (!MapQueries::SnapToFloor(level, *transform)) continue;

                before.positions.emplace_back(entityID, position);
                ++snapped;
            }

            if (snapped > 0) {
                PushEntityPositionsUndo(std::move(before), "editor.history.snap_to_floor", snapped);
                hasUnsavedChanges = true;
            }
        }

        const std::vector<ID> wallSnapTargets = multiEdit ? selectedEntities : std::vector<ID>{entity.id};

        if (entityInspectorState.wallSnapButtonRequested) {
            entityInspectorState.wallSnapButtonRequested = false;

            using WallSnapStatus = ImGuiDrawFunctions::EntityInspectorState::WallSnapStatus;

            switch (entityInspectorState.wallSnapStatus) {
                case WallSnapStatus::Off: WallSnap::BeginPicking(wallSnapTargets, entity.id); break;
                case WallSnapStatus::Picking: WallSnap::CancelPicking(); break;
                case WallSnapStatus::Snapped: DisableWallSnapsWithUndo(wallSnapTargets); break;
            }
        }

        if (entityInspectorState.switchSidesRequested) {
            entityInspectorState.switchSidesRequested = false;

            UndoStep step;
            step.kind = UNDO_ENTITIES;
            step.labelKey = "editor.history.switch_wall_side";
            step.entities = CaptureEntitiesSnapshot();
            step.wallSnaps = WallSnap::All();

            step.count = WallSnap::SwitchSides(level, wallSnapTargets);

            if (step.count > 0) {
                PushUndoStep(std::move(step));
                hasUnsavedChanges = true;
            }
        }

        if (entityInspectorState.wallSnapGapChanged) {
            entityInspectorState.wallSnapGapChanged = false;

            SetWallSnapGapWithUndo(level, wallSnapTargets, entityInspectorState.wallSnapGap);
        }
    }

    void DrawSelectedLinedefInspector(Level &level) {
        if (!editingLinedef || currentMode != MODE_GEOMETRY || !linedefMode) return;

        Linedef *linedef = level.GetLinedef(selectedLinedefID);
        if (linedef == nullptr) {
            selectedLinedefID = INVALID_ID;
            editingLinedef = false;
            return;
        }

        BeginLinedefInspectorEdit(linedefInspectorEdit, *linedef);

        const Vector2 startBeforeEdit = linedef->start;
        const Vector2 endBeforeEdit = linedef->end;

        if (ImGuiDrawFunctions::DrawLinedefEditor(*linedef, &editingLinedef, DRAGGABLE)) {
            DeleteSelectedLinedefs();
            return;
        }

        const bool pointsMoved = !SamePoint(startBeforeEdit, linedef->start) || !SamePoint(endBeforeEdit, linedef->end);

        RecordLinedefInspectorEdit(level, linedefInspectorEdit,
                                   pointsMoved ? "editor.history.edit_linedef_points" : "editor.history.edit_linedefs");
    }

    void DrawSelectionInspectors(Level &level) {
        DrawSelectedSectorInspector(level);
        DrawSelectedEntityInspector(level);
        DrawSelectedWallInspector(level);
        DrawSelectedLinedefInspector(level);
    }

    // =========================================================================
    //  Asset Browser panel — project asset file explorer, locked to the
    //  project's Assets folder. See AssetBrowser.{hpp,cpp} for the scanning /
    //  drawing implementation; this just hosts it in its own dockable window.
    // =========================================================================

    void DrawAssetBrowserPanel() {
        // First-frame init: the project is guaranteed to be loaded by the
        // time DrawEditorUI() runs, so this is the safe place to root the
        // browser rather than wiring it through Editor::Start().
        if (!assetBrowserInitialized) {
            assetBrowser.SetRootDirectory(ProjectManager::GetAssetsPath());
            assetBrowserInitialized = true;
        }

        // Every frame, not in the one-time init: the editor's ImGui context
        // (and its fonts) is rebuilt after each play session, and the browser
        // outlives it, so a pointer kept from the first frame dangles.
        assetBrowser.SetIconFont(editorIconFont);

        ImGui::Begin(Get("editor.asset_browser").c_str());

        if (ImGui::Button(Get("editor.asset_browser.open_explorer").c_str()))
            if (!SDL_OpenURL(ProjectManager::GetAssetsPath().string().c_str())) spdlog::error("Failed to open file explorer");

        ImGui::SameLine();

        if (ImGui::Button(Get("editor.asset_browser.refresh").c_str())) assetBrowser.Refresh();
        HoverTooltip(Get("editor.tooltip.asset_browser_refresh").c_str());

        ImGui::SameLine();
        ImGui::TextDisabled("%s", Get("editor.asset_browser.hint").c_str());

        ImGui::Spacing();

        assetBrowser.Draw(&GetPreviewTextureID, &AssignAssetToCanvasSelection);

        ImGui::End();

        assetBrowser.DrawTextEditorWindow(scriptEditorFont);
        assetBrowser.DrawFlipbookEditorWindows();
        assetBrowser.DrawPrefabEditorWindows();
    }
} // anonymous namespace

// =============================================================================
//  MapEditorInternal — public interface
// =============================================================================

namespace MapEditorInternal {
    void MarkUnsavedChanges() {
        hasUnsavedChanges = true;
    }

    using namespace Localisation;

    // Shared Asset Browser instance - defined here (not in the anonymous
    // namespace above) so ImGuiDrawFunctions.cpp can reach it too, via the
    // extern declaration in EditorInternal.hpp.
    AssetBrowser assetBrowser;
    bool assetBrowserInitialized = false;

    SDL_Texture *GetEditorTexture(const std::string &textureFileName) {
        if (textureFileName.empty()) {
            return nullptr;
        }

        return EditorTextureCache::Get(renderer, textureFileName);
    }

    // Null in this (SDL) editor; set by RuntimeEditor::Start() when the same
    // inspector code runs on top of the OpenGL backend. See EditorInternal.hpp.
    ImTextureID (*previewTextureProvider)(const std::string &textureFileName) = nullptr;

    // The ONLY place a texture name becomes an ImTextureID. Everything that
    // draws a thumbnail goes through here rather than casting an SDL_Texture*
    // itself, because that cast is only valid under imgui_impl_sdlrenderer3.
    ImTextureID GetPreviewTextureID(const std::string &textureFileName) {
        if (textureFileName.empty()) return ImTextureID{};

        if (previewTextureProvider != nullptr) return previewTextureProvider(textureFileName);

        SDL_Texture *texture = GetEditorTexture(textureFileName);

        return texture != nullptr ? reinterpret_cast<ImTextureID>(texture) : ImTextureID{};
    }

    void DrawTextureThumbnailBox(const std::string &textureFileName, const float size) {
        const ImTextureID texture = GetPreviewTextureID(textureFileName);

        if (texture != ImTextureID{}) {
            ImGui::Image(texture, ImVec2(size, size));
            return;
        }

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size, size));

        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
            cursor,
            ImVec2(cursor.x + size, cursor.y + size),
            IM_COL32(90, 90, 90, 255)
        );
        dl->AddRect(
            cursor,
            ImVec2(cursor.x + size, cursor.y + size),
            IM_COL32(150, 150, 150, 255)
        );

        const char *text = textureFileName.empty() ? "-" : "?";
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        dl->AddText(
            ImVec2(
                cursor.x + (size - textSize.x) * 0.5f,
                cursor.y + (size - textSize.y) * 0.5f
            ),
            IM_COL32(220, 220, 220, 255),
            text
        );
    }

    void DrawTextureThumbnailRow(const std::string &textureFileName) {
        constexpr float thumb = 32.0f;

        DrawTextureThumbnailBox(textureFileName, thumb);

        ImGui::SameLine(0.0f, 8.0f);
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            (thumb - ImGui::GetTextLineHeight()) * 0.5f
        );

        if (textureFileName.empty()) {
            ImGui::TextDisabled("%s", Get("editor.none").c_str());
            return;
        }

        ImGui::TextUnformatted(textureFileName.c_str());
    }

    // Attaches a drag-drop target AND a click-to-assign consumer (for the
    // Asset Browser's double-click-then-click workflow) to the item drawn
    // IMMEDIATELY before this call. Returns true if `value` was assigned.
    static bool AcceptAssetDropOrClick(std::string &value, const AssetKind kind) {
        bool changed = false;

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload(AssetBrowser::DragDropPayloadTypeFor(kind))) {
                const std::string droppedAbsolutePath(static_cast<const char *>(payload->Data));
                value = AssetBrowser::ToAssetReference(droppedAbsolutePath, kind);
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            std::filesystem::path confirmedPath;
            if (assetBrowser.ConsumePendingConfirmedSelection(kind, confirmedPath)) {
                value = AssetBrowser::ToAssetReference(confirmedPath, kind);
                changed = true;
            }
        }

        return changed;
    }

    bool AcceptCanvasAssetDrop(const ImGuiID dockspaceID, const AssetKind kind, std::string& outReference) {
        // The canvas is the dockspace's passthrough middle, so no ImGui
        // window is hovered there and BeginDragDropTarget(Custom) never
        // accepts. Instead: the drag being released this frame over the
        // central node, with no window under the cursor.
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (payload == nullptr || !payload->IsDataType(AssetBrowser::DragDropPayloadTypeFor(kind))) return false;
        if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left) || GImGui->HoveredWindow != nullptr) return false;

        const ImGuiDockNode* canvas = ImGui::DockBuilderGetCentralNode(dockspaceID);
        if (canvas == nullptr) return false;
        if (!ImGui::IsMouseHoveringRect(canvas->Pos, ImVec2(canvas->Pos.x + canvas->Size.x, canvas->Pos.y + canvas->Size.y), false))
            return false;

        outReference = AssetBrowser::ToAssetReference(static_cast<const char*>(payload->Data), kind);
        return true;
    }

    bool DrawAssetField(const char *label, std::string &value, const AssetKind kind, const float previewSize) {
        bool changed = false;

        ImGui::PushID(label);
        ImGui::BeginGroup();

        if (previewSize > 0.0f && (kind == AssetKind::Texture || kind == AssetKind::Model)) {
            const ImTextureID preview = kind == AssetKind::Texture ? GetPreviewTextureID(value) : ImTextureID{};
            const ImVec2 cursor = ImGui::GetCursorScreenPos();

            if (preview != ImTextureID{}) ImGui::Image(preview, ImVec2(previewSize, previewSize));
            else {
                ImGui::Dummy(ImVec2(previewSize, previewSize));
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(cursor, ImVec2(cursor.x + previewSize, cursor.y + previewSize),
                                  IM_COL32(35, 35, 40, 255));
                dl->AddRect(cursor, ImVec2(cursor.x + previewSize, cursor.y + previewSize), IM_COL32(90, 90, 100, 255));

                // Models are never rendered into a preview: the box names the file type, same as the asset browser.
                if (kind == AssetKind::Model && !value.empty()) {
                    std::string extension = std::filesystem::path(value).extension().string();
                    std::ranges::transform(extension, extension.begin(), [](const unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });

                    const ImVec2 textSize = ImGui::CalcTextSize(extension.c_str());
                    dl->AddText(
                        ImVec2(cursor.x + (previewSize - textSize.x) * 0.5f, cursor.y + (previewSize - textSize.y) * 0.5f),
                        IM_COL32(220, 220, 220, 255),
                        extension.c_str()
                    );
                }
            }

            changed |= AcceptAssetDropOrClick(value, kind);
            HoverTooltip(Get("editor.asset_field.tooltip").c_str());

            if (label != nullptr && label[0] != '\0') {
                const ImVec2 labelSize = ImGui::CalcTextSize(label);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (previewSize - labelSize.x) * 0.5f));
                ImGui::TextDisabled("%s", label);
            }

            if (!value.empty() && ImGui::SmallButton(Get("editor.asset_field.clear").c_str())) {
                value.clear();
                changed = true;
            }
        } else {
            if (label != nullptr && label[0] != '\0') {
                ImGui::TextUnformatted(label);
                ImGui::SameLine();
            }

            char buf[160];
            snprintf(buf, sizeof(buf), "%s", value.empty() ? "(none)" : value.c_str());
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputText("##value", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);

            changed |= AcceptAssetDropOrClick(value, kind);
            HoverTooltip(Get("editor.asset_field.tooltip").c_str());

            if (!value.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(Get("editor.asset_field.clear").c_str())) {
                    value.clear();
                    changed = true;
                }
            }
        }

        ImGui::EndGroup();
        ImGui::PopID();

        return changed;
    }

    void HandleAssetBrowserFileDrop(const SDL_WindowID windowID, const float x, const float y, const char *filePath) {
        if (filePath == nullptr || window == nullptr) return;
        if (windowID != SDL_GetWindowID(window)) return;
        if (!assetBrowser.IsScreenPointInside(x, y)) return;

        const std::filesystem::path source(filePath);

        if (assetBrowser.ImportExternalFile(source))
            ShowNotification((Get("editor.asset_browser.notification.imported") + source.filename().string()).c_str());
    }

    void ChangeMode() {
        const Mode previousMode = currentMode;
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);

        if (previousMode == MODE_SECTOR) CancelActiveDrawing();
    }

    void QueueLevelLoad(const std::string &levelName) {
        pendingLevelToLoad = levelName;
        spdlog::info("Queued level load: {}", levelName);
    }

    bool ProcessPendingLevelLoad() {
        if (!pendingLevelToLoad.has_value()) return false;

        const std::string levelToLoad = *pendingLevelToLoad;
        pendingLevelToLoad.reset();

        ResetInspectorState();

        editingSector = false;
        selectedSectorID = INVALID_ID;
        editingEntity = false;

        // Was a bare sectorBeingCreated.clear() - now goes through
        // CancelActiveDrawing() so a Rectangle/Polygon/Circle/Curve tool
        // left mid-shape (not just the freehand chain) is also reset
        // when the level underneath it changes.
        CancelActiveDrawing();
        pendingSectorParams = PendingSectorParams{};
        ClearUndoHistory();

        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;

        ClearWallSelection();
        ClearLinedefSelection();
        ClearSectorSelection();
        ClearEntitySelection();

        hasUnsavedChanges = false;

        spdlog::info("Processing queued level load: {}", levelToLoad);
        Editor::LoadLevel(levelToLoad);

        return true;
    }

    // =========================================================================
    //  Grid & Snapping / Drawing Tools panels
    // =========================================================================
    //
    // All labels go through Get()/Localisation as everything else in this
    // file does. The manual-corner controls reuse the editor.manual_sector.*
    // keys that already existed for this sub-mode; everything else needs the
    // new editor.grid.* / editor.draw.* keys.

    void DrawGridSnappingSection() {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        SectionHeader(Get("editor.grid.title").c_str());
        ImGui::Spacing();

        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputFloat(
            (Get("editor.grid.size") + "##EditorGridSize").c_str(),
            &GRID_SIZE,
            1.0f,
            8.0f,
            "%.1f"
        ))
            GRID_SIZE = std::max(GRID_SIZE, MIN_GRID_SIZE);

        HoverTooltip(FormatKeyBinds(Get("editor.tooltip.grid.size")).c_str());

        ImGui::SameLine();
        ImGui::BeginDisabled(GRID_SIZE == DEFAULT_GRID_SIZE);
        if (ImGui::Button((Get("editor.user_settings.default") + "##EditorGridSizeDefault").c_str())) GRID_SIZE = DEFAULT_GRID_SIZE;
        ImGui::EndDisabled();

        ImGui::Checkbox(Get("editor.grid.snap_to_grid").c_str(), &gridSnapEnabled);
        HoverTooltip(Get("editor.tooltip.grid.snap_to_grid").c_str());

        ImGui::SameLine(0.0f, 16.0f);

        ImGui::Checkbox(Get("editor.grid.snap_to_points").c_str(), &vertexSnapEnabled);
        HoverTooltip(Get("editor.tooltip.grid.snap_to_points").c_str());

        Level& level = LevelManager::CurrentLevel();

        // ---- Grid transform: where the grid's origin is and how it's turned.
        if (ImGui::TreeNode((Get("editor.grid.transform") + "##EditorGridTransform").c_str())) {
            EditorGridTransform& grid = level.gridTransform;

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::DragFloat2((Get("editor.grid.origin") + "##EditorGridOrigin").c_str(), &grid.origin.x, 0.25f, 0.0f, 0.0f, "%.2f"))
                MarkUnsavedChanges();
            HoverTooltip(FormatKeyBinds(Get("editor.tooltip.grid.origin")).c_str());

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::DragFloat((Get("editor.grid.angle") + "##EditorGridAngle").c_str(), &grid.angle, 0.25f, -360.0f, 360.0f, "%.2f"))
                MarkUnsavedChanges();
            HoverTooltip(Get("editor.tooltip.grid.angle").c_str());

            ImGui::BeginDisabled(selectedWallID == INVALID_ID);
            if (ImGui::Button((Get("editor.grid.align_to_wall") + "##EditorGridAlign").c_str())) AlignGridToWall(selectedWallID);
            ImGui::EndDisabled();
            HoverTooltip(FormatKeyBinds(Get("editor.tooltip.grid.align_to_wall")).c_str());

            ImGui::SameLine();
            ImGui::BeginDisabled(grid.IsDefault());
            if (ImGui::Button((Get("editor.grid.reset") + "##EditorGridReset").c_str())) ResetGridTransform();
            ImGui::EndDisabled();
            HoverTooltip(FormatKeyBinds(Get("editor.tooltip.grid.reset")).c_str());

            ImGui::TreePop();
        }

        // ---- Background image to trace over.
        if (ImGui::TreeNode((Get("editor.grid.background_image") + "##EditorBackgroundImage").c_str())) {
            EditorBackgroundImage& image = level.backgroundImage;

            if (DrawAssetField(Get("editor.grid.background_image.texture").c_str(), image.texture, AssetKind::Texture, 48.0f))
                MarkUnsavedChanges();

            if (ImGui::Checkbox((Get("editor.grid.background_image.visible") + "##EditorBackgroundVisible").c_str(), &image.visible))
                MarkUnsavedChanges();

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::DragFloat((Get("editor.grid.background_image.scale") + "##EditorBackgroundScale").c_str(), &image.scale, 0.005f, 0.001f, 1000.0f, "%.3f")) {
                image.scale = std::max(image.scale, 0.001f);
                MarkUnsavedChanges();
            }
            HoverTooltip(Get("editor.tooltip.grid.background_image.scale").c_str());

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderFloat((Get("editor.grid.background_image.opacity") + "##EditorBackgroundOpacity").c_str(), &image.opacity, 0.0f, 1.0f, "%.2f"))
                MarkUnsavedChanges();

            ImGui::TreePop();
        }
    }

    // A localised format string with one %d filled in.
    std::string FormatCount(const std::string& format, const int count) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), format.c_str(), count);
        return buffer;
    }

    std::string Lowercase(std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // A sub-heading inside a mode panel: spacing, separator, accent label.
    void ModePanelSection(const char* label) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        SectionHeader(label);
        ImGui::Spacing();
    }

    // Toast for a panel action: `doneKey` (with the count) when it did
    // something, `nothingKey` when it didn't.
    void NotifyActionResult(const int count, const char* doneKey, const char* nothingKey) {
        if (count > 0) ShowNotification(FormatCount(FormatKeyBinds(Get(doneKey)), count).c_str());
        else ShowNotification(Get(nothingKey).c_str(), /*isError=*/true);
    }

    // Two buttons that share a row equally.
    float HalfButtonWidth() {
        return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    }

    // Geometry / Wall Edit Mode's panel: the selection's facts, actions on
    // it, filters and the shortcuts. The wall's own properties stay in the
    // wall inspector, which a right click opens.
    // Geometry Mode's panel while Linedef Mode is on.
    void DrawLinedefModePanel(Level& level) {
        ModePanelSection(Get("editor.linedef.title").c_str());

        const int selectedCount = static_cast<int>(selectedLinedefs.size());

        if (selectedCount > 0) {
            const std::string selectedText = (selectedCount == 1)
                                                 ? Get("editor.linedef.selected_singular")
                                                 : Get("editor.linedef.selected_plural");

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
            ImGui::Text(selectedText.c_str(), selectedCount);
            ImGui::PopStyleColor();
        }
        else HintText(Get("editor.linedef.none_selected").c_str());

        // ---- Info -----------------------------------------------------------
        if (selectedCount > 0) {
            float totalLength = 0.0f;
            const Linedef* only = nullptr;

            for (const ID linedefID : selectedLinedefs) {
                const Linedef* linedef = level.GetLinedef(linedefID);
                if (linedef == nullptr) continue;

                totalLength += linedef->length;
                only = linedef;
            }

            if (selectedCount == 1 && only != nullptr)
                ImGui::Text("%s %.2f", Get("editor.geometry.info.length").c_str(), only->length);
            else ImGui::Text("%s %.2f", Get("editor.geometry.info.total_length").c_str(), totalLength);
        }

        // ---- Edit -----------------------------------------------------------
        ModePanelSection(Get("editor.geometry.edit").c_str());
        ImGui::BeginDisabled(selectedCount == 0);

        if (ImGui::Button(Get("editor.geometry.split").c_str(), ImVec2(HalfButtonWidth(), 0.0f)))
            NotifyActionResult(SplitSelectedLinedefsAtMidpoint(), "editor.linedef.split_done", "editor.linedef.split_nothing");
        HoverTooltip(FormatKeyBinds(Get("editor.tooltip.linedef.split")).c_str());

        ImGui::SameLine();

        if (ImGui::Button(Get("editor.linedef.flip").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            NotifyActionResult(FlipSelectedLinedefs(), "editor.linedef.flip_done", "editor.linedef.nothing_selected");
        HoverTooltip(FormatKeyBinds(Get("editor.tooltip.linedef.flip")).c_str());

        ImGui::EndDisabled();

        // ---- Shortcuts --------------------------------------------------------
        ImGui::Spacing();

        if (ImGui::CollapsingHeader(Get("editor.shortcuts").c_str())) {
            HintText(Get("editor.linedef.hint.draw").c_str());
            HintText(Get("editor.linedef.hint.select").c_str());
            HintText(Get("editor.linedef.hint.drag").c_str());
            HintText(FormatKeyBinds(Get("editor.linedef.hint.split")).c_str());
            HintText(FormatKeyBinds(Get("editor.linedef.hint.flip")).c_str());
            HintText(FormatKeyBinds(Get("editor.linedef.hint.delete")).c_str());
        }
    }

    void DrawGeometryModePanel() {
        Level& level = LevelManager::CurrentLevel();

        bool linedefModeValue = linedefMode;
        if (ImGui::Checkbox(Get("editor.linedef.mode").c_str(), &linedefModeValue)) SetLinedefMode(linedefModeValue);
        HoverTooltip(FormatKeyBinds(Get("editor.tooltip.linedef.mode")).c_str());

        if (linedefMode) {
            DrawLinedefModePanel(level);
            return;
        }

        ModePanelSection(Get("editor.geometry.title").c_str());

        ImGui::Checkbox(Get("editor.geometry.drag_carries_entities").c_str(), &dragCarriesEntities);
        HoverTooltip(Get("editor.tooltip.geometry.drag_carries_entities").c_str());

        const int selectedCount = static_cast<int>(selectedWalls.size());

        if (selectedCount > 0) {
            const std::string selectedText = (selectedCount == 1)
                                                 ? Get("editor.geometry.selected_singular")
                                                 : Get("editor.geometry.selected_plural");

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
            ImGui::Text(selectedText.c_str(), selectedCount);
            ImGui::PopStyleColor();
        }
        else HintText(Get("editor.geometry.none_selected").c_str());

        // ---- Info -----------------------------------------------------------
        if (selectedCount > 0) {
            float totalLength = 0.0f;
            const Wall* only = nullptr;

            for (const ID wallID : selectedWalls) {
                const auto it = level.wallIDToIndex.find(wallID);
                if (it == level.wallIDToIndex.end()) continue;

                totalLength += level.walls[it->second].length;
                only = &level.walls[it->second];
            }

            if (selectedCount == 1 && only != nullptr) {
                const auto sectorName = [&level](const ID sectorID) {
                    const Sector* sector = level.GetSector(sectorID);
                    return sector != nullptr ? SectorHierarchyLabel(*sector) : Get("editor.geometry.info.no_sector");
                };

                ImGui::Text("%s %.2f", Get("editor.geometry.info.length").c_str(), only->length);
                ImGui::Text("%s %s", Get("editor.geometry.info.type").c_str(),
                            Get(only->IsPortal() ? "editor.geometry.info.portal" : "editor.geometry.info.solid").c_str());
                ImGui::Text("%s %s", Get("editor.geometry.info.front").c_str(), sectorName(only->frontSector).c_str());
                ImGui::Text("%s %s", Get("editor.geometry.info.back").c_str(), sectorName(only->backSector).c_str());
            }
            else ImGui::Text("%s %.2f", Get("editor.geometry.info.total_length").c_str(), totalLength);
        }

        // ---- Edit -----------------------------------------------------------
        ModePanelSection(Get("editor.geometry.edit").c_str());
        ImGui::BeginDisabled(selectedCount == 0);

        if (ImGui::Button(Get("editor.geometry.split").c_str(), ImVec2(HalfButtonWidth(), 0.0f)))
            NotifyActionResult(SplitSelectedWallsAtMidpoint(), "editor.geometry.split_done", "editor.geometry.split_nothing");
        HoverTooltip(FormatKeyBinds(Get("editor.tooltip.geometry.split")).c_str());

        ImGui::SameLine();

        ImGui::BeginDisabled(selectedCount < 2);
        if (ImGui::Button(Get("editor.geometry.merge").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            NotifyActionResult(MergeSelectedWalls(), "editor.geometry.merge_done", "editor.geometry.merge_nothing");
        ImGui::EndDisabled();
        HoverTooltip(Get("editor.tooltip.geometry.merge").c_str());

        ImGui::EndDisabled();

        // ---- Apply to selection -----------------------------------------------
        ModePanelSection(Get("editor.geometry.batch").c_str());

        static WallBatchValues batch;

        ImGui::Checkbox("##BatchTopTexture", &batch.applyTopTexture);
        ImGui::SameLine();
        ImGui::BeginDisabled(!batch.applyTopTexture);
        DrawAssetField(Get("editor.geometry.batch.top_texture").c_str(), batch.topTexture, AssetKind::Texture, 32.0f);
        ImGui::EndDisabled();

        ImGui::Checkbox("##BatchBottomTexture", &batch.applyBottomTexture);
        ImGui::SameLine();
        ImGui::BeginDisabled(!batch.applyBottomTexture);
        DrawAssetField(Get("editor.geometry.batch.bottom_texture").c_str(), batch.bottomTexture, AssetKind::Texture, 32.0f);
        ImGui::EndDisabled();

        ImGui::Checkbox("##BatchColor", &batch.applyColor);
        ImGui::SameLine();
        ImGui::BeginDisabled(!batch.applyColor);
        ImGui::ColorEdit4(
            (Get("wall.color") + "##BatchWallColor").c_str(),
            &batch.color.x,
            ImGuiColorEditFlags_AlphaBar |
            ImGuiColorEditFlags_AlphaPreviewHalf |
            ImGuiColorEditFlags_HDR |
            ImGuiColorEditFlags_Float
        );
        ImGui::EndDisabled();

        ImGui::BeginDisabled(selectedCount == 0 || (!batch.applyTopTexture && !batch.applyBottomTexture && !batch.applyColor));
        PushAccentStyle();
        if (FullWidthButton(FormatCount(Get("editor.geometry.batch.apply"), selectedCount).c_str()))
            NotifyActionResult(ApplyToSelectedWalls(batch), "editor.geometry.batch.done", "editor.geometry.nothing_selected");
        PopAccentStyle();
        ImGui::EndDisabled();
        HoverTooltip(Get("editor.tooltip.geometry.batch").c_str());

        // ---- Texture alignment ------------------------------------------------
        ModePanelSection(Get("editor.geometry.align").c_str());
        ImGui::BeginDisabled(selectedCount == 0);

        ImGui::BeginDisabled(selectedCount < 2);
        if (FullWidthButton(Get("editor.geometry.align.chain").c_str()))
            NotifyActionResult(AlignSelectedWallTextures(), "editor.geometry.align.chain_done", "editor.geometry.align.chain_nothing");
        ImGui::EndDisabled();
        HoverTooltip(Get("editor.tooltip.geometry.align.chain").c_str());

        if (ImGui::Button(Get("editor.geometry.align.reset").c_str(), ImVec2(HalfButtonWidth(), 0.0f)))
            NotifyActionResult(ResetSelectedWallOffsets(), "editor.geometry.align.reset_done", "editor.geometry.nothing_selected");
        HoverTooltip(Get("editor.tooltip.geometry.align.reset").c_str());

        ImGui::SameLine();

        if (ImGui::Button(Get("editor.geometry.align.fit").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            NotifyActionResult(FitSelectedWallTexturesToHeight(), "editor.geometry.align.fit_done", "editor.geometry.align.fit_nothing");
        HoverTooltip(Get("editor.tooltip.geometry.align.fit").c_str());

        ImGui::EndDisabled();

        // ---- Filters ----------------------------------------------------------
        ModePanelSection(Get("editor.filters").c_str());

        bool wallFilterChanged = ImGui::Checkbox(Get("editor.geometry.filter.solid").c_str(), &showSolidWalls);
        HoverTooltip(Get("editor.tooltip.filters").c_str());
        ImGui::SameLine(0.0f, 16.0f);
        wallFilterChanged |= ImGui::Checkbox(Get("editor.geometry.filter.portal").c_str(), &showPortalWalls);
        HoverTooltip(Get("editor.tooltip.filters").c_str());

        // Hiding walls drops them from the selection, so nothing hidden is edited.
        if (wallFilterChanged) {
            std::erase_if(selectedWalls, [&level](const ID wallID) {
                const auto it = level.wallIDToIndex.find(wallID);
                return it != level.wallIDToIndex.end() && !WallPassesFilter(level.walls[it->second]);
            });

            if (std::find(selectedWalls.begin(), selectedWalls.end(), selectedWallID) == selectedWalls.end()) {
                selectedWallID = selectedWalls.empty() ? INVALID_ID : selectedWalls.front();
                editingWall = editingWall && selectedWallID != INVALID_ID;
            }
        }

        // ---- Shortcuts --------------------------------------------------------
        ImGui::Spacing();

        if (ImGui::CollapsingHeader(Get("editor.shortcuts").c_str())) {
            HintText(Get("editor.geometry.hint.select").c_str());
            HintText(Get("editor.geometry.hint.drag").c_str());
            HintText(FormatKeyBinds(Get("editor.geometry.hint.split")).c_str());
            HintText(FormatKeyBinds(Get("editor.geometry.hint.delete")).c_str());
        }
    }

    // Every world .prefab under Assets, as asset references, for the palette.
    std::vector<std::string> ScanWorldPrefabs() {
        std::vector<std::string> references;

        const fs::path assets = ProjectManager::GetAssetsPath();
        std::error_code ec;

        for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || it->path().extension() != Prefab::kExtension) continue;

            const std::string reference = fs::relative(it->path(), assets, ec).generic_string();
            if (ec) continue;

            // UI prefabs belong in the UI editor.
            const Level* prefab = Prefab::Get(reference);
            if (prefab == nullptr || prefab->ui_transforms.Has(Prefab::RootOf(*prefab).id)) continue;

            references.push_back(reference);
        }

        std::sort(references.begin(), references.end());
        return references;
    }

    // Selects every entity `matches` accepts; the first becomes the primary.
    template<typename F>
    void SelectEntitiesWhere(Level& level, F&& matches) {
        std::vector<ID> matching;

        for (const Entity& entity : level.entities)
            if (level.transforms.Has(entity.id) && matches(entity)) matching.push_back(entity.id);

        if (matching.empty()) {
            ClearEntitySelection();
            return;
        }

        SelectEntity(matching.front());
        selectedEntities = std::move(matching);
    }

    // One filter row: a visibility checkbox, the label with how many world
    // entities match, and a Select button for them.
    template<typename F>
    void DrawEntityFilterRow(Level& level, const char* id, const std::string& label, bool visible, F&& matches,
                             const std::function<void(bool)>& setVisible) {
        int count = 0;
        for (const Entity& entity : level.entities)
            if (level.transforms.Has(entity.id) && matches(entity)) ++count;

        ImGui::PushID(id);

        if (ImGui::Checkbox("##Visible", &visible)) setVisible(visible);
        HoverTooltip(Get("editor.tooltip.filters").c_str());

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s (%d)", label.c_str(), count);

        const float selectWidth = ImGui::CalcTextSize(Get("editor.entity_mode.filter.select").c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - selectWidth));

        ImGui::BeginDisabled(count == 0 || !visible);
        if (ImGui::SmallButton(Get("editor.entity_mode.filter.select").c_str())) SelectEntitiesWhere(level, matches);
        ImGui::EndDisabled();
        HoverTooltip(Get("editor.tooltip.entity_mode.filter.select").c_str());

        ImGui::PopID();
    }

    // Entity Mode's panel: what E places and how, actions on the selection,
    // filters and the shortcuts.
    // What E places in either editor: an empty entity or one of the
    // project's world prefabs, with a search box (a .prefab dropped on it
    // searches for its name). Both editors share the list, the search and
    // entityPaletteSelection. `hintKey` is the line under it, naming the
    // editor's own key.
    namespace {
        // Cleared to rescan Assets for prefabs on the next draw (Refresh,
        // Make Prefab).
        bool paletteScanned = false;
    }

    void DrawEntityPalette(const char* hintKey) {
        static std::vector<std::string> palettePrefabs;
        static char paletteSearch[64] = "";

        if (!paletteScanned) {
            palettePrefabs = ScanWorldPrefabs();
            paletteScanned = true;
        }

        // A prefab that was deleted or renamed falls back to an empty entity.
        if (!entityPaletteSelection.empty() &&
            std::find(palettePrefabs.begin(), palettePrefabs.end(), entityPaletteSelection) == palettePrefabs.end())
            entityPaletteSelection.clear();

        const float refreshWidth = ImGui::CalcTextSize(Get("editor.entity_mode.palette.refresh").c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - refreshWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##PaletteSearch", Get("editor.entity_mode.palette.search").c_str(), paletteSearch, sizeof(paletteSearch));

        // A .prefab dragged from the Asset Browser searches for its name and
        // becomes what gets placed - like dropping one on a prefab field.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetBrowser::DragDropPayloadTypeFor(AssetKind::Prefab))) {
                const std::string reference = AssetBrowser::ToAssetReference(static_cast<const char*>(payload->Data), AssetKind::Prefab);
                const std::string name = fs::path(reference).stem().string();

                std::strncpy(paletteSearch, name.c_str(), sizeof(paletteSearch) - 1);
                paletteSearch[sizeof(paletteSearch) - 1] = '\0';

                // One made since the last scan isn't listed yet.
                if (std::find(palettePrefabs.begin(), palettePrefabs.end(), reference) == palettePrefabs.end())
                    palettePrefabs = ScanWorldPrefabs();

                // UI prefabs aren't in the list and can't be placed.
                if (std::find(palettePrefabs.begin(), palettePrefabs.end(), reference) != palettePrefabs.end())
                    entityPaletteSelection = reference;
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button(Get("editor.entity_mode.palette.refresh").c_str())) paletteScanned = false;
        HoverTooltip(Get("editor.tooltip.entity_mode.palette.refresh").c_str());

        const std::string search = Lowercase(paletteSearch);
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float listHeight = std::min(static_cast<float>(palettePrefabs.size() + 1) * rowHeight, 8.0f * rowHeight) + ImGui::GetStyle().WindowPadding.y * 2.0f;

        if (ImGui::BeginChild("##Palette", ImVec2(0.0f, listHeight), /*border=*/true)) {
            if (ImGui::Selectable(Get("editor.entity_mode.palette.empty").c_str(), entityPaletteSelection.empty()))
                entityPaletteSelection.clear();

            for (const std::string& reference : palettePrefabs) {
                if (!search.empty() && Lowercase(reference).find(search) == std::string::npos) continue;

                // The file name, with the folder dimmed in the tooltip.
                const std::string name = fs::path(reference).stem().string();

                if (ImGui::Selectable((name + "##" + reference).c_str(), entityPaletteSelection == reference))
                    entityPaletteSelection = reference;

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", reference.c_str());
            }
        }
        ImGui::EndChild();

        HintText(FormatKeyBinds(Get(hintKey)).c_str());
    }

    void DrawEntityModePanel() {
        Level& level = LevelManager::CurrentLevel();

        ModePanelSection(Get("editor.entity_mode.title").c_str());

        int worldEntityCount = 0;
        for (const Entity& entity : level.entities)
            if (level.transforms.Has(entity.id)) ++worldEntityCount;

        const int selectedCount = static_cast<int>(selectedEntities.size());

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(selectedCount > 0 ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled));
        ImGui::Text(Get("editor.entity_mode.counts").c_str(), worldEntityCount, selectedCount);
        ImGui::PopStyleColor();

        // ---- Palette ----------------------------------------------------------
        ModePanelSection(Get("editor.entity_mode.palette").c_str());

        DrawEntityPalette("editor.entity_mode.palette.hint");

        // ---- Placement --------------------------------------------------------
        ModePanelSection(Get("editor.entity_mode.placement").c_str());

        ImGui::Checkbox(Get("editor.entity_mode.placement.on_floor").c_str(), &placeOnFloor);
        HoverTooltip(Get("editor.tooltip.entity_mode.placement.on_floor").c_str());
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::Checkbox(Get("editor.entity_mode.placement.random_yaw").c_str(), &placeRandomYaw);
        HoverTooltip(Get("editor.tooltip.entity_mode.placement.random_yaw").c_str());

        static char defaultTagBuf[64] = "";
        if (placeDefaultTag != defaultTagBuf) {
            std::strncpy(defaultTagBuf, placeDefaultTag.c_str(), sizeof(defaultTagBuf) - 1);
            defaultTagBuf[sizeof(defaultTagBuf) - 1] = '\0';
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        if (ImGui::InputTextWithHint(Get("editor.entity_mode.placement.tag").c_str(), Get("editor.entity_mode.placement.tag_none").c_str(),
                                     defaultTagBuf, sizeof(defaultTagBuf)))
            placeDefaultTag = defaultTagBuf;
        HoverTooltip(Get("editor.tooltip.entity_mode.placement.tag").c_str());

        const auto placeTag = TagRegistry::Find(placeDefaultTag);
        if (!placeDefaultTag.empty() && (!placeTag || TagRegistry::GetKind(*placeTag) != TagRegistry::TagKind::NORMAL)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.65f, 0.30f, 1.00f));
            ImGui::TextWrapped("%s", Get("editor.entity_mode.placement.tag_unknown").c_str());
            ImGui::PopStyleColor();
        }

        // ---- Selection --------------------------------------------------------
        ModePanelSection(Get("editor.entity_mode.selection").c_str());
        ImGui::BeginDisabled(selectedCount == 0);

        const auto alignRow = [](const char* axisKey, const EntityAlignment minimum, const EntityAlignment center, const EntityAlignment maximum) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Get(axisKey).c_str());
            ImGui::SameLine(60.0f);

            const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
            const std::pair<const char*, EntityAlignment> buttons[] = {
                {"editor.entity_mode.align.min", minimum},
                {"editor.entity_mode.align.center", center},
                {"editor.entity_mode.align.max", maximum},
            };

            for (int i = 0; i < 3; ++i) {
                if (i > 0) ImGui::SameLine();

                ImGui::PushID(static_cast<int>(buttons[i].second));
                if (ImGui::Button(Get(buttons[i].first).c_str(), ImVec2(width, 0.0f)))
                    NotifyActionResult(AlignSelectedEntities(buttons[i].second), "editor.entity_mode.moved", "editor.entity_mode.moved_nothing");
                ImGui::PopID();
            }
        };

        ImGui::BeginDisabled(selectedCount < 2);
        alignRow("editor.entity_mode.align.x", ALIGN_MIN_X, ALIGN_CENTER_X, ALIGN_MAX_X);
        HoverTooltip(Get("editor.tooltip.entity_mode.align").c_str());
        alignRow("editor.entity_mode.align.z", ALIGN_MIN_Z, ALIGN_CENTER_Z, ALIGN_MAX_Z);
        HoverTooltip(Get("editor.tooltip.entity_mode.align").c_str());
        ImGui::EndDisabled();

        ImGui::BeginDisabled(selectedCount < 3);
        if (ImGui::Button(Get("editor.entity_mode.distribute.x").c_str(), ImVec2(HalfButtonWidth(), 0.0f)))
            NotifyActionResult(DistributeSelectedEntities(true), "editor.entity_mode.moved", "editor.entity_mode.moved_nothing");
        HoverTooltip(Get("editor.tooltip.entity_mode.distribute").c_str());
        ImGui::SameLine();
        if (ImGui::Button(Get("editor.entity_mode.distribute.z").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            NotifyActionResult(DistributeSelectedEntities(false), "editor.entity_mode.moved", "editor.entity_mode.moved_nothing");
        HoverTooltip(Get("editor.tooltip.entity_mode.distribute").c_str());
        ImGui::EndDisabled();

        if (ImGui::Button(Get("editor.entity_mode.snap").c_str(), ImVec2(HalfButtonWidth(), 0.0f)))
            NotifyActionResult(SnapSelectedEntitiesToGrid(), "editor.entity_mode.moved", "editor.entity_mode.moved_nothing");
        HoverTooltip(Get("editor.tooltip.entity_mode.snap").c_str());
        ImGui::SameLine();
        if (ImGui::Button(Get("editor.entity_mode.drop").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            NotifyActionResult(DropSelectedEntitiesToFloor(), "editor.entity_mode.moved", "editor.entity_mode.moved_nothing");
        HoverTooltip(Get("editor.tooltip.entity_mode.drop").c_str());

        if (FullWidthButton(Get("editor.entity_mode.make_prefab").c_str())) {
            std::string error;
            const int created = CreatePrefabsFromSelectedEntities(&error);

            if (created > 0) {
                ShowNotification(FormatCount(Get("editor.entity_mode.make_prefab_done"), created).c_str());
                assetBrowser.Refresh();
                paletteScanned = false;
            }
            else {
                spdlog::error("Create prefab from selection failed: {}", error);
                ShowNotification(Get("editor.entity_mode.make_prefab_failed").c_str(), /*isError=*/true);
            }
        }
        HoverTooltip(Get("editor.tooltip.entity_mode.make_prefab").c_str());

        ImGui::EndDisabled();

        // ---- Filters ----------------------------------------------------------
        ModePanelSection(Get("editor.filters").c_str());

        bool entityFilterChanged = false;

        const auto toggleListEntry = [&entityFilterChanged]<typename T>(std::vector<T>& hidden, const T& value, const bool visible) {
            if (visible) std::erase(hidden, value);
            else if (std::find(hidden.begin(), hidden.end(), value) == hidden.end()) hidden.push_back(value);
            entityFilterChanged = true;
        };

#define TILKY_ENTITY_FILTER_ROW(Type, Bit, Storage, LabelKey)                                                   \
        if (Bit != CMP_TRANSFORM) {                                                                            \
            const int type = Bit;                                                                              \
            const bool visible = std::find(hiddenEntityComponentTypes.begin(), hiddenEntityComponentTypes.end(), type) \
                                 == hiddenEntityComponentTypes.end();                                          \
            DrawEntityFilterRow(level, #Bit, Get(LabelKey), visible,                                           \
                [type](const Entity& entity) { return entity.componentsMask.test(static_cast<size_t>(type)); }, \
                [&, type](const bool show) { toggleListEntry(hiddenEntityComponentTypes, type, show); });      \
        }
        TILKY_NORMAL_COMPONENTS(TILKY_ENTITY_FILTER_ROW)
#undef TILKY_ENTITY_FILTER_ROW

        if (const auto normalTags = TagRegistry::GetAllOfKind(TagRegistry::TagKind::NORMAL); !normalTags.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", Get("editor.entity_mode.filter.tags").c_str());

            for (const auto& [tagName, tagId] : normalTags) {
                const TagId tag = tagId;
                const bool visible = std::ranges::find(hiddenEntityTags, tag) == hiddenEntityTags.end();

                DrawEntityFilterRow(level, tagName.c_str(), tagName, visible,
                    [tag](const Entity& entity) { return std::ranges::find(entity.tagIds, tag) != entity.tagIds.end(); },
                    [&, tag](const bool show) { toggleListEntry(hiddenEntityTags, tag, show); });
            }
        }

        // Hiding entities drops them from the selection, so nothing hidden is edited.
        if (entityFilterChanged && std::erase_if(selectedEntities, [&level](const ID id) {
            const Entity* entity = level.GetEntity(id);
            return entity != nullptr && !EntityPassesFilter(*entity);
        }) > 0) {
            if (selectedEntities.empty()) ClearEntitySelection();
            else if (const Entity* primary = level.GetEntity(selectedEntities.front())) selectedEntity = *primary;
        }

        // ---- Shortcuts --------------------------------------------------------
        ImGui::Spacing();

        if (ImGui::CollapsingHeader(Get("editor.shortcuts").c_str())) {
            HintText(FormatKeyBinds(Get("editor.entity_mode.hint.create")).c_str());
            HintText(FormatKeyBinds(Get("editor.entity_mode.hint.edit")).c_str());
            HintText(Get("editor.entity_mode.hint.move").c_str());
            HintText(FormatKeyBinds(Get("editor.entity_mode.hint.gizmo")).c_str());
            HintText(FormatKeyBinds(Get("editor.entity_mode.hint.select")).c_str());
            HintText(FormatKeyBinds(Get("editor.entity_mode.hint.copy")).c_str());
            HintText(FormatKeyBinds(Get("editor.entity_mode.hint.focus")).c_str());
        }
    }

    void DrawToolButton(const char *label, const DrawTool tool, const float width) {
        const bool active = (currentDrawTool == tool);

        PushToggleStyle(active);

        if (ImGui::Button(label, ImVec2(width, 0.0f))) SetActiveDrawTool(tool);

        PopToggleStyle();
    }

    // Staircase tool's settings: calculation mode, only the fields that
    // mode actually uses, direction, Raise Ceiling, and - while a
    // rectangle is in progress - the same calculated values the preview
    // shows (BuildStaircasePlan is the single source of truth for both).
    void DrawStaircaseSettingsPanel() {
        constexpr float INPUT_WIDTH = 110.0f;

        // ---- Calculation Mode -----------------------------------------
        ImGui::TextUnformatted(Get("editor.staircase.calc_mode").c_str());

        int calcModeInt = static_cast<int>(staircaseCalcMode);

        if (ImGui::RadioButton((Get("editor.staircase.calc_mode.step_height") + "##StaircaseCalcStepHeight").c_str(), &calcModeInt, STAIRCASE_CALC_STEP_HEIGHT))
            staircaseCalcMode = STAIRCASE_CALC_STEP_HEIGHT;
        HoverTooltip(Get("editor.tooltip.staircase.calc_mode.step_height").c_str());

        ImGui::SameLine();

        if (ImGui::RadioButton((Get("editor.staircase.calc_mode.target_height") + "##StaircaseCalcTargetHeight").c_str(), &calcModeInt, STAIRCASE_CALC_TARGET_HEIGHT))
            staircaseCalcMode = STAIRCASE_CALC_TARGET_HEIGHT;
        HoverTooltip(Get("editor.tooltip.staircase.calc_mode.target_height").c_str());

        ImGui::SameLine();

        if (ImGui::RadioButton((Get("editor.staircase.calc_mode.step_count") + "##StaircaseCalcStepCount").c_str(), &calcModeInt, STAIRCASE_CALC_STEP_COUNT))
            staircaseCalcMode = STAIRCASE_CALC_STEP_COUNT;
        HoverTooltip(Get("editor.tooltip.staircase.calc_mode.step_count").c_str());

        ImGui::Spacing();

        // ---- Mode-specific fields - only what the selected mode uses --
        if (staircaseCalcMode == STAIRCASE_CALC_STEP_HEIGHT || staircaseCalcMode == STAIRCASE_CALC_STEP_COUNT) {
            ImGui::SetNextItemWidth(INPUT_WIDTH);
            ImGui::InputFloat((Get("editor.staircase.step_height") + "##StaircaseStepHeight").c_str(), &staircaseStepHeight, 1.0f, 10.0f, "%.2f");
            HoverTooltip(Get("editor.tooltip.staircase.step_height").c_str());
        }

        if (StaircaseModeUsesStepLength(staircaseCalcMode)) {
            ImGui::SetNextItemWidth(INPUT_WIDTH);
            ImGui::InputFloat((Get("editor.staircase.step_length") + "##StaircaseStepLength").c_str(), &staircaseStepLength, 1.0f, 10.0f, "%.2f");
            staircaseStepLength = std::max(staircaseStepLength, 0.0f);
            HoverTooltip(Get("editor.tooltip.staircase.step_length").c_str());
        }

        if (staircaseCalcMode == STAIRCASE_CALC_TARGET_HEIGHT) {
            ImGui::SetNextItemWidth(INPUT_WIDTH);
            ImGui::InputFloat((Get("editor.staircase.target_floor_height") + "##StaircaseTargetFloorHeight").c_str(), &staircaseTargetFloorHeight, 1.0f, 10.0f, "%.2f");
            HoverTooltip(Get("editor.tooltip.staircase.target_floor_height").c_str());
        }

        if (staircaseCalcMode == STAIRCASE_CALC_STEP_COUNT) {
            ImGui::SetNextItemWidth(INPUT_WIDTH);
            ImGui::InputInt((Get("editor.staircase.step_count") + "##StaircaseStepCount").c_str(), &staircaseStepCount, 1, 10);
            staircaseStepCount = std::max(staircaseStepCount, 1);
            HoverTooltip(Get("editor.tooltip.staircase.step_count").c_str());
        }

        ImGui::Spacing();

        // ---- Direction ---------------------------------------------------
        ImGui::TextUnformatted(Get("editor.staircase.direction").c_str());

        int dirInt = static_cast<int>(staircaseDirection);

        if (ImGui::RadioButton((Get("editor.staircase.direction.horizontal") + "##StaircaseDirHorizontal").c_str(), &dirInt, STAIRCASE_DIR_HORIZONTAL))
            staircaseDirection = STAIRCASE_DIR_HORIZONTAL;
        HoverTooltip(Get("editor.tooltip.staircase.direction.horizontal").c_str());

        ImGui::SameLine();

        if (ImGui::RadioButton((Get("editor.staircase.direction.vertical") + "##StaircaseDirVertical").c_str(), &dirInt, STAIRCASE_DIR_VERTICAL))
            staircaseDirection = STAIRCASE_DIR_VERTICAL;
        HoverTooltip(Get("editor.tooltip.staircase.direction.vertical").c_str());

        ImGui::Spacing();

        ImGui::Checkbox(Get("editor.staircase.raise_ceiling").c_str(), &staircaseRaiseCeiling);
        HoverTooltip(Get("editor.tooltip.staircase.raise_ceiling").c_str());

        // Same flag a single LALT/RALT press toggles (MapEditorInput.cpp) -
        // this checkbox is just another way to flip it, not a separate
        // input path.
        ImGui::Checkbox(Get("editor.staircase.reverse_rise").c_str(), &staircaseReverseRise);
        HoverTooltip(FormatKeyBinds(Get("editor.tooltip.staircase.reverse_rise")).c_str());

        ImGui::Spacing();
        HintText(Get("editor.draw.hint.staircase").c_str());

        // ---- Live calculated values - same plan the preview draws ------
        if (!staircaseHasFirstCorner) return;

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
        const Vector2 opposite = ResolveStaircaseCorner(mouseWorld);

        const StaircasePlan plan = BuildStaircasePlan(staircaseFirstCorner, opposite);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        SectionHeader(Get("editor.staircase.results.title").c_str());
        ImGui::Spacing();

        if (!plan.valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.35f, 0.35f, 1.00f));
            ImGui::TextWrapped("%s", plan.error.c_str());
            ImGui::PopStyleColor();
            return;
        }

        ImGui::Text("%s: %d", Get("editor.staircase.results.step_count").c_str(), plan.stepCount);
        ImGui::Text("%s: %.2f", Get("editor.staircase.results.effective_step_height").c_str(), plan.effectiveStepHeight);
        ImGui::Text("%s: %.2f", Get("editor.staircase.results.effective_step_length").c_str(), plan.effectiveStepLength);
        ImGui::Text("%s: %.2f", Get("editor.staircase.results.starting_floor").c_str(), plan.startingFloorHeight);
        ImGui::Text("%s: %.2f", Get("editor.staircase.results.final_floor").c_str(), plan.finalFloorHeight);

        if (plan.targetSingleSectorNote) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s", Get("editor.staircase.notice.target_single_step").c_str());
            ImGui::PopStyleColor();
        }

        if (plan.headroomWarning) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.15f, 1.0f));
            ImGui::TextWrapped("%s", Get("editor.staircase.warning.headroom").c_str());
            ImGui::PopStyleColor();
        }
    }

    // Tool-select buttons plus whichever settings/hint text apply to
    // the active tool. Sector Mode only, placed right above the existing
    // "New sector properties" section, since every tool here ultimately
    // produces a sector through that same pending-params pipeline.
    //
    // Three buttons per row (matching DrawMode's layout) rather than five
    // across, so translated tool names have room instead of being clipped.
    void DrawDrawingToolsPanel() {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        SectionHeader(Get("editor.draw.title").c_str());
        ImGui::Spacing();

        const float toolButtonWidth = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;

        DrawToolButton(Get("editor.draw.tool.freehand").c_str(), DRAWTOOL_FREEHAND, toolButtonWidth);
        ImGui::SameLine(0.0f, 4.0f);
        DrawToolButton(Get("editor.draw.tool.rectangle").c_str(), DRAWTOOL_RECTANGLE, toolButtonWidth);

        //ImGui::SameLine(0.0f, 4.0f);
        DrawToolButton(Get("editor.draw.tool.polygon").c_str(), DRAWTOOL_POLYGON, toolButtonWidth);
        ImGui::SameLine(0.0f, 4.0f);
        DrawToolButton(Get("editor.draw.tool.staircase").c_str(), DRAWTOOL_STAIRCASE, toolButtonWidth);

        DrawToolButton(Get("editor.draw.tool.circle").c_str(), DRAWTOOL_CIRCLE, toolButtonWidth);
        ImGui::SameLine(0.0f, 4.0f);
        DrawToolButton(Get("editor.draw.tool.curve").c_str(), DRAWTOOL_CURVE, toolButtonWidth);


        ImGui::Spacing();

        switch (currentDrawTool) {
            case DRAWTOOL_FREEHAND: {
                ImGui::Checkbox(Get("editor.manual_sector.mode").c_str(), &manualSectorMode);
                HoverTooltip(Get("editor.manual_sector.tooltip.mode").c_str());

                if (manualSectorMode) {
                    const int cornerCount = static_cast<int>(manualSectorDots.size());

                    const std::string cornerText = (cornerCount == 1)
                                                       ? Get("editor.manual_sector.corner_selected")
                                                       : Get("editor.manual_sector.corners_selected");

                    ImGui::Text(cornerText.c_str(), cornerCount);

                    ImGui::BeginDisabled(manualSectorDots.size() < 3);
                    if (ImGui::Button(Get("editor.manual_sector.create").c_str())) CreateManualSector();
                    ImGui::EndDisabled();

                    HoverTooltip(FormatKeyBinds(Get("editor.manual_sector.tooltip.create")).c_str());

                    ImGui::SameLine();

                    ImGui::BeginDisabled(manualSectorDots.empty());
                    if (ImGui::Button(Get("editor.manual_sector.clear_selected_dots").c_str()))
                        ClearManualSectorSelection();
                    ImGui::EndDisabled();
                } else HintText(Get("editor.draw.hint.freehand").c_str());

                break;
            }

            case DRAWTOOL_RECTANGLE:
                HintText(Get("editor.draw.hint.rectangle").c_str());
                break;

            case DRAWTOOL_POLYGON: {
                ImGui::SetNextItemWidth(110.0f);
                ImGui::SliderInt(
                    (Get("editor.draw.sides") + "##DrawPolygonSides").c_str(),
                    &polygonSideCount,
                    3,
                    MAX_POLYGON_SIDES
                );
                polygonSideCount = std::clamp(polygonSideCount, 3, MAX_POLYGON_SIDES);
                HoverTooltip(FormatKeyBinds(Get("editor.tooltip.draw.sides")).c_str());

                ImGui::SameLine();

                if (ImGui::Button(Get("editor.draw.hexagon").c_str())) polygonSideCount = 6;
                HoverTooltip(Get("editor.tooltip.draw.hexagon").c_str());

                HintText(Get("editor.draw.hint.polygon").c_str());
                break;
            }

            case DRAWTOOL_CIRCLE:
                ImGui::SetNextItemWidth(110.0f);
                ImGui::SliderInt(
                    (Get("editor.draw.segments") + "##DrawCircleSegments").c_str(),
                    &circleSegments,
                    3,
                    128
                );
                circleSegments = std::max(circleSegments, 3);
                HoverTooltip(Get("editor.tooltip.draw.segments").c_str());

                HintText(Get("editor.draw.hint.circle").c_str());
                break;

            case DRAWTOOL_CURVE:
                ImGui::SetNextItemWidth(110.0f);
                ImGui::SliderInt(
                    (Get("editor.draw.subdivisions") + "##DrawCurveSubdivisions").c_str(),
                    &curveSubdivisions,
                    1,
                    64
                );
                curveSubdivisions = std::max(curveSubdivisions, 1);
                HoverTooltip(Get("editor.tooltip.draw.subdivisions").c_str());

                HintText(Get("editor.draw.hint.curve").c_str());
                break;

            case DRAWTOOL_STAIRCASE:
                DrawStaircaseSettingsPanel();
                break;

            default: break;
        }

        ImGui::Spacing();

        HintText(FormatKeyBinds(Get("editor.draw.shortcuts.general")).c_str());
        HintText(FormatKeyBinds(Get("editor.draw.shortcuts.edit_sectors")).c_str());

        // Ctrl's meaning is the same for every tool (temporarily ignore
        // the grid - see ResolveSnapPointExcluding), so it's shown
        // unconditionally; Shift's meaning is tool-specific.
        HintText(FormatKeyBinds(Get("editor.draw.shortcuts.no_snap")).c_str());

        if (currentDrawTool == DRAWTOOL_STAIRCASE) {
            HintText(FormatKeyBinds(Get("editor.draw.shortcuts.staircase_constrain")).c_str());
            HintText(FormatKeyBinds(Get("editor.draw.shortcuts.staircase_reverse")).c_str());
        }
        else HintText(FormatKeyBinds(Get("editor.draw.shortcuts.constrain")).c_str());

        if (currentDrawTool == DRAWTOOL_POLYGON)
            HintText(FormatKeyBinds(Get("editor.draw.shortcuts.sides")).c_str());

        HintText(FormatKeyBinds(Get("editor.draw.shortcuts.tools")).c_str());
    }

    // =========================================================================
    //  Editor Panel
    // =========================================================================

    void DrawEditorPanel() {
        // Append * to the title bar when the level has unsaved changes. The
        // ### part keeps the window's ID the same either way, so it doesn't
        // jump to a different saved position when the * appears.
        std::string panelTitle = Get("editor.title");
        if (hasUnsavedChanges) panelTitle += "  *";
        panelTitle += "###EditorPanel";

        // Layouts saved before the ### ID still have this panel under its
        // plain title; start it there instead of at the default spot. Only
        // matters until the new ID has saved settings of its own.
        if (const ImGuiWindowSettings *previous = ImGui::FindWindowSettingsByID(ImHashStr(Get("editor.title").c_str()))) {
            // Saved positions are relative to the viewport they were saved in.
            const ImVec2 position(previous->ViewportPos.x + previous->Pos.x, previous->ViewportPos.y + previous->Pos.y);
            ImGui::SetNextWindowPos(position, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(previous->Size.x, previous->Size.y), ImGuiCond_FirstUseEver);
        }

        ImGui::Begin(panelTitle.c_str());

        // ---- Mode ---------------------------------------------------------
        SectionHeader(Get("editor.mode").c_str());
        ImGui::Spacing();
        DrawMode();
        ImGui::Spacing();

        // ---- View toggles -------------------------------------------------
        ImGui::Separator();
        ImGui::Spacing();

        {
            // Same order as TextureViewMode.
            static constexpr const char* labelKeys[] = {
                "editor.texture_view_mode.off",
                "editor.texture_view_mode.floor",
                "editor.texture_view_mode.ceiling",
                "editor.texture_view_mode.brightness",
                "editor.texture_view_mode.floor_height",
                "editor.texture_view_mode.ceiling_height",
                "editor.texture_view_mode.room_height",
            };
            static_assert(std::size(labelKeys) == static_cast<size_t>(TextureViewMode::Count));

            const int current = static_cast<int>(textureViewMode);

            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo(Get("editor.texture_view_mode").c_str(), Get(labelKeys[current]).c_str())) {
                for (int mode = 0; mode < static_cast<int>(TextureViewMode::Count); ++mode) {
                    const bool selected = mode == current;
                    if (ImGui::Selectable(Get(labelKeys[mode]).c_str(), selected))
                        textureViewMode = static_cast<TextureViewMode>(mode);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            HoverTooltip(FormatKeyBinds(Get("editor.tooltip.texture_view_mode")).c_str());
        }

        // ---- Grid & snapping (visible in every mode - Geometry Mode's
        // wall/endpoint dragging and every Sector drawing tool both
        // read GRID_SIZE/gridSnapEnabled/vertexSnapEnabled) -----------------
        DrawGridSnappingSection();

        // ---- Geometry Mode hints (only visible in Geometry mode) ---------
        if (currentMode == MODE_GEOMETRY) DrawGeometryModePanel();

        // ---- Entity Mode hints (only visible in Entity mode) -------------
        if (currentMode == MODE_ENTITY) DrawEntityModePanel();

        // ---- Sector creation params (only visible in Sector mode) ---------
        if (currentMode == MODE_SECTOR) {
            DrawDrawingToolsPanel();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            SectionHeader(Get("editor.new_sector_properties").c_str());
            ImGui::Separator();
            ImGui::Spacing();

            if (DrawAssetField(
                Get("editor.new_sector.wall_top_texture").c_str(),
                wallTopTexture,
                AssetKind::Texture,
                32.0f
            )) hasUnsavedChanges = true;

            ImGui::SameLine(100.0f);

            if (DrawAssetField(
                Get("editor.new_sector.wall_bottom_texture").c_str(),
                wallBottomTexture,
                AssetKind::Texture,
                32.0f
            )) hasUnsavedChanges = true;


            if (DrawAssetField(
                Get("editor.new_sector.ceil_texture").c_str(),
                ceilTexture,
                AssetKind::Texture,
                32.0f
            )) hasUnsavedChanges = true;

            ImGui::SameLine(100.0f);

            if (DrawAssetField(
                Get("editor.new_sector.floor_texture").c_str(),
                floorTexture,
                AssetKind::Texture,
                32.0f
            )) hasUnsavedChanges = true;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            constexpr float INPUT_WIDTH = 110.0f;

            ImGui::SetNextItemWidth(INPUT_WIDTH);

            if (ImGui::InputFloat(
                (Get("sector.floor_height") + "##NewSectorFloorHeight").c_str(),
                &floorHeight,
                1.0f,
                10.0f,
                "%.2f"
            )) hasUnsavedChanges = true;


            HoverTooltip(Get("editor.tooltip.floor_height").c_str());

            ImGui::SetNextItemWidth(INPUT_WIDTH);

            if (ImGui::InputFloat(
                (Get("sector.ceil_height") + "##NewSectorCeilHeight").c_str(),
                &ceilHeight,
                1.0f,
                10.0f,
                "%.2f"
            )) hasUnsavedChanges = true;


            HoverTooltip(Get("editor.tooltip.ceil_height").c_str());

            if (ceilHeight <= floorHeight) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));

                ImGui::TextWrapped(Get("editor.tooltip.sector.invalid_floor_heights").c_str());

                ImGui::PopStyleColor();
            }

            ImGui::SetNextItemWidth(INPUT_WIDTH);

            float lightPicker[3] = {
                lightValue.x / 255.0f,
                lightValue.y / 255.0f,
                lightValue.z / 255.0f
            };

            if (ImGui::ColorEdit3(
            (Get("sector.light_value") + "##NewSectorLight").c_str(),
                lightPicker,
                ImGuiColorEditFlags_Uint8
            )) {
                lightValue = {
                    lightPicker[0] * 255.0f,
                    lightPicker[1] * 255.0f,
                    lightPicker[2] * 255.0f
                };

                hasUnsavedChanges = true;
            }

            HoverTooltip(Get("editor.tooltip.light_value").c_str());

            ImGui::Spacing();

            constexpr ImGuiColorEditFlags_ COLOR_PICKER_FLAGS = static_cast<ImGuiColorEditFlags_>(ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_AlphaPreviewHalf |
                ImGuiColorEditFlags_HDR |
                ImGuiColorEditFlags_Float);

            if (ImGui::ColorEdit4(
                Get("wall.color").c_str(),
                &wallColor.x,
                COLOR_PICKER_FLAGS
            )) hasUnsavedChanges = true;


            if (ImGui::ColorEdit4(
                Get("sector.ceil_color").c_str(),
                &ceilColor.x,
                COLOR_PICKER_FLAGS
            )) hasUnsavedChanges = true;


            if (ImGui::ColorEdit4(
                Get("sector.floor_color").c_str(),
                &floorColor.x,
                COLOR_PICKER_FLAGS
            )) hasUnsavedChanges = true;

            ImGui::Spacing();

            pendingSectorParams.wallTopTexture = wallTopTexture;
            pendingSectorParams.wallBottomTexture = wallBottomTexture;
            pendingSectorParams.lightValue = lightValue;
            pendingSectorParams.wallColor = wallColor;

            if (pendingSectorParams.floors.empty()) pendingSectorParams.floors.resize(1);
            else if (pendingSectorParams.floors.size() > 1) pendingSectorParams.floors.resize(1);

            SectorFloor& pendingFloor = pendingSectorParams.floors.front();
            SectorSurface& floor = pendingFloor.floor;
            SectorSurface& ceiling = pendingFloor.ceiling;

            floor.height = floorHeight;
            floor.color = floorColor;
            floor.texture = floorTexture;

            ceiling.height = std::max(ceilHeight, floorHeight + MIN_ROOM_HEIGHT);

            ceiling.color = ceilColor;
            ceiling.texture = ceilTexture;
        }

        // ---- Floor selector -----------------------------------------------
        //ImGui::SetNextItemWidth(80.0f);
        //ImGui::InputInt(Get("editor.floor").c_str(), &currentFloor);
        //HoverTooltip("Active editor floor / Z-layer.");

        ImGui::End();
    }

    // =========================================================================
    //  History
    // =========================================================================

    void DrawHistoryPanel() {
        ImGui::SetNextWindowSize(ImVec2(260.0f, 320.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(Get("editor.history").c_str(), &historyVisible)) {
            std::vector<std::string> undoLabels;
            std::vector<std::string> redoLabels;

            for (const UndoStep& step : undoSteps) undoLabels.push_back(HistoryLabel(step.labelKey, step.count));
            for (auto it = redoSteps.rbegin(); it != redoSteps.rend(); ++it) redoLabels.push_back(HistoryLabel(it->labelKey, it->count));

            const int position = ImGuiDrawFunctions::DrawHistoryList(undoLabels, redoLabels, Get("editor.history.initial_state"));
            if (position >= 0) JumpToUndoPosition(static_cast<std::size_t>(position));
        }

        ImGui::End();
    }

    void DrawEditorUI() {
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);

        DrawMainMenuBar();
        ProcessPendingGameExport();
        ProcessPendingObjExport();
        DrawStatusBar();
        DrawDockSpace();
        SubmitCanvasEntityDragSource();

        Level &level = LevelManager::CurrentLevel();

        if (editorPanelVisible) DrawEditorPanel();

        // ---- Inspector ----------------------------------------------------
        // Each inspector is a window of its own, so they stay up even when
        // the Editor Panel is hidden.
        DrawSelectionInspectors(level);

        // ---- Other panels -------------------------------------------------
        if (worldSettingsVisible) DrawWorldSettings();
        if (hierarchyVisible) DrawHierarchyPanel(level);
        DrawToolbar();

        //if (editingEntity || editingSector || editingWall) // This cant work because of sector creation
        if (assetBrowserVisible) DrawAssetBrowserPanel();
        if (historyVisible) DrawHistoryPanel();

        // The 2D editor's actions that have no key of their own.
        static const bool paletteCommandsRegistered = [] {
            CommandPalette::Register("command_palette.command.obj_export", KEYSCOPE_MAP, &ShowObjExportDialog);
            CommandPalette::Register("command_palette.command.toggle_tag_explorer", KEYSCOPE_MAP, [] { tagExplorerVisible = !tagExplorerVisible; });
            CommandPalette::Register("command_palette.command.toggle_comments", KEYSCOPE_MAP, [] { commentsVisible = !commentsVisible; });
            CommandPalette::Register("command_palette.command.toggle_history", KEYSCOPE_MAP, [] { historyVisible = !historyVisible; });
            CommandPalette::Register("command_palette.command.toggle_paint_select", KEYSCOPE_MAP, [] { paintSelectTool = !paintSelectTool; });

            static constexpr const char* VIEW_MODE_COMMANDS[] = {
                "command_palette.command.view_off",
                "command_palette.command.view_floor",
                "command_palette.command.view_ceiling",
                "command_palette.command.view_brightness",
                "command_palette.command.view_floor_height",
                "command_palette.command.view_ceiling_height",
                "command_palette.command.view_room_height",
            };
            static_assert(std::size(VIEW_MODE_COMMANDS) == static_cast<size_t>(TextureViewMode::Count));

            for (int mode = 0; mode < static_cast<int>(TextureViewMode::Count); ++mode)
                CommandPalette::Register(VIEW_MODE_COMMANDS[mode], KEYSCOPE_MAP, [mode] { textureViewMode = static_cast<TextureViewMode>(mode); });

            return true;
        }();
        (void)paletteCommandsRegistered;

        CommandPalette::Draw(KEYSCOPE_MAP, [](const KeyBindScope scope) {
            if (scope == KEYSCOPE_RUNTIME) OpenRuntimeEditorFromEditor();
            else if (scope == KEYSCOPE_UI) currentState = STATE_UI;
        });

        if (tagExplorerVisible)
            TagExplorer::Draw(level, (Get("tag_explorer.title") + "###TagExplorer").c_str(), &tagExplorerVisible, &SelectAndShowObject);

        // A pin centres the view on itself; anything else is selected.
        if (commentsVisible)
            Comments::DrawPanel(level, (Get("comments.title") + "###Comments").c_str(), &commentsVisible, [](const EditorComment& comment) {
                if (comment.target == EditorCommentTarget::PIN) cameraPos = comment.position;
                else SelectAndShowObject(Comments::OwnerKindFor(comment.target), comment.targetID);
            });

        // ---- Sector-geometry rejection toast --------------------------------
        if (!lastGeometryError.empty()) {
            ShowNotification(lastGeometryError.c_str(), true);
            lastGeometryError.clear();
        }

        // ---- Toast notification -------------------------------------------
        DrawNotification(dt);
    }
} // namespace MapEditorInternal

namespace Editor {
    void OpenLevel(const std::string& levelName) {
        if (!currentMap.empty()) MapEditorInternal::Save(currentMap);
        MapEditorInternal::QueueLevelLoad(LevelSerialization::CleanLevelName(levelName));
    }
}
