#include "MapEditorInternal.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Map/LevelManager.hpp"
#include "../../Headers/Engine/Local/Local.hpp"

namespace MapEditorInternal {
    void MoveMode() {
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    }

    void DrawEditorUI() {
        using namespace Localisation;

        Level& level = LevelManager::CurrentLevel();

        ImGui::Begin(Get("editor.title").c_str());

        auto PutSpace = [](const int amount) {
            for (int i = 0; i < amount; i++) ImGui::Spacing();
        };

        if (ImGui::Button(Get("editor.mode").c_str())) {
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

        auto GetModeName = [](const int mode) -> const char* {
            switch (mode) {
                case MODE_DOT:
                    return Localisation::Get("mode.dot").c_str();

                case MODE_WALL:
                    return Localisation::Get("mode.wall").c_str();

                case MODE_SECTOR:
                    return Localisation::Get("mode.sector").c_str();

                case MODE_OBJECT:
                    return Localisation::Get("mode.object").c_str();

                default:
                    return Localisation::Get("mode.unknown").c_str();
            }
        };

        ImGui::Text("%s", GetModeName(currentMode));

        //region EDITING

        if (editingSector && currentMode == MODE_SECTOR && selectedSector != -1) {
            if (selectedSector < 0 ||
                selectedSector >= static_cast<int>(level.sectors.size())) {
                editingSector = false;
            }
            else {
                ImGui::Begin(Get("sector").c_str(), &editingSector);

                auto& sector = level.sectors[selectedSector];

                float ceilHeight = sector.ceilingHeight;
                float floorHeight = sector.floorHeight;
                int floorTexture = sector.floorTextureIndex;
                int floorCount = sector.floorCount;

                Vector3 ceilColor = sector.ceilingColor;
                Vector3 floorColor = sector.floorColor;

                ImGui::InputFloat(Get("sector.ceil_height").c_str(), &ceilHeight);
                ImGui::InputFloat(Get("sector.floor_height").c_str(), &floorHeight);
                ImGui::InputInt(Get("sector.floor_count").c_str(), &floorCount);

                floorCount = std::clamp(floorCount, 1, MAX_FLOOR_COUNT);

                ImGui::InputInt(Get("sector.ground_floor_texture").c_str(), &floorTexture);

                for (int i = 0; i < floorCount; ++i) {
                    std::string label =
                        Get("sector.ceiling_texture") + " " + std::to_string(i + 1);

                    ImGui::InputInt(label.c_str(), &sector.ceilingTextureIndices[i]);
                }

                ImGui::InputFloat3(Get("sector.ceiling_color").c_str(), &ceilColor.x);
                ImGui::InputFloat3(Get("sector.floor_color").c_str(), &floorColor.x);

                sector.ceilingHeight = ceilHeight;
                sector.floorHeight = floorHeight;
                sector.floorTextureIndex = floorTexture;
                sector.ceilingColor = ceilColor;
                sector.floorColor = floorColor;
                sector.floorCount = floorCount;

                if (ImGui::Button(Get("common.delete").c_str())) {
                    level.sectors.erase(level.sectors.begin() + selectedSector);
                    editingSector = false;
                }

                if (ImGui::Button(Get("common.close").c_str())) {
                    editingSector = false;
                }

                ImGui::Text("%s: %d", Get("common.id").c_str(), selectedSector);

                ImGui::End();
            }
        }

        if (editingWall && currentMode == MODE_WALL && selectedWall != -1) {
            if (selectedWall < 0 ||
                selectedWall >= static_cast<int>(level.walls.size())) {
                editingWall = false;
            }
            else {
                ImGui::Begin(Get("wall.title").c_str(), &editingWall);

                auto& wall = level.walls[selectedWall];

                Vector4 color = wall.color;

                int frontSector = wall.frontSector;
                int backSector = wall.backSector;
                int textureIndex = wall.textureIndex;

                int floor = wall.floor;

                ImGui::InputInt(Get("wall.front_sector").c_str(), &frontSector);
                ImGui::InputInt(Get("wall.back_sector").c_str(), &backSector);
                ImGui::InputInt(Get("wall.texture_index").c_str(), &textureIndex);
                ImGui::InputInt(Get("wall.floor").c_str(), &floor);

                ImGui::InputFloat4(Get("wall.color").c_str(), &color.x);

                wall.color = color;
                wall.textureIndex = textureIndex;
                wall.frontSector = frontSector;
                wall.backSector = backSector;
                wall.floor = floor;

                if (ImGui::Button(Get("common.delete").c_str())) {
                    level.walls.erase(level.walls.begin() + selectedWall);
                    editingWall = false;
                }

                if (ImGui::Button(Get("common.close").c_str())) {
                    editingWall = false;
                }

                ImGui::Text("%s: %d", Get("common.id").c_str(), selectedWall);

                ImGui::End();
            }
        }
        //endregion

        if (creatableSector) {
            if (ImGui::Button(Get("editor.create_sector").c_str())) {
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

        PutSpace(2);

        if (ImGui::Button(Get("editor.create_texture").c_str())) {
            textureInputs.push_back({});
        }

        for (int i = 0; i < static_cast<int>(textureInputs.size()); ++i) {
            ImGui::PushID(i);

            std::string label =
                Get("editor.texture") + " " + std::to_string(i);

            ImGui::InputText(label.c_str(), textureInputs[i].data(), textureInputs[i].size());

            ImGui::PopID();
        }

        PutSpace(5);

        int bgTextureIndex = MapEditor::backgroundTextureIndex;
        ImGui::InputInt(Get("editor.background_texture").c_str(), &bgTextureIndex);
        MapEditor::backgroundTextureIndex = bgTextureIndex;

        if (currentMode == MODE_OBJECT) {

        }

        ImGui::InputInt(Get("editor.floor").c_str(), &currentFloor);

        if (ImGui::Button(Get("editor.save").c_str())) {
            Save(MapEditor::currentMap);
        }

        PutSpace(1);

        if (ImGui::Button(Get("editor.save_and_play").c_str())) {
            if (Save(MapEditor::currentMap)) {
                SDL_Log("%s", MapEditor::currentMap.c_str());
                quit = true;
            }
        }

        PutSpace(3);

        if (ImGui::Button(Get("editor.shutdown").c_str())) {
            quit = true;
        }

        static char buf[64] = "";

        if (buf[0] == '\0' && !MapEditor::currentMap.empty()) {
            std::strncpy(buf, MapEditor::currentMap.c_str(), sizeof(buf) - 1);
        }

        if (ImGui::InputText(Get("editor.level_name").c_str(), buf, IM_ARRAYSIZE(buf))) {
            MapEditor::currentMap = buf;
        }

        ImGui::End();

        ImGui::Begin(Get("levels.title").c_str());

        for (int i = 0; i < static_cast<int>(MapEditor::maps.size()); ++i) {
            ImGui::PushID(i);

            std::string cleanName = MapEditor::maps[i];

            ImGui::Text("%s", cleanName.c_str());

            if (ImGui::Button(Get("levels.load").c_str())) {
                Save(MapEditor::currentMap);
                MapEditor::LoadLevel(cleanName);
            }

            ImGui::SameLine();

            if (ImGui::Button(Get("levels.delete").c_str())) {
                const std::string path = "../EngineAssets/Levels/" + cleanName + ".json";

                try {
                    if (std::filesystem::remove(path)) {
                        SDL_Log(
                            "%s %s",
                            Get("log.deleted_level").c_str(),
                            path.c_str()
                        );

                        if (MapEditor::currentMap == cleanName) {
                            MapEditor::currentMap = "test_level";
                            MapEditor::LoadLevel(MapEditor::currentMap);
                        }

                        UpdateLevels();
                    }
                    else {
                        SDL_Log(
                            "%s %s",
                            Get("log.failed_delete_missing").c_str(),
                            path.c_str()
                        );
                    }
                }
                catch (const std::filesystem::filesystem_error& e) {
                    SDL_Log(
                        "%s %s",
                        Get("log.delete_level_failed").c_str(),
                        e.what()
                    );
                }
            }

            ImGui::PopID();
        }

        ImGui::End();
    }
}