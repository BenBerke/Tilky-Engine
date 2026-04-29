#include "MapEditorInternal.hpp"

#include "imgui.h"

#include <array>
#include <string>
#include <vector>

#include "Headers/Renderer/TextureManager.hpp"

namespace MapEditorInternal {
    void MoveMode() {
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    }

    void DrawEditorUI() {
        ImGui::Begin("Editor");

        auto PutSpace = [](const int amount) {
            for (int i = 0; i < amount; i++) ImGui::Spacing();
        };

        if (ImGui::Button("Mode")) {
            const Mode previousMode = currentMode;

            MoveMode();

            if (previousMode == MODE_SECTOR) {
                FinishSectorSelection();
                creatableSector = false;
            }

            if (currentMode == MODE_SECTOR) {
                sectorBeingCreated.clear();
                creatableSector = false;
            }
        }

        auto GetModeName = [](const int mode) {
            switch (mode) {
                case MODE_DOT:
                    return "Dot Mode";

                case MODE_WALL:
                    return "Wall Mode";

                case MODE_SECTOR:
                    return "Sector Mode";

                case MODE_OBJECT:
                    return "Object Mode";

                default:
                    return "Unknown Mode";
            }
        };

        ImGui::Text("%s", GetModeName(currentMode));

        ImGui::Text("\nDots: %d", static_cast<int>(placedCorners.size()));
        ImGui::Text("Lines: %d", static_cast<int>(MapEditor::walls.size()));
        ImGui::Text("Sectors: %d", static_cast<int>(MapEditor::sectors.size()));

        //region EDITING

        if (editingSector && currentMode == MODE_SECTOR && selectedSector != -1) {
            ImGui::Begin("Sector", &editingSector);

            float ceilHeight = MapEditor::sectors[selectedSector].ceilingHeight;
            float floorHeight = MapEditor::sectors[selectedSector].floorHeight;

            int floorTexture = MapEditor::sectors[selectedSector].floorTextureIndex;
            int ceilTexture = MapEditor::sectors[selectedSector].ceilingTextureIndex;

            Vector3 ceilColor = MapEditor::sectors[selectedSector].ceilingColor;
            Vector3 floorColor = MapEditor::sectors[selectedSector].floorColor;

            ImGui::InputFloat("Ceil Height", &ceilHeight);
            ImGui::InputFloat("Floor Height", &floorHeight);
            ImGui::InputInt("Floor Texture Index", &floorTexture);
            ImGui::InputInt("Ceiling Texture Index", &ceilTexture);
            ImGui::InputFloat3("Ceiling Color", &ceilColor.x);
            ImGui::InputFloat3("Floor Color", &floorColor.x);

            MapEditor::sectors[selectedSector].ceilingHeight = ceilHeight;
            MapEditor::sectors[selectedSector].floorHeight = floorHeight;
            MapEditor::sectors[selectedSector].floorTextureIndex = floorTexture;
            MapEditor::sectors[selectedSector].ceilingTextureIndex = ceilTexture;
            MapEditor::sectors[selectedSector].ceilingColor = ceilColor;
            MapEditor::sectors[selectedSector].floorColor = floorColor;

            if (ImGui::Button("Delete")) {
                MapEditor::sectors.erase(MapEditor::sectors.begin() + selectedSector);
                editingSector = false;
            }

            if (ImGui::Button("Close")) {
                editingSector = false;
            }

            ImGui::Text("ID: %d", selectedSector);

            ImGui::End();
        }

        if (editingWall && currentMode == MODE_WALL && selectedWall != -1) {
            ImGui::Begin("Wall", &editingSector);

            Vector4 color = MapEditor::walls[selectedWall].color;

            int frontSector = MapEditor::walls[selectedWall].frontSector;
            int backSector = MapEditor::walls[selectedWall].backSector;
            int textureIndex = MapEditor::walls[selectedWall].textureIndex;

            int floor = MapEditor::walls[selectedWall].floor;;

            ImGui::InputInt("Front Sector", &frontSector);
            ImGui::InputInt("Back Sector", &backSector);
            ImGui::InputInt("Texture Index", &textureIndex);
            ImGui::InputInt("Floor", &floor);

            ImGui::InputFloat4("Wall Color", &color.x);

            MapEditor::walls[selectedWall].color = color;
            MapEditor::walls[selectedWall].textureIndex = textureIndex;
            MapEditor::walls[selectedWall].frontSector = frontSector;
            MapEditor::walls[selectedWall].backSector = backSector;
            MapEditor::walls[selectedWall].floor = floor;

            if (ImGui::Button("Delete")) {
                MapEditor::walls.erase(MapEditor::walls.begin() + selectedWall);
                editingWall = false;
            }

            if (ImGui::Button("Close")) {
                editingWall = false;
            }

            ImGui::Text("ID: %d", selectedWall);

            ImGui::End();
        }

        auto GetObjectName = [](const int enumType) {
            switch (enumType) {
                case OBJ_PLAYER_SPAWN:
                    return "Player Spawn";
                break;
                case OBJ_SPRITE:
                    return "Sprite";
                    break;
                default: return "Unknown"; break;
            }
        };

        if (editingObject && currentMode == MODE_OBJECT && selectedObject != -1) {
            ImGui::Begin("Object", &editingSector);

            Vector2 position = MapEditor::objects[selectedObject].position;
            int textureIndex = MapEditor::objects[selectedObject].textureIndex;

            ImGui::Text("ID:%d", MapEditor::objects[selectedObject].id);
            ImGui::Text("Type:%s", GetObjectName(MapEditor::objects[selectedObject].type));
            ImGui::InputFloat2("Position", &position.x);
            ImGui::InputInt("Texture Index", &textureIndex);

            MapEditor::objects[selectedObject].position = position;
            MapEditor::objects[selectedObject].textureIndex = textureIndex;

            if (MapEditor::objects[selectedObject].type != OBJ_PLAYER_SPAWN && ImGui::Button("Delete")) {
                MapEditor::objects.erase(MapEditor::objects.begin() + selectedObject);
                editingObject = false;
            }

            if (ImGui::Button("Close")) {
                editingObject = false;
            }

            ImGui::End();
        }
        //endregion

        if (creatableSector) {
            if (ImGui::Button("Create Sector")) {
                if (sectorBeingCreated.size() >= 3) {
                    if (!SamePoint(sectorBeingCreated.front(), sectorBeingCreated.back())) {
                        sectorBeingCreated.push_back(sectorBeingCreated.front());
                    }

                    FinishSectorSelection();
                    actions.push_back(ACTION_CREATE_SECTOR);

                    creatableSector = false;
                }
            }
        }

        if (ImGui::Button("Create Texture")) {
            textureInputs.push_back({});
        }

        for (int i = 0; i < static_cast<int>(textureInputs.size()); ++i) {
            ImGui::PushID(i);

            std::string label = "Texture " + std::to_string(i);
            ImGui::InputText(label.c_str(), textureInputs[i].data(), textureInputs[i].size());

            ImGui::PopID();
        }

        if (currentMode == MODE_OBJECT) {
            ImGui::Text("Object Details");
            PutSpace(3);
            const char* items[OBJ_COUNT] = {"Player Spawn", "Sprite"};
            static int item_current = 0;
            if (ImGui::BeginCombo("Select Type", items[item_current])) {
                for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
                    const bool is_selected = (item_current == n);
                    if (ImGui::Selectable(items[n], is_selected)) {
                        item_current = n;
                        currentObjectTypeToPlace = static_cast<ObjectType>(item_current);
                        currentObjectTypeToPlace = static_cast<ObjectType>(n);
                    };
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Close")) {
                editingObject = false;
            }
        }

        ImGui::InputInt("Floor", &currentFloor);

        if (ImGui::Button("Save & Play")) {
            if (SaveAndQuit()) {
                quit = true;
            }
        }
        PutSpace(3);

        if (ImGui::Button("Shutdown")) {
            quit = true;
        }

        ImGui::End();
    }
}
