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
                else if (currentMode == MODE_WALL) {
                    const bool clickedOnCorder = CornerExistsAt(snappedWorld);

                    if (clickedOnCorder) {
                        drawingLine = true;
                        lineStartWorld = snappedWorld;
                    }

                    const int clickedWall = GetWallAtPoint(mouseWorld);

                    if (clickedWall != -1 && !clickedOnCorder && MapEditor::walls[clickedWall].floor == currentFloor) {
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
                else if (currentMode == MODE_OBJECT) {
                    bool objectFound = false;

                    for (int i = 0; i < static_cast<int>(MapEditor::objects.size()); ++i) {
                        if (WithinRadius(mouseWorld, MapEditor::objects[i].position, objectSize) &&
                            MapEditor::objects[i].floor == currentFloor) {
                            objectFound = true;

                            selectedObject = i;

                            if (currentObjectTypeToPlace == MapEditor::objects[i].type) {
                                editingObject = true;
                                holdingObject = true;
                            }

                            break;
                        }
                    }

                    if (!objectFound) {
                        bool skipCreating = false;
                        holdingObject = true;
                        Object newObject{};
                        newObject.id = static_cast<int>(MapEditor::objects.size());
                        newObject.type = currentObjectTypeToPlace;
                        newObject.position = mouseWorld;
                        newObject.textureIndex = 0;

                        if (currentObjectTypeToPlace == OBJ_PLAYER_SPAWN) {
                            if (playerPlaced) {
                                for (Object& object : MapEditor::objects) {
                                    if (object.type == OBJ_PLAYER_SPAWN) {
                                        object.position = mouseWorld;
                                        selectedObject = object.id;
                                        editingObject = true;
                                        break;
                                    }
                                }

                                return;
                            }

                            playerPlaced = true;
                        }
                        else if (currentObjectTypeToPlace == OBJ_DECAL) {
                            const int clickedWall = GetWallAtPoint(mouseWorld);
                            if (clickedWall != -1) {
                                newObject.wallIndex = clickedWall;
                                const Wall& wall = MapEditor::walls[clickedWall];
                                newObject.floor = wall.floor;
                                int sectorIndex = wall.frontSector;

                                if (sectorIndex < 0 ||
                                    sectorIndex >= static_cast<int>(MapEditor::sectors.size())) {
                                    sectorIndex = wall.backSector;
                                    }

                                if (sectorIndex >= 0 &&
                                    sectorIndex < static_cast<int>(MapEditor::sectors.size())) {
                                    const Sector& sector = MapEditor::sectors[sectorIndex];

                                    const float sectorHeight =
                                        sector.ceilingHeight - sector.floorHeight;

                                    const int floor = std::clamp(
                                        wall.floor,
                                        0,
                                        std::max(1, sector.floorCount) - 1
                                    );

                                    newObject.decalBaseHeight =
                                        sector.floorHeight + sectorHeight * static_cast<float>(floor);
                                    }
                            }
                            else {
                                skipCreating = true;
                                holdingObject = false;
                            }
                        }

                        if (!skipCreating)
                        MapEditor::objects.push_back(newObject);

                        selectedObject = static_cast<int>(MapEditor::objects.size()) - 1;
                        editingObject = true;
                    }
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
                    const Wall newWall(
                        lineStartWorld,
                        snappedWorld,
                        {0, 0, 0, 255},
                        -1,
                        -1,
                        0,
                        currentFloor
                    );

                    MapEditor::walls.push_back(newWall);
                    actions.push_back(ACTION_CREATE_WALL);
                }

                drawingLine = false;
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && holdingObject) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

                MapEditor::objects[selectedObject].position = mouseWorld;
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
                holdingObject = false;
            }
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) {
            quit = true;
        }

        if (!keyboardBlockedByImgui) {

        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) {
            MoveMode();
        }
        if (InputManager::GetDoubleKeyDown(SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z)) {
            if (actions.empty()) return;
            switch (actions.back()) {
                case ACTION_CREATE_CORNER:
                    placedCorners.pop_back();
                    break;
                case ACTION_CREATE_WALL:
                    MapEditor::walls.pop_back();
                    break;
                case ACTION_CREATE_SECTOR:
                    MapEditor::sectors.pop_back();
                    break;
                case ACTION_CREATE_OBJECT:
                    MapEditor::objects.pop_back();
                    break;
                default: break;
            }
            actions.pop_back();
        }
    }
}