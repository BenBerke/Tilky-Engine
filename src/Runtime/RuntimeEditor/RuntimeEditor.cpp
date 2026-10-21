//
// Created by berke on 6/3/2026.
//

#include "Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp"

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Map/WallPieces.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"

#include "Headers/Runtime/Renderer/IRenderer.hpp"

#include "../../Editor/EditorInternal.hpp"
#include "../../Editor/WallSnap.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Editor/AssetBrowser.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Runtime/RuntimeEditor/TransformGizmo.hpp"
#include "Headers/Runtime/RuntimeEditor/MultiEdit.hpp"
#include "External/tracy/profiler/src/profiler/IconsFontAwesome6.h"

#include <string_view>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <numbers>
#include <limits>
#include <optional>
#include <SDL3/SDL.h>
#include <string>
#include <vector>

#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

namespace {
    IRenderer* runtimeRenderer = nullptr;
    ComponentCamera* camera = nullptr;
    ComponentTransform* transform = nullptr;

    // InputManager::SetRelativeMouseMode needs the window, and Shutdown() has
    // to release the cursor while the renderer reference is already being torn
    // down - so cache it in Start() rather than reaching through the renderer
    // at each call site.
    SDL_Window* editorWindow = nullptr;

    constexpr float MOUSE_SENSITIVITY = 0.5f;
    constexpr float BASE_MOVE_SPEED = 50.0f;
    constexpr float RAY_LENGTH = 10000.0f;

    float moveSpeed = 50.0f;

    // Toolbar layout and its camera speed slider range. Q/E and Shift
    // still change and reset the speed directly.
    constexpr float TOOLBAR_BUTTON_SIZE = 32.0f;
    constexpr float TOOLBAR_SLIDER_WIDTH = 140.0f;
    constexpr float MIN_SLIDER_MOVE_SPEED = 1.0f;
    constexpr float MAX_SLIDER_MOVE_SPEED = 1000.0f;

    Vector3 GetCameraForward(const ComponentCamera& camera) {
        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;
        const float pitchRadians = camera.pitch * std::numbers::pi_v<float> / 180.0f;
        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);
        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        return Vector3Math::Normalized({yawSin * pitchCos, pitchSin, yawCos * pitchCos});
    }

    Vector3 GetCameraLeft(const ComponentCamera& camera) {
        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;
        return Vector3Math::Normalized({std::cos(yawRadians), 0.0f, -std::sin(yawRadians)});
    }

    Vector3 GetMouseRayDirection(const ComponentCamera& camera, const Vector2 mousePosition, const Vector2 viewportSize) {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return camera.forward;

        const float ndcX = (2.0f * mousePosition.x / viewportSize.x) - 1.0f;
        const float ndcY = 1.0f - (2.0f * mousePosition.y / viewportSize.y);

        const Vector3 forward = Vector3Math::Normalized(camera.forward);
        const Vector3 left = GetCameraLeft(camera);
        const Vector3 up = Vector3Math::Normalized(Vector3Math::Cross(forward, left));
        const float aspect = viewportSize.x / viewportSize.y;
        const float fovRadians = camera.fov * std::numbers::pi_v<float> / 180.0f;
        const float tanHalfFov = std::tan(fovRadians * 0.5f);

        return Vector3Math::Normalized(
            forward +
            left * (-ndcX * tanHalfFov * aspect) +
            up * (ndcY * tanHalfFov)
        );
    }

    ImGuiDrawFunctions::EntityInspectorState entityInspectorState;

    bool editingEntity = false;
    std::optional<ID> selectedEntityId;

    bool editingWall = false;
    int selectedWall = -1;

    bool editingSector = false;
    int selectedSector = -1;

    RayHitType selectedSectorSurface = RayHitType::None;
    int selectedSectorFloor = -1;

    // Multi-selection (Ctrl + right-click). Each list holds every selected
    // item of its kind, the primary - the one the inspector above shows -
    // included. Only one kind is ever selected at a time.
    std::vector<ID> selectedEntityIds;
    std::vector<int> selectedWalls; // indices into level.walls

    // A floor or ceiling of one of a sector's floors. Floors and ceilings
    // count as one kind, so both can be in the selection together.
    struct SelectedFlat {
        int sectorIndex = -1;
        int floorIndex = -1;
        RayHitType surface = RayHitType::SectorFloor;

        bool operator==(const SelectedFlat&) const = default;
    };

    std::vector<SelectedFlat> selectedFlats;

    enum class SelectionKind { None, Entity, Wall, Flat };

    // Camera look and UV-offset dragging both consume relative mouse motion.
    // Keep the cursor locked while either interaction is active and release it
    // only after both have ended. Update() owns this; Draw() only reads it.
    bool cameraLooking = false;

    bool backToEditorRequested = false;

    bool cursorLocked = false;
    Vector2 cursorBeforeLock = {};
    std::optional<SDL_Rect> mouseRectBeforeLock;

    // Set true to print the live payload next to the cursor while dragging.
    constexpr bool DEBUG_DRAG_DROP = false;

    // Indices, not pointers: the cache outlives the RayHit by a frame and the
    // level vectors can reallocate in between.
    struct SurfaceRef {
        RayHitType type = RayHitType::None;
        int wallIndex = -1;
        WallSurfaceSlot wallSlot = WallSurfaceSlot::Top; // the wall texture under the cursor
        int sectorIndex = -1;
        int floorIndex = -1;
        ID entityID = INVALID_ID; // when type is Entity
    };

    SurfaceRef hoveredSurface;

    // Where the mouse ray hit, when hoveredSurface.type isn't None.
    Vector3 hoveredHitPosition = {};
    Vector3 hoveredRayDirection = {};

    // Captured on button-down so a drag that slides off the surface keeps
    // editing the one it started on.
    SurfaceRef uvDragSurface;
    bool draggingUv = false;

    // +1 or -1, decided once on button-down and applied to the horizontal
    // delta of the whole drag. Re-deriving it per frame would let it flip
    // mid-drag as the camera or the ray moves.
    float uvDragHorizontalSign = 1.0f;

    // Most recent sign a wall drag actually resolved. Reused when the camera is
    // edge-on to a wall and the viewed face cannot be told apart.
    float lastWallDragHorizontalSign = 1.0f;

    // How a floor or ceiling drag maps the mouse onto the texture offset,
    // decided once on button-down like uvDragHorizontalSign. Offset X slides
    // the texture along world X, offset Y along world Z; `horizontalAxis` is
    // the one left/right mouse movement drives (0 = X, 1 = Y), up/down
    // drives the other. The signs make the texture follow the cursor.
    struct FlatUvDrag {
        int horizontalAxis = 0;
        float horizontalSign = 1.0f;
        float verticalSign = 1.0f;
    };

    FlatUvDrag flatUvDrag;

    // The transform gizmo on the entity being edited. Chosen with the Tools
    // window's buttons or the 1/2 keys.
    TransformGizmo::Tool gizmoTool = TransformGizmo::Tool::Move;

    // Saved by Update() for Draw(), which has no viewport size of its own.
    Vector2 gizmoViewportSize = {};

    // Whether the drag in progress has changed anything yet, so ending it
    // only marks the level unsaved when it did.
    bool gizmoChangedTransform = false;
}

namespace {
    void ResetEntityInspectorState() {
        entityInspectorState = {};
    }

    void ClearEntitySelection() {
        editingEntity = false;
        selectedEntityId.reset();
        selectedEntityIds.clear();
        ResetEntityInspectorState();
    }

    void ClearWallSelection() {
        editingWall = false;
        selectedWall = -1;
        selectedWalls.clear();
    }

    void ClearFlatSelection() {
        editingSector = false;
        selectedSector = -1;
        selectedSectorSurface = RayHitType::None;
        selectedSectorFloor = -1;
        selectedFlats.clear();
    }

    SelectionKind CurrentSelectionKind() {
        if (editingEntity && selectedEntityId.has_value()) return SelectionKind::Entity;
        if (editingWall && selectedWall >= 0) return SelectionKind::Wall;
        if (editingSector && selectedSector >= 0) return SelectionKind::Flat;
        return SelectionKind::None;
    }

    // Plain click: the clicked item becomes the whole selection.
    void SelectOnlyEntity(const ID entityID) {
        ClearWallSelection();
        ClearFlatSelection();

        if (!selectedEntityId.has_value() || *selectedEntityId != entityID) ResetEntityInspectorState();

        selectedEntityIds = {entityID};
        selectedEntityId = entityID;
        editingEntity = true;
    }

    void SelectOnlyWall(const int wallIndex) {
        ClearEntitySelection();
        ClearFlatSelection();

        selectedWalls = {wallIndex};
        selectedWall = wallIndex;
        editingWall = true;
    }

    void SetPrimaryFlat(const SelectedFlat& flat) {
        selectedSector = flat.sectorIndex;
        selectedSectorFloor = flat.floorIndex;
        selectedSectorSurface = flat.surface;
        editingSector = true;
    }

    void SelectOnlyFlat(const SelectedFlat& flat) {
        ClearEntitySelection();
        ClearWallSelection();

        selectedFlats = {flat};
        SetPrimaryFlat(flat);
    }

    // Ctrl-click on the kind already selected: adds the item and makes it
    // the primary, or - if it was already selected - drops it, handing the
    // inspector to the most recently added item still left.
    void ToggleEntity(const ID entityID) {
        if (const auto it = std::ranges::find(selectedEntityIds, entityID); it != selectedEntityIds.end()) {
            selectedEntityIds.erase(it);

            if (selectedEntityIds.empty()) {
                ClearEntitySelection();
                return;
            }

            if (*selectedEntityId == entityID) {
                selectedEntityId = selectedEntityIds.back();
                ResetEntityInspectorState();
            }

            return;
        }

        selectedEntityIds.push_back(entityID);
        selectedEntityId = entityID;
        ResetEntityInspectorState();
    }

    void ToggleWall(const int wallIndex) {
        if (const auto it = std::ranges::find(selectedWalls, wallIndex); it != selectedWalls.end()) {
            selectedWalls.erase(it);

            if (selectedWalls.empty()) {
                ClearWallSelection();
                return;
            }

            if (selectedWall == wallIndex) selectedWall = selectedWalls.back();
            return;
        }

        selectedWalls.push_back(wallIndex);
        selectedWall = wallIndex;
    }

    void ToggleFlat(const SelectedFlat& flat) {
        if (const auto it = std::ranges::find(selectedFlats, flat); it != selectedFlats.end()) {
            const bool wasPrimary = it->sectorIndex == selectedSector &&
                                    it->floorIndex == selectedSectorFloor &&
                                    it->surface == selectedSectorSurface;

            selectedFlats.erase(it);

            if (selectedFlats.empty()) {
                ClearFlatSelection();
                return;
            }

            if (wasPrimary) SetPrimaryFlat(selectedFlats.back());
            return;
        }

        selectedFlats.push_back(flat);
        SetPrimaryFlat(flat);
    }

    // Relative mode supplies motion without moving the OS pointer. The
    // one-pixel rectangle fixes its absolute position as well. Restore that
    // position before leaving relative mode so the cursor reappears exactly
    // where the interaction started.
    void SetCursorLocked(const bool locked) {
        if (editorWindow == nullptr) return;

        if (locked) {
            if (cursorLocked) return;

            SDL_GetMouseState(&cursorBeforeLock.x, &cursorBeforeLock.y);

            const SDL_Rect* previousRect = SDL_GetWindowMouseRect(editorWindow);
            mouseRectBeforeLock = previousRect != nullptr
                ? std::optional<SDL_Rect>{*previousRect}
                : std::nullopt;

            InputManager::SetRelativeMouseMode(editorWindow, true);

            const SDL_Rect lockRect = {
                static_cast<int>(cursorBeforeLock.x),
                static_cast<int>(cursorBeforeLock.y),
                1, 1
            };

            if (!SDL_SetWindowMouseRect(editorWindow, &lockRect))
                spdlog::warn("Runtime editor could not fix the cursor position: {}", SDL_GetError());

            cursorLocked = true;
            return;
        }

        if (cursorLocked) {
            if (!SDL_SetWindowMouseRect(
                    editorWindow,
                    mouseRectBeforeLock.has_value() ? &*mouseRectBeforeLock : nullptr))
                spdlog::warn("Runtime editor could not restore the mouse rectangle: {}", SDL_GetError());

            // Do not move the pointer over another application after focus loss.
            if (SDL_GetMouseFocus() == editorWindow)
                SDL_WarpMouseInWindow(editorWindow, cursorBeforeLock.x, cursorBeforeLock.y);

            cursorLocked = false;
            mouseRectBeforeLock.reset();
        }

        InputManager::SetRelativeMouseMode(editorWindow, false);
    }

    // The gizmo only shows while an entity's inspector is open.
    ComponentTransform* GizmoTarget(Level& level) {
        if (!editingEntity || !selectedEntityId.has_value()) return nullptr;

        return level.transforms.Get(*selectedEntityId);
    }

    bool BuildGizmoView(TransformGizmo::View& view) {
        if (camera == nullptr || transform == nullptr) return false;
        if (gizmoViewportSize.x <= 0.0f || gizmoViewportSize.y <= 0.0f) return false;

        view.eye = transform->position;
        view.forward = Vector3Math::Normalized(camera->forward);
        view.left = GetCameraLeft(*camera);
        view.up = Vector3Math::Normalized(Vector3Math::Cross(view.forward, view.left));
        view.tanHalfFov = std::tan(camera->fov * std::numbers::pi_v<float> / 180.0f * 0.5f);
        view.viewportSize = gizmoViewportSize;

        return true;
    }

    const char* EntityMarkerModeKey(const MapEditorInternal::EntityMarkerMode mode) {
        switch (mode) {
            case MapEditorInternal::EntityMarkerMode::Always: return "runtime_editor.entity_markers.always";
            case MapEditorInternal::EntityMarkerMode::VisibleOnly: return "runtime_editor.entity_markers.visible_only";
            default: return "runtime_editor.entity_markers.off";
        }
    }

    // Always -> VisibleOnly -> Off -> Always. `notify` shows the new mode in a
    // toast, for the key, which has no button to look at.
    void CycleEntityMarkerMode(const bool notify) {
        using MapEditorInternal::EntityMarkerMode;

        const int next = (static_cast<int>(MapEditorInternal::entityMarkerMode) + 1) % static_cast<int>(EntityMarkerMode::Count);
        MapEditorInternal::entityMarkerMode = static_cast<EntityMarkerMode>(next);

        if (notify) {
            const std::string text = Localisation::Get("runtime_editor.entity_markers") + ": "
                                   + Localisation::Get(EntityMarkerModeKey(MapEditorInternal::entityMarkerMode));
            ImGuiDrawFunctions::ShowNotification(text.c_str(), false, 1.5f);
        }
    }

