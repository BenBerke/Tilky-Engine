#include "MapEditorInternal.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <optional>

#include "Headers/Map/LevelManager.hpp"
#include "../../Headers/Engine/Local/Local.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "misc/cpp/imgui_stdlib.h"

namespace {
    bool addingComponent = false;
    int selectedComponent = -1;

    std::optional<std::string> pendingLevelToLoad;
}


namespace MapEditorInternal {
    void ChangeMode() {
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    }

    void QueueLevelLoad(const std::string& levelName) {
        pendingLevelToLoad = levelName;
        spdlog::info("Queued level load: {}", levelName);
    }

    bool ProcessPendingLevelLoad() {
        if (!pendingLevelToLoad.has_value()) {
            return false;
        }

        const std::string levelToLoad = *pendingLevelToLoad;
        pendingLevelToLoad.reset();

        addingComponent = false;
        selectedComponent = -1;

        editingSector = false;
        selectedSector = -1;

        editingWall = false;
        selectedWall = -1;

        editingEntity = false;
        editingComponent = false;

        creatableSector = false;
        sectorBeingCreated.clear();
        actions.clear();

        spdlog::info("Processing queued level load: {}", levelToLoad);

        MapEditor::LoadLevel(levelToLoad);

        return true;
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

            ChangeMode();

            if (previousMode == MODE_SECTOR) {
                FinishSectorSelection();
                creatableSector = false;
            }

            if (currentMode == MODE_SECTOR) {
                sectorBeingCreated.clear();
                creatableSector = false;
            }
        }

        auto GetModeName = [](const int mode) -> std::string  {
            switch (mode) {
                case MODE_DOT:
                    return Get("mode.dot");

                case MODE_WALL:
                    return Get("mode.wall");

                case MODE_SECTOR:
                    return Get("mode.sector");

                case MODE_ENTITY:
                    return Get("mode.entity");

                default:
                    return Get("mode.unknown");
            }
        };

        const std::string modeName = GetModeName(currentMode);
        ImGui::Text("%s", modeName.c_str());

        //region EDITING

