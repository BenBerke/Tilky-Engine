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

        editorZoom = std::clamp(
            editorZoom,
            MIN_EDITOR_ZOOM,
            MAX_EDITOR_ZOOM
        );

        if (zoomChanged) {
            const Vector2 mouseWorldAfterZoom = ScreenToWorld(mouseScreen, cameraPos);

            cameraPos.x += mouseWorldBeforeZoom.x - mouseWorldAfterZoom.x;
            cameraPos.y += mouseWorldBeforeZoom.y - mouseWorldAfterZoom.y;
        }
    }

    void HandleEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();

                cameraPos.x -= mouseDelta.x / editorZoom;
                cameraPos.y += mouseDelta.y / editorZoom;
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (currentMode == MODE_SECTOR) {
                    if (CornerExistsAt(snappedWorld)) {
                        AddSectorSelectionPoint(snappedWorld);
                        creatableSector = sectorBeingCreated.size() >= 3;
                    }
                    else {
                        for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i) {
                            if (IsPointInsidePolygon(level.sectors[i].vertices, mouseWorld)) {
                                editingSector = !editingSector;
                                selectedSector = i;
                                break;
                            }
                        }
                    }
                }
                else if (currentMode == MODE_WALL) {
                    const bool clickedOnCorder = CornerExistsAt(snappedWorld);

                    if (clickedOnCorder) {
                        drawingLine = true;
                        lineStartWorld = snappedWorld;
                    }

                    const int clickedWall = GetWallAtPoint(mouseWorld);

                    if (clickedWall != -1 && !clickedOnCorder && level.walls[clickedWall].floor == currentFloor) {
                        selectedWall = clickedWall;
                        editingWall = true;
                    }
                }
                else if (currentMode == MODE_DOT) {
                    bool cornerAlreadyExists = false;

                    for (int i = 0; i < static_cast<int>(placedCorners.size()); ++i) {
                        if (SamePoint(placedCorners[i], snappedWorld)) {
                            cornerAlreadyExists = true;

                            if (IsCornerConnectedToLine(snappedWorld)) {
                                break;
                            }

                            placedCorners.erase(placedCorners.begin() + i);
                            break;
                        }
                    }

                    if (!cornerAlreadyExists) {
                        placedCorners.push_back(snappedWorld);
                        actions.push_back(ACTION_CREATE_CORNER);
                    }
                }
                else if (currentMode == MODE_ENTITY) {
                    Entity *en = EntityAt(mouseWorld);

                    if (en != nullptr) {
                        selectedEntity = *en;
                        holdingEntity = true;
                    } else {
                        static constexpr bool isUIEntity = false;
                        const ID id = level.CreateEntity(isUIEntity);
                        selectedEntity = *level.GetEntity(id);

                        auto *t = selectedEntity.GetComponent<ComponentTransform>();
                        if (t != nullptr) {
                            t->SetPosition({mouseWorld.x, mouseWorld.y, 0.0f});
                            t->floor = currentFloor;
                        }
                    }

                    editingEntity = true;

                }
            }
            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (drawingLine && currentMode == MODE_WALL) {
                    const Vector2 startScreen = WorldToScreen(lineStartWorld, cameraPos);
                    const Vector2 endScreen = WorldToScreen(snappedWorld, cameraPos);

                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    DrawThickLine(renderer, startScreen, endScreen, 5.0f);
                }
                else if (holdingEntity && currentMode == MODE_ENTITY) {
                    if (auto* t = selectedEntity.GetComponent<ComponentTransform>()) [[likely]] {
                        t->SetPosition({mouseWorld.x, mouseWorld.y, t->position.z});
                    }
                    else [[unlikely]] spdlog::error("Entity does not have transform component");
                }
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (drawingLine && currentMode == MODE_WALL) {
                    if (CornerExistsAt(snappedWorld) && !SamePoint(lineStartWorld, snappedWorld)) {
                        const Wall newWall(
                            lineStartWorld,
                            snappedWorld,
                            {255, 255, 255, 255},
                            -1,
                            -1,
                            0,
                            currentFloor
                        );

                        level.walls.push_back(newWall);
                        actions.push_back(ACTION_CREATE_WALL);
                    }
                }
                drawingLine = false;
                holdingEntity = false;
            }

            UpdateEditorZoom();
        }

        if (!keyboardBlockedByImgui) {
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) {
            ChangeMode();
        }

        if (InputManager::QuitRequested()) {
            shutdown = true;
            quit = true;
        }

        if (InputManager::GetDoubleKeyDown(SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z)) {
            if (actions.empty()) return;
            switch (actions.back()) {
                case ACTION_CREATE_CORNER:
                    placedCorners.pop_back();
                    break;
                case ACTION_CREATE_WALL:
                    level.walls.pop_back();
                    break;
                case ACTION_CREATE_SECTOR:
                    level.sectors.pop_back();
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
    }
}