    // The renderer draws them, depth tested in VisibleOnly.
    void UpdateEntityMarkers() {
        using MapEditorInternal::EntityMarkerMode;

        switch (MapEditorInternal::entityMarkerMode) {
            case EntityMarkerMode::Always: runtimeRenderer->SetEntityMarkers(IRenderer::EntityMarkers::Always); break;
            case EntityMarkerMode::VisibleOnly: runtimeRenderer->SetEntityMarkers(IRenderer::EntityMarkers::VisibleOnly); break;
            default: runtimeRenderer->SetEntityMarkers(IRenderer::EntityMarkers::Off); break;
        }
    }

    int FindWallIndex(const Level& level, const Wall* wallToFind) {
        if (wallToFind == nullptr) return -1;

        for (int i = 0; i < static_cast<int>(level.walls.size()); i++) if (&level.walls[i] == wallToFind) return i;

        return -1;
    }

    int FindSectorIndex(Level& level, const Sector* sector) {
        if (sector == nullptr) return -1;

        for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i) if (&level.sectors[i] == sector) return i;

        return -1;
    }

    // The browser sends an absolute path; the level stores an asset reference
    // (assets-root-relative, generic separators). ToAssetReference is the one
    // function that performs that conversion, and it is what DrawAssetField
    // already calls on drop - so going through it here guarantees the runtime
    // editor and the inspector can never write two different strings for the
    // same file. A bare filename does not resolve, which is why a hand-rolled
    // conversion rendered black.
    std::string PayloadToTextureReference(const ImGuiPayload* payload) {
        if (payload == nullptr || payload->Data == nullptr || payload->DataSize <= 0) return {};

        const char* raw = static_cast<const char*>(payload->Data);
        const std::string text(raw, strnlen(raw, static_cast<size_t>(payload->DataSize)));

        if (text.empty()) return {};

        return AssetBrowser::ToAssetReference(std::filesystem::path(text), AssetKind::Texture);
    }

    std::string DescribeHoveredSurface() {
        switch (hoveredSurface.type) {
            case RayHitType::Wall:
                return "wall #" + std::to_string(hoveredSurface.wallIndex);

            case RayHitType::SectorFloor:
                return "floor of sector #" + std::to_string(hoveredSurface.sectorIndex);

            case RayHitType::SectorCeiling:
                return "ceiling of sector #" + std::to_string(hoveredSurface.sectorIndex);

            default:
                return {};
        }
    }

    // What the surface is showing right now. A texture that already renders
    // correctly is the ground truth for the string format the engine expects.
    std::string CurrentTextureOfHoveredSurface(Level& level) {
        switch (hoveredSurface.type) {
            case RayHitType::Wall: {
                if (hoveredSurface.wallIndex < 0 ||
                    hoveredSurface.wallIndex >= static_cast<int>(level.walls.size())) return {};

                return level.walls[hoveredSurface.wallIndex].Surface(hoveredSurface.wallSlot).texture;
            }

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (hoveredSurface.sectorIndex < 0 ||
                    hoveredSurface.sectorIndex >= static_cast<int>(level.sectors.size())) return {};

                Sector& sector = level.sectors[hoveredSurface.sectorIndex];

                if (hoveredSurface.floorIndex < 0 ||
                    hoveredSurface.floorIndex >= static_cast<int>(sector.floors.size())) return {};

                const SectorFloor& sectorFloor = sector.floors[hoveredSurface.floorIndex];

                return hoveredSurface.type == RayHitType::SectorFloor
                    ? sectorFloor.floor.texture
                    : sectorFloor.ceiling.texture;
            }

            default:
                return {};
        }
    }

    bool ApplyTextureToHoveredSurface(Level& level, const std::string& textureFileName) {
        switch (hoveredSurface.type) {
            case RayHitType::Wall: {
                if (hoveredSurface.wallIndex < 0 ||
                    hoveredSurface.wallIndex >= static_cast<int>(level.walls.size())) return false;

                level.walls[hoveredSurface.wallIndex].Surface(hoveredSurface.wallSlot).texture = textureFileName;
                return true;
            }

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (hoveredSurface.sectorIndex < 0 ||
                    hoveredSurface.sectorIndex >= static_cast<int>(level.sectors.size())) return false;

                Sector& sector = level.sectors[hoveredSurface.sectorIndex];

                if (hoveredSurface.floorIndex < 0 ||
                    hoveredSurface.floorIndex >= static_cast<int>(sector.floors.size())) return false;

                SectorFloor& sectorFloor = sector.floors[hoveredSurface.floorIndex];

                if (hoveredSurface.type == RayHitType::SectorFloor)
                    sectorFloor.floor.texture = textureFileName;
                else
                    sectorFloor.ceiling.texture = textureFileName;

                return true;
            }

            default:
                return false;
        }
    }

    // WallSurface::textureOffset and SectorSurface::textureOffset are the same idea
    // living in two structs, so the bounds checks happen here once instead of
    // at the call site. Returns false for entities and for stale indices.
    bool AddUvOffset(Level& level, const SurfaceRef& surface, const Vector2 delta) {
        switch (surface.type) {
            case RayHitType::Wall: {
                if (surface.wallIndex < 0 ||
                    surface.wallIndex >= static_cast<int>(level.walls.size())) return false;

                WallSurface& wallSurface = level.walls[surface.wallIndex].Surface(surface.wallSlot);

                wallSurface.textureOffset.x += delta.x;
                wallSurface.textureOffset.y += delta.y;

                return true;
            }

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (surface.sectorIndex < 0 ||
                    surface.sectorIndex >= static_cast<int>(level.sectors.size())) return false;

                Sector& sector = level.sectors[surface.sectorIndex];

                if (surface.floorIndex < 0 ||
                    surface.floorIndex >= static_cast<int>(sector.floors.size())) return false;

                SectorFloor& sectorFloor = sector.floors[surface.floorIndex];

                SectorSurface& target = surface.type == RayHitType::SectorFloor
                    ? sectorFloor.floor
                    : sectorFloor.ceiling;

                target.textureOffset.x += delta.x;
                target.textureOffset.y += delta.y;

                return true;
            }

            default:
                return false;
        }
    }

    // Calls fn(surface) with the WallSurface or SectorSurface under the
    // cursor. False (fn not called) for entities, nothing, or stale indices.
    template <typename Fn>
    bool ForHoveredSurface(Level& level, Fn&& fn) {
        switch (hoveredSurface.type) {
            case RayHitType::Wall:
                if (hoveredSurface.wallIndex < 0 || hoveredSurface.wallIndex >= static_cast<int>(level.walls.size())) return false;

                fn(level.walls[hoveredSurface.wallIndex].Surface(hoveredSurface.wallSlot));
                return true;

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (hoveredSurface.sectorIndex < 0 || hoveredSurface.sectorIndex >= static_cast<int>(level.sectors.size())) return false;

                Sector& sector = level.sectors[hoveredSurface.sectorIndex];
                if (hoveredSurface.floorIndex < 0 || hoveredSurface.floorIndex >= static_cast<int>(sector.floors.size())) return false;

                SectorFloor& sectorFloor = sector.floors[hoveredSurface.floorIndex];
                fn(hoveredSurface.type == RayHitType::SectorFloor ? sectorFloor.floor : sectorFloor.ceiling);
                return true;
            }

            default: return false;
        }
    }

    // Calls fn(surface) for every surface a texture action applies to: the
    // selected walls (both their textures) or flats, or, with neither
    // selected and hoverAllowed, the surface under the cursor. Returns how
    // many walls or flats that was.
    template <typename Fn>
    int ForEachTargetSurface(Level& level, const bool hoverAllowed, Fn&& fn) {
        int count = 0;

        if (!selectedWalls.empty()) {
            for (const int wallIndex : selectedWalls) {
                if (wallIndex < 0 || wallIndex >= static_cast<int>(level.walls.size())) continue;

                Wall& wall = level.walls[wallIndex];
                fn(wall.top);
                fn(wall.bottom);

                // Only a middle that exists: giving every selected wall a
                // middle texture would fill all of their openings.
                if (wall.HasMiddle()) fn(wall.middle);
                ++count;
            }
            return count;
        }

        if (!selectedFlats.empty()) {
            for (const SelectedFlat& flat : selectedFlats) {
                if (flat.sectorIndex < 0 || flat.sectorIndex >= static_cast<int>(level.sectors.size())) continue;

                Sector& sector = level.sectors[flat.sectorIndex];
                if (flat.floorIndex < 0 || flat.floorIndex >= static_cast<int>(sector.floors.size())) continue;

                SectorFloor& sectorFloor = sector.floors[flat.floorIndex];
                fn(flat.surface == RayHitType::SectorFloor ? sectorFloor.floor : sectorFloor.ceiling);
                ++count;
            }
            return count;
        }

        return hoverAllowed && ForHoveredSurface(level, fn) ? 1 : 0;
    }

    // Each surface toggles on its own, so a mixed selection stays mixed.
    int FlipTextures(Level& level, const bool flipX, const bool flipY, const bool hoverAllowed) {
        return ForEachTargetSurface(level, hoverAllowed, [&](auto& surface) {
            if (flipX) surface.flipTextureX = !surface.flipTextureX;
            if (flipY) surface.flipTextureY = !surface.flipTextureY;
        });
    }

    // Ctrl+C / Ctrl+V copy and paste a surface's texture; Alt+C / Alt+V
    // its offset, scale and flip as well. Walls and flats share one clipboard.
    struct SurfaceClipboard {
        bool hasTexture = false;
        bool hasPlacement = false; // copied with Alt

        std::string texture;
        Vector2 textureOffset = {0.0f, 0.0f};
        Vector2 textureScale = {1.0f, 1.0f};
        bool flipTextureX = false;
        bool flipTextureY = false;
    };

    SurfaceClipboard surfaceClipboard;


    // Moves a sector's light by `delta` on its brightest channel, scaling the
    // other two with it so the tint stays the same. From black it comes back
    // as grey. Children follow, same as an inspector edit.
    void AdjustSectorBrightness(Level& level, Sector& sector, const float delta) {
        const Vector3 lightBefore = sector.light;
        const float brightest = std::max({sector.light.x, sector.light.y, sector.light.z});
        const float target = std::clamp(brightest + delta, 0.0f, 255.0f);

        if (brightest <= 0.0f) sector.light = {target, target, target};
        else {
            const float factor = target / brightest;
            sector.light = {sector.light.x * factor, sector.light.y * factor, sector.light.z * factor};
        }

        if (!sector.children.empty()) level.PropagateSectorChanges(sector.id, sector.floors, lightBefore);
    }

    // Ctrl+R / Ctrl+F copy and paste a whole wall's or sector's properties.
    // Walls: colour, both textures with their placement, tags. Sectors:
    // light, tags, and every floor's floor and ceiling (heights included),
    // floor N onto floor N - floors past the shorter count are left alone
    // and the floor count itself never changes. Never copied: name, scripts,
    // geometry and sector links.
    struct PropertyClipboard {
        enum class Kind { None, Wall, Sector } kind = Kind::None;

        // Wall
        Vector4 color = {};
        WallSurface top;
        WallSurface bottom;
        WallSurface middle;
        bool middleHasCollision = true;
        bool middleIgnoredByRaycast = false;

        // Sector
        Vector3 light = {};
        std::vector<SectorFloor> floors;

        std::vector<std::string> tags;
        std::vector<uint16_t> tagIds;
    };

    PropertyClipboard propertyClipboard;

    // The wall or sector under the cursor (a floor or ceiling's sector).
    // False if the cursor is on neither.
    bool CopyHoveredProperties(Level& level) {
        if (hoveredSurface.type == RayHitType::Wall) {
            if (hoveredSurface.wallIndex < 0 || hoveredSurface.wallIndex >= static_cast<int>(level.walls.size())) return false;

            const Wall& wall = level.walls[hoveredSurface.wallIndex];

            propertyClipboard = {};
            propertyClipboard.kind = PropertyClipboard::Kind::Wall;
            propertyClipboard.color = wall.color;
            propertyClipboard.top = wall.top;
            propertyClipboard.bottom = wall.bottom;
            propertyClipboard.middle = wall.middle;
            propertyClipboard.middleHasCollision = wall.middleHasCollision;
            propertyClipboard.middleIgnoredByRaycast = wall.middleIgnoredByRaycast;
            propertyClipboard.tags = wall.tags;
            propertyClipboard.tagIds = wall.tagIds;
            return true;
        }

        if (hoveredSurface.type == RayHitType::SectorFloor || hoveredSurface.type == RayHitType::SectorCeiling) {
            if (hoveredSurface.sectorIndex < 0 || hoveredSurface.sectorIndex >= static_cast<int>(level.sectors.size())) return false;

            const Sector& sector = level.sectors[hoveredSurface.sectorIndex];

            propertyClipboard = {};
            propertyClipboard.kind = PropertyClipboard::Kind::Sector;
            propertyClipboard.light = sector.light;
            propertyClipboard.floors = sector.floors;
            propertyClipboard.tags = sector.tags;
            propertyClipboard.tagIds = sector.tagIds;
            return true;
        }

        return false;
    }

    void PasteWallProperties(Wall& wall) {
        wall.color = propertyClipboard.color;
        wall.top = propertyClipboard.top;
        wall.bottom = propertyClipboard.bottom;
        wall.middle = propertyClipboard.middle;
        wall.middleHasCollision = propertyClipboard.middleHasCollision;
        wall.middleIgnoredByRaycast = propertyClipboard.middleIgnoredByRaycast;
        wall.tags = propertyClipboard.tags;
        wall.tagIds = propertyClipboard.tagIds;
    }

    void PasteSectorProperties(Level& level, Sector& sector) {
        const std::vector<SectorFloor> floorsBefore = sector.floors;
        const Vector3 lightBefore = sector.light;

        sector.light = propertyClipboard.light;
        sector.tags = propertyClipboard.tags;
        sector.tagIds = propertyClipboard.tagIds;

        // The surfaces only: a floor that's mid-move keeps moving.
        const std::size_t floorCount = std::min(sector.floors.size(), propertyClipboard.floors.size());

        for (std::size_t i = 0; i < floorCount; ++i) {
            sector.floors[i].floor = propertyClipboard.floors[i].floor;
            sector.floors[i].ceiling = propertyClipboard.floors[i].ceiling;
        }

        if (!sector.children.empty()) level.PropagateSectorChanges(sector.id, floorsBefore, lightBefore);
    }

    // Onto the selection when it's the copied kind (a selected floor or
    // ceiling stands for its sector), otherwise onto the hovered one. A
    // selection of the other kind takes nothing, the hovered one included.
    // Returns how many walls or sectors it pasted onto.
    int PasteProperties(Level& level, const bool hoverAllowed) {
        int count = 0;

        if (propertyClipboard.kind == PropertyClipboard::Kind::Wall) {
            if (!selectedFlats.empty()) return 0;

            if (!selectedWalls.empty()) {
                for (const int wallIndex : selectedWalls) {
                    if (wallIndex < 0 || wallIndex >= static_cast<int>(level.walls.size())) continue;

                    PasteWallProperties(level.walls[wallIndex]);
                    ++count;
                }
                return count;
            }

            if (hoverAllowed && hoveredSurface.type == RayHitType::Wall &&
                hoveredSurface.wallIndex >= 0 && hoveredSurface.wallIndex < static_cast<int>(level.walls.size())) {
                PasteWallProperties(level.walls[hoveredSurface.wallIndex]);
                return 1;
            }
            return 0;
        }

        if (propertyClipboard.kind == PropertyClipboard::Kind::Sector) {
            if (!selectedWalls.empty()) return 0;

            std::vector<int> sectorIndices;

            if (!selectedFlats.empty()) {
                for (const SelectedFlat& flat : selectedFlats)
                    if (std::ranges::find(sectorIndices, flat.sectorIndex) == sectorIndices.end()) sectorIndices.push_back(flat.sectorIndex);
            }
            else if (hoverAllowed && (hoveredSurface.type == RayHitType::SectorFloor || hoveredSurface.type == RayHitType::SectorCeiling))
                sectorIndices.push_back(hoveredSurface.sectorIndex);

            for (const int sectorIndex : sectorIndices) {
                if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) continue;

                PasteSectorProperties(level, level.sectors[sectorIndex]);
                ++count;
            }
        }

        return count;
    }

    // ---- Undo / redo (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z) ---------------------
    // Covers the edits made straight in the view - texture drags, scrolling,
    // flips, pastes, resets, texture cycling, alignment, dropped textures and
    // wall deletes - as whole copies of the level's walls and sectors, and
    // placed entities as a copy of every entity. Not the inspector's field
    // edits.
    struct GeometryState {
        std::vector<Wall> walls;
        std::vector<Sector> sectors;

        // Names the step in the History window.
        const char* labelKey = ""; // en.json
        int count = 0;             // shown after the label when above 1

        // Set on entity steps, which hold every entity instead of the walls
        // and sectors.
        std::optional<MapEditorInternal::EntitiesSnapshot> entities;

        // Set on steps that change wall snaps: every snap, swapped with
        // WallSnap::All() along with the rest.
        std::optional<WallSnap::Snaps> wallSnaps;
    };

    constexpr std::size_t MAX_UNDO_STEPS = 64;

    std::vector<GeometryState> undoStack;
    std::vector<GeometryState> redoStack;

    // Every entity as the gizmo drag in progress found them, committed as an
    // undo step once the drag has changed something.
    GeometryState gizmoUndo;

    // Scroll notches of the same kind closer together than this are one
    // undo step.
    constexpr Uint64 SCROLL_UNDO_MERGE_MS = 500;
    Uint64 lastScrollEditTicks = 0;
    std::string_view lastScrollLabel;

    // A texture drag's state from the press. It only becomes an undo step
    // once the drag moves something, so a plain click leaves none.
    GeometryState uvDragUndo;
    bool uvDragPushedUndo = false;

    // The walls and sectors as they are now, to record once an edit has
    // changed something.
    GeometryState CaptureGeometry(const Level& level, const char* labelKey) {
        return {level.walls, level.sectors, labelKey, 0};
    }

    void CommitUndo(GeometryState before) {
        undoStack.push_back(std::move(before));
        if (undoStack.size() > MAX_UNDO_STEPS) undoStack.erase(undoStack.begin());

        redoStack.clear();
    }

    // Records `before` when the edit changed `count` (more than 0) things.
    void CommitUndoIf(GeometryState before, const int count) {
        if (count <= 0) return;

        before.count = count;
        CommitUndo(std::move(before));
    }

    // Call before changing walls or sectors.
    void PushUndo(const Level& level, const char* labelKey, const int count = 0) {
        GeometryState state = CaptureGeometry(level, labelKey);
        state.count = count;
        CommitUndo(std::move(state));
    }

    // Call before adding or removing entities.
    GeometryState CaptureEntities(const Level& level, const char* labelKey) {
        GeometryState state;
        state.labelKey = labelKey;
        state.entities.emplace();
        state.entities->entities = level.entities;
        state.entities->nextEntityID = level.nextEntityID;

#define CAPTURE_STORAGE(Type, Bit, Storage, LabelKey) state.entities->Storage = level.Storage;
        TILKY_COMPONENTS(CAPTURE_STORAGE)
#undef CAPTURE_STORAGE

        return state;
    }

    void SwapEntities(Level& level, MapEditorInternal::EntitiesSnapshot& snapshot) {
        std::swap(level.entities, snapshot.entities);
        std::swap(level.nextEntityID, snapshot.nextEntityID);

#define SWAP_STORAGE(Type, Bit, Storage, LabelKey) std::swap(level.Storage, snapshot.Storage);
        TILKY_COMPONENTS(SWAP_STORAGE)
#undef SWAP_STORAGE

        std::erase_if(selectedEntityIds, [&](const ID id) { return level.GetEntity(id) == nullptr; });

        if (selectedEntityIds.empty()) ClearEntitySelection();
        else if (!selectedEntityId.has_value() || level.GetEntity(*selectedEntityId) == nullptr) SelectOnlyEntity(selectedEntityIds.back());

        runtimeRenderer->RefreshTexturesIfLevelNeedsNew();
        MapEditorInternal::MarkUnsavedChanges();
    }

    // Swaps `state` in, leaving what was there in `state`. Selected indices
    // that no longer exist are dropped.
    void SwapGeometry(Level& level, GeometryState& state) {
        if (state.wallSnaps.has_value()) std::swap(*state.wallSnaps, WallSnap::All());

        if (state.entities.has_value()) {
            SwapEntities(level, *state.entities);
            return;
        }

        std::swap(level.walls, state.walls);
        std::swap(level.sectors, state.sectors);

        // Sector::walls points into level.walls, which is now a different
        // array - every raycast and wall query walks those pointers.
        MapQueries::RebuildSectorRuntimeLinks(level);

        const int wallCount = static_cast<int>(level.walls.size());
        std::erase_if(selectedWalls, [&](const int index) { return index < 0 || index >= wallCount; });

        if (selectedWalls.empty()) ClearWallSelection();
        else if (selectedWall < 0 || selectedWall >= wallCount) selectedWall = selectedWalls.back();

        const int sectorCount = static_cast<int>(level.sectors.size());
        std::erase_if(selectedFlats, [&](const SelectedFlat& flat) {
            return flat.sectorIndex < 0 || flat.sectorIndex >= sectorCount ||
                   flat.floorIndex < 0 || flat.floorIndex >= static_cast<int>(level.sectors[flat.sectorIndex].floors.size());
        });

        if (selectedFlats.empty()) ClearFlatSelection();
        else if (std::ranges::find(selectedFlats, SelectedFlat{selectedSector, selectedSectorFloor, selectedSectorSurface}) == selectedFlats.end())
            SetPrimaryFlat(selectedFlats.back());
    }

    bool Undo(Level& level) {
        if (undoStack.empty()) return false;

        GeometryState state = std::move(undoStack.back());
        undoStack.pop_back();

        SwapGeometry(level, state);
        redoStack.push_back(std::move(state));
        return true;
    }

    bool Redo(Level& level) {
        if (redoStack.empty()) return false;

        GeometryState state = std::move(redoStack.back());
        redoStack.pop_back();

        SwapGeometry(level, state);
        undoStack.push_back(std::move(state));
        return true;
    }

    // ---- Texture shortcuts ------------------------------------------------

    // Alt+R: offset 0, scale 1, no flip.
    int ResetTexturePlacement(Level& level, const bool hoverAllowed) {
        return ForEachTargetSurface(level, hoverAllowed, [](auto& surface) {
            surface.textureOffset = {0.0f, 0.0f};
            surface.textureScale = {1.0f, 1.0f};
            surface.flipTextureX = false;
            surface.flipTextureY = false;
        });
    }

    // The image `direction` steps from `texture` among the images in its
    // folder, in name order, wrapping round. Empty when there is nothing to
    // step to (no texture, a path outside Assets, a folder with one image).
    std::string StepTextureInFolder(const std::string& texture, const int direction) {
        if (texture.empty()) return {};

        const std::filesystem::path relative = std::filesystem::path(texture).lexically_normal();
        if (relative.is_absolute()) return {};

        const std::filesystem::path folder = ProjectManager::GetAssetsPath() / relative.parent_path();

        std::vector<std::string> names;
        std::error_code ec;

        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (!entry.is_regular_file(ec)) continue;

            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") names.push_back(entry.path().filename().string());
        }

        if (names.size() < 2) return {};

        std::ranges::sort(names);

        const std::string current = relative.filename().string();
        const auto it = std::ranges::find(names, current);
        if (it == names.end()) return {};

        const int count = static_cast<int>(names.size());
        const int index = ((static_cast<int>(it - names.begin()) + direction) % count + count) % count;

        return (relative.parent_path() / names[index]).generic_string();
    }

    // [ / ]: each target surface steps to the previous / next image in its
    // own texture's folder.
    int CycleTextures(Level& level, const int direction, const bool hoverAllowed) {
        return ForEachTargetSurface(level, hoverAllowed, [&](auto& surface) {
            if (std::string next = StepTextureInFolder(surface.texture, direction); !next.empty()) surface.texture = std::move(next);
        });
    }

    // Runs one of the Map Editor's wall texture tools - they work on its own
    // selection - on this editor's selected walls. The tool's undo step
    // lands on the Map Editor's history, so it's taken back off and that
    // history's redo steps put back: this editor's own undo covers the
    // change. Returns how many walls the tool changed.
    int RunMapEditorWallTool(Level& level, int (*tool)()) {
        if (selectedWalls.empty() || &level != &LevelManager::CurrentLevel()) return 0;

        const std::vector<ID> savedSelection = MapEditorInternal::selectedWalls;
        const ID savedPrimary = MapEditorInternal::selectedWallID;
        const bool savedEditing = MapEditorInternal::editingWall;
        const std::size_t savedSteps = MapEditorInternal::undoSteps.size();
        std::vector<MapEditorInternal::UndoStep> savedRedo = std::move(MapEditorInternal::redoSteps);

        MapEditorInternal::selectedWalls.clear();
        for (const int index : selectedWalls)
            if (index >= 0 && index < static_cast<int>(level.walls.size())) MapEditorInternal::selectedWalls.push_back(level.walls[index].id);

        MapEditorInternal::selectedWallID = selectedWall >= 0 && selectedWall < static_cast<int>(level.walls.size())
            ? level.walls[selectedWall].id
            : INVALID_ID;

        const int changed = MapEditorInternal::selectedWalls.empty() ? 0 : tool();

        while (MapEditorInternal::undoSteps.size() > savedSteps) MapEditorInternal::undoSteps.pop_back();
        MapEditorInternal::redoSteps = std::move(savedRedo);

        MapEditorInternal::selectedWalls = savedSelection;
        MapEditorInternal::selectedWallID = savedPrimary;
        MapEditorInternal::editingWall = savedEditing;

        return changed;
    }

    // Alt+right click: selects every wall (or every floor/ceiling) using the
    // texture of the one clicked, which stays the primary. False if it isn't
    // a textured wall, floor or ceiling.
    bool SelectAllWithHoveredTexture(Level& level) {
        std::string texture;
        if (!ForHoveredSurface(level, [&](const auto& surface) { texture = surface.texture; }) || texture.empty()) return false;

        if (hoveredSurface.type == RayHitType::Wall) {
            SelectOnlyWall(hoveredSurface.wallIndex);

            for (int i = 0; i < static_cast<int>(level.walls.size()); ++i)
                if (i != hoveredSurface.wallIndex &&
                    (level.walls[i].top.texture == texture || level.walls[i].bottom.texture == texture || level.walls[i].middle.texture == texture))
                    selectedWalls.push_back(i);

            return true;
        }

        SelectOnlyFlat({hoveredSurface.sectorIndex, hoveredSurface.floorIndex, hoveredSurface.type});

        for (int sectorIndex = 0; sectorIndex < static_cast<int>(level.sectors.size()); ++sectorIndex) {
            const Sector& sector = level.sectors[sectorIndex];

            for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
                for (const RayHitType type : {RayHitType::SectorFloor, RayHitType::SectorCeiling}) {
                    const SectorSurface& surface = type == RayHitType::SectorFloor ? sector.floors[floorIndex].floor : sector.floors[floorIndex].ceiling;
                    const SelectedFlat flat = {sectorIndex, floorIndex, type};

                    if (surface.texture == texture && std::ranges::find(selectedFlats, flat) == selectedFlats.end())
                        selectedFlats.push_back(flat);
                }
            }
        }

        return true;
    }

    std::size_t SelectionCount() {
        return selectedEntityIds.size() + selectedWalls.size() + selectedFlats.size();
    }

    // F: moves the camera back along its view direction until the selection
    // is in front of it, at a distance that fits its size. The angle stays.
    void FocusSelection(Level& level) {
        bool any = false;
        Vector3 boundsMin{};
        Vector3 boundsMax{};

        const auto include = [&](const Vector3& point) {
            if (!any) {
                boundsMin = boundsMax = point;
                any = true;
                return;
            }

            boundsMin = {std::min(boundsMin.x, point.x), std::min(boundsMin.y, point.y), std::min(boundsMin.z, point.z)};
            boundsMax = {std::max(boundsMax.x, point.x), std::max(boundsMax.y, point.y), std::max(boundsMax.z, point.z)};
        };

        for (const ID id : selectedEntityIds)
            if (const ComponentTransform* entityTransform = level.transforms.Get(id)) include(entityTransform->position);

        for (const int index : selectedWalls) {
            if (index < 0 || index >= static_cast<int>(level.walls.size())) continue;

            const Wall& wall = level.walls[index];
            const Sector* sector = level.GetSector(wall.frontSector != INVALID_ID ? wall.frontSector : wall.backSector);
            const float bottom = sector != nullptr && !sector->floors.empty() ? sector->floors.front().floor.height : 0.0f;
            const float top = sector != nullptr && !sector->floors.empty() ? sector->floors.back().ceiling.height : 0.0f;

            include({wall.start.x, bottom, wall.start.y});
            include({wall.end.x, top, wall.end.y});
        }

        for (const SelectedFlat& flat : selectedFlats) {
            if (flat.sectorIndex < 0 || flat.sectorIndex >= static_cast<int>(level.sectors.size())) continue;

            const Sector& sector = level.sectors[flat.sectorIndex];
            if (flat.floorIndex < 0 || flat.floorIndex >= static_cast<int>(sector.floors.size())) continue;

            const SectorFloor& sectorFloor = sector.floors[flat.floorIndex];
            const float height = flat.surface == RayHitType::SectorFloor ? sectorFloor.floor.height : sectorFloor.ceiling.height;

            for (const Vector2& vertex : sector.vertices) include({vertex.x, height, vertex.y});
        }

        if (!any || transform == nullptr || camera == nullptr) return;

        const Vector3 center = {
            (boundsMin.x + boundsMax.x) * 0.5f,
            (boundsMin.y + boundsMax.y) * 0.5f,
            (boundsMin.z + boundsMax.z) * 0.5f
        };

        const float dx = boundsMax.x - boundsMin.x;
        const float dy = boundsMax.y - boundsMin.y;
        const float dz = boundsMax.z - boundsMin.z;

        constexpr float MIN_FOCUS_DISTANCE = 4.0f;
        const float distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), MIN_FOCUS_DISTANCE);

        transform->SetPosition(center - camera->forward * distance);
    }

    // An entity made by `create` where the mouse ray hits, pulled back along
    // the ray a little so it lands on the camera's side of a wall. Selected,
    // with an undo step. Nothing when the ray hits nothing.
    template<typename Create>
    void PlaceAtHover(Level& level, Create&& create) {
        if (hoveredSurface.type == RayHitType::None) return;

        constexpr float PULL_BACK = 0.01f;

        GeometryState before = CaptureEntities(level, "editor.history.place_entity");

        const ID id = create(hoveredHitPosition - hoveredRayDirection * PULL_BACK);

        if (id == INVALID_ID) {
            ImGuiDrawFunctions::ShowNotification(Localisation::Get("editor.entity_mode.palette.place_failed").c_str(), true, 2.0f);
            return;
        }

        if (ComponentTransform* placed = level.transforms.Get(id)) placed->UpdateObjectSectorAndFloor(level.sectors);

        CommitUndo(std::move(before));
        SelectOnlyEntity(id);

        runtimeRenderer->RefreshTexturesIfLevelNeedsNew();
        MapEditorInternal::MarkUnsavedChanges();
    }

    // The Entity Mode palette's selection, placed as above.
    void PlacePaletteEntityAtHover(Level& level) {
        PlaceAtHover(level, [&level](const Vector3& position) {
            return MapEditorInternal::CreatePaletteEntity(level, position);
        });
    }

    // A .prefab dropped on the 3D view: just that prefab with its own values,
    // whatever the palette's placement settings are.
    void PlacePrefabAtHover(Level& level, const std::string& reference) {
        PlaceAtHover(level, [&level, &reference](const Vector3& position) {
            // UI prefabs belong in the UI editor.
            const Level* prefab = Prefab::Get(reference);
            if (prefab == nullptr || prefab->ui_transforms.Has(Prefab::RootOf(*prefab).id)) return INVALID_ID;

            const ID id = Prefab::Instantiate(level, reference);

            if (ComponentTransform* placed = level.transforms.Get(id)) {
                placed->SetPosition(position);
                placed->isDirty = true;
            }

            return id;
        });
    }

    // How many surfaces a texture dropped now would change: the whole
    // selection when the surface under the cursor is part of it, else 1 (or 0
    // with nothing under the cursor).
    int DropTargetCount() {
        if (hoveredSurface.type == RayHitType::Wall && std::ranges::find(selectedWalls, hoveredSurface.wallIndex) != selectedWalls.end())
            return static_cast<int>(selectedWalls.size());

        if (hoveredSurface.type == RayHitType::SectorFloor || hoveredSurface.type == RayHitType::SectorCeiling) {
            const SelectedFlat hovered{hoveredSurface.sectorIndex, hoveredSurface.floorIndex, hoveredSurface.type};
            if (std::ranges::find(selectedFlats, hovered) != selectedFlats.end()) return static_cast<int>(selectedFlats.size());
        }

        return hoveredSurface.type == RayHitType::None ? 0 : 1;
    }

    // A drop onto a selected surface textures the whole selection: the
    // dropped-on slot (top or bottom) of every selected wall, or every
    // selected flat. Anywhere else only the surface under the cursor changes.
    int ApplyTextureToDropTarget(Level& level, const std::string& textureReference) {
        if (DropTargetCount() <= 1) return ApplyTextureToHoveredSurface(level, textureReference) ? 1 : 0;

        int count = 0;

        if (hoveredSurface.type == RayHitType::Wall) {
            for (const int wallIndex : selectedWalls) {
                if (wallIndex < 0 || wallIndex >= static_cast<int>(level.walls.size())) continue;

                level.walls[wallIndex].Surface(hoveredSurface.wallSlot).texture = textureReference;
                ++count;
            }
            return count;
        }

        for (const SelectedFlat& flat : selectedFlats) {
            if (flat.sectorIndex < 0 || flat.sectorIndex >= static_cast<int>(level.sectors.size())) continue;

            Sector& sector = level.sectors[flat.sectorIndex];
            if (flat.floorIndex < 0 || flat.floorIndex >= static_cast<int>(sector.floors.size())) continue;

            SectorFloor& sectorFloor = sector.floors[flat.floorIndex];
            (flat.surface == RayHitType::SectorFloor ? sectorFloor.floor : sectorFloor.ceiling).texture = textureReference;
            ++count;
        }
        return count;
    }

    // A drop that doesn't land leaves no undo step behind.
    int ApplyTextureToDropTargetWithUndo(Level& level, const std::string& textureReference) {
        PushUndo(level, "runtime_editor.history.apply_texture", DropTargetCount());
        if (const int count = ApplyTextureToDropTarget(level, textureReference); count > 0) return count;

        undoStack.pop_back();
        return 0;
    }

    void ShowToastf(const char* key, const int value) {
        char message[128];
        std::snprintf(message, sizeof(message), Localisation::Get(key).c_str(), value);
        ImGuiDrawFunctions::ShowNotification(message, false, 1.5f);
    }

    // Copies the hovered surface. False if the cursor isn't on one.
    bool CopyHoveredSurface(Level& level, const bool withPlacement) {
        return ForHoveredSurface(level, [&](const auto& surface) {
            surfaceClipboard = {};
            surfaceClipboard.hasTexture = true;
            surfaceClipboard.hasPlacement = withPlacement;
            surfaceClipboard.texture = surface.texture;

            if (withPlacement) {
                surfaceClipboard.textureOffset = surface.textureOffset;
                surfaceClipboard.textureScale = surface.textureScale;
                surfaceClipboard.flipTextureX = surface.flipTextureX;
                surfaceClipboard.flipTextureY = surface.flipTextureY;
            }
        });
    }

    // Placement only goes with Alt, and only if it was copied with Alt.
    int PasteSurface(Level& level, const bool withPlacement, const bool hoverAllowed) {
        if (!surfaceClipboard.hasTexture) return 0;

        const bool pastePlacement = withPlacement && surfaceClipboard.hasPlacement;

        return ForEachTargetSurface(level, hoverAllowed, [&](auto& surface) {
            surface.texture = surfaceClipboard.texture;

            if (pastePlacement) {
                surface.textureOffset = surfaceClipboard.textureOffset;
                surface.textureScale = surfaceClipboard.textureScale;
                surface.flipTextureX = surfaceClipboard.flipTextureX;
                surface.flipTextureY = surfaceClipboard.flipTextureY;
            }
        });
    }

    bool IsUvEditableSurface(const RayHitType type) {
        return type == RayHitType::Wall ||
               type == RayHitType::SectorFloor ||
               type == RayHitType::SectorCeiling;
    }

    // Multiplier for the horizontal drag delta so the wall texture follows the
    // cursor on whichever face the camera is looking at.
    //
    // Conventions (Rendering.vs.glsl renderWall, OpenGLMapBuild.cpp):
    //  - wall.start/end are map (x, y), which the renderer places at world
    //    (x, z).
    //  - U grows from start toward end on BOTH faces (no culling, no
    //    gl_FrontFacing handling), and the offset is added after scale/flip:
    //        u = (+/-)s * scale.x / tileSize + offset.x / tileSize
    //    so with no flip a larger offset.x slides the texture toward start,
    //    i.e. against the wall direction. The dragged surface's
    //    flipTextureX (or a negative scale.x) reverses that.
    //  - The view's right vector is cross(forward, up) =
    //    (-cos yaw, 0, sin yaw), the same basis GetMouseRayDirection uses.
    //
    // The face is picked by where the camera stands relative to the wall line;
    // the ray's hit point stands in for "a point on the wall".
    float ComputeWallHorizontalDragSign(
        const Wall& wall,
        const WallSurface& surface,
        const Vector3 hitPosition,
        const Vector3 cameraPosition,
        const ComponentCamera& camera,
        const float fallback) {
        // Below this the wall is edge-on (or the camera sits on its plane):
        // which face is showing is not stable, so do not guess.
        constexpr float AMBIGUITY_EPSILON = 1e-4f;

        const float wallX = wall.end.x - wall.start.x;
        const float wallY = wall.end.y - wall.start.y;
        const float wallLength = std::sqrt(wallX * wallX + wallY * wallY);

        const float viewX = hitPosition.x - cameraPosition.x;
        const float viewY = hitPosition.z - cameraPosition.z;
        const float viewLength = std::sqrt(viewX * viewX + viewY * viewY);

        if (wallLength <= AMBIGUITY_EPSILON || viewLength <= AMBIGUITY_EPSILON) return fallback;

        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;
        const float rightX = -std::cos(yawRadians);
        const float rightY = std::sin(yawRadians);
        const float forwardX = std::sin(yawRadians);
        const float forwardY = std::cos(yawRadians);

        const float dirX = wallX / wallLength;
        const float dirY = wallY / wallLength;
        const float toWallX = viewX / viewLength;
        const float toWallY = viewY / viewLength;

        // Screen x of a point is right/forward, so the derivative along the
        // wall has the sign of this cross product (forward > 0 for anything
        // the mouse ray can hit). Positive: start -> end runs left to right.
        const float screenSlide =
            (dirX * rightX + dirY * rightY) * (toWallX * forwardX + toWallY * forwardY) -
            (toWallX * rightX + toWallY * rightY) * (dirX * forwardX + dirY * forwardY);

        if (std::abs(screenSlide) < AMBIGUITY_EPSILON) return fallback;

        // Which way along start -> end the texture slides when offset.x grows.
        float textureSlide = -1.0f;
        if (surface.flipTextureX) textureSlide = -textureSlide;
        if (surface.textureScale.x < 0.0f) textureSlide = -textureSlide;

        return (screenSlide > 0.0f ? 1.0f : -1.0f) * textureSlide;
    }

    // Which texture axis each mouse axis drives on a floor or ceiling, from
    // where a step along world X and along world Z at the clicked point lands
    // on screen. Projecting covers every case at once: facing any way, floors
    // (whose far side is at the top of the screen) and ceilings (whose near
    // side is), and slopes. `fallback` when the camera is edge-on to the
    // surface and the axes can't be told apart.
    FlatUvDrag ComputeFlatUvDrag(
        const SectorSurface& surface,
        const WallPieces::SectorBounds& bounds,
        const Vector3 hitPosition,
        const TransformGizmo::View& view,
        const FlatUvDrag& fallback) {
        constexpr float STEP = 0.5f; // world units
        constexpr float MIN_PIXELS = 1e-3f;

        const auto onSurface = [&](const float x, const float z) {
            return Vector3{x, WallPieces::GetSurfaceHeight(surface, bounds, {x, z}), z};
        };

        Vector2 origin, alongX, alongZ;
        if (!TransformGizmo::ProjectPoint(view, onSurface(hitPosition.x, hitPosition.z), origin) ||
            !TransformGizmo::ProjectPoint(view, onSurface(hitPosition.x + STEP, hitPosition.z), alongX) ||
            !TransformGizmo::ProjectPoint(view, onSurface(hitPosition.x, hitPosition.z + STEP), alongZ))
            return fallback;

        // Screen movement (pixels, y down) of one step along each world axis.
        const Vector2 screenAxes[2] = {
            {alongX.x - origin.x, alongX.y - origin.y},
            {alongZ.x - origin.x, alongZ.y - origin.y}
        };

        float lengths[2];
        for (int axis = 0; axis < 2; ++axis) {
            lengths[axis] = std::sqrt(screenAxes[axis].x * screenAxes[axis].x + screenAxes[axis].y * screenAxes[axis].y);
            if (lengths[axis] < MIN_PIXELS) return fallback;
        }

        // The pairing whose axes line up best with the screen's.
        const float xHorizontal = std::abs(screenAxes[0].x) / lengths[0] + std::abs(screenAxes[1].y) / lengths[1];
        const float zHorizontal = std::abs(screenAxes[1].x) / lengths[1] + std::abs(screenAxes[0].y) / lengths[0];

        FlatUvDrag drag;
        drag.horizontalAxis = xHorizontal >= zHorizontal ? 0 : 1;
        const int verticalAxis = 1 - drag.horizontalAxis;

        // uv = position * scale (negated when flipped) + offset, so a larger
        // offset slides the texture toward negative world positions, and a
        // flip or a negative scale turns that around.
        const auto worldSlide = [&surface](const int axis) {
            const bool flipped = axis == 0 ? surface.flipTextureX : surface.flipTextureY;
            const float scale = axis == 0 ? surface.textureScale.x : surface.textureScale.y;

            float slide = -1.0f;
            if (flipped) slide = -slide;
            if (scale < 0.0f) slide = -slide;
            return slide;
        };

        const float horizontalScreen = screenAxes[drag.horizontalAxis].x * worldSlide(drag.horizontalAxis);
        const float verticalScreen = screenAxes[verticalAxis].y * worldSlide(verticalAxis);

        drag.horizontalSign = horizontalScreen >= 0.0f ? 1.0f : -1.0f;
        drag.verticalSign = verticalScreen >= 0.0f ? 1.0f : -1.0f;

        return drag;
    }

    void DrawDropHint(const std::string& text) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        const ImVec2 mousePosition = ImGui::GetMousePos();
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 origin = {mousePosition.x + 24.0f, mousePosition.y + 24.0f};

        drawList->AddRectFilled(
            {origin.x - 6.0f, origin.y - 4.0f},
            {origin.x + textSize.x + 6.0f, origin.y + textSize.y + 4.0f},
            IM_COL32(20, 20, 20, 200),
            4.0f
        );

        drawList->AddText(origin, IM_COL32(255, 255, 255, 255), text.c_str());
    }
}

