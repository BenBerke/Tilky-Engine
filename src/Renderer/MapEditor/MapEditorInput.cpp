#include "MapEditorInternal.hpp"

#include "Headers/Engine/InputManager.hpp"

namespace MapEditorInternal {
    void HandleEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();

                cameraPos.x -= mouseDelta.x;
                cameraPos.y += mouseDelta.y;
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
                        for (int i = static_cast<int>(MapEditor::sectors.size()) - 1; i >= 0; --i) {
                            if (IsPointInsidePolygon(MapEditor::sectors[i].vertices, mouseWorld)) {
                                editingSector = !editingSector;
                                selectedSector = i;
                                break;
                            }
                        }
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
                    }
                }
                else if (currentMode == MODE_OBJECT) {
                    if (currentObjectToPlace == PLAYER) {
                        if (playerPlaced) {
                            for (Object& object : objects) {
                                if (object.type == PLAYER) {
                                    object.position = mouseWorld;
                                }
                            }
                        }
                        else {
                            Object player = {PLAYER, mouseWorld};
                            playerPlaced = true;
                            objects.push_back(player);
                        }
                    }
                }
            }

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) && currentMode == MODE_WALL) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (CornerExistsAt(snappedWorld)) {
                    drawingLine = true;
                    lineStartWorld = snappedWorld;
                }
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && drawingLine && currentMode == MODE_WALL) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                const Vector2 startScreen = WorldToScreen(lineStartWorld, cameraPos);
                const Vector2 endScreen = WorldToScreen(snappedWorld, cameraPos);

                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                DrawThickLine(renderer, startScreen, endScreen, 5.0f);
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT) && drawingLine && currentMode == MODE_WALL) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (CornerExistsAt(snappedWorld) && !SamePoint(lineStartWorld, snappedWorld)) {
                    placedLines.push_back({
                        lineStartWorld,
                        snappedWorld
                    });

                    const Wall newWall(
                        lineStartWorld,
                        snappedWorld,
                        {0, 0, 0, 255},
                        -1,
                        -1,
                        0
                    );

                    MapEditor::walls.push_back(newWall);
                }

                drawingLine = false;
            }
        }

        if (!keyboardBlockedByImgui) {
            if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) {
                MoveMode();
            }

            if (InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) {
                quit = true;
            }
        }
    }
}