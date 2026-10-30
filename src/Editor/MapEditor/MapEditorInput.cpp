#include <iostream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "../EditorInternal.hpp"
#include "../Comments.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/MapTopology.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"

namespace {
    bool holdingEntity = false;

    // Captured when a canvas press starts holdingEntity, so a drag that ends
    // on a script field (instead of on the canvas) can be undone - the
    // entities have been following the cursor the whole time. See
    // RevertCanvasEntityDrag.
    std::vector<std::pair<ID, Vector3>> canvasDragStartPositions;
    std::vector<ID> selectionBeforeCanvasDrag;
    ID primaryBeforeCanvasDrag = INVALID_ID;

    // The entity the inspector was showing when the press landed. The press
    // selects the pressed entity straight away, which would swap the
    // inspector - and the field the user is aiming at - out from under the
    // drag, so the inspector stays on this one until the frame after the
    // release (the drop is delivered during the release frame's UI pass).
    ID inspectorPinnedDuringCanvasDrag = INVALID_ID;

    // Set when the release of a canvas drag recorded an undo step, so a drop
    // onto a field (RevertCanvasEntityDrag) can take that step back off.
    bool canvasDragPushedUndo = false;

    // The move-gizmo part the current hold started on, GIZMO_NONE for a
    // drag of the entity body. A gizmo drag also keeps where on the gizmo the
    // cursor grabbed (relative to the primary's position), so the entity
    // doesn't jump to the cursor the way a body drag does.
    MapEditorInternal::EntityGizmoPart activeGizmoPart = MapEditorInternal::GIZMO_NONE;
    Vector2 gizmoGrabOffset = {0.0f, 0.0f};

    void BeginCanvasEntityHold(Level& level, const MapEditorInternal::EntityGizmoPart gizmoPart) {
        canvasDragStartPositions.clear();

        for (const ID entityID : MapEditorInternal::selectedEntities)
            if (const ComponentTransform* transform = level.transforms.Get(entityID))
                canvasDragStartPositions.emplace_back(entityID, transform->position);

        activeGizmoPart = gizmoPart;
        holdingEntity = true;
        canvasDragPushedUndo = false;
    }

    // On release: one step for every entity the hold moved.
    void PushCanvasEntityDragUndo(const Level& level) {
        MapEditorInternal::EntityTransformUndo before;

        for (const auto& [entityID, position] : canvasDragStartPositions) {
            const ComponentTransform* transform = level.transforms.Get(entityID);
            if (transform == nullptr || transform->position == position) continue;

            before.positions.emplace_back(entityID, position);
        }

        if (before.positions.empty()) return;

        const int moved = static_cast<int>(before.positions.size());
        MapEditorInternal::PushEntityPositionsUndo(std::move(before), "editor.history.move_entities", moved);
        canvasDragPushedUndo = true;
    }
}

namespace MapEditorInternal {
    void UpdateEditorZoom(const bool keyboardBlockedByImgui) {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorldBeforeZoom = ScreenToWorld(mouseScreen, cameraPos);

        bool zoomChanged = false;

        if ((!keyboardBlockedByImgui && GetKeyBindDown(KEYBIND_ZOOM_IN)) || InputManager::GetMouseWheelScrollUp()) {
            editorZoom *= 1.15f;
            zoomChanged = true;
        }

        if ((!keyboardBlockedByImgui && GetKeyBindDown(KEYBIND_ZOOM_OUT)) || InputManager::GetMouseWheelScrollDown()) {
            editorZoom /= 1.15f;
            zoomChanged = true;
        }

        editorZoom = std::clamp(editorZoom, MIN_EDITOR_ZOOM, MAX_EDITOR_ZOOM);

        if (zoomChanged) {
            const Vector2 mouseWorldAfterZoom = ScreenToWorld(mouseScreen, cameraPos);

            cameraPos.x += mouseWorldBeforeZoom.x - mouseWorldAfterZoom.x;
            cameraPos.y += mouseWorldBeforeZoom.y - mouseWorldAfterZoom.y;
        }
    }

    // =========================================================================
    //  Sector and entity selection
    // =========================================================================
    //
    // Declared in EditorInternal.hpp, where the invariants are written out.
    // These sit alongside SelectWall/ToggleWallSelection/ExtendWallSelectionTo/
    // ClearWallSelection and behave identically, with one deliberate
    // difference: they never touch editingSector/editingEntity, because on
    // this canvas a left-click selects without opening the inspector.

    void SelectSector(const ID sectorID) {
        const Level& level = LevelManager::CurrentLevel();
        if (level.sectorIDToIndex.find(sectorID) == level.sectorIDToIndex.end()) return;

        selectedSectors.assign(1, sectorID);
        selectedSectorID = sectorID;
    }

    void ToggleSectorSelection(const ID sectorID) {
        const Level& level = LevelManager::CurrentLevel();
        if (level.sectorIDToIndex.find(sectorID) == level.sectorIDToIndex.end()) return;

        const auto it = std::find(selectedSectors.begin(), selectedSectors.end(), sectorID);

        if (it == selectedSectors.end()) {
            selectedSectors.push_back(sectorID);
            selectedSectorID = sectorID;
            return;
        }

        selectedSectors.erase(it);

        // Ctrl-clicking the primary out of the selection has to hand the
        // primary role to something still selected, or the inspector goes on
        // editing a sector the user just deselected.
        if (selectedSectorID == sectorID) {
            selectedSectorID = selectedSectors.empty() ? INVALID_ID : selectedSectors.back();
            if (selectedSectorID == INVALID_ID) editingSector = false;
        }
    }

    void ExtendSectorSelectionTo(const ID sectorID) {
        const Level& level = LevelManager::CurrentLevel();
        if (level.sectorIDToIndex.find(sectorID) == level.sectorIDToIndex.end()) return;

        // Shift with nothing selected yet is just a plain click.
        if (selectedSectorID == INVALID_ID) {
            SelectSector(sectorID);
            return;
        }

        const ID firstID = std::min(sectorID, selectedSectorID);
        const ID lastID = std::max(sectorID, selectedSectorID);

        selectedSectors.clear();

        for (const Sector& sector : level.sectors)
            if (sector.id >= firstID && sector.id <= lastID) selectedSectors.push_back(sector.id);

        // The clicked sector anchors the next Shift-click, so it becomes the
        // primary even though the range was measured from the previous one.
        selectedSectorID = sectorID;
    }

    void ClearSectorSelection() {
        selectedSectors.clear();
        selectedSectorID = INVALID_ID;
        editingSector = false;
    }

    // selectedEntity stays a copy of the primary rather than a bare ID
    // because the rest of the entity path already expects that (the
    // holdingEntity drag, the clipboard, FocusCameraOnSelection). The plural
    // list holds IDs, like the sector and wall ones.

    ID wallSnapHoveredWallID = INVALID_ID;

    bool IsPaintSelectActive(const bool mouseBlockedByImGui) {
        return !mouseBlockedByImGui && currentState == STATE_MAP && (paintSelectTool || GetKeyBind(KEYBIND_PAINT_SELECT));
    }

    namespace {
        // Adds `id` to `selection` (making it the primary when the selection
        // was empty) or takes it out, keeping `primary` on something still
        // selected.
        void PaintInto(std::vector<ID>& selection, ID& primary, const ID id, const bool remove) {
            const auto it = std::ranges::find(selection, id);

            if (remove) {
                if (it == selection.end()) return;

                selection.erase(it);
                if (primary == id) primary = selection.empty() ? INVALID_ID : selection.back();
                return;
            }

            if (it != selection.end()) return;

            selection.push_back(id);
            if (primary == INVALID_ID) primary = id;
        }