namespace RuntimeEditorUi {
    constexpr bool DRAGGABLE = true;    // =========================================================================
    //  Toolbar: square icon buttons centred along the top of the window, the
    //  same look as the Map Editor's (MapEditorUI.cpp, DrawToolbar)
    // =========================================================================

    void HoverTooltip(const std::string& text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text.c_str());
    }

    // Shows `icon` (a Font Awesome glyph) when this context's icon font
    // loaded, otherwise `fallback`. `active` draws it pressed, for the tool
    // that's selected.
    bool ToolbarButton(const char* id, const char* icon, const char* fallback, const std::string& tooltip, const bool active) {
        ImFont* iconFont = runtimeRenderer != nullptr ? runtimeRenderer->GetImGuiIconFont() : nullptr;

        ImGui::PushID(id);

        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (iconFont != nullptr) ImGui::PushFont(iconFont, 0.0f);

        const bool pressed = ImGui::Button(iconFont != nullptr ? icon : fallback, ImVec2(TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE));

        if (iconFont != nullptr) ImGui::PopFont();
        if (active) ImGui::PopStyleColor();

        HoverTooltip(tooltip);

        ImGui::PopID();
        return pressed;
    }

    // Toolbar menus that stay open until their button is pressed again.
    bool stepsMenuOpen = false;
    bool statisticsMenuOpen = false;
    bool paletteMenuOpen = false;

    // A toolbar menu's window, under the button just drawn. A window rather
    // than a popup: ImGui closes popups on any click outside them, and when
    // another window takes focus. Always pair with ImGui::End().
    bool BeginToolbarMenu(const char* id) {
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();

        ImGui::SetNextWindowPos(ImVec2(buttonMin.x, buttonMax.y + ImGui::GetStyle().ItemSpacing.y));

        constexpr ImGuiWindowFlags menuFlags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_AlwaysAutoResize;

        return ImGui::Begin(id, nullptr, menuFlags);
    }

    void DrawToolbarSeparator() {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Slider the height of the toolbar buttons' row, centred on them.
    void BeginToolbarSlider() {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (TOOLBAR_BUTTON_SIZE - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::SetNextItemWidth(TOOLBAR_SLIDER_WIDTH);
    }

    void DrawToolbar() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float MARGIN = 6.0f;

        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + MARGIN),
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

        ImGui::Begin("##RuntimeEditorToolbar", nullptr, overlayFlags);

        // ---- Gizmo tools
        if (ToolbarButton("move", ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "M",
                          Localisation::Get("runtime_editor.tool.move") + "\n" + MapEditorInternal::FormatKeyBinds(Localisation::Get("runtime_editor.tooltip.tool.move")),
                          gizmoTool == TransformGizmo::Tool::Move))
            gizmoTool = TransformGizmo::Tool::Move;

        ImGui::SameLine();

        if (ToolbarButton("rotate", ICON_FA_ROTATE, "R",
                          Localisation::Get("runtime_editor.tool.rotate") + "\n" + MapEditorInternal::FormatKeyBinds(Localisation::Get("runtime_editor.tooltip.tool.rotate")),
                          gizmoTool == TransformGizmo::Tool::Rotate))
            gizmoTool = TransformGizmo::Tool::Rotate;

        DrawToolbarSeparator();

        // ---- Camera
        const std::string speedFormat = Localisation::Get("runtime_editor.camera_speed") + ": %.0f";

        BeginToolbarSlider();
        ImGui::SliderFloat("##CameraSpeed", &moveSpeed, MIN_SLIDER_MOVE_SPEED, MAX_SLIDER_MOVE_SPEED,
                           speedFormat.c_str());
        HoverTooltip(MapEditorInternal::FormatKeyBinds(Localisation::Get("runtime_editor.tooltip.camera_speed")));

        ImGui::SameLine();

        // Edits the user setting itself, so the value is the same one the
        // map editor's User Settings window shows and saves.
        const std::string fovFormat = Localisation::Get("editor.runtime_editor.fov") + ": %.0f";

        BeginToolbarSlider();
        ImGui::SliderFloat("##CameraFov", &MapEditorInternal::runtimeCameraFov,
                           MapEditorInternal::MIN_RUNTIME_CAMERA_FOV, MapEditorInternal::MAX_RUNTIME_CAMERA_FOV,
                           fovFormat.c_str());

        DrawToolbarSeparator();

        // ---- Entity markers
        {
            using MapEditorInternal::EntityMarkerMode;

            const EntityMarkerMode mode = MapEditorInternal::entityMarkerMode;
            const char* icon = mode == EntityMarkerMode::Always ? ICON_FA_CIRCLE_DOT
                             : mode == EntityMarkerMode::VisibleOnly ? ICON_FA_EYE
                             : ICON_FA_EYE_SLASH;
            const char* fallback = mode == EntityMarkerMode::Always ? "E+"
                                 : mode == EntityMarkerMode::VisibleOnly ? "E"
                                 : "E-";

            if (ToolbarButton("entity_markers", icon, fallback,
                              Localisation::Get("runtime_editor.entity_markers") + ": " + Localisation::Get(EntityMarkerModeKey(mode)) + "\n"
                              + MapEditorInternal::FormatKeyBinds(Localisation::Get("runtime_editor.tooltip.entity_markers")),
                              mode != EntityMarkerMode::Off))
                CycleEntityMarkerMode(false);
        }

        ImGui::SameLine();

        // ---- Palette (what the place-entity key makes)
        if (ToolbarButton("palette", ICON_FA_CUBES, "Pl",
                          Localisation::Get("editor.entity_mode.palette") + "\n" + MapEditorInternal::FormatKeyBinds(Localisation::Get("runtime_editor.tooltip.palette")),
                          paletteMenuOpen))
            paletteMenuOpen = !paletteMenuOpen;

        if (paletteMenuOpen && BeginToolbarMenu("##RuntimeEditorPalette")) {
            // Fixes the menu's width: the palette's hint wraps to it.
            constexpr float PALETTE_MENU_WIDTH = 280.0f;
            ImGui::Dummy(ImVec2(PALETTE_MENU_WIDTH, 0.0f));

            ImGui::TextUnformatted(Localisation::Get("editor.entity_mode.palette").c_str());
            ImGui::Separator();

            MapEditorInternal::DrawEntityPalette("runtime_editor.palette.hint");
        }

        if (paletteMenuOpen) ImGui::End();

        ImGui::SameLine();

        // ---- Step sizes
        if (ToolbarButton("steps", ICON_FA_SLIDERS, "Sz",
                          Localisation::Get("runtime_editor.steps") + "\n" + MapEditorInternal::FormatKeyBinds(Localisation::Get("runtime_editor.tooltip.steps")),
                          stepsMenuOpen))
            stepsMenuOpen = !stepsMenuOpen;

        if (stepsMenuOpen && BeginToolbarMenu("##RuntimeEditorSteps")) {
            using namespace MapEditorInternal;

            ImGui::TextUnformatted(Localisation::Get("runtime_editor.steps").c_str());
            ImGui::Separator();

            const std::string defaultLabel = Localisation::Get("runtime_editor.steps.default");

            for (int step = 0; step < RT_STEP_COUNT; ++step) {
                const RuntimeStepInfo& info = RUNTIME_STEP_INFO[step];
                ImGui::PushID(step);

                ImGui::SetNextItemWidth(TOOLBAR_SLIDER_WIDTH);
                ImGui::SliderFloat(Localisation::Get(info.labelKey).c_str(), &runtimeSteps[step], info.min, info.max, info.format,
                                   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

                ImGui::SameLine();
                ImGui::BeginDisabled(runtimeSteps[step] == info.defaultValue);
                if (ImGui::SmallButton(defaultLabel.c_str())) runtimeSteps[step] = info.defaultValue;
                ImGui::EndDisabled();

                ImGui::PopID();
            }
        }

        if (stepsMenuOpen) ImGui::End();

        ImGui::SameLine();

        // ---- History
        if (ToolbarButton("history", ICON_FA_CLOCK_ROTATE_LEFT, "H",
                          Localisation::Get("editor.history") + "\n" + Localisation::Get("runtime_editor.tooltip.history"),
                          MapEditorInternal::runtimeHistoryVisible))
            MapEditorInternal::runtimeHistoryVisible = !MapEditorInternal::runtimeHistoryVisible;

        ImGui::SameLine();

        // ---- Statistics
        if (ToolbarButton("statistics", ICON_FA_CHART_SIMPLE, "St",
                          Localisation::Get("runtime_editor.statistics") + "\n" + Localisation::Get("runtime_editor.tooltip.statistics"),
                          statisticsMenuOpen))
            statisticsMenuOpen = !statisticsMenuOpen;

        if (statisticsMenuOpen && BeginToolbarMenu("##RuntimeEditorStatistics")) {
            // ImGui's own running average, so the number is readable instead
            // of changing every frame.
            const float framerate = ImGui::GetIO().Framerate;

            ImGui::TextUnformatted(Localisation::Get("runtime_editor.statistics").c_str());
            ImGui::Separator();
            ImGui::Text("%s: %.0f", Localisation::Get("runtime_editor.statistics.fps").c_str(), framerate);
            ImGui::Text("%s: %.2f ms", Localisation::Get("runtime_editor.statistics.frame_time").c_str(),
                        framerate > 0.0f ? 1000.0f / framerate : 0.0f);
        }

        if (statisticsMenuOpen) ImGui::End();

        ImGui::End();
    }

    // Same list as the map editor's History window, over this editor's own
    // undo steps.
    void DrawHistoryWindow(Level& level) {
        ImGui::SetNextWindowSize(ImVec2(260.0f, 320.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin((Localisation::Get("editor.history") + "##RuntimeEditor").c_str(), &MapEditorInternal::runtimeHistoryVisible)) {
            std::vector<std::string> undoLabels;
            std::vector<std::string> redoLabels;

            for (const GeometryState& state : undoStack) undoLabels.push_back(MapEditorInternal::HistoryLabel(state.labelKey, state.count));
            for (auto it = redoStack.rbegin(); it != redoStack.rend(); ++it) redoLabels.push_back(MapEditorInternal::HistoryLabel(it->labelKey, it->count));

            const int position = ImGuiDrawFunctions::DrawHistoryList(undoLabels, redoLabels, Localisation::Get("editor.history.initial_state"));

            if (position >= 0) {
                while (static_cast<int>(undoStack.size()) > position && Undo(level)) {}
                while (static_cast<int>(undoStack.size()) < position && Redo(level)) {}
            }
        }

        ImGui::End();
    }

    void Draw(Level& level) {
        // Lets panels dock to the window's edges and corners, like in the Map
        // Editor. The empty middle lets clicks through to the game.
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        // The browser is shared with the Map Editor, which hands it a font
        // from the editor's ImGui context every frame. This context has its
        // own copy; drawing with the other one shows the wrong glyphs.
        if (runtimeRenderer != nullptr) MapEditorInternal::assetBrowser.SetIconFont(runtimeRenderer->GetImGuiIconFont());

        UpdateEntityMarkers();

        if (const ComponentTransform* gizmoTarget = GizmoTarget(level)) {
            TransformGizmo::View view;

            if (BuildGizmoView(view)) {
                TransformGizmo::Draw(
                    view,
                    gizmoTool,
                    *gizmoTarget,
                    InputManager::GetMousePosition(),
                    !cameraLooking && !ImGui::GetIO().WantCaptureMouse
                );
            }
        }

        if (editingEntity) {
            // Drop anything deleted since it was selected (scripts can destroy
            // entities while the runtime editor is open).
            std::erase_if(selectedEntityIds, [&](const ID id) { return level.GetEntity(id) == nullptr; });

            if (!selectedEntityId.has_value() || level.GetEntity(*selectedEntityId) == nullptr) {
                ClearEntitySelection();
                return;
            }

            Entity* entityToEdit = level.GetEntity(*selectedEntityId);
            const ID primaryID = entityToEdit->id;

            // Snapshotted only when there is someone to mirror onto.
            const bool mirrors = selectedEntityIds.size() > 1;
            const nlohmann::json before = mirrors ? MultiEdit::EntityState(level, primaryID) : nlohmann::json{};

            {
                using WallSnapStatus = ImGuiDrawFunctions::EntityInspectorState::WallSnapStatus;

                const WallSnap::Snap* snap = WallSnap::Find(primaryID);
                entityInspectorState.wallSnapStatus = WallSnap::IsPicking() ? WallSnapStatus::Picking
                                                    : snap != nullptr ? WallSnapStatus::Snapped
                                                    : WallSnapStatus::Off;
                if (snap != nullptr) entityInspectorState.wallSnapGap = snap->gap;
            }

            const bool deleteRequested =
                ImGuiDrawFunctions::DrawEntityEditor(*entityToEdit,entityInspectorState, &editingEntity, DRAGGABLE);

            if (deleteRequested) {
                const std::vector<ID> idsToDelete = selectedEntityIds;

                ClearEntitySelection();

                for (const ID id : idsToDelete) level.DestroyEntity(id);
                return;
            }

            if (!editingEntity) {
                ClearEntitySelection();
                return;
            }

            if (entityInspectorState.editingComponent && entityInspectorState.selectedComponent != -1) {
                ImGuiDrawFunctions::DrawComponentEditor(
                    *entityToEdit,
                    entityInspectorState,
                    &entityInspectorState.editingComponent, DRAGGABLE
                );
            }

            if (mirrors) {
                const nlohmann::json after = MultiEdit::EntityState(level, primaryID);

                if (after != before)
                    for (const ID id : selectedEntityIds)
                        if (id != primaryID) MultiEdit::MirrorEntityEdit(level, before, after, id);
            }

            // After the mirroring above, which would otherwise copy the
            // primary's new height onto the others.
            if (entityInspectorState.snapToFloorRequested) {
                entityInspectorState.snapToFloorRequested = false;

                GeometryState undo = CaptureEntities(level, "editor.history.snap_to_floor");
                int snapped = 0;

                for (const ID id : selectedEntityIds)
                    if (ComponentTransform* entityTransform = level.transforms.Get(id); entityTransform != nullptr && MapQueries::SnapToFloor(level, *entityTransform))
                        ++snapped;

                CommitUndoIf(std::move(undo), snapped);
                if (snapped > 0) MapEditorInternal::MarkUnsavedChanges();
            }

            if (entityInspectorState.wallSnapButtonRequested) {
                entityInspectorState.wallSnapButtonRequested = false;

                using WallSnapStatus = ImGuiDrawFunctions::EntityInspectorState::WallSnapStatus;

                switch (entityInspectorState.wallSnapStatus) {
                    case WallSnapStatus::Off: WallSnap::BeginPicking(selectedEntityIds, primaryID); break;
                    case WallSnapStatus::Picking: WallSnap::CancelPicking(); break;
                    case WallSnapStatus::Snapped: WallSnap::Disable(selectedEntityIds); break;
                }
            }

            if (entityInspectorState.switchSidesRequested) {
                entityInspectorState.switchSidesRequested = false;

                GeometryState undo = CaptureEntities(level, "editor.history.switch_wall_side");
                undo.wallSnaps = WallSnap::All();

                const int switched = WallSnap::SwitchSides(level, selectedEntityIds);
                CommitUndoIf(std::move(undo), switched);
                if (switched > 0) MapEditorInternal::MarkUnsavedChanges();
            }

            if (entityInspectorState.wallSnapGapChanged) {
                entityInspectorState.wallSnapGapChanged = false;

                WallSnap::SetGap(level, selectedEntityIds, entityInspectorState.wallSnapGap);
                MapEditorInternal::MarkUnsavedChanges();
            }
        }

        if (editingWall) {
            std::erase_if(selectedWalls, [&](const int index) { return index < 0 || index >= static_cast<int>(level.walls.size()); });

            if (selectedWall < 0 || selectedWall >= static_cast<int>(level.walls.size())) {
                ClearWallSelection();
                return;
            }

            const bool mirrors = selectedWalls.size() > 1;
            const nlohmann::json before = mirrors ? MultiEdit::WallState(level.walls[selectedWall]) : nlohmann::json{};

            const bool deleteRequested =
                ImGuiDrawFunctions::DrawWallEditor(level.walls[selectedWall], &editingWall, DRAGGABLE);

            if (deleteRequested) {
                // Highest index first, so each erase leaves the rest valid.
                std::vector<int> indicesToDelete = selectedWalls;
                std::ranges::sort(indicesToDelete, std::greater{});

                ClearWallSelection();
                PushUndo(level, "editor.history.delete_walls", static_cast<int>(indicesToDelete.size()));

                for (const int index : indicesToDelete) level.walls.erase(level.walls.begin() + index);

                // The erase moved the walls Sector::walls points at.
                MapQueries::RebuildSectorRuntimeLinks(level);
                return;
            }

            if (!editingWall) {
                ClearWallSelection();
                return;
            }

            if (mirrors) {
                const nlohmann::json after = MultiEdit::WallState(level.walls[selectedWall]);

                if (after != before)
                    for (const int index : selectedWalls)
                        if (index != selectedWall) MultiEdit::MirrorWallEdit(before, after, level.walls[index]);
            }
        }

        if (editingSector) {
            std::erase_if(selectedFlats, [&](const SelectedFlat& flat) {
                return flat.sectorIndex < 0 || flat.sectorIndex >= static_cast<int>(level.sectors.size());
            });

            if (selectedSector < 0 || selectedSector >= static_cast<int>(level.sectors.size())) {
                ClearFlatSelection();
            }
            else {
                // The inspector edits whole sectors, so a selection of several
                // floors/ceilings mirrors onto each other sector they belong to.
                std::vector<int> otherSectors;

                for (const SelectedFlat& flat : selectedFlats)
                    if (flat.sectorIndex != selectedSector && std::ranges::find(otherSectors, flat.sectorIndex) == otherSectors.end())
                        otherSectors.push_back(flat.sectorIndex);

                const bool mirrors = !otherSectors.empty();
                const nlohmann::json before = mirrors ? MultiEdit::SectorState(level.sectors[selectedSector]) : nlohmann::json{};

                bool open = true;

                ImGuiDrawFunctions::DrawSectorEditor(
                    level.sectors[selectedSector],
                    &open,
                    true
                );

                if (!open) {
                    ClearFlatSelection();
                }
                else if (mirrors) {
                    const nlohmann::json after = MultiEdit::SectorState(level.sectors[selectedSector]);

                    if (after != before)
                        for (const int index : otherSectors) MultiEdit::MirrorSectorEdit(before, after, level.sectors[index]);
                }
            }
        }

        // The cursor is free unless the middle button is down, so the browser
        // is up whenever it could actually be clicked.
        if (!cameraLooking) {
            ImGui::Begin((Localisation::Get("editor.asset_browser") + "##RuntimeEditor").c_str());

            if (ImGui::Button(Localisation::Get("editor.asset_browser.refresh").c_str()))  MapEditorInternal::assetBrowser.Refresh();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", Localisation::Get("editor.tooltip.asset_browser_refresh").c_str());

            ImGui::Spacing();

            // Same resolver DrawAssetField uses, so the browser thumbnails and
            // the inspector previews can never disagree about what a texture
            // name maps to. Routes back through previewTextureProvider, which
            // Start() pointed at this renderer.
            MapEditorInternal::assetBrowser.Draw(&MapEditorInternal::GetPreviewTextureID);

            ImGui::End();

            if (MapEditorInternal::runtimeHistoryVisible) DrawHistoryWindow(level);

            DrawToolbar();
        }

        // Outside the check above, so open scripts, flipbooks and prefabs don't
        // vanish while looking around. No code font: the map editor's belongs to
        // its own ImGui context, so the script editor uses the default one here.
        MapEditorInternal::assetBrowser.DrawTextEditorWindow(nullptr);
        MapEditorInternal::assetBrowser.DrawFlipbookEditorWindows();
        MapEditorInternal::assetBrowser.DrawPrefabEditorWindows();

        // Centred a little below the toolbar: its 6px margin, 32px buttons
        // and padding end ~54px down.
        constexpr float TOAST_TOP_OFFSET = 62.0f;

        ImGuiDrawFunctions::DrawNotificationBelow(ImGui::GetIO().DeltaTime, ImGui::GetMainViewport()->WorkPos.y + TOAST_TOP_OFFSET);

        // ── Drop textures straight onto geometry ─────────────────────────────
        // The 3D view is not an ImGui window, so there is no item to hang a
        // BeginDragDropTarget() off. Watch the live payload instead and apply it
        // on release outside every ImGui window. ImGui keeps the payload alive
        // for the frame the button goes up, so this fires exactly once.
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            payload != nullptr &&
            payload->IsDataType(AssetBrowser::DragDropPayloadTypeFor(AssetKind::Texture))) {
            // AllowWhenBlockedByActiveItem is required: the drag source owns the
            // active id, so a plain AnyWindow query always answers false.
            constexpr ImGuiHoveredFlags hoverFlags =
                ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;

            const bool overViewport = !ImGui::IsWindowHovered(hoverFlags);
            const std::string textureReference = PayloadToTextureReference(payload);
            const int dropCount = DropTargetCount();
            const std::string surfaceName = dropCount > 1
                ? std::to_string(dropCount) + (hoveredSurface.type == RayHitType::Wall ? " selected walls" : " selected flats")
                : DescribeHoveredSurface();

            if (DEBUG_DRAG_DROP) {
                DrawDropHint(
                    "would write=" + (textureReference.empty() ? std::string("<none>") : textureReference) +
                    "\ncurrently   =" + [&] {
                        const std::string current = CurrentTextureOfHoveredSurface(level);
                        return current.empty() ? std::string("<empty>") : current;
                    }() +
                    "\nsurface=" + (surfaceName.empty() ? "<none>" : surfaceName) +
                    "  viewport=" + (overViewport ? "yes" : "no")
                );
            }
            else if (overViewport && !textureReference.empty() && !surfaceName.empty()) {
                DrawDropHint(textureReference + "  ->  " + surfaceName);
            }

            if (overViewport &&
                !textureReference.empty() &&
                !surfaceName.empty() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                ApplyTextureToDropTargetWithUndo(level, textureReference) > 0) {
                // Every image in Assets is already in the atlas, so this only
                // rebuilds for one added since it was built.
                if (runtimeRenderer != nullptr) runtimeRenderer->RefreshTexturesIfLevelNeedsNew();

                EditorFunctions::Print("Applied " + textureReference + " to " + surfaceName);
            }
        }

        // ── Drop prefabs into the level ──────────────────────────────────────
        // Watched the same way. Lands where the mouse ray hits, which Update()
        // keeps current while dragging.
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            payload != nullptr &&
            payload->IsDataType(AssetBrowser::DragDropPayloadTypeFor(AssetKind::Prefab)) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            PlacePrefabAtHover(level, AssetBrowser::ToAssetReference(static_cast<const char*>(payload->Data), AssetKind::Prefab));
        }

        // Crosshair, useless because mouse position is used for selection
        // const ImGuiViewport* viewport = ImGui::GetMainViewport();
        // const ImVec2 center = viewport->GetCenter();
        //
        // constexpr float size = 6.0f;
        //
        // ImGui::GetForegroundDrawList()->AddRectFilled(
        //     {center.x - size * 0.5f, center.y - size * 0.5f},
        //     {center.x + size * 0.5f, center.y + size * 0.5f},
        //     IM_COL32(255, 255, 255, 255)
        // );
    }
}