        if (editingSector && currentMode == MODE_SECTOR && selectedSector != -1) {
            if (selectedSector < 0 ||
                selectedSector >= static_cast<int>(level.sectors.size())) {
                editingSector = false;
            }
            else {
                ImGui::Begin(Get("sector.title").c_str(), &editingSector);

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

        if (editingEntity && currentMode == MODE_ENTITY) {
            Entity *entityPtr = nullptr;

            for (Entity &entity: level.entities) {
                if (entity.id == selectedEntity.id) {
                    entityPtr = &entity;
                    break;
                }
            }

            if (entityPtr == nullptr) {
                editingEntity = false;
                editingComponent = false;
                selectedComponent = -1;
            } else {
                Entity &entity = *entityPtr;

                ImGui::Begin(Get("entity.title").c_str(), &editingEntity);

                ImGui::Text("%u", entity.id);
                ImGui::SameLine();
                ImGui::InputText(Get("entity.name").c_str(), &entity.name);

                if (ImGui::Button(Get("entity.add_component").c_str())) {
                    addingComponent = !addingComponent;
                }

                static int componentToAdd = CMP_SPRITE;

                if (addingComponent) {
                    ImGui::PushID("add_component_combo");

                    std::array<std::string, CMP_COUNT> componentNames;

                    componentNames[CMP_TRANSFORM] = Get("component.transform");
                    componentNames[CMP_SPRITE] = Get("component.sprite");
                    componentNames[CMP_DECAL] = Get("component.decal");
                    componentNames[CMP_PLAYER_SPAWN] = Get("component.player_spawn");

                    if (ImGui::BeginCombo(Get("component.component").c_str(),componentNames[componentToAdd].c_str())) {
                        for (int i = 0; i < CMP_COUNT; i++) {
                            if (i == CMP_TRANSFORM) {
                                continue;
                            }

                            const bool isSelected = componentToAdd == i;

                            if (ImGui::Selectable(componentNames[i].c_str(), isSelected)) {
                                componentToAdd = i;
                            }

                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }

                        ImGui::EndCombo();
                    }

                    if (ImGui::Button(Get("common.add").c_str())) {
                        if (componentToAdd == CMP_SPRITE &&
                            !entity.HasComponent<ComponentSprite>()) {
                            entity.AddComponent<ComponentSprite>();
                        } else if (componentToAdd == CMP_DECAL &&
                                   !entity.HasComponent<ComponentDecal>()) {
                            entity.AddComponent<ComponentDecal>();
                        } else if (componentToAdd == CMP_PLAYER_SPAWN &&
                                   !entity.HasComponent<ComponentPlayerSpawn>()) {
                            entity.AddComponent<ComponentPlayerSpawn>();
                        }

                        addingComponent = false;
                        componentToAdd = CMP_SPRITE;
                    }

                    ImGui::SameLine();

                    if (ImGui::Button(Get("common.cancel").c_str())) {
                        addingComponent = false;
                        componentToAdd = CMP_SPRITE;
                    }

                    ImGui::PopID();
                }

                PutSpace(2);
                ImGui::Text("%s", Get("entity.components").c_str());

                auto DrawComponentRow = [&](const char *label, const int componentType) {
                    ImGui::PushID(componentType);

                    ImGui::Text("%s", label);
                    ImGui::SameLine();

                    if (ImGui::Button(Get("common.edit").c_str())) {
                        selectedComponent = componentType;
                        editingComponent = true;
                    }

                    ImGui::PopID();
                };

                if (entity.HasComponent<ComponentTransform>())
                    DrawComponentRow(Get("component.transform").c_str(), CMP_TRANSFORM);

                if (entity.HasComponent<ComponentSprite>())
                    DrawComponentRow(Get("component.sprite").c_str(), CMP_SPRITE);

                if (entity.HasComponent<ComponentDecal>())
                    DrawComponentRow(Get("component.decal").c_str(), CMP_DECAL);

                if (entity.HasComponent<ComponentPlayerSpawn>())
                    DrawComponentRow(Get("component.player_spawn").c_str(), CMP_PLAYER_SPAWN);

                PutSpace(2);

                ImGui::PushID("entity_buttons");

                if (ImGui::Button(Get("common.delete").c_str())) {
                    const EntityID idToDelete = entity.id;

                    editingComponent = false;
                    selectedComponent = -1;
                    editingEntity = false;

                    level.DestroyEntity(idToDelete);
                }

                ImGui::SameLine();

                if (ImGui::Button(Get("common.close").c_str())) {
                    editingComponent = false;
                    selectedComponent = -1;
                    editingEntity = false;
                }

                ImGui::PopID();

                ImGui::End();
            }
        }

        if (editingComponent && currentMode == MODE_ENTITY && selectedComponent != -1) {
            Entity *entityPtr = nullptr;

            for (Entity &entity: level.entities) {
                if (entity.id == selectedEntity.id) {
                    entityPtr = &entity;
                    break;
                }
            }

            if (entityPtr == nullptr) {
                editingComponent = false;
                selectedComponent = -1;
            } else {
                Entity &entity = *entityPtr;

                std::string componentName;

                switch (selectedComponent) {
                    case CMP_TRANSFORM:
                        componentName = Get("component.transform");
                        break;
                    case CMP_SPRITE:
                        componentName = Get("component.sprite");
                        break;
                    case CMP_DECAL:
                        componentName = Get("component.decal");
                        break;
                    case CMP_PLAYER_SPAWN:
                        componentName = Get("component.player_spawn");
                        break;
                    default:
                        componentName = Get("bug.unknown");
                        break;
                }

                const std::string windowTitle = componentName + "##component_editor";

                ImGui::Begin(windowTitle.c_str(), &editingComponent);
                ImGui::PushID(selectedComponent);

                if (selectedComponent == CMP_TRANSFORM) {
                    ComponentTransform *c = entity.GetComponent<ComponentTransform>();

                    if (c == nullptr) {
                        ImGui::Text("Transform component missing");
                    } else {
                        float positionValues[2] = {c->position.x, c->position.y};
                        float scaleValues[2] = {c->scale.x, c->scale.y};
                        int floor = c->floor;

                        ImGui::Text("%s", Get("component.transform.position").c_str());
                        ImGui::Text("X    Y");
                        ImGui::SetNextItemWidth(220.0f);
                        ImGui::InputFloat2("##position", positionValues);

                        ImGui::Spacing();

                        ImGui::Text("%s", Get("component.transform.scale").c_str());
                        ImGui::Text("X    Y");
                        ImGui::SetNextItemWidth(220.0f);
                        ImGui::InputFloat2("##scale", scaleValues);

                        ImGui::Spacing();

                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputInt(Get("component.transform.floor").c_str(), &floor);

                        c->position = {positionValues[0], positionValues[1]};
                        c->scale = {scaleValues[0], scaleValues[1]};
                        c->floor = floor;

                        ImGui::Spacing();

                        if (ImGui::Button(Get("common.delete").c_str())) {
                            entity.RemoveComponent<ComponentTransform>();
                            editingComponent = false;
                            selectedComponent = -1;
                        }
                    }
                } else if (selectedComponent == CMP_SPRITE) {
                    ComponentSprite *c = entity.GetComponent<ComponentSprite>();

                    if (c == nullptr) {
                        ImGui::Text("Sprite component missing");
                    } else {
                        int textureIndex = c->textureIndex;

                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputInt(Get("component.sprite.texture_index").c_str(), &textureIndex);

                        c->textureIndex = textureIndex;

                        if (ImGui::Button(Get("common.delete").c_str())) {
                            entity.RemoveComponent<ComponentSprite>();
                            editingComponent = false;
                            selectedComponent = -1;
                        }
                    }
                } else if (selectedComponent == CMP_PLAYER_SPAWN) {
                    ImGui::Text("%s", Get("component.player_spawn").c_str());

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentPlayerSpawn>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                } else if (selectedComponent == CMP_DECAL) {
                    ComponentDecal *c = entity.GetComponent<ComponentDecal>();

                    if (c == nullptr) {
                        ImGui::Text("Decal component missing");
                    } else {
                        int wallIndex = c->wallIndex;
                        float verticalPos = c->verticalPos;
                        float horizontalPos = c->horizontalPos;
                        float wallNormalOffset = c->wallNormalOffset;
                        float wallT = c->wallT;
                        float baseHeight = c->baseHeight;
                        bool absHeight = c->absHeight;

                        ImGui::InputInt(Get("component.decal.attached_wall").c_str(), &wallIndex);
                        ImGui::InputFloat(Get("component.decal.wall_offset").c_str(), &horizontalPos);
                        ImGui::InputFloat(Get("component.decal.wall_normal_offset").c_str(), &wallNormalOffset);
                        ImGui::InputFloat(Get("component.decal.z_offset").c_str(), &verticalPos);
                        ImGui::InputFloat("Wall T", &wallT);
                        ImGui::InputFloat("Base Height", &baseHeight);
                        ImGui::Checkbox(Get("component.decal.abs_height").c_str(), &absHeight);

                        c->wallIndex = wallIndex;
                        c->verticalPos = verticalPos;
                        c->horizontalPos = horizontalPos;
                        c->wallNormalOffset = wallNormalOffset;
                        c->wallT = wallT;
                        c->baseHeight = baseHeight;
                        c->absHeight = absHeight;

                        if (ImGui::Button(Get("common.delete").c_str())) {
                            entity.RemoveComponent<ComponentDecal>();
                            editingComponent = false;
                            selectedComponent = -1;
                        }
                    }
                }

                ImGui::SameLine();

                if (ImGui::Button(Get("common.close").c_str())) {
                    editingComponent = false;
                    selectedComponent = -1;
                }

                ImGui::PopID();
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

        if (currentMode == MODE_ENTITY) {

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
                if (!MapEditor::currentMap.empty()) {
                    Save(MapEditor::currentMap);
                }

                QueueLevelLoad(cleanName);

                spdlog::info("Queued level load: {}", cleanName);
            }

            ImGui::SameLine();

            if (ImGui::Button(Get("levels.delete").c_str())) {
                const std::filesystem::path path =
                    ProjectManager::GetLevelsPath() / (cleanName + ".json");

                try {
                    if (std::filesystem::remove(path)) {
                        spdlog::info("Deleted level: {}", path.string());

                        if (MapEditor::currentMap == cleanName) {
                            MapEditor::currentMap = "";
                        }

                        UpdateLevels();
                    }
                    else spdlog::error("Failed to delete level, file may not exist: {}", path.string());

                }
                catch (const std::filesystem::filesystem_error& e) {
                    spdlog::error("Failed to delete level: {}", e.what());
                }
            }

            ImGui::PopID();
        }

        ImGui::End();
    }
}