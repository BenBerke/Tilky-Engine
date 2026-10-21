#include <iostream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../EditorInternal.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapTopology.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
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
    void UpdateEditorZoom() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorldBeforeZoom = ScreenToWorld(mouseScreen, cameraPos);

        bool zoomChanged = false;

        if (GetKeyBindDown(KEYBIND_ZOOM_IN) || InputManager::GetMouseWheelScrollUp()) {
            editorZoom *= 1.15f;
            zoomChanged = true;
        }

        if (GetKeyBindDown(KEYBIND_ZOOM_OUT) || InputManager::GetMouseWheelScrollDown()) {
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
                    cameraPos = {transform->position.x, transform->position.y};
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
                dragMutatedGeometry = true;
            }

            ApplyGeometryDragOffset(dragOriginalWalls, dragOriginalSectors, dragMovingPoints, delta);
            dragDelta = delta;
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

            if (!dragMutatedGeometry) return;

            dragMutatedGeometry = false;

            // A plain move keeps the sectors the drag already moved and
            // re-triangulated as they are; only a drop onto other walls
            // re-derives them, since that's what changes which sector
            // borders which wall.
            RebuildGeometryAfterWallDrag(movedPoints, draggedWallIDs);
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
            if (mode == MODE_GEOMETRY) ClearWallSelection();
            else if (mode == MODE_ENTITY) ClearEntitySelection();
        }

        void ApplyBoxSelection(const Vector2& boxMin, const Vector2& boxMax, const bool additive) {
            Level& level = LevelManager::CurrentLevel();

            if (!additive) ClearSelectionForMode(boxSelectMode);

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
                return;
            }

            const Vector2 boxMin = {std::min(boxSelectStartWorld.x, mouseWorld.x), std::min(boxSelectStartWorld.y, mouseWorld.y)};
            const Vector2 boxMax = {std::max(boxSelectStartWorld.x, mouseWorld.x), std::max(boxSelectStartWorld.y, mouseWorld.y)};

            ApplyBoxSelection(boxMin, boxMax, MultiSelectModifierHeld());
        }

        // Geometry Mode's whole pointer story: hover, select, drag, release.
        // Sectors are never touched here - the moved walls are handed back
        // to the topology pass on release, and it re-derives whatever
        // sectors those walls bounded.
        void HandleGeometryModeMouse(const Vector2 &mouseWorld) {
            const Level &level = LevelManager::CurrentLevel();

            ID endpointWallID = INVALID_ID;
            bool endpointIsStart = false;
            const bool overEndpoint = PickSelectedWallEndpointAt(mouseWorld, &endpointWallID, &endpointIsStart);

            const ID wallUnderCursor = PickWallAt(mouseWorld);

            if (!draggingWallGeometry) hoveredWallID = wallUnderCursor;

            // Right click opens the wall for editing. It never starts a drag,
            // so the inspector can't be opened and the geometry nudged by the
            // same click.
            if (!draggingWallGeometry && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
                if (wallUnderCursor == INVALID_ID) {
                    editingWall = false;
                    return;
                }

                // Same as Ctrl+left click: adds or removes the wall, and the
                // inspector edits the whole selection.
                if (MultiSelectModifierHeld()) {
                    ToggleWallSelection(wallUnderCursor);
                    return;
                }

                const bool alreadyInSelection =
                        std::find(selectedWalls.begin(), selectedWalls.end(), wallUnderCursor) != selectedWalls.end();

                if (!alreadyInSelection) SelectWall(wallUnderCursor);

                selectedWallID = wallUnderCursor;
                editingWall = true;
                return;
            }

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
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

        if (canvasDragPushedUndo && !undoSteps.empty()) undoSteps.pop_back();
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

    // Copies the clipboard's entities that still exist. The primary lands
    // under the cursor and the rest keep their offsets from it; heights are
    // kept. The copies become the selection.
    void PasteEntityClipboard(Level& level, const Vector2& mouseWorld, const char* labelKey) {
        std::erase_if(entityClipboard, [&level](const ID id) { return level.GetEntity(id) == nullptr; });
        if (entityClipboard.empty()) return;

        EntitiesSnapshot before = CaptureEntitiesSnapshot();

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

        PushEntitiesUndo(std::move(before), labelKey, static_cast<int>(pasted.size()));

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
                    return PickWallAt(mouseWorld);

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

    void HandleEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        // Released last frame - the drop (if any) has been delivered, so the
        // inspector can follow the selection again.
        if (!holdingEntity) inspectorPinnedDuringCanvasDrag = INVALID_ID;

        ValidateSelections(level);

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

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
        // already moved on screen by then.
        if (currentMode != MODE_GEOMETRY || mouseBlockedByImGui) {
            hoveredWallID = INVALID_ID;
            EndWallDrag();
        }

        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();

                cameraPos.x -= mouseDelta.x / editorZoom;
                cameraPos.y -= mouseDelta.y / editorZoom;
            }
            else if (currentMode == MODE_GEOMETRY) HandleGeometryModeMouse(mouseWorld);
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                if (currentMode == MODE_SECTOR) {
                    // Dispatches to whichever drawing tool is active
                    // (Freehand/Rectangle/Polygon/Circle/Curve); each one
                    // resolves its own grid/vertex snapping internally
                    // (via Resolve*Point, mirroring what ResolveSnapPoint
                    // used to do here directly for the freehand-only path).
                    HandleSectorDrawClick(mouseWorld);
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

            //right click cancels/stops sector chain creation.
            if (currentMode == MODE_SECTOR && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
                const bool freehandHasPlacedPoints =
                    currentDrawTool == DRAWTOOL_FREEHAND &&
                    (manualSectorMode ? !manualSectorDots.empty() : !sectorBeingCreated.empty());

                if (freehandHasPlacedPoints) UndoLastDrawPoint();
                else if (IsDrawingInProgress()) CancelActiveDrawing();
                else HandleSectorModeRightClick(mouseWorld);
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT) && currentMode == MODE_ENTITY) {
                if (WallSnap::IsPicking()) HandleWallSnapRightClick(level, mouseWorld);
                else HandleEntityModeRightClick(mouseWorld);
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

            UpdateEditorZoom();
        }

        UpdateBoxSelect(mouseWorld);

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
            if (GetKeyBindDown(KEYBIND_GEOMETRY_MODE)) currentMode = MODE_GEOMETRY;
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

            if (GetComboDown(COMBO_SELECT_ALL)) SelectAllInMode();
            if (GetComboDown(COMBO_DUPLICATE) && currentMode == MODE_ENTITY) DuplicateSelectedEntities();
        }

        if (InputManager::QuitRequested()) {
            shutdown = true;
            quit = true;
        }

        if (GetComboDown(COMBO_REDO)) RedoLastAction();
        else if (GetComboDown(COMBO_UNDO)) {
            if (undoSteps.empty()) return;
            UndoLastAction();
        }

        // Not while a text field has the keyboard: Ctrl+C/V there are its own.
        // Texture copy/paste first: in Entity Mode it shares Ctrl+C/V with
        // entity copy/paste, and wins while the cursor is on an entity.
        const bool textureShortcutTaken =
            !keyboardBlockedByImgui && HandleCanvasTextureShortcuts(level, mouseWorld, mouseBlockedByImGui);

        if (!keyboardBlockedByImgui && !textureShortcutTaken && currentMode == MODE_ENTITY) {
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

            editingEntity = false;
        }
    }
}