namespace RuntimeEditor {
    void Start(Level& level, IRenderer& renderer) {
        (void)level;

        backToEditorRequested = false;

        spdlog::info("Runtime editor started");

        runtimeRenderer = &renderer;

        if (runtimeRenderer == nullptr) {
            spdlog::critical("Runtime renderer could not be found");
            return;
        }

        renderer.SetUseEditorCamera(true);

        camera = renderer.GetEditorCamera();
        transform = renderer.GetEditorCameraTransform();

        if (camera == nullptr || transform == nullptr) {
            spdlog::error("RuntimeEditor::Start failed: editor camera or transform was not created");
            return;
        }
#ifndef TILKY_STANDALONE
        transform->position = level.runtimeCamPos;
        camera->pitch = level.runtimeCamRot.x;
        camera->yaw = level.runtimeCamRot.y;
#endif
        camera->forward = GetCameraForward(*camera);
        camera->fov = MapEditorInternal::runtimeCameraFov;

        editorWindow = renderer.GetWindow();

        if (editorWindow == nullptr) spdlog::error("Runtime editor could not get the window; cursor lock is disabled");

        // Unlocked by default. Middle-button camera look and left-button UV
        // dragging decide the lock state fresh every Update().
        cameraLooking = false;
        draggingUv = false;
        uvDragSurface = {};
        SetCursorLocked(false);

        spdlog::info("Runtime editor is using renderer editor-only camera");

        if (!MapEditorInternal::assetBrowserInitialized) {
            MapEditorInternal::assetBrowser.SetRootDirectory(ProjectManager::GetAssetsPath());
            MapEditorInternal::assetBrowserInitialized = true;
        }

        // The shared inspector widgets (DrawAssetField and the thumbnail
        // helpers, all defined in MapEditorUI.cpp) resolve previews through the
        // Map Editor's SDL_Renderer, which does not exist in this process - so
        // every asset field would draw an empty box. Point them at the live
        // renderer instead, which hands back IDs the active ImGui backend can
        // actually bind. Cleared again in Shutdown().
        MapEditorInternal::previewTextureProvider = [](const std::string& fileName) -> ImTextureID {
            return runtimeRenderer != nullptr ? runtimeRenderer->GetImGuiTextureID(fileName) : ImTextureID{};
        };
    }

