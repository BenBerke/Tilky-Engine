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
                // Feature: Wall Mode is gone. Sector Mode now owns wall
                // creation entirely via the chain workflow below.
                if (currentMode == MODE_SECTOR) {
                    const Vector2 snapped = ResolveSnapPoint(mouseWorld);
                    TrySectorChainClick(snapped);
                }
                else if (currentMode == MODE_DOT) {
                    // Feature #6: dots place at the exact mouse position by
                    // default; holding Shift snaps the placement to grid.
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
                        if (t != nullptr) t->SetPosition({mouseWorld.x, mouseWorld.y, 0.0f});
                    }

                    // Left click only selects/places/moves — it must never
                    // open the inspector (feature: Entity Mode click behavior).
                    // editingEntity is intentionally NOT set here.
                }
            }

            //right click cancels/stops sector chain creation.
            if (currentMode == MODE_SECTOR && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) CancelSectorChain();

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
                if (currentMode == MODE_ENTITY)
                    HandleEntityModeRightClick(mouseWorld);
                else if (currentMode == MODE_SECTOR && sectorBeingCreated.empty() && !manualSectorMode)
                    // Only select/inspect when there's no chain in progress —
                    // the line above already uses right click to cancel a chain.
                    HandleSectorModeRightClick(mouseWorld);

                else if (currentMode == MODE_DOT) HandleDotModeRightClick(mouseWorld);

            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && holdingEntity && currentMode == MODE_ENTITY) {
                if (auto* t = selectedEntity.GetComponent<ComponentTransform>()) [[likely]]
                    t->SetPosition({mouseWorld.x, mouseWorld.y, t->position.z});
                else [[unlikely]] spdlog::error("Entity does not have transform component");
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) holdingEntity = false;


            UpdateEditorZoom();
        }

        if (!keyboardBlockedByImgui) {
            if (currentMode == MODE_SECTOR && InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) CancelSectorChain();
            if (InputManager::GetKeyDown(SDL_SCANCODE_F)) FocusCameraOnSelection();
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
                    if (!dots.empty()) {
                        RemoveDot(dots.back().id);
                    }
                    break;
                case ACTION_CREATE_WALL:
                    if (!level.walls.empty()) level.walls.pop_back();
                    break;
                case ACTION_CREATE_SECTOR:
                    if (!level.sectors.empty()) level.sectors.pop_back();
                    break;
                case ACTION_CREATE_OBJECT:
                    if (!level.entities.empty()) {
                        const Entity entity = level.entities.back();
                        level.DestroyEntity(entity.id);
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
    }
}