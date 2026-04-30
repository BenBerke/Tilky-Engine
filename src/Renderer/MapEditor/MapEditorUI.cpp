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

        //ImGui::Text("\nDots: %d", static_cast<int>(placedCorners.size()));
        //ImGui::Text("Lines: %d", static_cast<int>(MapEditor::walls.size()));
        //ImGui::Text("Sectors: %d", static_cast<int>(MapEditor::sectors.size()));

        //region EDITING

        if (editingSector && currentMode == MODE_SECTOR && selectedSector != -1) {
            ImGui::Begin("Sector", &editingSector);

            auto& sector = MapEditor::sectors[selectedSector];

            float ceilHeight = sector.ceilingHeight;
            float floorHeight = sector.floorHeight;
            int floorTexture = sector.floorTextureIndex;
            int floorCount = sector.floorCount;

            Vector3 ceilColor = sector.ceilingColor;
            Vector3 floorColor = sector.floorColor;

            ImGui::InputFloat("Ceil Height", &ceilHeight);
            ImGui::InputFloat("Floor Height", &floorHeight);
            ImGui::InputInt("Floor Count", &floorCount);

            floorCount = std::clamp(floorCount, 1, MAX_FLOOR_COUNT);

            ImGui::InputInt("Ground Floor Texture", &floorTexture);

            for (int i = 0; i < floorCount; ++i) {
                std::string label = "Ceiling Texture " + std::to_string(i + 1);
                ImGui::InputInt(label.c_str(), &sector.ceilingTextureIndices[i]);
            }

            ImGui::InputFloat3("Ceiling Color", &ceilColor.x);
            ImGui::InputFloat3("Floor Color", &floorColor.x);

            sector.ceilingHeight = ceilHeight;
            sector.floorHeight = floorHeight;
            sector.floorTextureIndex = floorTexture;
            sector.ceilingColor = ceilColor;
            sector.floorColor = floorColor;
            sector.floorCount = floorCount;

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

            auto& wall = MapEditor::walls[selectedWall];

            Vector4 color = wall.color;

            int frontSector = wall.frontSector;
            int backSector = wall.backSector;
            int textureIndex = wall.textureIndex;

            int floor = wall.floor;;

            ImGui::InputInt("Front Sector", &frontSector);
            ImGui::InputInt("Back Sector", &backSector);
            ImGui::InputInt("Texture Index", &textureIndex);
            ImGui::InputInt("Floor", &floor);

            ImGui::InputFloat4("Wall Color", &color.x);

            wall.color = color;
            wall.textureIndex = textureIndex;
            wall.frontSector = frontSector;
            wall.backSector = backSector;
            wall.floor = floor;

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
            ImGui::Begin("Object");

            auto& object = MapEditor::objects[selectedObject];

            Vector2 position = object.position;
            int textureIndex = object.textureIndex;

            ImGui::Text("ID:%d", object.id);
            ImGui::Text("Type:%s", GetObjectName(object.type));
            ImGui::InputFloat2("Position", &position.x);
            ImGui::InputInt("Texture Index", &textureIndex);

            object.position = position;
            object.textureIndex = textureIndex;

            if (object.type != OBJ_PLAYER_SPAWN && ImGui::Button("Delete")) {
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

        PutSpace(5);

        if (currentMode == MODE_OBJECT) {
            ImGui::Text("Object Details");
            PutSpace(3);
            const char* items[OBJ_COUNT] = {"Player Spawn", "Sprite", "Decal"};
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
            PutSpace(5);
        }
        ImGui::InputInt("Floor", &currentFloor);

        if (ImGui::Button("Save")) Save(MapEditor::currentMap);
        PutSpace(1);

        if (ImGui::Button("Save & Play")) {
            if (Save(MapEditor::currentMap)) {
                SDL_Log("%s", MapEditor::currentMap.c_str());
                quit = true;
            }
        }
        PutSpace(3);

        if (ImGui::Button("Shutdown")) {
            quit = true;
        }

        static char buf[64] = "";

        if (buf[0] == '\0' && !MapEditor::currentMap.empty()) {
            strncpy(buf, MapEditor::currentMap.c_str(), sizeof(buf) - 1);
        }

        if (ImGui::InputText("Level Name", buf, IM_ARRAYSIZE(buf))) {
            MapEditor::currentMap = buf;
        }

        ImGui::End(); // Editor

        ImGui::Begin("Levels");
        ImGui::Begin("Levels");

        for (int i = 0; i < static_cast<int>(MapEditor::maps.size()); ++i) {
            ImGui::PushID(i);

            std::string mapName = MapEditor::maps[i];
            std::string cleanName = mapName.substr(0, mapName.find('.'));

            ImGui::Text("%s", cleanName.c_str());

            if (ImGui::Button("Load Level")) {
                Save(MapEditor::currentMap);
                MapEditor::LoadLevel(cleanName);
            }

            ImGui::SameLine();

            if (ImGui::Button("Delete Level")) {
                const std::string path = "../Assets/Levels/" + cleanName + ".json";

                try {
                    if (std::filesystem::remove(path)) {
                        SDL_Log("Deleted level: %s", path.c_str());

                        if (MapEditor::currentMap == cleanName) {
                            MapEditor::currentMap = "test_level";
                            MapEditor::LoadLevel(MapEditor::currentMap);
                        }

                        UpdateLevels();
                    }
                    else {
                        SDL_Log("Failed to delete level, file may not exist: %s", path.c_str());
                    }
                }
                catch (const std::filesystem::filesystem_error& e) {
                    SDL_Log("Delete level failed: %s", e.what());
                }
            }

            ImGui::PopID();
        }

        ImGui::End();
        ImGui::End();
    }
}