    void Update(Level& level,
        IRenderer& renderer,
        const bool relativeMouseMod,
        const bool mouseBlockedByImGui,
        const bool keyboardBlockedByImGui,
        const float screenWidth,
        const float screenHeight) {
        // Still in the signature so the call site does not have to change, but
        // the runtime editor owns its cursor state now.
        (void)relativeMouseMod;

        if (!renderer.IsUsingEditorCamera()) renderer.SetUseEditorCamera(true);

        if (camera == nullptr || transform == nullptr) {
            camera = renderer.GetEditorCamera();
            transform = renderer.GetEditorCameraTransform();
        }

        if (camera == nullptr || transform == nullptr) return;

        camera->fov = MapEditorInternal::runtimeCameraFov;

        //region look

        const bool middleHeld = InputManager::GetMouseButton(SDL_BUTTON_MIDDLE);

        // The ImGui test gates only the *start* of a look. Once the cursor is
        // hidden ImGui keeps reporting the position it froze at, so re-testing
        // every frame would drop the drag the moment you swing past a panel.
        // Not WantCaptureMouse: ImGui sets it while any popup is open, wherever
        // the cursor is, and middle-click does not close popups - so an open
        // toolbar menu (Steps, Statistics) would block looking entirely.
        constexpr ImGuiHoveredFlags lookHoverFlags =
            ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup;

        const bool windowFocused = editorWindow != nullptr && SDL_GetKeyboardFocus() == editorWindow;
        const bool wantLook =
            windowFocused &&
            middleHeld &&
            !draggingUv &&
            (cameraLooking || !ImGui::IsWindowHovered(lookHoverFlags));

        if (wantLook != cameraLooking) {
            cameraLooking = wantLook;
            SetCursorLocked(cameraLooking || draggingUv);
        }

        if (cameraLooking) {
            camera->yaw -= InputManager::GetMouseDelta().x * MOUSE_SENSITIVITY;
            camera->pitch -= InputManager::GetMouseDelta().y * MOUSE_SENSITIVITY;
        }

        //endregion

        camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
        camera->yaw = std::fmod(camera->yaw, 360.0f);
        if (camera->yaw < 0.0f) camera->yaw += 360.0f;

        // Runtime editor cameras are not updated by the normal gameplay
        // CameraSystem, so keep the renderer-facing direction synchronized.
        camera->forward = GetCameraForward(*camera);

        //region movement

        Vector3 movement = {0.0f, 0.0f, 0.0f};

        // Keeps running during a gizmo drag, so you can fly while moving or
        // rotating an entity.
        {
            const Vector3 forward = camera->forward;
            const Vector3 left = GetCameraLeft(*camera);

            using namespace MapEditorInternal;

            if (GetKeyBind(KEYBIND_RT_FLY_FORWARD)) movement = movement + forward;
            if (GetKeyBind(KEYBIND_RT_FLY_BACK)) movement = movement - forward;
            if (GetKeyBind(KEYBIND_RT_FLY_LEFT)) movement = movement + left;
            if (GetKeyBind(KEYBIND_RT_FLY_RIGHT)) movement = movement - left;
            if (GetKeyBind(KEYBIND_RT_FLY_UP)) movement.y += 1.0f;
            if (GetKeyBind(KEYBIND_RT_FLY_DOWN)) movement.y -= 1.0f;

            if (GetKeyBindDown(KEYBIND_RT_SPEED_UP)) moveSpeed += 3.0f;
            if (GetKeyBindDown(KEYBIND_RT_SPEED_DOWN)) moveSpeed -= 3.0f;
            if (GetKeyBind(KEYBIND_RT_RESET_SPEED)) moveSpeed = BASE_MOVE_SPEED;

            moveSpeed = std::fmax(moveSpeed, 0.0001f);

            const float movementLengthSq = movement.x * movement.x + movement.y * movement.y + movement.z * movement.z;

            if (movementLengthSq > 0.0f) {
                movement = movement * (1.0f / std::sqrt(movementLengthSq));
                transform->AddPosition(movement * moveSpeed * GameTime::deltaTime);
            }
        }

        //endregion

        const Vector3 rayOrigin = transform->position;

        const Vector2 viewportSize = {
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight)
        };

