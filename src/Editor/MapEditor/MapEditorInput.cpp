#include <iostream>

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

            if (selectedDotID != INVALID_ID &&
                dotIDToIndex.find(selectedDotID) == dotIDToIndex.end()) {
                selectedDotID = INVALID_ID;
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

            if (currentMode == MODE_DOT && selectedDotID != INVALID_ID) {
                const auto it = dotIDToIndex.find(selectedDotID);

                if (it != dotIDToIndex.end()) cameraPos = dots[it->second].position;


                return;
            }

            if (currentMode == MODE_ENTITY && editingEntity)
                if (const ComponentTransform* transform = level.transforms.Get(selectedEntity.id))
                    cameraPos = {transform->position.x, transform->position.y};
        }
    }

    void HandleEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        ValidateSelections(level);

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();

                cameraPos.x -= mouseDelta.x / editorZoom;
                cameraPos.y += mouseDelta.y / editorZoom;
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                if (currentMode == MODE_SECTOR) {
                    // Dispatches to whichever drawing tool is active
                    // (Freehand/Rectangle/Polygon/Circle/Curve); each one
                    // resolves its own grid/vertex snapping internally
                    // (via Resolve*Point, mirroring what ResolveSnapPoint
                    // used to do here directly for the freehand-only path).
                    HandleSectorDrawClick(mouseWorld);
                }
                else if (currentMode == MODE_DOT) {
                    const bool snapToGridHeld =
                        InputManager::GetKey(SDL_SCANCODE_LSHIFT) ||
                        InputManager::GetKey(SDL_SCANCODE_RSHIFT);

                    const Vector2 placePoint = snapToGridHeld ? SnapToGrid(mouseWorld) : mouseWorld;

                    AddDot(placePoint);
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
                else if (currentMode == MODE_DOT) HandleDotModeRightClick(mouseWorld);
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
                if (InputManager::GetKeyDown(SDL_SCANCODE_RETURN) || InputManager::GetKeyDown(SDL_SCANCODE_KP_ENTER))
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
            if (!selectedWalls.empty()) {
                const std::vector<ID> wallsToDelete = selectedWalls;
                selectedWalls.clear();

                for (const ID id : wallsToDelete) DeleteWall(id);
            }
            if (!selectedDots.empty()) {
                const std::vector<ID> dotsToDelete = selectedDots;
                selectedDots.clear();

                for (const ID id : dotsToDelete) DeleteDot(id);
            }
            editingEntity = false;
        }
    }
}