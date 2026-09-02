#include <iostream>

#include <algorithm>
#include <vector>

#include "../EditorInternal.hpp"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"

namespace {
    bool holdingEntity = false;
}

namespace MapEditorInternal {
    void UpdateEditorZoom() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorldBeforeZoom = ScreenToWorld(mouseScreen, cameraPos);

        bool zoomChanged = false;

        if (InputManager::GetKeyDown(SDL_SCANCODE_EQUALS) ||
            InputManager::GetKeyDown(SDL_SCANCODE_KP_PLUS) || InputManager::GetMouseWheelScrollUp()) {
            editorZoom *= 1.15f;
            zoomChanged = true;
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_MINUS) ||
            InputManager::GetKeyDown(SDL_SCANCODE_KP_MINUS) || InputManager::GetMouseWheelScrollDown()) {
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

    namespace {
        //  defensively drops any selection that no longer
        // resolves through its ID map (e.g. a sector deleted from outside
        // the normal DeleteSector() path, or any other desync).
        void ValidateSelections(Level& level) {
            if (selectedSectorID != INVALID_ID &&
                level.sectorIDToIndex.find(selectedSectorID) == level.sectorIDToIndex.end()) {
                selectedSectorID = INVALID_ID;
                editingSector = false;
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
                PushGeometryUndoSnapshot(dragUndoSnapshot);
                dragMutatedGeometry = true;
            }

            ApplyGeometryDragOffset(dragOriginalWalls, dragOriginalSectors, dragMovingPoints, delta);
        }

        // Commits whatever the drag ended up doing. Called on mouse-up, and
        // also when ImGui takes the mouse mid-drag: the walls have already
        // visibly moved by then, so finishing is far less surprising than
        // silently reverting.
        void EndWallDrag() {
            if (!draggingWallGeometry) return;

            draggingWallGeometry = false;

            dragOriginalWalls.clear();
            dragOriginalSectors.clear();
            dragMovingPoints.clear();
            dragSnapIgnoredWalls.clear();

            if (!dragMutatedGeometry) return;

            dragMutatedGeometry = false;

            // The move variant, not RebuildGeometryAfterEdit: the sectors
            // moved with the walls and were re-triangulated as they went,
            // so re-deriving them here would throw away perfectly good
            // sectors (and their textures) to build identical new ones.
            RebuildGeometryAfterMove();
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
            return InputManager::GetKey(SDL_SCANCODE_LCTRL) || InputManager::GetKey(SDL_SCANCODE_RCTRL);
        }

        bool RangeSelectModifierHeld() {
            return InputManager::GetKey(SDL_SCANCODE_LSHIFT) || InputManager::GetKey(SDL_SCANCODE_RSHIFT);
        }

        // Geometry Mode's whole pointer story: hover, select, drag, release.
        // Sectors are never touched here - the moved walls are handed back
        // to the topology pass on release, and it re-derives whatever
        // sectors those walls bounded.
        void HandleGeometryModeMouse(const Vector2& mouseWorld) {
            const Level& level = LevelManager::CurrentLevel();

            ID endpointWallID = INVALID_ID;
            bool endpointIsStart = false;
            const bool overEndpoint = PickSelectedWallEndpointAt(mouseWorld, &endpointWallID, &endpointIsStart);

            const ID wallUnderCursor = PickWallAt(mouseWorld);

            if (!draggingWallGeometry) hoveredWallID = wallUnderCursor;

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                // Endpoint handles win over the wall body whenever the
                // cursor is near both, so grabbing the corner of a short
                // wall is never a coin flip.
                if (overEndpoint) {
                    const auto it = level.wallIDToIndex.find(endpointWallID);

                    if (it != level.wallIDToIndex.end()) {
                        const Wall& wall = level.walls[it->second];
                        const Vector2 endpoint = endpointIsStart ? wall.start : wall.end;

                        BeginWallDrag(mouseWorld, {endpoint}, endpoint, false);
                        return;
                    }
                }

                // Empty space clears the selection, and with it the wall
                // inspector.
                if (wallUnderCursor == INVALID_ID) {
                    ClearWallSelection();
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
                else {
                    selectedWallID = wallUnderCursor;
                    editingWall = true;
                }

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

    void HandleEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        ValidateSelections(level);

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

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
                cameraPos.y += mouseDelta.y / editorZoom;
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
                    Entity *en = EntityAt(mouseWorld);

                    if (en != nullptr) {
                        selectedEntity = *en;
                        holdingEntity = true;
                    }
                    else {
                        static constexpr bool isUIEntity = false;
                        const ID id = level.CreateEntity(isUIEntity);
                        selectedEntity = *level.GetEntity(id);

                        auto *t = selectedEntity.GetComponent<ComponentTransform>();
                        if (t != nullptr) t->SetPosition({mouseWorld.x, 0.0f, mouseWorld.y});
                    }

                    // Left click only selects/places/moves — it must never
                    // open the inspector
                    // editingEntity is intentionally NOT set here.
                }
            }

            //right click cancels/stops sector chain creation.
            if (currentMode == MODE_SECTOR && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
                // BUG FIX: this used to call CancelSectorChain() here
                // unconditionally and *then*, in the block below, gate
                // select/inspect on sectorBeingCreated.empty() - but by
                // that point the chain had already just been cleared, so
                // that check was always vacuously true and a right-click
                // mid-chain both cancelled the chain *and* immediately
                // selected/inspected whatever was under the cursor in the
                // same click. Capturing whether a drawing was actually in
                // progress *before* cancelling restores the originally
                // intended behaviour: right-click during an in-progress
                // drawing only cancels it.
                const bool wasDrawingInProgress = IsDrawingInProgress();
                CancelActiveDrawing();

                if (!wasDrawingInProgress) HandleSectorModeRightClick(mouseWorld);
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
                if (currentMode == MODE_ENTITY) HandleEntityModeRightClick(mouseWorld);
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && holdingEntity && currentMode == MODE_ENTITY) {
                if (auto* t = selectedEntity.GetComponent<ComponentTransform>()) [[likely]]
                    t->SetPosition({mouseWorld.x, t->position.y, mouseWorld.y});
                else [[unlikely]] spdlog::error("Entity does not have transform component");
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) holdingEntity = false;

            UpdateEditorZoom();
        }

        if (!keyboardBlockedByImgui) {
            if (currentMode == MODE_SECTOR && InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) CancelActiveDrawing();
            if (InputManager::GetKeyDown(SDL_SCANCODE_F)) FocusCameraOnSelection();

            // Drawing-tool shortcuts - all Sector Mode only, and all
            // no-ops when nothing relevant is in progress (ConfirmActiveDrawing/
            // UndoLastDrawPoint already check that internally).
            if (currentMode == MODE_SECTOR) {
                if (InputManager::GetKeyDown(SDL_SCANCODE_RETURN) ||
                    InputManager::GetKeyDown(SDL_SCANCODE_KP_ENTER) ||
                    InputManager::GetKeyDown(SDL_SCANCODE_SPACE))
                    ConfirmActiveDrawing();

                if (InputManager::GetKeyDown(SDL_SCANCODE_BACKSPACE)) UndoLastDrawPoint();

                // [ / ] adjust the Regular Polygon tool's side count even
                // while a polygon is mid-preview, so the shape under the
                // cursor updates live instead of only from the UI slider.
                if (InputManager::GetKeyDown(SDL_SCANCODE_LEFTBRACKET))
                    polygonSideCount = std::max(3, polygonSideCount - 1);
                if (InputManager::GetKeyDown(SDL_SCANCODE_RIGHTBRACKET))
                    polygonSideCount = std::min(MAX_POLYGON_SIDES, polygonSideCount + 1);

                if (InputManager::GetKeyDown(SDL_SCANCODE_1)) SetActiveDrawTool(DRAWTOOL_FREEHAND);
                if (InputManager::GetKeyDown(SDL_SCANCODE_2)) SetActiveDrawTool(DRAWTOOL_RECTANGLE);
                if (InputManager::GetKeyDown(SDL_SCANCODE_3)) SetActiveDrawTool(DRAWTOOL_POLYGON);
                if (InputManager::GetKeyDown(SDL_SCANCODE_4)) SetActiveDrawTool(DRAWTOOL_CIRCLE);
                if (InputManager::GetKeyDown(SDL_SCANCODE_5)) SetActiveDrawTool(DRAWTOOL_CURVE);
            }
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) ChangeMode();

        if (InputManager::QuitRequested()) {
            shutdown = true;
            quit = true;
        }

        if (InputManager::GetDoubleKeyDown(SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z)) {
            if (actions.empty()) return;
            switch (actions.back()) {
                case ACTION_CREATE_CORNER:
                    if (!dots.empty()) DeleteDot(dots.back().id);
                    break;
                case ACTION_CREATE_WALL:
                    if (!level.walls.empty()) DeleteWall(level.walls.back().id);
                    break;
                case ACTION_CREATE_SECTOR:
                    if (!level.sectors.empty()) DeleteSector(level.sectors.back().id);
                    break;
                case ACTION_CREATE_OBJECT:
                    if (!level.entities.empty()) {
                        const Entity entity = level.entities.back();
                        level.DestroyEntity(entity.id);
                    }
                    break;
                case ACTION_APPLY_GEOMETRY:
                    // One ApplyDrawnGeometry call can create/split many
                    // walls and sectors at once, so undo restores the
                    // whole-operation snapshot rather than guessing at
                    // "the last wall" / "the last sector" the way the
                    // simpler action kinds above do.
                    if (!geometrySnapshots.empty()) {
                        RestoreGeometrySnapshot(geometrySnapshots.back());
                        geometrySnapshots.pop_back();
                    }
                    break;
                default: break;
            }
            actions.pop_back();
        }

        if (InputManager::GetDoubleKeyDown(SDL_SCANCODE_LCTRL, SDL_SCANCODE_C)) [[unlikely]] {
            if (editingEntity || holdingEntity) {
                entityInClipboard = selectedEntity;
                hasEntityInClipboard = true;
            }
        }

        if (InputManager::GetDoubleKeyDown(SDL_SCANCODE_LCTRL, SDL_SCANCODE_V)) [[unlikely]]
            if (hasEntityInClipboard) level.CreateEntity(entityInClipboard);

        if (InputManager::GetKeyDown(SDL_SCANCODE_DELETE)) [[unlikely]] {
            if (!selectedEntities.empty()) {
                const std::vector<ID> entitiesToDelete = selectedEntities;
                selectedEntities.clear();

                for (const ID id : entitiesToDelete) level.DestroyEntity(id);
            }
            if (!selectedSectors.empty()) {
                const std::vector<ID> sectorsToDelete = selectedSectors;
                selectedSectors.clear();

                for (const ID id : sectorsToDelete) DeleteSector(id);
            }
            // One undoable operation with a single topology rebuild for
            // the whole wall selection, rather than one of each per wall.
            DeleteSelectedWalls();

            editingEntity = false;
        }
    }
}