        // Camera look aims through the centre. A UV drag keeps using the point
        // at which its cursor was locked, so hidden relative input cannot move
        // the ray's absolute screen position.
        const Vector2 rayScreenPosition = cameraLooking
            ? Vector2{viewportSize.x * 0.5f, viewportSize.y * 0.5f}
            : draggingUv
                ? cursorBeforeLock
                : InputManager::GetMousePosition();

        const Vector3 rayDirection = GetMouseRayDirection(
            *camera,
            rayScreenPosition,
            viewportSize
        );

        const std::optional<RayHit> hit = GameFunctions::Raycast(
            level,
            rayOrigin,
            rayDirection,
            RAY_LENGTH,
            camera->ownerID,
            false
        );

        // Cached for Draw(), which needs a drop target but has no viewport size
        // to build a ray from. Recomputed every frame whether or not anything is
        // being dragged, so the drop path stays a pure lookup.
        hoveredSurface = {};

        if (hit.has_value()) {
            hoveredSurface.type = hit->type;
            hoveredHitPosition = hit->position;
            hoveredRayDirection = rayDirection;

            if (hit->type == RayHitType::Wall) {
                hoveredSurface.wallIndex = FindWallIndex(level, hit->wall);

                if (hit->wall != nullptr) {
                    hoveredSurface.wallSlot = WallPieces::SlotAtHeight(
                        level,
                        *hit->wall,
                        {hit->position.x, hit->position.z},
                        hit->position.y
                    );
                }
            }
            else if (hit->type == RayHitType::SectorFloor || hit->type == RayHitType::SectorCeiling) {
                hoveredSurface.sectorIndex = FindSectorIndex(level, hit->sector);
                hoveredSurface.floorIndex = hit->sectorFloorIndex;
            }
            else if (hit->HitEntity()) hoveredSurface.entityID = hit->entity->id;
        }