        // One frame of a paint-select drag at `mouseWorld`.
        void PaintSelectAt(Level& level, const Vector2& mouseWorld, const bool remove) {
            const float radius = paintSelectRadius / std::max(editorZoom, 0.0001f);
            const float radiusSq = radius * radius;

            switch (currentMode) {
                case MODE_GEOMETRY:
                    if (linedefMode) {
                        for (const Linedef& linedef : level.linedefs)
                            if (DistancePointToSegmentSq(mouseWorld, linedef.start, linedef.end) <= radiusSq)
                                PaintInto(selectedLinedefs, selectedLinedefID, linedef.id, remove);

                        editingLinedef = selectedLinedefID != INVALID_ID;
                    }
                    else {
                        for (const Wall& wall : level.walls)
                            if (WallPassesFilter(wall) && DistancePointToSegmentSq(mouseWorld, wall.start, wall.end) <= radiusSq)
                                PaintInto(selectedWalls, selectedWallID, wall.id, remove);

                        editingWall = selectedWallID != INVALID_ID;
                    }
                    break;

                case MODE_SECTOR: {
                    // The sector under the brush's centre, holes respected,
                    // so a sector nested in another is reachable.
                    for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i) {
                        const Sector& sector = level.sectors[i];
                        if (!Geometry::IsPointInPolygon(sector.vertices, sector.innerLoops, mouseWorld)) continue;

                        PaintInto(selectedSectors, selectedSectorID, sector.id, remove);
                        break;
                    }

                    editingSector = selectedSectorID != INVALID_ID;
                    break;
                }

                case MODE_ENTITY: {
                    const float entityRadius = radius + ENTITY_SIZE * 0.5f;
                    ID primary = selectedEntities.empty() ? INVALID_ID : selectedEntity.id;

                    for (const ComponentTransform& transform : level.transforms.components) {
                        const Entity* entity = level.GetEntity(transform.ownerID);
                        if (entity == nullptr || level.ui_transforms.Has(entity->id) || !EntityPassesFilter(*entity)) continue;

                        const Vector2 position = {transform.position.x, transform.position.z};
                        if (Vector2Math::DistanceSquared(position, mouseWorld) > entityRadius * entityRadius) continue;

                        PaintInto(selectedEntities, primary, entity->id, remove);
                    }

                    if (const Entity* entity = primary == INVALID_ID ? nullptr : level.GetEntity(primary)) selectedEntity = *entity;
                    else editingEntity = false;
                    break;
                }

                default: break;
            }
        }
    }

    namespace {
        void FocusCameraOnSelection();
    }

    void SelectAndShowObject(const EventOwnerKind kind, const ID id) {
        switch (kind) {
            case EventOwnerKind::LINE:
                currentMode = MODE_GEOMETRY;
                SetLinedefMode(false);
                SelectWall(id);
                break;

            case EventOwnerKind::SECTOR:
                currentMode = MODE_SECTOR;
                SelectSector(id);
                editingSector = selectedSectorID != INVALID_ID;
                break;

            case EventOwnerKind::LINEDEF:
                currentMode = MODE_GEOMETRY;
                SetLinedefMode(true);
                SelectLinedef(id);
                break;

            case EventOwnerKind::ENTITY:
                currentMode = MODE_ENTITY;
                SelectEntity(id);
                editingEntity = !selectedEntities.empty();
                break;

            case EventOwnerKind::LEVEL:
                worldSettingsVisible = true;
                return;
        }

        FocusCameraOnSelection();
    }

    void AssignSelectionGroup(const int index) {
        if (index < 0 || index >= EDITOR_SELECTION_GROUP_COUNT) return;

        Level& level = LevelManager::CurrentLevel();
        level.selectionGroups[index] = {selectedWalls, selectedSectors, selectedLinedefs, selectedEntities};
        MarkUnsavedChanges();
    }

    int SelectSelectionGroup(const int index) {
        if (index < 0 || index >= EDITOR_SELECTION_GROUP_COUNT) return 0;

        Level& level = LevelManager::CurrentLevel();
        const EditorSelectionGroup& group = level.selectionGroups[index];

        ClearWallSelection();
        ClearSectorSelection();
        ClearLinedefSelection();
        ClearEntitySelection();

        for (const ID id : group.walls)
            if (level.wallIDToIndex.contains(id)) {
                if (selectedWalls.empty()) SelectWall(id);
                else selectedWalls.push_back(id);
            }

        for (const ID id : group.sectors)
            if (level.GetSector(id) != nullptr) {
                if (selectedSectors.empty()) SelectSector(id);
                else selectedSectors.push_back(id);
            }

        for (const ID id : group.linedefs)
            if (level.GetLinedef(id) != nullptr) {
                if (selectedLinedefs.empty()) SelectLinedef(id);
                else selectedLinedefs.push_back(id);
            }

        for (const ID id : group.entities)
            if (level.GetEntity(id) != nullptr) {
                if (selectedEntities.empty()) SelectEntity(id);
                else selectedEntities.push_back(id);
            }

        // Stay in the current mode when it shows part of the group.
        const bool currentShowsSome =
            (currentMode == MODE_GEOMETRY && (linedefMode ? !selectedLinedefs.empty() : !selectedWalls.empty())) ||
            (currentMode == MODE_SECTOR && !selectedSectors.empty()) ||
            (currentMode == MODE_ENTITY && !selectedEntities.empty());

        if (!currentShowsSome) {
            if (!selectedEntities.empty()) currentMode = MODE_ENTITY;
            else if (!selectedWalls.empty()) {
                currentMode = MODE_GEOMETRY;
                SetLinedefMode(false);
            }
            else if (!selectedLinedefs.empty()) {
                currentMode = MODE_GEOMETRY;
                SetLinedefMode(true);
            }
            else if (!selectedSectors.empty()) currentMode = MODE_SECTOR;
        }

        if (currentMode == MODE_SECTOR && !selectedSectors.empty()) editingSector = true;

        return static_cast<int>(selectedWalls.size() + selectedSectors.size() + selectedLinedefs.size() + selectedEntities.size());
    }

    namespace {
        // Right-click while Snap to Wall waits for a wall: a wall finishes
        // it (one undo step, snaps included), anything else cancels it.
        void HandleWallSnapRightClick(Level& level, const Vector2& mouseWorld) {
            const ID wallID = PickWallAt(mouseWorld);
            const Wall* wall = nullptr;

            for (const Wall& candidate : level.walls)
                if (candidate.id == wallID) wall = &candidate;

            if (wall == nullptr) {
                WallSnap::CancelPicking();
                return;
            }

            UndoStep step;
            step.kind = UNDO_ENTITIES;
            step.labelKey = "editor.history.snap_to_wall";
            step.entities = CaptureEntitiesSnapshot();
            step.wallSnaps = WallSnap::All();

            // The cursor's side of the wall is the side it goes on.
            step.count = WallSnap::FinishPicking(level, *wall, mouseWorld, std::nullopt, mouseWorld);

            if (step.count > 0) {
                PushUndoStep(std::move(step));
                MarkUnsavedChanges();
            }
        }
    }

    void SelectEntity(const ID entityID) {
        Level& level = LevelManager::CurrentLevel();

        Entity* entity = level.GetEntity(entityID);
        if (entity == nullptr) return;

        selectedEntity = *entity;
        selectedEntities.assign(1, entityID);
    }

    void ToggleEntitySelection(const ID entityID) {
        Level& level = LevelManager::CurrentLevel();

        Entity* entity = level.GetEntity(entityID);
        if (entity == nullptr) return;

        const auto it = std::find(selectedEntities.begin(), selectedEntities.end(), entityID);

        if (it == selectedEntities.end()) {
            selectedEntities.push_back(entityID);
            selectedEntity = *entity;
            return;
        }

        selectedEntities.erase(it);

        if (selectedEntity.id != entityID) return;

        if (selectedEntities.empty()) {
            editingEntity = false;
            return;
        }

        if (Entity* newPrimary = level.GetEntity(selectedEntities.back())) selectedEntity = *newPrimary;
    }

    void ExtendEntitySelectionTo(const ID entityID) {
        Level& level = LevelManager::CurrentLevel();

        Entity* entity = level.GetEntity(entityID);
        if (entity == nullptr) return;

        if (selectedEntities.empty()) {
            SelectEntity(entityID);
            return;
        }

        const ID firstID = std::min(entityID, selectedEntity.id);
        const ID lastID = std::max(entityID, selectedEntity.id);

        selectedEntities.clear();

        for (const Entity& rangeEntity : level.entities)
            if (rangeEntity.id >= firstID && rangeEntity.id <= lastID)
                selectedEntities.push_back(rangeEntity.id);

        selectedEntity = *entity;
    }

    void ClearEntitySelection() {
        selectedEntities.clear();
        editingEntity = false;
    }

    namespace {
        //  defensively drops any selection that no longer
        // resolves through its ID map (e.g. a sector deleted from outside
        // the normal DeleteSector() path, or any other desync).
        void ValidateSelections(Level& level) {
            // Sectors and entities get the same defensive pruning the wall
            // selection gets below: the plural lists can go stale from an
            // undo, a level load or a topology rebuild without anything
            // calling DeleteSector/DestroyEntity.
            for (int i = static_cast<int>(selectedSectors.size()) - 1; i >= 0; --i)
                if (level.sectorIDToIndex.find(selectedSectors[i]) == level.sectorIDToIndex.end())
                    selectedSectors.erase(selectedSectors.begin() + i);

            for (int i = static_cast<int>(selectedEntities.size()) - 1; i >= 0; --i)
                if (level.GetEntity(selectedEntities[i]) == nullptr)
                    selectedEntities.erase(selectedEntities.begin() + i);

            if (selectedSectorID != INVALID_ID &&
                level.sectorIDToIndex.find(selectedSectorID) == level.sectorIDToIndex.end()) {
                // Hand the primary to something still selected rather than
                // dropping the rest of the selection with it - same reseat
                // the wall block does.
                selectedSectorID = selectedSectors.empty() ? INVALID_ID : selectedSectors.front();
                editingSector = editingSector && selectedSectorID != INVALID_ID;
            }

            // Walls can disappear from under the Geometry Mode selection
            // without going through DeleteWall - an undo, a level load, or
            // a topology rebuild that merged or dropped them - so prune it
            // the same defensive way.
            for (int i = static_cast<int>(selectedWalls.size()) - 1; i >= 0; --i)
                if (level.wallIDToIndex.find(selectedWalls[i]) == level.wallIDToIndex.end())
                    selectedWalls.erase(selectedWalls.begin() + i);

            if (selectedWallID != INVALID_ID &&
                level.wallIDToIndex.find(selectedWallID) == level.wallIDToIndex.end()) {
                selectedWallID = selectedWalls.empty() ? INVALID_ID : selectedWalls.front();
                editingWall = selectedWallID != INVALID_ID;
            }

            // Same for linedefs (an undo can drop them too).
            std::erase_if(selectedLinedefs, [&level](const ID linedefID) { return level.GetLinedef(linedefID) == nullptr; });

            if (selectedLinedefID != INVALID_ID && level.GetLinedef(selectedLinedefID) == nullptr) {
                selectedLinedefID = selectedLinedefs.empty() ? INVALID_ID : selectedLinedefs.front();
                editingLinedef = selectedLinedefID != INVALID_ID;
            }
        }

        // "pressing F focuses/moves the editor camera to the
        // selected item if the current editor camera system supports that".
        // cameraPos is a plain mutable world-space position with no smooth
        // follow/animation system in this codebase, so "supports that" means
        // a direct snap - which is what this does.
        void FocusCameraOnSelection() {
            Level& level = LevelManager::CurrentLevel();

            if (currentMode == MODE_SECTOR && selectedSectorID != INVALID_ID) {
                const auto it = level.sectorIDToIndex.find(selectedSectorID);

                if (it != level.sectorIDToIndex.end()) {
                    const Sector& sector = level.sectors[it->second];

                    if (!sector.vertices.empty()) {
                        Vector2 sum{0.0f, 0.0f};

                        for (const Vector2& v : sector.vertices) {
                            sum.x += v.x;
                            sum.y += v.y;
                        }

                        const float n = static_cast<float>(sector.vertices.size());
                        cameraPos = {sum.x / n, sum.y / n};
                    }
                }

                return;
            }

            if (currentMode == MODE_GEOMETRY && linedefMode) {
                if (const Linedef* linedef = level.GetLinedef(selectedLinedefID))
                    cameraPos = (linedef->start + linedef->end) * 0.5f;

                return;
            }

            if (currentMode == MODE_GEOMETRY && selectedWallID != INVALID_ID) {
                const auto it = level.wallIDToIndex.find(selectedWallID);

                if (it != level.wallIDToIndex.end()) {
                    const Wall& wall = level.walls[it->second];

                    cameraPos = {
                        (wall.start.x + wall.end.x) * 0.5f,
                        (wall.start.y + wall.end.y) * 0.5f
                    };
                }

                return;
            }

            if (currentMode == MODE_ENTITY && editingEntity)
                if (const ComponentTransform* transform = level.transforms.Get(selectedEntity.id))
                    cameraPos = {transform->position.x, transform->position.z};
        }

        // Centres the camera on the middle of the map's bounding box (every
        // sector vertex, wall end and entity) and zooms so all of it fits.
        void FrameWholeMap() {
            const Level& level = LevelManager::CurrentLevel();

            Vector2 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            Vector2 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            bool any = false;

            const auto Include = [&](const float x, const float y) {
                min.x = std::min(min.x, x);
                min.y = std::min(min.y, y);
                max.x = std::max(max.x, x);
                max.y = std::max(max.y, y);
                any = true;
            };

            for (const Sector& sector : level.sectors)
                for (const Vector2& v : sector.vertices) Include(v.x, v.y);

            for (const Wall& wall : level.walls) {
                Include(wall.start.x, wall.start.y);
                Include(wall.end.x, wall.end.y);
            }

            for (const Linedef& linedef : level.linedefs) {
                Include(linedef.start.x, linedef.start.y);
                Include(linedef.end.x, linedef.end.y);
            }

            for (const ComponentTransform& transform : level.transforms.components)
                Include(transform.position.x, transform.position.z);

            if (!any) return;

            cameraPos = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};

            // Leave a margin so the edges aren't hidden under the panels.
            constexpr float FIT_MARGIN = 0.8f;
            const float width = std::max(max.x - min.x, 1.0f);
            const float height = std::max(max.y - min.y, 1.0f);
            const float fitZoom = std::min(static_cast<float>(screenWidth) / width,
                                           static_cast<float>(screenHeight) / height) * FIT_MARGIN;

            editorZoom = std::clamp(fitZoom, MIN_EDITOR_ZOOM, MAX_EDITOR_ZOOM);
        }

        // =====================================================================
        //  Geometry / Wall Edit Mode — pointer handling
        // =====================================================================
        //
        // Drag state lives here rather than in EditorState.cpp because
        // nothing outside this file needs it: drawing only needs to know
        // *that* a drag is running (draggingWallGeometry) and which wall is
        // hovered (hoveredWallID), and both of those are shared state.
        //
        // dragOriginalWalls/dragOriginalSectors are the wall and sector
        // lists as they were when the drag started. Every frame rewrites
        // the level from them (see ApplyGeometryDragOffset) instead of
        // nudging by a per-frame delta, so nothing drifts and a corner
        // shared by several walls can't end up half-moved.
        //
        // The sectors are captured for the same reason: a move edits the
        // existing sectors in place so they keep their textures, heights,
        // light and IDs, which means the drag needs their pre-drag
        // boundaries to recompute from.
        std::vector<Wall> dragOriginalWalls;
        std::vector<Sector> dragOriginalSectors;
        std::vector<Vector2> dragMovingPoints;
        std::vector<ID> dragSnapIgnoredWalls;

        Vector2 dragAnchorOrigin{}; // the point whose snapped position defines the drag's offset
        Vector2 dragGrabOffset{};   // mouseWorld - anchor at press, so a body drag doesn't jump on grab
        Vector2 dragDelta{};        // offset the last UpdateWallDrag applied to dragMovingPoints

        GeometrySnapshot dragUndoSnapshot;
        bool dragMutatedGeometry = false;

        // Entities inside the sectors the drag moves whole, with where they
        // were when it started (Sector Drag Carries Entities).
        EntityTransformUndo dragCarriedEntities;

        // Every entity standing in a sector all of whose corners are moving,
        // so the drag picks the whole sector up. Snapped entities stay on
        // their wall.
        EntityTransformUndo FindCarriedEntities(const Level& level, const std::vector<Vector2>& movingPoints) {
            EntityTransformUndo carried;
            if (!dragCarriesEntities) return carried;

            const auto isMoving = [&movingPoints](const Vector2& point) {
                return std::ranges::any_of(movingPoints, [&point](const Vector2& moving) { return SamePoint(moving, point); });
            };

            std::vector<int> movedSectors;

            for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i) {
                const Sector& sector = level.sectors[i];
                if (sector.vertices.empty() || !std::ranges::all_of(sector.vertices, isMoving)) continue;

                const bool innerLoopsMove = std::ranges::all_of(sector.innerLoops, [&isMoving](const std::vector<Vector2>& loop) {
                    return std::ranges::all_of(loop, isMoving);
                });

                if (innerLoopsMove) movedSectors.push_back(i);
            }

            if (movedSectors.empty()) return carried;

            for (const ComponentTransform& transform : level.transforms.components) {
                if (level.wallSnaps.contains(transform.ownerID)) continue;

                const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, {transform.position.x, transform.position.z});

                if (std::ranges::find(movedSectors, sectorIndex) != movedSectors.end())
                    carried.positions.emplace_back(transform.ownerID, transform.position);
            }

            return carried;
        }

        void BeginWallDrag(const Vector2& mouseWorld,
                           const std::vector<Vector2>& movingPoints,
                           const Vector2& anchor,
                           const bool keepGrabOffset) {
            const Level& level = LevelManager::CurrentLevel();

            dragOriginalWalls = level.walls;
            dragOriginalSectors = level.sectors;
            dragMovingPoints = movingPoints;

            // Anything this drag is deforming has to stop being a snap
            // target for the drag itself, or the moving point snaps
            // straight back onto its own starting position.
            dragSnapIgnoredWalls.clear();

            for (const Wall& wall : level.walls) {
                for (const Vector2& movingPoint : movingPoints) {
                    if (!SamePoint(wall.start, movingPoint) && !SamePoint(wall.end, movingPoint)) continue;

                    dragSnapIgnoredWalls.push_back(wall.id);
                    break;
                }
            }

            dragAnchorOrigin = anchor;

            // Endpoint drags put the handle exactly where the (snapped)
            // cursor is; body drags keep the offset the wall was grabbed
            // at, so it doesn't jump to centre itself on the pointer.
            dragGrabOffset = keepGrabOffset
                                 ? Vector2{mouseWorld.x - anchor.x, mouseWorld.y - anchor.y}
                                 : Vector2{0.0f, 0.0f};

            dragDelta = {0.0f, 0.0f};

            dragUndoSnapshot = CaptureGeometryUndoSnapshot();
            dragMutatedGeometry = false;

            dragCarriedEntities = FindCarriedEntities(level, movingPoints);

            draggingWallGeometry = true;
        }

        void UpdateWallDrag(const Vector2& mouseWorld) {
            const Vector2 desired = {
                mouseWorld.x - dragGrabOffset.x,
                mouseWorld.y - dragGrabOffset.y
            };

            // Same grid/vertex snapping settings every other placement in
            // the editor obeys - ResolveSnapPoint's usual behaviour, minus
            // the geometry this drag is moving.
            const Vector2 target = ResolveSnapPointExcluding(desired, dragMovingPoints, dragSnapIgnoredWalls);

            const Vector2 delta = {
                target.x - dragAnchorOrigin.x,
                target.y - dragAnchorOrigin.y
            };

            // The undo entry is pushed lazily, on the first frame the drag
            // actually changes something, so a click that merely happens to
            // land on a wall doesn't litter the undo stack.
            if (!dragMutatedGeometry && (delta.x != 0.0f || delta.y != 0.0f)) {
                PushGeometryUndoSnapshot(dragUndoSnapshot, "editor.history.move_points", static_cast<int>(dragMovingPoints.size()));
                if (!dragCarriedEntities.positions.empty()) undoSteps.back().carriedEntities = dragCarriedEntities;
                dragMutatedGeometry = true;
            }

            ApplyGeometryDragOffset(dragOriginalWalls, dragOriginalSectors, dragMovingPoints, delta);
            dragDelta = delta;

            Level& level = LevelManager::CurrentLevel();

            for (const auto& [entityID, start] : dragCarriedEntities.positions) {
                ComponentTransform* transform = level.transforms.Get(entityID);
                if (transform == nullptr) continue;

                transform->SetPosition({start.x + delta.x, start.y, start.z + delta.y});
                transform->isDirty = true;
            }
        }

        // Commits whatever the drag ended up doing. Called on mouse-up, and
        // also when ImGui takes the mouse mid-drag: the walls have already
        // visibly moved by then, so finishing is far less surprising than
        // silently reverting.
        void EndWallDrag() {
            if (!draggingWallGeometry) return;

            draggingWallGeometry = false;

            std::vector<Vector2> movedPoints;
            movedPoints.reserve(dragMovingPoints.size());

            for (const Vector2& point : dragMovingPoints)
                movedPoints.push_back({point.x + dragDelta.x, point.y + dragDelta.y});

            const std::vector<ID> draggedWallIDs = dragSnapIgnoredWalls;

            dragOriginalWalls.clear();
            dragOriginalSectors.clear();
            dragMovingPoints.clear();
            dragSnapIgnoredWalls.clear();
            dragCarriedEntities = {};

            if (!dragMutatedGeometry) return;

            dragMutatedGeometry = false;

            // A plain move keeps the sectors the drag already moved and
            // re-triangulated as they are; only a drop onto other walls
            // re-derives them, since that's what changes which sector
            // borders which wall.
            RebuildGeometryAfterWallDrag(movedPoints, draggedWallIDs);
        }

        // Puts the walls (and the entities they carried) back where the drag
        // found them and takes its undo step back off - for a wall dropped
        // onto a field, which was assigning it, not moving it.
        void CancelWallDrag() {
            if (!draggingWallGeometry) return;

            draggingWallGeometry = false;

            if (dragMutatedGeometry) {
                ApplyGeometryDragOffset(dragOriginalWalls, dragOriginalSectors, dragMovingPoints, {0.0f, 0.0f});

                Level& level = LevelManager::CurrentLevel();

                for (const auto& [entityID, start] : dragCarriedEntities.positions) {
                    ComponentTransform* transform = level.transforms.Get(entityID);
                    if (transform == nullptr) continue;

                    transform->SetPosition(start);
                    transform->isDirty = true;
                }

                if (!undoSteps.empty()) {
                    undoSteps.pop_back();
                    ++undoHistoryVersion;
                }
            }

            dragOriginalWalls.clear();
            dragOriginalSectors.clear();
            dragMovingPoints.clear();
            dragSnapIgnoredWalls.clear();
            dragCarriedEntities = {};
            dragMutatedGeometry = false;
            dragDelta = {0.0f, 0.0f};
        }

        // ---- Linedef drag -------------------------------------------------
        // A body drag moves both ends of every selected linedef; an endpoint
        // drag moves just that one end. Like the wall drag, each frame is
        // rewritten from the linedefs as they were when the drag started.
        struct LinedefDragPart {
            Linedef original;
            bool moveStart = false;
            bool moveEnd = false;
        };

        std::vector<LinedefDragPart> linedefDragParts;
        std::vector<ID> linedefDragIDs;  // the dragged linedefs, ignored by the snap

        Vector2 linedefDragAnchorOrigin{};
        Vector2 linedefDragGrabOffset{};

        GeometrySnapshot linedefDragUndoSnapshot;
        bool linedefDragMutated = false;

        void BeginLinedefDrag(const Vector2& mouseWorld, std::vector<LinedefDragPart> parts,
                              const Vector2& anchor, const bool keepGrabOffset) {
            linedefDragParts = std::move(parts);

            linedefDragIDs.clear();
            for (const LinedefDragPart& part : linedefDragParts) linedefDragIDs.push_back(part.original.id);

            linedefDragAnchorOrigin = anchor;
            linedefDragGrabOffset = keepGrabOffset
                                        ? Vector2{mouseWorld.x - anchor.x, mouseWorld.y - anchor.y}
                                        : Vector2{0.0f, 0.0f};

            linedefDragUndoSnapshot = CaptureGeometryUndoSnapshot();
            linedefDragMutated = false;

            draggingLinedefGeometry = true;
        }

        void UpdateLinedefDrag(const Vector2& mouseWorld) {
            const Vector2 desired = {
                mouseWorld.x - linedefDragGrabOffset.x,
                mouseWorld.y - linedefDragGrabOffset.y
            };

            const Vector2 target = ResolveLinedefSnapPoint(desired, linedefDragIDs);
            const Vector2 delta = {target.x - linedefDragAnchorOrigin.x, target.y - linedefDragAnchorOrigin.y};

            // Pushed lazily, so a click that doesn't move anything leaves no step.
            if (!linedefDragMutated && (delta.x != 0.0f || delta.y != 0.0f)) {
                PushGeometryUndoSnapshot(linedefDragUndoSnapshot, "editor.history.move_linedef_points", static_cast<int>(linedefDragParts.size()));
                linedefDragMutated = true;
            }

            Level& level = LevelManager::CurrentLevel();

            for (const LinedefDragPart& part : linedefDragParts) {
                Linedef* linedef = level.GetLinedef(part.original.id);
                if (linedef == nullptr) continue;

                linedef->start = part.moveStart ? part.original.start + delta : part.original.start;
                linedef->end = part.moveEnd ? part.original.end + delta : part.original.end;
                linedef->RefreshDerived();
            }
        }

        void EndLinedefDrag() {
            if (!draggingLinedefGeometry) return;

            draggingLinedefGeometry = false;
            linedefDragParts.clear();
            linedefDragIDs.clear();
            linedefDragMutated = false;
        }

        // The distinct endpoint positions of the current selection. A
        // corner shared by two selected walls must appear once, or it gets
        // offset twice and the walls tear apart.
        std::vector<Vector2> UniqueEndpointsOfSelection() {
            const Level& level = LevelManager::CurrentLevel();

            std::vector<Vector2> points;

            const auto add = [&](const Vector2& point) {
                for (const Vector2& existing : points)
                    if (SamePoint(existing, point)) return;

                points.push_back(point);
            };

            for (const ID wallID : selectedWalls) {
                const auto it = level.wallIDToIndex.find(wallID);
                if (it == level.wallIDToIndex.end()) continue;

                add(level.walls[it->second].start);
                add(level.walls[it->second].end);
            }

            return points;
        }

        bool MultiSelectModifierHeld() {
            return GetKeyBind(KEYBIND_MULTI_SELECT);
        }

        bool RangeSelectModifierHeld() {
            return GetKeyBind(KEYBIND_RANGE_SELECT);
        }

        bool SplitModifierHeld() {
            return GetKeyBind(KEYBIND_SPLIT_WALL);
        }

        // Offsets every selected entity except the primary. The primary has
        // already been snapped to the cursor by the drag path below, so the
        // rest just needs the same delta - copying the primary's position
        // onto them would stack the whole selection on one point.
        void MoveSelectedEntitiesBy(Level& level, const Vector3& delta, const ID primaryID) {
            if (selectedEntities.size() <= 1) return;
            if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f) return;

            for (const ID entityID : selectedEntities) {
                if (entityID == primaryID) continue;

                ComponentTransform* transform = level.transforms.Get(entityID);
                if (transform == nullptr) continue;

                transform->SetPosition({
                    transform->position.x + delta.x,
                    transform->position.y + delta.y,
                    transform->position.z + delta.z
                });

                transform->isDirty = true;
            }
        }

        // ---- Canvas reference drag ----------------------------------------
        // A left press on a wall, or on empty canvas inside a sector, can end
        // on a panel instead of the canvas: then it is a drag of that wall or
        // sector onto a Wall/Sector field (SubmitCanvasRefDragSource). The
        // press still does its usual thing on the way - moves the wall,
        // box-selects, starts a drawing - and a drop takes that back off
        // (RevertCanvasRefDrag), the same way an entity drag works.

        enum class CanvasPressAction { WALL_DRAG, BOX_SELECT, SECTOR_DRAWING };

        struct SelectionSnapshot {
            std::vector<ID> walls;
            std::vector<ID> sectors;
            std::vector<ID> linedefs;
            std::vector<ID> entities;
            ID wallID = INVALID_ID;
            ID sectorID = INVALID_ID;
            ID linedefID = INVALID_ID;
            Entity entity;
            bool editingWall = false;
            bool editingSector = false;
            bool editingLinedef = false;
            bool editingEntity = false;
            bool drawingLinedef = false;
        };

        SelectionSnapshot CaptureSelection() {
            return {selectedWalls, selectedSectors, selectedLinedefs, selectedEntities,
                    selectedWallID, selectedSectorID, selectedLinedefID, selectedEntity,
                    editingWall, editingSector, editingLinedef, editingEntity, drawingLinedef};
        }

        void RestoreSelection(const SelectionSnapshot& snapshot) {
            selectedWalls = snapshot.walls;
            selectedSectors = snapshot.sectors;
            selectedLinedefs = snapshot.linedefs;
            selectedEntities = snapshot.entities;
            selectedWallID = snapshot.wallID;
            selectedSectorID = snapshot.sectorID;
            selectedLinedefID = snapshot.linedefID;
            selectedEntity = snapshot.entity;
            editingWall = snapshot.editingWall;
            editingSector = snapshot.editingSector;
            editingLinedef = snapshot.editingLinedef;
            editingEntity = snapshot.editingEntity;
            drawingLinedef = snapshot.drawingLinedef;
        }

        struct CanvasRefPress {
            bool active = false;
            const char* payloadType = nullptr;
            ID id = INVALID_ID;
            CanvasPressAction action = CanvasPressAction::BOX_SELECT;

            // The selection (and with it the inspector) as it was before the
            // press, put back by a drop.
            SelectionSnapshot selectionBefore;

            // The last UI pass made this press an ImGui drag source.
            bool sourceSubmitted = false;

            // Released over a panel: the drop, if any, is delivered in this
            // frame's UI pass, so the press's own release (ending the wall
            // move, applying the box) waits until the next frame.
            bool releasePending = false;
        };

        CanvasRefPress canvasRefPress;

        void BeginCanvasRefPress(const CanvasPressAction action, const char* payloadType, const ID id,
                                 SelectionSnapshot selectionBefore) {
            if (id == INVALID_ID) return;

            canvasRefPress.active = true;
            canvasRefPress.payloadType = payloadType;
            canvasRefPress.id = id;
            canvasRefPress.action = action;
            canvasRefPress.selectionBefore = std::move(selectionBefore);
        }

        // The sector containing `point`, or INVALID_ID.
        ID SectorIDAt(const Vector2& point) {
            const Level& level = LevelManager::CurrentLevel();

            const int index = MapQueries::FindSectorContainingPoint(level.sectors, point, -1);
            return index >= 0 ? level.sectors[index].id : INVALID_ID;
        }

        // ---- Box selection --------------------------------------------------
        // A left drag that starts on empty canvas in Geometry or Entity Mode
        // draws a box; on release everything it touches is selected - walls
        // that cross or sit in it, entities whose footprint overlaps it. A
        // plain box replaces the selection, Ctrl on release adds to it. A
        // press that never drags still just clears the selection, as before.

        constexpr float BOX_SELECT_DRAG_THRESHOLD = 4.0f; // screen pixels

        bool boxSelectPending = false;  // left is down after an empty-canvas press
        bool boxSelectDragged = false;  // ...and has moved past the threshold
        Mode boxSelectMode = MODE_GEOMETRY;
        Vector2 boxSelectStartWorld{};
        Vector2 boxSelectStartScreen{};

        void BeginBoxSelect(const Vector2& mouseWorld) {
            boxSelectPending = true;
            boxSelectDragged = false;
            boxSelectMode = currentMode;
            boxSelectStartWorld = mouseWorld;
            boxSelectStartScreen = InputManager::GetMousePosition();

            // Carried onto a panel, the press is a drag of the wall under it
            // (Entity Mode and Linedef Mode, where walls aren't picked) or
            // else the sector it is inside.
            const ID wallID = PickWallAt(mouseWorld);

            if (wallID != INVALID_ID) BeginCanvasRefPress(CanvasPressAction::BOX_SELECT, WALL_REF_PAYLOAD, wallID, CaptureSelection());
            else BeginCanvasRefPress(CanvasPressAction::BOX_SELECT, SECTOR_REF_PAYLOAD, SectorIDAt(mouseWorld), CaptureSelection());
        }

        // Liang-Barsky: does any part of segment a-b lie in the box?
        bool SegmentTouchesBox(const Vector2& a, const Vector2& b, const Vector2& boxMin, const Vector2& boxMax) {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;

            const float p[4] = {-dx, dx, -dy, dy};
            const float q[4] = {a.x - boxMin.x, boxMax.x - a.x, a.y - boxMin.y, boxMax.y - a.y};

            float t0 = 0.0f;
            float t1 = 1.0f;

            for (int i = 0; i < 4; ++i) {
                if (p[i] == 0.0f) {
                    if (q[i] < 0.0f) return false; // parallel to this edge and outside it
                    continue;
                }

                const float t = q[i] / p[i];

                if (p[i] < 0.0f) {
                    if (t > t1) return false;
                    t0 = std::max(t0, t);
                }
                else {
                    if (t < t0) return false;
                    t1 = std::min(t1, t);
                }
            }

            return true;
        }

        void ClearSelectionForMode(const Mode mode) {
            if (mode == MODE_GEOMETRY && linedefMode) ClearLinedefSelection();
            else if (mode == MODE_GEOMETRY) ClearWallSelection();
            else if (mode == MODE_ENTITY) ClearEntitySelection();
        }

        void ApplyBoxSelection(const Vector2& boxMin, const Vector2& boxMax, const bool additive) {
            Level& level = LevelManager::CurrentLevel();

            if (!additive) ClearSelectionForMode(boxSelectMode);

            if (boxSelectMode == MODE_GEOMETRY && linedefMode) {
                for (const Linedef& linedef : level.linedefs) {
                    if (!SegmentTouchesBox(linedef.start, linedef.end, boxMin, boxMax)) continue;
                    if (std::ranges::find(selectedLinedefs, linedef.id) != selectedLinedefs.end()) continue;

                    selectedLinedefs.push_back(linedef.id);
                    selectedLinedefID = linedef.id;
                }

                editingLinedef = false;
                return;
            }

            if (boxSelectMode == MODE_GEOMETRY) {
                for (const Wall& wall : level.walls) {
                    if (!SegmentTouchesBox(wall.start, wall.end, boxMin, boxMax)) continue;
                    if (std::find(selectedWalls.begin(), selectedWalls.end(), wall.id) != selectedWalls.end()) continue;

                    selectedWalls.push_back(wall.id);
                    selectedWallID = wall.id;
                }

                // Like a left click: selects, the inspector stays closed
                // until a wall is right clicked.
                editingWall = false;
                return;
            }

            if (boxSelectMode == MODE_ENTITY) {
                for (Entity& entity : level.entities) {
                    // Filtered-out entities can't be clicked either; UI
                    // entities live in the UI Editor's space, not this one.
                    if (!EntityPassesFilter(entity)) continue;

                    const ComponentTransform* transform = level.transforms.Get(entity.id);
                    if (transform == nullptr) continue;

                    // The same footprint EntityAt picks with.
                    const float halfX = transform->scale.x * 0.5f;
                    const float halfZ = transform->scale.z * 0.5f;

                    const bool overlaps =
                        transform->position.x - halfX <= boxMax.x && transform->position.x + halfX >= boxMin.x &&
                        transform->position.z - halfZ <= boxMax.y && transform->position.z + halfZ >= boxMin.y;

                    if (!overlaps) continue;
                    if (std::find(selectedEntities.begin(), selectedEntities.end(), entity.id) != selectedEntities.end()) continue;

                    selectedEntities.push_back(entity.id);
                    selectedEntity = entity;
                }
            }
        }

        // Every frame, ImGui or not, so a release over a panel still ends it.
        void UpdateBoxSelect(const Vector2& mouseWorld) {
            if (!boxSelectPending) return;

            if (currentMode != boxSelectMode || InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) {
                boxSelectPending = false;
                return;
            }

            const Vector2 mouseScreen = InputManager::GetMousePosition();

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) {
                const float dx = mouseScreen.x - boxSelectStartScreen.x;
                const float dy = mouseScreen.y - boxSelectStartScreen.y;

                if (dx * dx + dy * dy > BOX_SELECT_DRAG_THRESHOLD * BOX_SELECT_DRAG_THRESHOLD) boxSelectDragged = true;
                return;
            }

            boxSelectPending = false;

            if (!boxSelectDragged) {
                ClearSelectionForMode(boxSelectMode);

                // In Linedef Mode a click on empty canvas places the start
                // of a new linedef; the next click places its end.
                if (boxSelectMode == MODE_GEOMETRY && linedefMode) {
                    linedefDrawStart = ResolveLinedefSnapPoint(boxSelectStartWorld, {});
                    drawingLinedef = true;
                }

                return;
            }

            const Vector2 boxMin = {std::min(boxSelectStartWorld.x, mouseWorld.x), std::min(boxSelectStartWorld.y, mouseWorld.y)};
            const Vector2 boxMax = {std::max(boxSelectStartWorld.x, mouseWorld.x), std::max(boxSelectStartWorld.y, mouseWorld.y)};

            ApplyBoxSelection(boxMin, boxMax, MultiSelectModifierHeld());
        }

        // Linedef Mode's pointer story: the wall one below, for linedefs,
        // plus drawing. Only linedefs can be picked here.
        void HandleLinedefModeMouse(const Vector2 &mouseWorld) {
            const Level &level = LevelManager::CurrentLevel();

            ID endpointLinedefID = INVALID_ID;
            bool endpointIsStart = false;
            const bool overEndpoint = !drawingLinedef &&
                                      PickSelectedLinedefEndpointAt(mouseWorld, &endpointLinedefID, &endpointIsStart);

            const ID linedefUnderCursor = PickLinedefAt(mouseWorld);

            // The end of an unselected linedef continues from it rather
            // than picking the line (its end square is highlighted instead).
            Vector2 unselectedEnd{};
            const bool overUnselectedEnd = !drawingLinedef && !overEndpoint &&
                                           PickUnselectedLinedefEndpointAt(mouseWorld, &unselectedEnd);

            if (!draggingLinedefGeometry)
                hoveredLinedefID = drawingLinedef || overUnselectedEnd ? INVALID_ID : linedefUnderCursor;

            // Right click (dropping a half-drawn linedef, or opening what is
            // under the cursor) is handled in HandleEditorInput, for every mode.

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                // While a linedef is half drawn every click places its end.
                // A click on its own start does nothing.
                if (drawingLinedef) {
                    if (AddLinedef(linedefDrawStart, ResolveLinedefSnapPoint(mouseWorld, {}))) drawingLinedef = false;
                    return;
                }

                if (SplitModifierHeld()) {
                    const Linedef* linedef = level.GetLinedef(linedefUnderCursor);
                    if (linedef == nullptr) return;

                    Vector2 point = MapTopology::ClosestPointOnSegment(mouseWorld, linedef->start, linedef->end);

                    if (gridSnapEnabled && !GetKeyBind(KEYBIND_DISABLE_SNAP))
                        point = MapTopology::ClosestPointOnSegment(SnapToGrid(point), linedef->start, linedef->end);

                    if (!SplitLinedefAt(linedefUnderCursor, point)) lastGeometryError = Localisation::Get("editor.geometry.split_too_close");
                    return;
                }

                // Endpoint handles win over the line body, as with walls.
                if (overEndpoint) {
                    if (const Linedef* linedef = level.GetLinedef(endpointLinedefID)) {
                        const Vector2 endpoint = endpointIsStart ? linedef->start : linedef->end;

                        BeginLinedefDrag(mouseWorld, {{*linedef, endpointIsStart, !endpointIsStart}}, endpoint, false);
                        return;
                    }
                }

                // An unselected linedef's end: a new linedef starts exactly
                // there, like a click on empty space.
                if (overUnselectedEnd) {
                    ClearLinedefSelection();
                    linedefDrawStart = unselectedEnd;
                    drawingLinedef = true;
                    return;
                }

                // Empty space: a drag box-selects, a click starts a linedef
                // (see UpdateBoxSelect).
                if (linedefUnderCursor == INVALID_ID) {
                    BeginBoxSelect(mouseWorld);
                    return;
                }

                if (MultiSelectModifierHeld()) {
                    ToggleLinedefSelection(linedefUnderCursor);
                    return;
                }

                if (RangeSelectModifierHeld()) {
                    ExtendLinedefSelectionTo(linedefUnderCursor);
                    return;
                }

                const bool alreadyInSelection =
                        std::ranges::find(selectedLinedefs, linedefUnderCursor) != selectedLinedefs.end();

                if (!alreadyInSelection || selectedLinedefs.size() <= 1) SelectLinedef(linedefUnderCursor);
                else selectedLinedefID = linedefUnderCursor;

                // Left click is move-only: the inspector opens on right click.
                editingLinedef = false;

                const Linedef* grabbed = level.GetLinedef(linedefUnderCursor);
                if (grabbed == nullptr) return;

                std::vector<LinedefDragPart> parts;
                for (const ID linedefID : selectedLinedefs)
                    if (const Linedef* linedef = level.GetLinedef(linedefID)) parts.push_back({*linedef, true, true});

                BeginLinedefDrag(mouseWorld, std::move(parts), grabbed->start, true);
                return;
            }

            if (!draggingLinedefGeometry) return;

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) UpdateLinedefDrag(mouseWorld);
            else EndLinedefDrag();
        }

        // Geometry Mode's whole pointer story: hover, select, drag, release.
        // Sectors are never touched here - the moved walls are handed back
        // to the topology pass on release, and it re-derives whatever
        // sectors those walls bounded.
        void HandleGeometryModeMouse(const Vector2 &mouseWorld) {
            if (linedefMode) {
                HandleLinedefModeMouse(mouseWorld);
                return;
            }

            const Level &level = LevelManager::CurrentLevel();

            ID endpointWallID = INVALID_ID;
            bool endpointIsStart = false;
            const bool overEndpoint = PickSelectedWallEndpointAt(mouseWorld, &endpointWallID, &endpointIsStart);

            const ID wallUnderCursor = PickWallAt(mouseWorld);

            if (!draggingWallGeometry) hoveredWallID = wallUnderCursor;

            // Right click is handled in HandleEditorInput, for every mode.

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                // For a wall drag that ends on a field instead (see
                // BeginCanvasRefPress).
                SelectionSnapshot selectionBefore = CaptureSelection();

                // Alt+click splits the wall under the cursor at that point,
                // snapped along the wall to the grid unless Ctrl is held.
                if (SplitModifierHeld()) {
                    const auto it = wallUnderCursor != INVALID_ID ? level.wallIDToIndex.find(wallUnderCursor) : level.wallIDToIndex.end();
                    if (it == level.wallIDToIndex.end()) return;

                    const Wall &wall = level.walls[it->second];
                    Vector2 point = MapTopology::ClosestPointOnSegment(mouseWorld, wall.start, wall.end);

                    if (gridSnapEnabled && !GetKeyBind(KEYBIND_DISABLE_SNAP))
                        point = MapTopology::ClosestPointOnSegment(SnapToGrid(point), wall.start, wall.end);

                    if (!SplitWallAt(wallUnderCursor, point)) lastGeometryError = Localisation::Get("editor.geometry.split_too_close");
                    return;
                }

                // Endpoint handles win over the wall body whenever the
                // cursor is near both, so grabbing the corner of a short
                // wall is never a coin flip.
                if (overEndpoint) {
                    const auto it = level.wallIDToIndex.find(endpointWallID);

                    if (it != level.wallIDToIndex.end()) {
                        const Wall &wall = level.walls[it->second];
                        const Vector2 endpoint = endpointIsStart ? wall.start : wall.end;

                        BeginWallDrag(mouseWorld, {endpoint}, endpoint, false);
                        BeginCanvasRefPress(CanvasPressAction::WALL_DRAG, WALL_REF_PAYLOAD, endpointWallID, std::move(selectionBefore));
                        return;
                    }
                }

                // Empty space starts a box selection. Released without a
                // drag, it clears the selection (and the wall inspector).
                if (wallUnderCursor == INVALID_ID) {
                    BeginBoxSelect(mouseWorld);
                    return;
                }

                // Modifier clicks build a selection; they never start a
                // drag, so multi-selecting can't nudge geometry by accident.
                if (MultiSelectModifierHeld()) {
                    ToggleWallSelection(wallUnderCursor);
                    return;
                }

                if (RangeSelectModifierHeld()) {
                    ExtendWallSelectionTo(wallUnderCursor);
                    return;
                }

                const bool alreadyInSelection =
                        std::find(selectedWalls.begin(), selectedWalls.end(), wallUnderCursor) != selectedWalls.end();

                // Clicking a wall that is already part of a multi-selection
                // keeps that selection and drags all of it; clicking
                // anything else selects just that wall.
                if (!alreadyInSelection || selectedWalls.size() <= 1) SelectWall(wallUnderCursor);
                else selectedWallID = wallUnderCursor;

                // Left click is move-only: selection changes, the inspector
                // stays closed until the wall is right clicked.
                editingWall = false;

                const auto it = level.wallIDToIndex.find(wallUnderCursor);
                if (it == level.wallIDToIndex.end()) return;

                BeginWallDrag(mouseWorld, UniqueEndpointsOfSelection(), level.walls[it->second].start, true);
                BeginCanvasRefPress(CanvasPressAction::WALL_DRAG, WALL_REF_PAYLOAD, wallUnderCursor, std::move(selectionBefore));
                return;
            }

            if (!draggingWallGeometry) return;

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) UpdateWallDrag(mouseWorld);
            else EndWallDrag();
        }
    }

    void SubmitCanvasEntityDragSource() {
        if (!holdingEntity || currentMode != MODE_ENTITY) return;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

        // Only become a drag-and-drop source while the cursor is over a
        // panel. On the canvas this is an ordinary entity move, and an
        // active extern source would also claim ImGui's active ID (and with
        // it the keyboard, blocking WASD) for no reason. ImGui itself stops
        // reporting hovered windows for a press that started outside them,
        // so the hit test has to be asked for directly.
        ImGuiWindow* hoveredWindow = nullptr;
        ImGuiWindow* hoveredWindowUnderMoving = nullptr;
        ImGui::FindHoveredWindowEx(ImGui::GetIO().MousePos, false, &hoveredWindow, &hoveredWindowUnderMoving);
        if (hoveredWindow == nullptr) return;

        const Entity* entity = LevelManager::CurrentLevel().GetEntity(selectedEntity.id);
        if (entity == nullptr) return;

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
            const LevelObjectDragPayload payload{entity->id, true};
            ImGui::SetDragDropPayload(ENTITY_REF_PAYLOAD, &payload, sizeof(payload));

            ImGui::Text("%s  (#%u)", entity->name.c_str(), static_cast<unsigned>(entity->id));
            ImGui::EndDragDropSource();
        }
    }

    void RevertCanvasEntityDrag() {
        Level& level = LevelManager::CurrentLevel();

        if (canvasDragPushedUndo && !undoSteps.empty()) {
            undoSteps.pop_back();
            ++undoHistoryVersion;
        }
        canvasDragPushedUndo = false;

        for (const auto& [entityID, position] : canvasDragStartPositions) {
            ComponentTransform* transform = level.transforms.Get(entityID);
            if (transform == nullptr) continue;

            transform->SetPosition(position);
            transform->isDirty = true;
        }

        canvasDragStartPositions.clear();

        // The press that started the drag also selected the dragged entity.
        // Dropping it on a field means the user was assigning a reference,
        // not picking a new selection, so the inspector goes back to the
        // entity whose field was just filled.
        if (const Entity* previousPrimary = level.GetEntity(primaryBeforeCanvasDrag)) {
            selectedEntities = selectionBeforeCanvasDrag;
            selectedEntity = *previousPrimary;
        }

        holdingEntity = false;
        activeGizmoPart = GIZMO_NONE;
    }

    ID GetCanvasDragPinnedEntity() {
        return editingEntity ? inspectorPinnedDuringCanvasDrag : INVALID_ID;
    }

    void SubmitCanvasRefDragSource() {
        canvasRefPress.sourceSubmitted = false;

        if (!canvasRefPress.active || canvasRefPress.releasePending || holdingEntity) return;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

        // The press's own action has to still be running - a wall drag the
        // mode switch already committed, say, is no longer this press.
        const bool actionRunning =
            canvasRefPress.action == CanvasPressAction::WALL_DRAG ? draggingWallGeometry
            : canvasRefPress.action == CanvasPressAction::BOX_SELECT ? boxSelectPending
            : true;
        if (!actionRunning) return;

        // Only over a panel, as with entities - see SubmitCanvasEntityDragSource.
        ImGuiWindow* hoveredWindow = nullptr;
        ImGuiWindow* hoveredWindowUnderMoving = nullptr;
        ImGui::FindHoveredWindowEx(ImGui::GetIO().MousePos, false, &hoveredWindow, &hoveredWindowUnderMoving);
        if (hoveredWindow == nullptr) return;

        const Level& level = LevelManager::CurrentLevel();
        std::string label;

        if (canvasRefPress.payloadType == WALL_REF_PAYLOAD) {
            const auto it = level.wallIDToIndex.find(canvasRefPress.id);
            if (it == level.wallIDToIndex.end()) return;

            label = WallHierarchyLabel(level.walls[it->second]);
        }
        else {
            const Sector* sector = level.GetSector(canvasRefPress.id);
            if (sector == nullptr) return;

            label = SectorHierarchyLabel(*sector);
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
            const LevelObjectDragPayload payload{canvasRefPress.id, true};
            ImGui::SetDragDropPayload(canvasRefPress.payloadType, &payload, sizeof(payload));

            ImGui::TextUnformatted(label.c_str());
            ImGui::EndDragDropSource();

            canvasRefPress.sourceSubmitted = true;
        }
    }

    void RevertCanvasRefDrag() {
        if (!canvasRefPress.active) return;

        switch (canvasRefPress.action) {
            case CanvasPressAction::WALL_DRAG: CancelWallDrag(); break;
            case CanvasPressAction::BOX_SELECT: boxSelectPending = false; break;
            case CanvasPressAction::SECTOR_DRAWING: CancelActiveDrawing(); break;
        }

        RestoreSelection(canvasRefPress.selectionBefore);
        canvasRefPress = {};
    }

    void ClearSelectionsExcept(const EventOwnerKind keep) {
        if (keep != EventOwnerKind::LINE) ClearWallSelection();
        if (keep != EventOwnerKind::SECTOR) ClearSectorSelection();
        if (keep != EventOwnerKind::LINEDEF) ClearLinedefSelection();
        if (keep != EventOwnerKind::ENTITY) ClearEntitySelection();
    }

    void HandleCanvasRightClick(const Vector2& mouseWorld) {
        const bool toggle = MultiSelectModifierHeld();

        if (const Entity* entity = EntityAt(mouseWorld)) {
            const ID entityID = entity->id;
            ClearSelectionsExcept(EventOwnerKind::ENTITY);

            if (toggle) {
                ToggleEntitySelection(entityID);
                editingEntity = !selectedEntities.empty();
                return;
            }

            SelectEntity(entityID);
            editingEntity = true;
            return;
        }

        // Linedefs are drawn over the walls in Linedef Mode, so they win
        // there; anywhere else the wall does.
        const ID wallID = PickWallAt(mouseWorld);
        const ID linedefID = PickLinedefAt(mouseWorld);
        const bool pickLinedef = linedefID != INVALID_ID && (wallID == INVALID_ID || (currentMode == MODE_GEOMETRY && linedefMode));

        if (pickLinedef) {
            ClearSelectionsExcept(EventOwnerKind::LINEDEF);

            if (toggle) {
                ToggleLinedefSelection(linedefID);
                return;
            }

            // Right clicking a linedef that is already part of a
            // multi-selection keeps that selection, so the inspector edits
            // all of it.
            if (std::ranges::find(selectedLinedefs, linedefID) == selectedLinedefs.end()) SelectLinedef(linedefID);

            selectedLinedefID = linedefID;
            editingLinedef = true;
            return;
        }

        if (wallID != INVALID_ID) {
            ClearSelectionsExcept(EventOwnerKind::LINE);

            if (toggle) {
                ToggleWallSelection(wallID);
                return;
            }

            if (std::ranges::find(selectedWalls, wallID) == selectedWalls.end()) SelectWall(wallID);

            selectedWallID = wallID;
            editingWall = true;
            return;
        }

        if (const ID sectorID = SectorIDAt(mouseWorld); sectorID != INVALID_ID) {
            ClearSelectionsExcept(EventOwnerKind::SECTOR);

            if (toggle) {
                ToggleSectorSelection(sectorID);

                // Deselecting the last one already closed the inspector.
                if (selectedSectorID != INVALID_ID) editingSector = true;
                return;
            }

            SelectSector(sectorID);
            editingSector = true;
            return;
        }

        // Nothing there: the inspector closes, the selection stays.
        editingWall = false;
        editingSector = false;
        editingLinedef = false;
        editingEntity = false;
    }

    // Copies the clipboard's entities that still exist. The primary lands
    // under the cursor and the rest keep their offsets from it; heights are
    // kept. The copies become the selection.
    void PasteEntityClipboard(Level& level, const Vector2& mouseWorld, const char* labelKey) {
        std::erase_if(entityClipboard, [&level](const ID id) { return level.GetEntity(id) == nullptr; });
        if (entityClipboard.empty()) return;

        EntitiesSnapshot before = CaptureEntitiesSnapshot();
        WallSnap::Snaps snapsBefore = WallSnap::All(); // copies of snapped entities are snapped too

        // Offsets are measured from the first copied entity that has a transform.
        const ComponentTransform* anchor = nullptr;
        for (const ID id : entityClipboard)
            if ((anchor = level.transforms.Get(id)) != nullptr) break;

        const Vector2 offset = anchor != nullptr
            ? Vector2{mouseWorld.x - anchor->position.x, mouseWorld.y - anchor->position.z}
            : Vector2{0.0f, 0.0f};

        std::vector<ID> pasted;
        pasted.reserve(entityClipboard.size());

        for (const ID id : entityClipboard) {
            // CreateEntity can grow the entity list, so copy before calling it.
            Entity original = *level.GetEntity(id);
            const ID copyID = level.CreateEntity(original);

            if (ComponentTransform* transform = level.transforms.Get(copyID))
                transform->SetPosition({transform->position.x + offset.x, transform->position.y, transform->position.z + offset.y});

            pasted.push_back(copyID);
        }

        PushEntitiesUndo(std::move(before), labelKey, static_cast<int>(pasted.size()), std::move(snapsBefore));

        SelectEntity(pasted.front());
        selectedEntities = pasted;
    }

    void DuplicateSelectedEntities() {
        Level& level = LevelManager::CurrentLevel();
        if (selectedEntities.empty()) return;

        // Ctrl+C's order: the primary first, so it's the one placed at the target.
        std::vector<ID> clipboard;
        if (level.GetEntity(selectedEntity.id) != nullptr) clipboard.push_back(selectedEntity.id);

        for (const ID id : selectedEntities)
            if (id != selectedEntity.id) clipboard.push_back(id);

        const ComponentTransform* anchor = nullptr;
        for (const ID id : clipboard)
            if ((anchor = level.transforms.Get(id)) != nullptr) break;

        if (anchor == nullptr) return;

        const Vector2 target = {anchor->position.x + GRID_SIZE, anchor->position.z + GRID_SIZE};

        // Through the paste, without touching what Ctrl+C copied.
        std::swap(entityClipboard, clipboard);
        PasteEntityClipboard(level, target, "editor.history.duplicate_entities");
        std::swap(entityClipboard, clipboard);
    }

    // =========================================================================
    //  Entity Mode move gizmo
    // =========================================================================

    namespace {
        // All in screen pixels, so the gizmo is the same size at every zoom.
        constexpr float GIZMO_GAP = 4.0f; // between the entity's box and the gizmo
        constexpr float GIZMO_ARROW_LENGTH = 48.0f;
        constexpr float GIZMO_SQUARE_SIZE = 14.0f;
        constexpr float GIZMO_PICK_RADIUS = 6.0f;

        float DistanceToSegment(const Vector2& point, const Vector2& a, const Vector2& b) {
            const float abX = b.x - a.x;
            const float abY = b.y - a.y;
            const float lengthSq = abX * abX + abY * abY;

            float t = lengthSq > 0.0f ? ((point.x - a.x) * abX + (point.y - a.y) * abY) / lengthSq : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);

            const float dx = point.x - (a.x + abX * t);
            const float dy = point.y - (a.y + abY * t);
            return std::sqrt(dx * dx + dy * dy);
        }
    }

    bool GetEntityGizmoLayout(EntityGizmoLayout* layout) {
        if (currentMode != MODE_ENTITY || selectedEntities.empty()) return false;

        const ComponentTransform* transform = LevelManager::CurrentLevel().transforms.Get(selectedEntity.id);
        if (transform == nullptr) return false;

        const Vector2 origin = WorldToScreen({transform->position.x, transform->position.z}, cameraPos);
        const float start = ENTITY_SIZE * editorZoom * 0.5f + GIZMO_GAP;

        // World +Z is screen-down on this canvas, so the Z arrow points down.
        layout->xStart = {origin.x + start, origin.y};
        layout->xTip = {origin.x + start + GIZMO_ARROW_LENGTH, origin.y};
        layout->zStart = {origin.x, origin.y + start};
        layout->zTip = {origin.x, origin.y + start + GIZMO_ARROW_LENGTH};
        layout->square = {origin.x + start, origin.y + start, GIZMO_SQUARE_SIZE, GIZMO_SQUARE_SIZE};

        return true;
    }

    EntityGizmoPart PickEntityGizmoPart(const Vector2& mouseScreen) {
        EntityGizmoLayout layout;
        if (!GetEntityGizmoLayout(&layout)) return GIZMO_NONE;

        const SDL_FRect& square = layout.square;

        if (mouseScreen.x >= square.x && mouseScreen.x <= square.x + square.w &&
            mouseScreen.y >= square.y && mouseScreen.y <= square.y + square.h)
            return GIZMO_FREE;

        const float xDistance = DistanceToSegment(mouseScreen, layout.xStart, layout.xTip);
        const float zDistance = DistanceToSegment(mouseScreen, layout.zStart, layout.zTip);

        if (xDistance > GIZMO_PICK_RADIUS && zDistance > GIZMO_PICK_RADIUS) return GIZMO_NONE;

        return xDistance <= zDistance ? GIZMO_X : GIZMO_Z;
    }

    void DrawSelectionBox() {
        if (!boxSelectPending || !boxSelectDragged) return;

        // From where the drag started in the world (the camera can pan
        // mid-drag) to the cursor.
        const Vector2 start = WorldToScreen(boxSelectStartWorld, cameraPos);
        const Vector2 end = InputManager::GetMousePosition();

        const SDL_FRect box = {
            std::min(start.x, end.x),
            std::min(start.y, end.y),
            std::fabs(end.x - start.x),
            std::fabs(end.y - start.y)
        };

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(renderer, 90, 150, 255, 40);
        SDL_RenderFillRect(renderer, &box);

        SDL_SetRenderDrawColor(renderer, 90, 150, 255, 200);
        SDL_RenderRect(renderer, &box);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    EntityGizmoPart GetActiveEntityGizmoPart() {
        return holdingEntity ? activeGizmoPart : GIZMO_NONE;
    }

    // =========================================================================
    //  Texture copy/paste on the canvas - the runtime editor's shortcuts
    // =========================================================================
    //
    // Copy takes from what the cursor is on (Geometry and Sector Mode fall
    // back to the primary selection). Paste goes to the whole selection when
    // the cursor is on a selected item or on nothing, and to just the hovered
    // item otherwise. A sector's floor or ceiling is picked by the texture
    // view mode, Off counting as floor.

    namespace {
        // A wall copies its three textures as a set and a sprite its texture
        // for every angle. `primary` is the single texture other kinds of
        // target take from them.
        struct CanvasTextureClipboard {
            enum class Source { None, Wall, Flat, Sprite } source = Source::None;
            bool hasPlacement = false;

            WallSurface primary;
            std::array<WallSurface, WALL_SURFACE_SLOTS.size()> wallSurfaces;
            std::array<std::string, 8> spriteTextures;
            SideCount spriteSideCount = SIDECOUNT_SINGLE;
        };

        CanvasTextureClipboard canvasTextureClipboard;

        // The floor or ceiling the canvas is showing for this sector.
        SectorSurface* ShownFlat(Sector& sector) {
            if (sector.floors.empty()) return nullptr;

            SectorFloor& floor = sector.floors.front();
            return textureViewMode == TextureViewMode::Ceiling ? &floor.ceiling : &floor.floor;
        }

        int WallIndexOf(const Level& level, const ID wallID) {
            const auto it = level.wallIDToIndex.find(wallID);
            return it == level.wallIDToIndex.end() ? -1 : it->second;
        }

        int SectorIndexOf(const Level& level, const ID sectorID) {
            const auto it = level.sectorIDToIndex.find(sectorID);
            return it == level.sectorIDToIndex.end() ? -1 : it->second;
        }

        // The wall, sector or entity under the cursor in the current mode.
        ID HoveredCanvasItem(const Level& level, const Vector2& mouseWorld) {
            switch (currentMode) {
                case MODE_GEOMETRY:
                    // Linedefs have no textures, and walls can't be picked in Linedef Mode.
                    return linedefMode ? INVALID_ID : PickWallAt(mouseWorld);

                case MODE_SECTOR:
                    // Hole-aware, like the hover highlight.
                    for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i)
                        if (Geometry::IsPointInPolygon(level.sectors[i].vertices, level.sectors[i].innerLoops, mouseWorld))
                            return level.sectors[i].id;
                    return INVALID_ID;

                case MODE_ENTITY: {
                    const Entity* entity = EntityAt(mouseWorld);
                    return entity != nullptr && level.sprites.Get(entity->id) != nullptr ? entity->id : INVALID_ID;
                }

                default:
                    return INVALID_ID;
            }
        }

        const std::vector<ID>& CanvasSelection() {
            switch (currentMode) {
                case MODE_GEOMETRY: return selectedWalls;
                case MODE_SECTOR: return selectedSectors;
                default: return selectedEntities;
            }
        }

        template <typename Surface>
        void CopyPlacement(const Surface& from, WallSurface& to) {
            to.texture = from.texture;
            to.textureOffset = from.textureOffset;
            to.textureScale = from.textureScale;
            to.flipTextureX = from.flipTextureX;
            to.flipTextureY = from.flipTextureY;
        }

        bool CopyCanvasTexture(Level& level, const ID itemID, const bool withPlacement) {
            CanvasTextureClipboard copied;
            copied.hasPlacement = withPlacement;

            if (currentMode == MODE_GEOMETRY) {
                const int index = WallIndexOf(level, itemID);
                if (index < 0) return false;

                const Wall& wall = level.walls[index];
                bool anyTexture = false;

                for (size_t slot = 0; slot < WALL_SURFACE_SLOTS.size(); ++slot) {
                    copied.wallSurfaces[slot] = wall.Surface(WALL_SURFACE_SLOTS[slot]);
                    anyTexture = anyTexture || !copied.wallSurfaces[slot].texture.empty();
                }

                if (!anyTexture) return false;

                copied.source = CanvasTextureClipboard::Source::Wall;
                copied.primary = wall.top;
            }
            else if (currentMode == MODE_SECTOR) {
                const int index = SectorIndexOf(level, itemID);
                if (index < 0) return false;

                const SectorSurface* flat = ShownFlat(level.sectors[index]);
                if (flat == nullptr || flat->texture.empty()) return false;

                copied.source = CanvasTextureClipboard::Source::Flat;
                CopyPlacement(*flat, copied.primary);
            }
            else if (currentMode == MODE_ENTITY) {
                const ComponentSprite* sprite = level.sprites.Get(itemID);
                if (sprite == nullptr || std::ranges::all_of(sprite->textureFileNames, &std::string::empty)) return false;

                copied.source = CanvasTextureClipboard::Source::Sprite;
                copied.spriteTextures = sprite->textureFileNames;
                copied.spriteSideCount = sprite->sideCount;
                copied.primary.texture = sprite->textureFileNames[0];
            }
            else return false;

            canvasTextureClipboard = std::move(copied);
            return true;
        }

        // Texture, plus offset/scale/flip with `withPlacement`.
        template <typename Surface>
        void PastePrimary(Surface& surface, const bool withPlacement) {
            const WallSurface& primary = canvasTextureClipboard.primary;

            surface.texture = primary.texture;
            if (!withPlacement) return;

            surface.textureOffset = primary.textureOffset;
            surface.textureScale = primary.textureScale;
            surface.flipTextureX = primary.flipTextureX;
            surface.flipTextureY = primary.flipTextureY;
        }

        // Returns how many items changed.
        int PasteCanvasTexture(Level& level, const std::vector<ID>& targets, bool withPlacement) {
            const CanvasTextureClipboard& clipboard = canvasTextureClipboard;
            if (clipboard.source == CanvasTextureClipboard::Source::None || targets.empty()) return 0;

            withPlacement = withPlacement && clipboard.hasPlacement;
            const char* labelKey = withPlacement ? "runtime_editor.history.paste_texture_placement" : "runtime_editor.history.paste_texture";

            int changed = 0;

            if (currentMode == MODE_GEOMETRY || currentMode == MODE_SECTOR) {
                GeometrySnapshot before = CaptureGeometryUndoSnapshot();

                for (const ID id : targets) {
                    if (currentMode == MODE_GEOMETRY) {
                        const int index = WallIndexOf(level, id);
                        if (index < 0) continue;

                        Wall& wall = level.walls[index];

                        if (clipboard.source == CanvasTextureClipboard::Source::Wall) {
                            for (size_t slot = 0; slot < WALL_SURFACE_SLOTS.size(); ++slot) {
                                WallSurface& surface = wall.Surface(WALL_SURFACE_SLOTS[slot]);

                                if (withPlacement) surface = clipboard.wallSurfaces[slot];
                                else surface.texture = clipboard.wallSurfaces[slot].texture;
                            }
                        }
                        else PastePrimary(wall.top, withPlacement);
                    }
                    else {
                        const int index = SectorIndexOf(level, id);
                        if (index < 0) continue;

                        SectorSurface* flat = ShownFlat(level.sectors[index]);
                        if (flat == nullptr) continue;

                        PastePrimary(*flat, withPlacement);
                    }

                    ++changed;
                }

                if (changed > 0) PushGeometryUndoSnapshot(std::move(before), labelKey, changed);
                return changed;
            }

            EntitiesSnapshot before = CaptureEntitiesSnapshot();

            for (const ID id : targets) {
                ComponentSprite* sprite = level.sprites.Get(id);
                if (sprite == nullptr) continue;

                if (clipboard.source == CanvasTextureClipboard::Source::Sprite) {
                    sprite->textureFileNames = clipboard.spriteTextures;
                    sprite->sideCount = clipboard.spriteSideCount;
                }
                else sprite->textureFileNames[0] = clipboard.primary.texture;

                ++changed;
            }

            if (changed > 0) PushEntitiesUndo(std::move(before), labelKey, changed);
            return changed;
        }

        // True when it took the key press. In Entity Mode that is only while
        // the cursor is on an entity with a sprite, so Ctrl+C / Ctrl+V still
        // copy and paste entities everywhere else.
        bool HandleCanvasTextureShortcuts(Level& level, const Vector2& mouseWorld, const bool mouseBlockedByImGui) {
            const bool copyWithPlacement = GetComboDown(COMBO_RT_COPY_TEXTURE_PLACEMENT);
            const bool copy = copyWithPlacement || GetComboDown(COMBO_RT_COPY_TEXTURE);
            const bool pasteWithPlacement = GetComboDown(COMBO_RT_PASTE_TEXTURE_PLACEMENT);
            const bool paste = pasteWithPlacement || GetComboDown(COMBO_RT_PASTE_TEXTURE);

            if (!copy && !paste) return false;

            const ID hovered = mouseBlockedByImGui ? INVALID_ID : HoveredCanvasItem(level, mouseWorld);
            if (currentMode == MODE_ENTITY && hovered == INVALID_ID) return false;

            // With no texture copied yet, Ctrl+V on an entity still pastes entities.
            const bool clipboardEmpty = canvasTextureClipboard.source == CanvasTextureClipboard::Source::None;
            if (currentMode == MODE_ENTITY && !copy && clipboardEmpty) return false;

            if (copy) {
                ID source = hovered;

                if (source == INVALID_ID) {
                    if (currentMode == MODE_GEOMETRY) source = selectedWallID;
                    else if (currentMode == MODE_SECTOR) source = selectedSectorID;
                }

                if (source != INVALID_ID && CopyCanvasTexture(level, source, copyWithPlacement))
                    ImGuiDrawFunctions::ShowNotification(Localisation::Get(copyWithPlacement
                        ? "runtime_editor.toast.texture_and_placement_copied"
                        : "runtime_editor.toast.texture_copied").c_str(), false, 1.5f);
            }

            if (paste) {
                const std::vector<ID>& selection = CanvasSelection();
                const bool hoveredSelected = std::ranges::find(selection, hovered) != selection.end();

                const std::vector<ID> targets = hovered == INVALID_ID || hoveredSelected
                    ? selection
                    : std::vector<ID>{hovered};

                PasteCanvasTexture(level, targets, pasteWithPlacement);
            }

            return true;
        }

        // The wall's top, the shown flat, or sprite texture 0 of each target
        // in the current mode, as one undo step. False if none changed.
        bool ApplyTextureToCanvasItems(Level& level, const std::vector<ID>& targets, const std::string& texture) {
            constexpr const char* labelKey = "runtime_editor.history.apply_texture";
            int changed = 0;

            if (currentMode == MODE_ENTITY) {
                EntitiesSnapshot before = CaptureEntitiesSnapshot();

                for (const ID id : targets) {
                    ComponentSprite* sprite = level.sprites.Get(id);
                    if (sprite == nullptr) continue;

                    sprite->textureFileNames[0] = texture;
                    ++changed;
                }

                if (changed > 0) PushEntitiesUndo(std::move(before), labelKey, changed);
                return changed > 0;
            }

            GeometrySnapshot before = CaptureGeometryUndoSnapshot();

            for (const ID id : targets) {
                if (currentMode == MODE_GEOMETRY) {
                    const int index = WallIndexOf(level, id);
                    if (index < 0) continue;

                    level.walls[index].top.texture = texture;
                }
                else {
                    const int index = SectorIndexOf(level, id);
                    if (index < 0) continue;

                    SectorSurface* flat = ShownFlat(level.sectors[index]);
                    if (flat == nullptr) continue;

                    flat->texture = texture;
                }

                ++changed;
            }

            if (changed > 0) PushGeometryUndoSnapshot(std::move(before), labelKey, changed);
            return changed > 0;
        }
    }

    bool DropTextureOnCanvas(const std::string& texture, const Vector2& mouseWorld) {
        Level& level = LevelManager::CurrentLevel();

        const ID hovered = HoveredCanvasItem(level, mouseWorld);
        if (hovered == INVALID_ID) return false;

        // Same targets as a paste: the whole selection when dropped on a
        // selected item, otherwise just the one under the cursor.
        const std::vector<ID>& selection = CanvasSelection();
        const std::vector<ID> targets = std::ranges::find(selection, hovered) != selection.end()
            ? selection
            : std::vector<ID>{hovered};

        return ApplyTextureToCanvasItems(level, targets, texture);
    }

    bool AssignAssetToCanvasSelection(const AssetKind kind, const std::string& reference) {
        if (kind != AssetKind::Texture) return false;

        const std::vector<ID>& selection = CanvasSelection();
        if (selection.empty()) return false;

        return ApplyTextureToCanvasItems(LevelManager::CurrentLevel(), selection, reference);
    }

    void HandleEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        // Released last frame - the drop (if any) has been delivered, so the
        // inspector can follow the selection again.
        if (!holdingEntity) inspectorPinnedDuringCanvasDrag = INVALID_ID;

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        // A wall/sector press released over a panel last frame and not
        // dropped on a field (a drop clears canvasRefPress): its own release
        // happens now, the way it would have on the canvas.
        if (canvasRefPress.releasePending) {
            const CanvasPressAction action = canvasRefPress.action;
            canvasRefPress = {};

            if (action == CanvasPressAction::WALL_DRAG) EndWallDrag();
            else if (action == CanvasPressAction::BOX_SELECT) UpdateBoxSelect(mouseWorld);
        }
        else if (canvasRefPress.active && !InputManager::GetMouseButton(SDL_BUTTON_LEFT)) {
            // Released over a panel as a drag source: the drop arrives in
            // this frame's UI pass, so the release waits a frame. Released
            // on the canvas, it was an ordinary press.
            if (canvasRefPress.sourceSubmitted) canvasRefPress.releasePending = true;
            else canvasRefPress = {};
        }

        const bool canvasRefReleasing = canvasRefPress.releasePending;

        // A new press starts over; the mode handlers below register it
        // again if it can become a wall/sector drag.
        if (!mouseBlockedByImGui && InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) canvasRefPress = {};

        if (InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) {
            editingEntity = false;
            editingSector = false;
            editingWall = false;

            selectedEntities = {};
            selectedEntity = {};
            selectedWalls = {};
            selectedWallID = -1;
            selectedSectors = {};
            selectedSectorID = -1;

            ClearLinedefSelection();
            drawingLinedef = false;
        }

        ValidateSelections(level);

        // Snap to Wall waits for a right-click on a wall in Entity Mode only.
        WallSnap::CancelPickingIfSelectionChanged(selectedEntities);

        if (WallSnap::IsPicking() &&
            (currentMode != MODE_ENTITY || (!keyboardBlockedByImgui && InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE))))
            WallSnap::CancelPicking();

        wallSnapHoveredWallID = WallSnap::IsPicking() && !mouseBlockedByImGui ? PickWallAt(mouseWorld) : INVALID_ID;

        // Geometry editing is gated entirely on Geometry Mode being active
        // and ImGui not having the mouse: a click that lands on a panel
        // must never move or deselect a wall behind it. A drag that is
        // interrupted (by a mode switch, or by the pointer crossing onto a
        // panel) is committed rather than abandoned - the walls have
        // already moved on screen by then. A wall drag being released over a
        // panel waits for its drop (see canvasRefPress).
        if (currentMode != MODE_GEOMETRY || mouseBlockedByImGui) {
            hoveredWallID = INVALID_ID;
            hoveredLinedefID = INVALID_ID;
            if (!canvasRefReleasing) EndWallDrag();
            EndLinedefDrag();
        }

        // A half-drawn linedef only lives in Linedef Mode.
        if (currentMode != MODE_GEOMETRY || !linedefMode) drawingLinedef = false;

        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();

                cameraPos.x -= mouseDelta.x / editorZoom;
                cameraPos.y -= mouseDelta.y / editorZoom;
            }
            // Paint selection takes both buttons over while it's active:
            // nothing else gets the click.
            else if (IsPaintSelectActive(mouseBlockedByImGui)) {
                const bool ctrlHeld = InputManager::GetKey(SDL_SCANCODE_LCTRL) || InputManager::GetKey(SDL_SCANCODE_RCTRL);

                if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) PaintSelectAt(level, mouseWorld, ctrlHeld);
                else if (paintSelectTool && InputManager::GetMouseButton(SDL_BUTTON_RIGHT)) PaintSelectAt(level, mouseWorld, true);
            }
            else if (currentMode == MODE_GEOMETRY) {
                if (!canvasRefReleasing) HandleGeometryModeMouse(mouseWorld);
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                if (currentMode == MODE_SECTOR) {
                    // A press that starts a drawing inside a sector can be
                    // carried onto a panel instead, as a drag of that sector
                    // (the drawing is then dropped). Only a fresh drawing: a
                    // point added to one in progress isn't taken back.
                    const bool startsDrawing = !IsDrawingInProgress();
                    SelectionSnapshot selectionBefore = startsDrawing ? CaptureSelection() : SelectionSnapshot{};

                    // Dispatches to whichever drawing tool is active
                    // (Freehand/Rectangle/Polygon/Circle/Curve); each one
                    // resolves its own grid/vertex snapping internally
                    // (via Resolve*Point, mirroring what ResolveSnapPoint
                    // used to do here directly for the freehand-only path).
                    HandleSectorDrawClick(mouseWorld);

                    if (startsDrawing)
                        BeginCanvasRefPress(CanvasPressAction::SECTOR_DRAWING, SECTOR_REF_PAYLOAD, SectorIDAt(mouseWorld), std::move(selectionBefore));
                }
                else if (currentMode == MODE_ENTITY) {
                    // The gizmo sits on top of everything, so it is tested
                    // before the entities under it.
                    const EntityGizmoPart gizmoPart = PickEntityGizmoPart(mouseScreen);
                    Entity *en = gizmoPart == GIZMO_NONE ? EntityAt(mouseWorld) : nullptr;

                    if (gizmoPart != GIZMO_NONE) {
                        // Moves the selection as it is, whatever modifiers
                        // are held - Ctrl only matters later, for snapping.
                        selectionBeforeCanvasDrag = selectedEntities;
                        primaryBeforeCanvasDrag = selectedEntity.id;
                        inspectorPinnedDuringCanvasDrag = editingEntity ? primaryBeforeCanvasDrag : INVALID_ID;

                        if (const ComponentTransform* primary = level.transforms.Get(selectedEntity.id))
                            gizmoGrabOffset = {mouseWorld.x - primary->position.x, mouseWorld.y - primary->position.z};

                        BeginCanvasEntityHold(level, gizmoPart);
                    }
                    else if (en != nullptr) {
                        // The same modifier grammar Geometry Mode already
                        // uses for walls: Ctrl toggles one entity in or out,
                        // Shift takes the ID range from the current primary,
                        // a plain click replaces. Modifier clicks never start
                        // a drag, so building a selection can't nudge an
                        // entity by accident.
                        if (MultiSelectModifierHeld()) ToggleEntitySelection(en->id);
                        else if (RangeSelectModifierHeld()) ExtendEntitySelectionTo(en->id);
                        else {
                            const bool alreadyInSelection =
                                std::find(selectedEntities.begin(), selectedEntities.end(), en->id)
                                    != selectedEntities.end();

                            selectionBeforeCanvasDrag = selectedEntities;
                            primaryBeforeCanvasDrag = selectedEntities.empty() ? INVALID_ID : selectedEntity.id;
                            inspectorPinnedDuringCanvasDrag = editingEntity ? primaryBeforeCanvasDrag : INVALID_ID;

                            // Clicking an entity that is already part of a
                            // multi-selection keeps that selection and drags
                            // all of it; clicking anything else selects just
                            // that entity. Mirrors the wall rule exactly.
                            if (!alreadyInSelection || selectedEntities.size() <= 1) SelectEntity(en->id);
                            else selectedEntity = *en;

                            BeginCanvasEntityHold(level, GIZMO_NONE);
                        }
                    }
                    // Empty space starts a box selection, same as Geometry
                    // Mode; released without a drag it clears the selection.
                    // Creating an entity is the E key's job (see below), so a
                    // missed click never spawns one.
                    else BeginBoxSelect(mouseWorld);

                    // Left click only selects/moves — it must never
                    // open the inspector
                    // editingEntity is intentionally NOT set here.
                }
            }

            // Right click: whatever the current mode has in progress gets it
            // first (a sector drawing, Snap to Wall picking, a half-drawn
            // linedef, a drag); otherwise it opens what is under the cursor,
            // whatever the mode.
            if (IsPaintSelectActive(mouseBlockedByImGui)) {}
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
                const bool freehandHasPlacedPoints =
                    currentMode == MODE_SECTOR && currentDrawTool == DRAWTOOL_FREEHAND &&
                    (manualSectorMode ? !manualSectorDots.empty() : !sectorBeingCreated.empty());

                if (freehandHasPlacedPoints) UndoLastDrawPoint();
                else if (currentMode == MODE_SECTOR && IsDrawingInProgress()) CancelActiveDrawing();
                else if (currentMode == MODE_ENTITY && WallSnap::IsPicking()) HandleWallSnapRightClick(level, mouseWorld);
                else if (currentMode == MODE_GEOMETRY && linedefMode && drawingLinedef) drawingLinedef = false;
                else if (draggingWallGeometry || draggingLinedefGeometry || holdingEntity) {}
                else HandleCanvasRightClick(mouseWorld);
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && holdingEntity && currentMode == MODE_ENTITY) {
                if (auto* t = selectedEntity.GetComponent<ComponentTransform>()) [[likely]] {
                    // The primary snaps to the cursor exactly as before;
                    // everything else in the selection moves by the same
                    // offset, so a multi-selection keeps its shape instead
                    // of collapsing onto the cursor.
                    const Vector3 positionBeforeDrag = t->position;

                    Vector2 target = mouseWorld;

                    // Gizmo drags keep the grab offset, snap like wall
                    // dragging does (Ctrl ignores the grid), and an arrow
                    // only lets its own axis change.
                    if (activeGizmoPart != GIZMO_NONE) {
                        target = {mouseWorld.x - gizmoGrabOffset.x, mouseWorld.y - gizmoGrabOffset.y};

                        if (gridSnapEnabled && GetKeyBind(KEYBIND_DISABLE_SNAP)) target = SnapToGrid(target);

                        if (activeGizmoPart == GIZMO_X) target.y = t->position.z;
                        else if (activeGizmoPart == GIZMO_Z) target.x = t->position.x;
                    }

                    t->SetPosition({target.x, t->position.y, target.y});

                    MoveSelectedEntitiesBy(level, {
                        t->position.x - positionBeforeDrag.x,
                        t->position.y - positionBeforeDrag.y,
                        t->position.z - positionBeforeDrag.z
                    }, selectedEntity.id);
                }
                else [[unlikely]] spdlog::error("Entity does not have transform component");
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
                if (holdingEntity) PushCanvasEntityDragUndo(level);

                holdingEntity = false;
                activeGizmoPart = GIZMO_NONE;
            }

            UpdateEditorZoom(keyboardBlockedByImgui);
        }

        // A box press being released over a panel waits for its drop (see
        // canvasRefPress).
        if (!canvasRefReleasing) UpdateBoxSelect(mouseWorld);

        if (!keyboardBlockedByImgui) {
            // Ctrl is for shortcuts (Ctrl+S, Ctrl+A, ...), so while it is
            // held the camera keys do nothing.
            const bool ctrlHeld = InputManager::GetKey(SDL_SCANCODE_LCTRL) || InputManager::GetKey(SDL_SCANCODE_RCTRL);
            Vector2 movement = {.0f, .0f};

            if (!ctrlHeld) {
                if (GetKeyBind(KEYBIND_PAN_UP)) movement.y = cameraSpeed;
                if (GetKeyBind(KEYBIND_PAN_DOWN)) movement.y = -cameraSpeed;
                if (GetKeyBind(KEYBIND_PAN_LEFT)) movement.x = cameraSpeed;
                if (GetKeyBind(KEYBIND_PAN_RIGHT)) movement.x = -cameraSpeed;

                if (GetKeyBindDown(KEYBIND_STEP_UP)) movement.y = cameraStepSize;
                if (GetKeyBindDown(KEYBIND_STEP_DOWN)) movement.y = -cameraStepSize;
                if (GetKeyBindDown(KEYBIND_STEP_LEFT)) movement.x = cameraStepSize;
                if (GetKeyBindDown(KEYBIND_STEP_RIGHT)) movement.x = -cameraStepSize;
            }

            cameraPos.x -= movement.x / editorZoom;
            cameraPos.y -= movement.y / editorZoom;

            if (currentMode == MODE_SECTOR && InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) CancelActiveDrawing();
            if (GetKeyBindDown(KEYBIND_FOCUS_SELECTION)) FocusCameraOnSelection();
            if (GetKeyBindDown(KEYBIND_FRAME_MAP)) FrameWholeMap();

            // E places a new entity under the cursor. Only while the cursor
            // is on the canvas - over a panel there is no world point to
            // place it at.
            // What it places, and how, comes from the Entity Mode panel's
            // palette and placement settings.
            if (currentMode == MODE_ENTITY && !mouseBlockedByImGui && !holdingEntity &&
                GetKeyBindDown(KEYBIND_PLACE_ENTITY)) {
                if (PlacePaletteEntityAt(mouseWorld) == INVALID_ID)
                    lastGeometryError = Localisation::Get("editor.entity_mode.palette.place_failed");
            }

            // Drawing-tool shortcuts - all Sector Mode only, and all
            // no-ops when nothing relevant is in progress (ConfirmActiveDrawing/
            // UndoLastDrawPoint already check that internally).
            if (currentMode == MODE_SECTOR) {
                if (GetKeyBindDown(KEYBIND_FINISH_SHAPE)) ConfirmActiveDrawing();

                if (GetKeyBindDown(KEYBIND_UNDO_POINT)) UndoLastDrawPoint();

                // [ / ] adjust the Regular Polygon tool's side count even
                // while a polygon is mid-preview, so the shape under the
                // cursor updates live instead of only from the UI slider.
                if (GetKeyBindDown(KEYBIND_FEWER_SIDES))
                    polygonSideCount = std::max(3, polygonSideCount - 1);
                if (GetKeyBindDown(KEYBIND_MORE_SIDES))
                    polygonSideCount = std::min(MAX_POLYGON_SIDES, polygonSideCount + 1);

                // Alt flips which end of the Staircase tool's rectangle is
                // the bottom of the stairs. A single press toggles it (not
                // a held modifier), same as the checkbox in the settings
                // panel - BuildStaircasePlan (MapEditorGeometry.cpp) just
                // reads staircaseReverseRise, it never touches input state.
                if (GetKeyBindDown(KEYBIND_FLIP_STAIRCASE))
                    staircaseReverseRise = !staircaseReverseRise;

                if (GetKeyBindDown(KEYBIND_TOOL_FREEHAND)) SetActiveDrawTool(DRAWTOOL_FREEHAND);
                if (GetKeyBindDown(KEYBIND_TOOL_RECTANGLE)) SetActiveDrawTool(DRAWTOOL_RECTANGLE);
                if (GetKeyBindDown(KEYBIND_TOOL_POLYGON)) SetActiveDrawTool(DRAWTOOL_POLYGON);
                if (GetKeyBindDown(KEYBIND_TOOL_CIRCLE)) SetActiveDrawTool(DRAWTOOL_CIRCLE);
                if (GetKeyBindDown(KEYBIND_TOOL_CURVE)) SetActiveDrawTool(DRAWTOOL_CURVE);
                if (GetKeyBindDown(KEYBIND_TOOL_STAIRCASE)) SetActiveDrawTool(DRAWTOOL_STAIRCASE);
            }

            if (GetKeyBindDown(KEYBIND_CYCLE_MODE)) ChangeMode();
            if (GetKeyBindDown(KEYBIND_GEOMETRY_MODE)) {
                // Already in Geometry Mode: the same key toggles Linedef Mode.
                if (currentMode == MODE_GEOMETRY) SetLinedefMode(!linedefMode);
                else currentMode = MODE_GEOMETRY;
            }
            if (GetKeyBindDown(KEYBIND_ENTITY_MODE)) currentMode = MODE_ENTITY;
            if (GetKeyBindDown(KEYBIND_SECTOR_MODE)) currentMode = MODE_SECTOR;

            if (GetKeyBindDown(KEYBIND_TOGGLE_GRID_SNAP)) {
                gridSnapEnabled = !gridSnapEnabled;
                ImGuiDrawFunctions::ShowNotification(Localisation::Get(gridSnapEnabled ? "editor.toast.grid_snap_on" : "editor.toast.grid_snap_off").c_str(), false, 1.5f);
            }

            const bool gridSmaller = GetKeyBindDown(KEYBIND_GRID_SMALLER);
            const bool gridLarger = GetKeyBindDown(KEYBIND_GRID_LARGER);

            if (gridSmaller != gridLarger) {
                constexpr float MAX_GRID_SIZE = 1024.0f;
                GRID_SIZE = std::clamp(gridLarger ? GRID_SIZE * 2.0f : GRID_SIZE * 0.5f, MIN_GRID_SIZE, MAX_GRID_SIZE);

                char message[128];
                std::snprintf(message, sizeof(message), Localisation::Get("editor.toast.grid_size").c_str(), GRID_SIZE);
                ImGuiDrawFunctions::ShowNotification(message, false, 1.5f);
            }

            // Grid tools. The origin needs the cursor on the canvas; aligning
            // takes the wall under the cursor, else the selected one.
            if (GetKeyBindDown(KEYBIND_GRID_ORIGIN_HERE) && !mouseBlockedByImGui && SetGridOriginAtCursor(mouseWorld))
                ImGuiDrawFunctions::ShowNotification(Localisation::Get("editor.toast.grid_origin_moved").c_str(), false, 1.5f);

            if (GetKeyBindDown(KEYBIND_ALIGN_GRID_TO_WALL)) {
                const ID hovered = mouseBlockedByImGui ? INVALID_ID : PickWallAt(mouseWorld);

                if (AlignGridToWall(hovered != INVALID_ID ? hovered : selectedWallID))
                    ImGuiDrawFunctions::ShowNotification(Localisation::Get("editor.toast.grid_aligned").c_str(), false, 1.5f);
                else
                    ImGuiDrawFunctions::ShowNotification(Localisation::Get("editor.toast.grid_align_no_wall").c_str(), true, 2.0f);
            }

            if (GetKeyBindDown(KEYBIND_RESET_GRID)) {
                ResetGridTransform();
                ImGuiDrawFunctions::ShowNotification(Localisation::Get("editor.toast.grid_reset").c_str(), false, 1.5f);
            }

            // Ctrl turns them away from the cursor instead.
            if (GetKeyBindDown(KEYBIND_POINT_AT_CURSOR) && !mouseBlockedByImGui && !selectedEntities.empty()) {
                const bool away = InputManager::GetKey(SDL_SCANCODE_LCTRL) || InputManager::GetKey(SDL_SCANCODE_RCTRL);
                const int turned = PointEntitiesAt(level, selectedEntities, mouseWorld, away);

                if (turned > 0) {
                    char message[128];
                    std::snprintf(message, sizeof(message), Localisation::Get("editor.toast.entities_pointed").c_str(), turned);
                    ImGuiDrawFunctions::ShowNotification(message, false, 1.5f);
                }
            }

            // Floors and ceilings of the selected sectors, else of the one
            // under the cursor, to the next neighbouring height (Ctrl: the
            // highest or lowest). The lowest floor interval, the one the
            // canvas shows.
            {
                const bool raiseFloor = GetKeyBindDown(KEYBIND_RAISE_FLOOR_TO_NEIGHBOUR);
                const bool lowerFloor = GetKeyBindDown(KEYBIND_LOWER_FLOOR_TO_NEIGHBOUR);
                const bool raiseCeiling = GetKeyBindDown(KEYBIND_RAISE_CEILING_TO_NEIGHBOUR);
                const bool lowerCeiling = GetKeyBindDown(KEYBIND_LOWER_CEILING_TO_NEIGHBOUR);

                if (raiseFloor || lowerFloor || raiseCeiling || lowerCeiling) {
                    std::vector<ID> sectorIDs = selectedSectors;

                    if (sectorIDs.empty() && !mouseBlockedByImGui) {
                        const int hovered = MapQueries::FindSectorContainingPoint(level.sectors, mouseWorld);
                        if (hovered >= 0) sectorIDs.push_back(level.sectors[hovered].id);
                    }

                    std::vector<FlatTarget> targets;
                    for (const ID sectorID : sectorIDs) targets.push_back({sectorID, 0, raiseCeiling || lowerCeiling});

                    const bool extreme = InputManager::GetKey(SDL_SCANCODE_LCTRL) || InputManager::GetKey(SDL_SCANCODE_RCTRL);
                    const int moved = MoveFlatsToNeighbourHeight(level, targets, raiseFloor || raiseCeiling, extreme);

                    char message[128];
                    std::snprintf(message, sizeof(message), Localisation::Get(moved > 0
                        ? "editor.toast.moved_to_neighbour"
                        : "editor.toast.no_neighbour_height").c_str(), moved);
                    ImGuiDrawFunctions::ShowNotification(message, moved == 0, 1.5f);
                }
            }

            // A comment on what's under the cursor in this mode, or a pin on
            // its own where nothing is.
            if (GetKeyBindDown(KEYBIND_ADD_COMMENT) && !mouseBlockedByImGui) {
                EditorCommentTarget target = EditorCommentTarget::PIN;
                ID targetID = INVALID_ID;

                if (currentMode == MODE_GEOMETRY) {
                    targetID = linedefMode ? PickLinedefAt(mouseWorld) : PickWallAt(mouseWorld);
                    if (targetID != INVALID_ID) target = linedefMode ? EditorCommentTarget::LINEDEF : EditorCommentTarget::WALL;
                }
                else if (currentMode == MODE_SECTOR) {
                    for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i) {
                        if (!Geometry::IsPointInPolygon(level.sectors[i].vertices, level.sectors[i].innerLoops, mouseWorld)) continue;

                        target = EditorCommentTarget::SECTOR;
                        targetID = level.sectors[i].id;
                        break;
                    }
                }
                else if (currentMode == MODE_ENTITY) {
                    if (const Entity* entity = EntityAt(mouseWorld)) {
                        target = EditorCommentTarget::ENTITY;
                        targetID = entity->id;
                    }
                }

                Comments::Add(level, target, targetID, mouseWorld);
                commentsVisible = true;
            }

            // The selected sectors take the light of the one under the cursor.
            if (GetKeyBindDown(KEYBIND_MATCH_BRIGHTNESS) && !mouseBlockedByImGui && !selectedSectors.empty()) {
                const int hovered = MapQueries::FindSectorContainingPoint(level.sectors, mouseWorld);
                const int changed = hovered >= 0 ? MatchSectorBrightness(level, selectedSectors, level.sectors[hovered].id) : 0;

                char message[128];
                std::snprintf(message, sizeof(message), Localisation::Get("editor.toast.brightness_matched").c_str(), changed);
                ImGuiDrawFunctions::ShowNotification(message, changed == 0, 1.5f);
            }

            if (GetKeyBindDown(KEYBIND_FLIP_LINEDEF) && currentMode == MODE_GEOMETRY && linedefMode)
                FlipSelectedLinedefs();

            if (GetComboDown(COMBO_SELECT_ALL)) SelectAllInMode();

            for (int group = 0; group < EDITOR_SELECTION_GROUP_COUNT; ++group) {
                if (!GetComboDown(static_cast<EditorCombo>(COMBO_SELECT_GROUP_1 + group))) continue;

                char message[128];

                if (GetKeyBind(KEYBIND_ASSIGN_GROUP_MODIFIER)) {
                    AssignSelectionGroup(group);
                    std::snprintf(message, sizeof(message), Localisation::Get("editor.toast.group_assigned").c_str(), (group + 1) % 10);
                    ImGuiDrawFunctions::ShowNotification(message, false, 1.5f);
                }
                else {
                    const int count = SelectSelectionGroup(group);
                    std::snprintf(message, sizeof(message), Localisation::Get(count > 0 ? "editor.toast.group_selected" : "editor.toast.group_empty").c_str(), (group + 1) % 10, count);
                    ImGuiDrawFunctions::ShowNotification(message, count == 0, 1.5f);
                }
            }
            if (GetComboDown(COMBO_DUPLICATE) && currentMode == MODE_ENTITY) DuplicateSelectedEntities();
        }

        if (InputManager::QuitRequested()) {
            shutdown = true;
            quit = true;
        }

        // Everything below is keyboard-only, and a text field or the script
        // editor has its own Ctrl+Z, Delete, Ctrl+C/V.
        if (keyboardBlockedByImgui) return;

        if (GetComboDown(COMBO_REDO)) RedoLastAction();
        else if (GetComboDown(COMBO_UNDO)) {
            if (undoSteps.empty()) return;
            UndoLastAction();
        }

        // Texture copy/paste first: in Entity Mode it shares Ctrl+C/V with
        // entity copy/paste, and wins while the cursor is on an entity.
        const bool textureShortcutTaken = HandleCanvasTextureShortcuts(level, mouseWorld, mouseBlockedByImGui);

        if (!textureShortcutTaken && currentMode == MODE_ENTITY) {
            if (GetComboDown(COMBO_COPY_ENTITIES) && !selectedEntities.empty()) [[unlikely]] {
                // Any selection, not only a dragged or inspected entity.
                // The primary goes first: paste places it under the cursor.
                entityClipboard.assign(1, selectedEntity.id);

                for (const ID id : selectedEntities)
                    if (id != selectedEntity.id) entityClipboard.push_back(id);
            }

            if (GetComboDown(COMBO_PASTE_ENTITIES)) [[unlikely]]
                PasteEntityClipboard(level, mouseWorld, "editor.history.paste_entities");
        }

        if (GetKeyBindDown(KEYBIND_DELETE)) [[unlikely]] {
            if (!selectedEntities.empty()) {
                const std::vector<ID> entitiesToDelete = selectedEntities;
                selectedEntities.clear();

                EntitiesSnapshot before = CaptureEntitiesSnapshot();
                for (const ID id : entitiesToDelete) level.DestroyEntity(id);
                PushEntitiesUndo(std::move(before), "editor.history.delete_entities", static_cast<int>(entitiesToDelete.size()));
            }
            if (!selectedSectors.empty()) {
                const std::vector<ID> sectorsToDelete = selectedSectors;
                selectedSectors.clear();

                GeometrySnapshot before = CaptureGeometryUndoSnapshot();
                for (const ID id : sectorsToDelete) DeleteSector(id);
                PushGeometryUndoSnapshot(std::move(before), "editor.history.delete_sectors", static_cast<int>(sectorsToDelete.size()));
            }
            // One undoable operation with a single topology rebuild for
            // the whole wall selection, rather than one of each per wall.
            DeleteSelectedWalls();
            DeleteSelectedLinedefs();

            editingEntity = false;
        }
    }
}