        renderer.ClearSurfaceMarks();

        // Blue tint on everything selected. A wall is selected whole, so both
        // its pieces get it.
        {
            SurfaceMark mark;
            mark.metaData = SURFACE_META_SELECTED;

            mark.kind = MarkedSurfaceKind::Entity;
            for (const ID id : selectedEntityIds) {
                mark.entityID = id;
                renderer.MarkSurface(mark);
            }

            mark.kind = MarkedSurfaceKind::Wall;
            for (const int index : selectedWalls) {
                mark.wallIndex = index;

                for (const WallSurfaceSlot slot : WALL_SURFACE_SLOTS) {
                    mark.wallSlot = slot;
                    renderer.MarkSurface(mark);
                }
            }

            for (const SelectedFlat& flat : selectedFlats) {
                mark.kind = flat.surface == RayHitType::SectorCeiling ? MarkedSurfaceKind::Ceiling : MarkedSurfaceKind::Floor;
                mark.sectorIndex = flat.sectorIndex;
                mark.floorIndex = flat.floorIndex;
                renderer.MarkSurface(mark);
            }
        }

        // Red tint on the hovered surface. Same "over the 3D view" test as
        // Draw()'s drop target, so it also shows while dragging a texture in
        // from the asset browser, but not while the cursor is over a panel.

        constexpr ImGuiHoveredFlags hoverFlags =
            ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;

        if (cameraLooking || !ImGui::IsWindowHovered(hoverFlags)) {
            SurfaceMark mark;
            mark.metaData = SURFACE_META_HOVERED;

            if (hoveredSurface.type == RayHitType::Wall && hoveredSurface.wallIndex >= 0) {
                mark.kind = MarkedSurfaceKind::Wall;
                mark.wallIndex = hoveredSurface.wallIndex;
                mark.wallSlot = hoveredSurface.wallSlot;
                renderer.MarkSurface(mark);
            }
            else if ((hoveredSurface.type == RayHitType::SectorFloor || hoveredSurface.type == RayHitType::SectorCeiling) &&
                     hoveredSurface.sectorIndex >= 0) {
                mark.kind = hoveredSurface.type == RayHitType::SectorFloor ? MarkedSurfaceKind::Floor : MarkedSurfaceKind::Ceiling;
                mark.sectorIndex = hoveredSurface.sectorIndex;
                mark.floorIndex = hoveredSurface.floorIndex;
                renderer.MarkSurface(mark);
            }
            else if (hoveredSurface.type == RayHitType::Entity && hoveredSurface.entityID != INVALID_ID) {
                mark.kind = MarkedSurfaceKind::Entity;
                mark.entityID = hoveredSurface.entityID;
                renderer.MarkSurface(mark);
            }
        }

        //region uv drag

        // The asset browser's drop path also rides the left button, so stay out
        // of the way while a payload is live - otherwise dragging a texture in
        // smears the UVs on the way to the surface.
        const bool dragDropActive = ImGui::GetDragDropPayload() != nullptr;

        //region transform gizmo

        // Before the UV drag below, so a press on a handle never slides the
        // texture of the surface behind it.
        gizmoViewportSize = viewportSize;

        ComponentTransform* gizmoTarget = GizmoTarget(level);
        TransformGizmo::View gizmoView;
        const bool haveGizmoView = BuildGizmoView(gizmoView);

        if (windowFocused && !keyboardBlockedByImGui && !TransformGizmo::IsDragging()) {
            if (MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_MOVE_TOOL)) gizmoTool = TransformGizmo::Tool::Move;
            if (MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_ROTATE_TOOL)) gizmoTool = TransformGizmo::Tool::Rotate;
        }

        if (TransformGizmo::IsDragging() &&
            (gizmoTarget == nullptr || !haveGizmoView || !InputManager::GetMouseButton(SDL_BUTTON_LEFT) || !windowFocused)) {
            TransformGizmo::EndDrag();

            if (gizmoChangedTransform) {
                CommitUndoIf(std::move(gizmoUndo), static_cast<int>(selectedEntityIds.size()));
                MapEditorInternal::MarkUnsavedChanges();
            }

            gizmoUndo = {};
            gizmoChangedTransform = false;
        }

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) &&
            gizmoTarget != nullptr &&
            haveGizmoView &&
            windowFocused &&
            !mouseBlockedByImGui &&
            !dragDropActive &&
            !cameraLooking) {
            gizmoChangedTransform = false;

            if (TransformGizmo::BeginDrag(gizmoView, gizmoTool, *gizmoTarget, InputManager::GetMousePosition()))
                gizmoUndo = CaptureEntities(level, gizmoTool == TransformGizmo::Tool::Move
                    ? "runtime_editor.history.move_entities"
                    : "runtime_editor.history.rotate_entities");
        }

        if (TransformGizmo::IsDragging()) {
            const bool snap = MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_DISABLE_SNAP);

            const Vector3 positionBefore = gizmoTarget->position;
            const Quaternion rotationBefore = gizmoTarget->rotation;

            if (TransformGizmo::UpdateDrag(gizmoView, *gizmoTarget, InputManager::GetMousePosition(), snap)) {
                gizmoChangedTransform = true;

                // Lighting and the entity's sector both follow its position.
                gizmoTarget->UpdateObjectSectorAndFloor(level.sectors);

                // The rest of the selection follows by the same step: moved
                // by the same offset, or turned by the same world-axis
                // rotation about each one's own origin.
                const Vector3 moved = gizmoTarget->position - positionBefore;
                const Quaternion turned = (gizmoTarget->rotation * rotationBefore.Inversed()).Normalized();

                for (const ID id : selectedEntityIds) {
                    if (id == *selectedEntityId) continue;

                    ComponentTransform* other = level.transforms.Get(id);
                    if (other == nullptr) continue;

                    if (gizmoTool == TransformGizmo::Tool::Move) other->AddPosition(moved);
                    else other->rotation = (turned * other->rotation).Normalized();

                    other->UpdateObjectSectorAndFloor(level.sectors);
                }
            }
        }

        // After the gizmo, so a snapped entity dragged off its wall is back
        // on it before anything draws.
        if (WallSnap::Enforce(level)) MapEditorInternal::MarkUnsavedChanges();

        //endregion

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) &&
            windowFocused &&
            !mouseBlockedByImGui &&
            !dragDropActive &&
            !cameraLooking &&
            !TransformGizmo::IsDragging() &&
            IsUvEditableSurface(hoveredSurface.type)) {
            uvDragUndo = CaptureGeometry(level, "runtime_editor.history.drag_texture");
            uvDragPushedUndo = false;

            uvDragSurface = hoveredSurface;
            draggingUv = true;

            // Decided once here and held for the whole drag. Floors and
            // ceilings keep the raw delta.
            uvDragHorizontalSign = 1.0f;

            if (uvDragSurface.type == RayHitType::Wall &&
                hit.has_value() &&
                uvDragSurface.wallIndex >= 0 &&
                uvDragSurface.wallIndex < static_cast<int>(level.walls.size())) {
                const Wall& dragWall = level.walls[uvDragSurface.wallIndex];

                lastWallDragHorizontalSign = ComputeWallHorizontalDragSign(
                    dragWall,
                    dragWall.Surface(uvDragSurface.wallSlot),
                    hit->position,
                    rayOrigin,
                    *camera,
                    lastWallDragHorizontalSign
                );

                uvDragHorizontalSign = lastWallDragHorizontalSign;
            }
            else if ((uvDragSurface.type == RayHitType::SectorFloor || uvDragSurface.type == RayHitType::SectorCeiling) &&
                     hit.has_value() && haveGizmoView &&
                     uvDragSurface.sectorIndex >= 0 &&
                     uvDragSurface.sectorIndex < static_cast<int>(level.sectors.size())) {
                const Sector& dragSector = level.sectors[uvDragSurface.sectorIndex];

                if (uvDragSurface.floorIndex >= 0 && uvDragSurface.floorIndex < static_cast<int>(dragSector.floors.size())) {
                    const SectorFloor& dragFloor = dragSector.floors[uvDragSurface.floorIndex];

                    flatUvDrag = ComputeFlatUvDrag(
                        uvDragSurface.type == RayHitType::SectorFloor ? dragFloor.floor : dragFloor.ceiling,
                        WallPieces::ComputeSectorBounds(dragSector),
                        hit->position,
                        gizmoView,
                        flatUvDrag
                    );
                }
            }

            SetCursorLocked(cameraLooking || draggingUv);
        }

        if (draggingUv &&
            (!InputManager::GetMouseButton(SDL_BUTTON_LEFT) || !windowFocused)) {
            draggingUv = false;
            uvDragSurface = {};
            SetCursorLocked(cameraLooking || draggingUv);
        }

        if (draggingUv) {
            const Vector2 mouseDelta = InputManager::GetMouseDelta();

            // On a wall flipped in Y the texture runs the other way, so up
            // has to lower the offset for it to still follow the cursor.
            float uvDragVerticalSign = 1.0f;

            if (uvDragSurface.type == RayHitType::Wall &&
                uvDragSurface.wallIndex >= 0 &&
                uvDragSurface.wallIndex < static_cast<int>(level.walls.size()) &&
                level.walls[uvDragSurface.wallIndex].Surface(uvDragSurface.wallSlot).flipTextureY)
                uvDragVerticalSign = -1.0f;

            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                // Negated so the texture tracks the cursor rather than running
                // from it. Flip the signs if the sampler wants the opposite.
                // Walls and sector surfaces share the speed so a drag feels
                // the same on either.
                const float dragSpeed = MapEditorInternal::runtimeSteps[MapEditorInternal::RT_STEP_TEXTURE_DRAG];
                const float deltaX = mouseDelta.x * uvDragHorizontalSign * dragSpeed;
                const float deltaY = -mouseDelta.y * uvDragVerticalSign * dragSpeed;

                if (!uvDragPushedUndo) {
                    CommitUndo(std::move(uvDragUndo));
                    uvDragPushedUndo = true;
                }

                const bool xOnly = MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_TEXTURE_X_ONLY);
                const bool yOnly = !xOnly && MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_TEXTURE_Y_ONLY);

                // The axis locks are mouse directions, so on a floor or
                // ceiling they lock whichever texture axis that direction drives.
                if (uvDragSurface.type == RayHitType::SectorFloor || uvDragSurface.type == RayHitType::SectorCeiling) {
                    const float horizontal = yOnly ? 0.0f : mouseDelta.x * flatUvDrag.horizontalSign * dragSpeed;
                    const float vertical = xOnly ? 0.0f : mouseDelta.y * flatUvDrag.verticalSign * dragSpeed;

                    AddUvOffset(level, uvDragSurface, flatUvDrag.horizontalAxis == 0
                        ? Vector2{horizontal, vertical}
                        : Vector2{vertical, horizontal});
                }
                else if (xOnly)
                    AddUvOffset(level, uvDragSurface, {deltaX, 0});
                else if (yOnly)
                    AddUvOffset(level, uvDragSurface, {0, deltaY});
                else
                    AddUvOffset(level, uvDragSurface, {deltaX, deltaY});
            }
        }

        // Flip texture. Not while left is held: X / Y are the axis locks of
        // the texture drag above, and pressing one mid-drag mustn't flip.
        if (windowFocused &&
            !keyboardBlockedByImGui &&
            !InputManager::GetMouseButton(SDL_BUTTON_LEFT) &&
            !TransformGizmo::IsDragging()) {
            const bool flipX = MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_FLIP_TEXTURE_X);
            const bool flipY = MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_FLIP_TEXTURE_Y);

            // The hovered fallback needs the cursor on the view, not a panel.
            if (flipX || flipY) {
                GeometryState before = CaptureGeometry(level, "runtime_editor.history.flip_texture");
                CommitUndoIf(std::move(before), FlipTextures(level, flipX, flipY, !mouseBlockedByImGui));
            }
        }

        if (windowFocused && !keyboardBlockedByImGui) {
            using MapEditorInternal::GetComboDown;

            if (MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_BACK_TO_EDITOR)) backToEditorRequested = true;

            // Texture copy / paste. With placement wins when both fire.
            const bool copyWithPlacement = GetComboDown(MapEditorInternal::COMBO_RT_COPY_TEXTURE_PLACEMENT);

            if ((copyWithPlacement || GetComboDown(MapEditorInternal::COMBO_RT_COPY_TEXTURE)) &&
                !mouseBlockedByImGui && CopyHoveredSurface(level, copyWithPlacement)) {
                ImGuiDrawFunctions::ShowNotification(Localisation::Get(copyWithPlacement
                    ? "runtime_editor.toast.texture_and_placement_copied"
                    : "runtime_editor.toast.texture_copied").c_str(), false, 1.5f);
            }

            const bool pasteWithPlacement = GetComboDown(MapEditorInternal::COMBO_RT_PASTE_TEXTURE_PLACEMENT);

            if (pasteWithPlacement || GetComboDown(MapEditorInternal::COMBO_RT_PASTE_TEXTURE)) {
                GeometryState before = CaptureGeometry(level, pasteWithPlacement
                    ? "runtime_editor.history.paste_texture_placement"
                    : "runtime_editor.history.paste_texture");
                CommitUndoIf(std::move(before), PasteSurface(level, pasteWithPlacement, !mouseBlockedByImGui));

                // The copied image may not be in the atlas yet.
                runtimeRenderer->RefreshTexturesIfLevelNeedsNew();
            }

            // Wall / sector property copy and paste.
            if (GetComboDown(MapEditorInternal::COMBO_RT_COPY_PROPERTIES) && !mouseBlockedByImGui && CopyHoveredProperties(level)) {
                ImGuiDrawFunctions::ShowNotification(Localisation::Get(propertyClipboard.kind == PropertyClipboard::Kind::Wall
                    ? "runtime_editor.toast.wall_properties_copied"
                    : "runtime_editor.toast.sector_properties_copied").c_str(), false, 1.5f);
            }

            if (GetComboDown(MapEditorInternal::COMBO_RT_PASTE_PROPERTIES)) {
                GeometryState before = CaptureGeometry(level, "runtime_editor.history.paste_properties");
                CommitUndoIf(std::move(before), PasteProperties(level, !mouseBlockedByImGui));
                runtimeRenderer->RefreshTexturesIfLevelNeedsNew();
            }

            // Reset offset, scale and flip.
            if (GetComboDown(MapEditorInternal::COMBO_RT_RESET_PLACEMENT)) {
                GeometryState before = CaptureGeometry(level, "runtime_editor.history.reset_placement");
                CommitUndoIf(std::move(before), ResetTexturePlacement(level, !mouseBlockedByImGui));
            }

            // Fit each selected wall's texture to its height, or continue the
            // textures across the selected walls' corners.
            if (GetComboDown(MapEditorInternal::COMBO_RT_FIT_TEXTURES)) {
                GeometryState before = CaptureGeometry(level, "editor.history.fit_wall_textures");
                const int changed = RunMapEditorWallTool(level, &MapEditorInternal::FitSelectedWallTexturesToHeight);
                CommitUndoIf(std::move(before), changed);
                ShowToastf("editor.geometry.align.fit_done", changed);
            }
            else if (GetComboDown(MapEditorInternal::COMBO_RT_ALIGN_TEXTURES)) {
                GeometryState before = CaptureGeometry(level, "editor.history.align_wall_textures");
                const int changed = RunMapEditorWallTool(level, &MapEditorInternal::AlignSelectedWallTextures);
                CommitUndoIf(std::move(before), changed);
                ShowToastf("editor.geometry.align.chain_done", changed);
            }

            if (GetComboDown(MapEditorInternal::COMBO_RT_REDO)) Redo(level);
            else if (GetComboDown(MapEditorInternal::COMBO_RT_UNDO)) Undo(level);

            const bool cycleBack = MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_PREVIOUS_TEXTURE);
            const bool cycleForward = MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_NEXT_TEXTURE);

            if (cycleBack != cycleForward) {
                GeometryState before = CaptureGeometry(level, "runtime_editor.history.change_texture");
                CommitUndoIf(std::move(before), CycleTextures(level, cycleForward ? 1 : -1, !mouseBlockedByImGui));
            }

            if (MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_FULLBRIGHT)) {
                runtimeRenderer->SetFullbright(!runtimeRenderer->IsFullbright());
                ImGuiDrawFunctions::ShowNotification(Localisation::Get(runtimeRenderer->IsFullbright()
                    ? "runtime_editor.toast.fullbright_on"
                    : "runtime_editor.toast.fullbright_off").c_str(), false, 1.5f);
            }

            if (MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_FOCUS_SELECTION)) FocusSelection(level);

            if (MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_TOGGLE_ENTITY_MARKERS)) CycleEntityMarkerMode(true);

            if (!mouseBlockedByImGui && MapEditorInternal::GetKeyBindDown(MapEditorInternal::KEYBIND_RT_PLACE_ENTITY)) PlacePaletteEntityAtHover(level);

        }

        // ---- Snap to Wall waiting for a wall
        // A right-click finishes it on a wall or cancels it on anything else,
        // and selects nothing either way. Entities are looked through.
        bool rightClickTaken = false;

        WallSnap::CancelPickingIfSelectionChanged(selectedEntityIds);

        if (WallSnap::IsPicking() && !keyboardBlockedByImGui && InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE))
            WallSnap::CancelPicking();

        if (WallSnap::IsPicking() && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT) && !mouseBlockedByImGui) {
            rightClickTaken = true;

            const std::optional<RayHit> wallHit = GameFunctions::RaycastGeometry(level, rayOrigin, rayDirection, RAY_LENGTH);

            if (!wallHit.has_value() || !wallHit->HitWall()) WallSnap::CancelPicking();
            else {
                GeometryState undo = CaptureEntities(level, "editor.history.snap_to_wall");
                undo.wallSnaps = WallSnap::All();

                const int snapped = WallSnap::FinishPicking(
                    level,
                    *wallHit->wall,
                    {wallHit->position.x, wallHit->position.z},
                    wallHit->position.y,
                    {rayOrigin.x, rayOrigin.z}
                );

                CommitUndoIf(std::move(undo), snapped);

                if (snapped > 0) {
                    runtimeRenderer->RefreshTexturesIfLevelNeedsNew();
                    MapEditorInternal::MarkUnsavedChanges();
                }
            }
        }

        // Select by texture: its key held while right clicking.
        const bool selectByTextureHeld = MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_SELECT_BY_TEXTURE);

        if (!rightClickTaken && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT) && !mouseBlockedByImGui && selectByTextureHeld && SelectAllWithHoveredTexture(level))
            ShowToastf("runtime_editor.toast.selected_by_texture", static_cast<int>(SelectionCount()));

        //endregion

        if (!rightClickTaken && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT) && !mouseBlockedByImGui && !selectByTextureHeld) {
            if (!hit.has_value()) {
                spdlog::info("Runtime editor ray missed");
                return;
            }

            spdlog::info(
                "Runtime editor ray hit. type={}, entity={}, wall={}, sector={}",
                static_cast<int>(hit->type),
                hit->entity != nullptr,
                hit->wall != nullptr,
                hit->sector != nullptr
            );

            // Ctrl adds to (or toggles within) the selection, but only items
            // of the kind already selected. Ctrl-clicking another kind does
            // nothing at all - the selection and the inspector stay as they were.
            const bool additive = MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_ADD_TO_SELECTION);
            const SelectionKind currentKind = CurrentSelectionKind();

            const auto rejectsKind = [&](const SelectionKind clickedKind) {
                return additive && currentKind != SelectionKind::None && currentKind != clickedKind;
            };

            switch (hit->type) {
                case RayHitType::Entity: {
                    if (hit->entity == nullptr) {
                        spdlog::error("RayHitType::Entity had null entity pointer");
                        return;
                    }

                    if (rejectsKind(SelectionKind::Entity)) break;

                    const ID hitEntityId = hit->entity->id;

                    if (additive && currentKind == SelectionKind::Entity) ToggleEntity(hitEntityId);
                    else SelectOnlyEntity(hitEntityId);

                    spdlog::info("Selected entity {} ({} selected)", hitEntityId, selectedEntityIds.size());
                    break;
                }

                case RayHitType::Wall: {
                    if (hit->wall == nullptr) {
                        spdlog::error("RayHitType::Wall had null wall pointer");
                        return;
                    }

                    if (rejectsKind(SelectionKind::Wall)) break;

                    const int wallIndex = FindWallIndex(level, hit->wall);

                    if (wallIndex == -1) {
                        spdlog::error("Selected wall was not found in level.walls");
                        return;
                    }

                    if (additive && currentKind == SelectionKind::Wall) ToggleWall(wallIndex);
                    else SelectOnlyWall(wallIndex);

                    spdlog::info("Selected wall {} ({} selected)", wallIndex, selectedWalls.size());
                    break;
                }

                case RayHitType::SectorFloor:
                case RayHitType::SectorCeiling: {
                    if (hit->sector == nullptr) {
                        spdlog::error("Sector ray hit had null sector pointer");
                        return;
                    }

                    if (rejectsKind(SelectionKind::Flat)) break;

                    const int sectorIndex = FindSectorIndex(level, hit->sector);

                    if (sectorIndex == -1) {
                        spdlog::error("Selected sector was not found in level.sectors");
                        return;
                    }

                    const SelectedFlat flat = {sectorIndex, hit->sectorFloorIndex, hit->type};

                    if (additive && currentKind == SelectionKind::Flat) ToggleFlat(flat);
                    else SelectOnlyFlat(flat);

                    spdlog::info(
                        "Selected sector {} floor={} surface={} ({} selected)",
                        sectorIndex,
                        flat.floorIndex,
                        hit->type == RayHitType::SectorFloor ? "floor" : "ceiling",
                        selectedFlats.size()
                    );

                    break;
                }

                case RayHitType::None:
                default: spdlog::warn("Runtime editor ray hit had invalid hit type"); break;
            }
        }

        const float wheel = InputManager::GetMouseWheelScroll();

        if (wheel != 0.0f && !mouseBlockedByImGui && hit.has_value()) {
            using namespace MapEditorInternal;

            constexpr float MIN_ROOM_HEIGHT = 1.0f;
            // The fine scroll key swaps every step for its fine one.
            const bool fine = GetKeyBind(KEYBIND_RT_FINE_SCROLL);
            const float heightDelta = wheel * runtimeSteps[fine ? RT_STEP_FINE_HEIGHT : RT_STEP_HEIGHT];
            const float uvScaleDelta = wheel * runtimeSteps[fine ? RT_STEP_FINE_TEXTURE_SCALE : RT_STEP_TEXTURE_SCALE];

            const bool flatHit = hit->type == RayHitType::SectorFloor || hit->type == RayHitType::SectorCeiling;
            const char* scrollLabel =
                hit->type == RayHitType::Wall ? "runtime_editor.history.scale_texture"
                : flatHit && GetKeyBind(KEYBIND_RT_BRIGHTNESS_SCROLL) ? "runtime_editor.history.change_brightness"
                : hit->type == RayHitType::SectorFloor ? "runtime_editor.history.change_floor_height"
                : hit->type == RayHitType::SectorCeiling ? "runtime_editor.history.change_ceiling_height"
                : nullptr;

            const Uint64 now = SDL_GetTicks();

            if (scrollLabel != nullptr) {
                if (now - lastScrollEditTicks > SCROLL_UNDO_MERGE_MS || lastScrollLabel != scrollLabel) PushUndo(level, scrollLabel);

                lastScrollEditTicks = now;
                lastScrollLabel = scrollLabel;
            }

            if (hit->type == RayHitType::Wall && hit->wall != nullptr) {
                WallSurface& wallSurface = hit->wall->Surface(hoveredSurface.wallSlot);

                if (!MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_TEXTURE_Y_ONLY))
                wallSurface.textureScale.x += uvScaleDelta;
                if (!MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_TEXTURE_X_ONLY))
                wallSurface.textureScale.y += uvScaleDelta;
            }
            else if ((hit->type == RayHitType::SectorFloor ||
                      hit->type == RayHitType::SectorCeiling) &&
                     hit->sector != nullptr &&
                     hit->sectorFloorIndex >= 0 &&
                     hit->sectorFloorIndex < static_cast<int>(hit->sector->floors.size())) {
                Sector& sector = *hit->sector;
                const int floorIndex = hit->sectorFloorIndex;
                SectorFloor& floor = sector.floors[floorIndex];

                // Ctrl+scroll: the sector's brightness instead of a height.
                if (MapEditorInternal::GetKeyBind(MapEditorInternal::KEYBIND_RT_BRIGHTNESS_SCROLL)) {
                    AdjustSectorBrightness(level, sector, wheel * runtimeSteps[fine ? RT_STEP_FINE_BRIGHTNESS : RT_STEP_BRIGHTNESS]);
                }
                else if (hit->type == RayHitType::SectorFloor) {
                    const float minimumHeight =
                        floorIndex > 0 ? sector.floors[floorIndex - 1].ceiling.height : std::numeric_limits<float>::lowest();

                    const float maximumHeight = floor.ceiling.height - MIN_ROOM_HEIGHT;

                    if (minimumHeight <= maximumHeight) {
                        floor.floor.height = std::clamp(
                            floor.floor.height + heightDelta,
                            minimumHeight,
                            maximumHeight
                        );
                    }
                }
                else if (hit->type == RayHitType::SectorCeiling) {
                    const float minimumHeight = floor.floor.height + MIN_ROOM_HEIGHT;

                    const float maximumHeight =
                        floorIndex + 1 < static_cast<int>(sector.floors.size())
                            ? sector.floors[floorIndex + 1].floor.height
                            : std::numeric_limits<float>::max();

                    if (minimumHeight <= maximumHeight) {
                        floor.ceiling.height = std::clamp(
                            floor.ceiling.height + heightDelta,
                            minimumHeight,
                            maximumHeight
                        );
                    }
                }
            }
        }

        ImGuiDrawFunctions::SetImGuiFocus(!cameraLooking && !draggingUv && !TransformGizmo::IsDragging());

        // An edit may have pointed a surface at an image the atlas doesn't
        // have yet. Not a full refresh: that rescans Assets and froze a frame
        // at the end of every inspector edit or UV drag.
        if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) runtimeRenderer->RefreshTexturesIfLevelNeedsNew();
    } // Update

    void Draw(Level &level) {
        RuntimeEditorUi::Draw(level);
    }

    bool BackToEditorRequested() {
        return backToEditorRequested;
    }

    void Shutdown(Level& level) {
        (void)level;
#ifndef TILKY_STANDALONE
        level.runtimeCamPos = transform->position;
        level.runtimeCamRot.x = camera->pitch;
        level.runtimeCamRot.y = camera->yaw;
#endif
        // Must be cleared before control can return to the Map Editor: leaving
        // it set would feed that editor's SDL backend renderer-specific IDs.
        MapEditorInternal::previewTextureProvider = nullptr;

        // Play mode reuses the renderer; it mustn't inherit the hover tint.
        if (runtimeRenderer != nullptr) runtimeRenderer->ClearSurfaceMarks();
        runtimeRenderer = nullptr;
        camera = nullptr;
        transform = nullptr;

        ClearEntitySelection();
        ClearWallSelection();
        ClearFlatSelection();

        // Release both relative mode and our cursor rectangle before switching editors.
        SetCursorLocked(false);

        // A drag cut short by leaving still changed the level.
        if (TransformGizmo::IsDragging() && gizmoChangedTransform) MapEditorInternal::MarkUnsavedChanges();
        TransformGizmo::EndDrag();
        gizmoChangedTransform = false;

        editorWindow = nullptr;
        cameraLooking = false;
        draggingUv = false;
        uvDragSurface = {};
        hoveredSurface = {};

        spdlog::info("Runtime editor shut down");
    }
}