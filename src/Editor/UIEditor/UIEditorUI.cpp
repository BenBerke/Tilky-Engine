//
// Created by berke on 5/16/2026.
//

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Headers/Editor/Editor.hpp"
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "src/Editor/EditorInternal.hpp"


namespace {
    using namespace Localisation;
    using namespace MapEditorInternal;
    bool addingComponent = false;
    int selectedComponent = -1;

    void PutSpace(const int n) {
        for (int i = 0; i < n; i++) ImGui::Spacing();
    }

    void DrawTextureCategory() {
        const Level &level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_textures").c_str())) {
            EditorTextureCache::RefreshLevelTexturesFromFolder();
        }

        for (int i = 0; i < static_cast<int>(level.textures.size()); i++) {
            ImGui::PushID(i);

            ImGui::Text("%d: %s", i, level.textures[i].fileName.c_str());

            ImGui::PopID();
        }
        PutSpace(2);
    }

    void EditingEntity() {
        Entity *entityPtr = nullptr;
        Level &level = LevelManager::CurrentLevel();

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
        }
        else {
            Entity &entity = *entityPtr;

            ImGui::Begin(Get("entity.title").c_str(), &editingEntity);

            ImGui::Text("%u", entity.id);
            ImGui::SameLine();
            ImGui::InputText(Get("entity.name").c_str(), &entity.name);

            if (ImGui::Button(Get("entity.add_component").c_str())) {
                addingComponent = !addingComponent;
            }


            constexpr int EXTRA_COMPONENT_COUNT = 1;
            constexpr int FIRST_UI_COMPONENT = CMP_UI_TRANSFORM;
            constexpr int UI_COMPONENT_COUNT = CMP_COUNT - FIRST_UI_COMPONENT;

            constexpr int SCRIPT_COMPONENT_INDEX = UI_COMPONENT_COUNT;

            constexpr int compCount = UI_COMPONENT_COUNT + EXTRA_COMPONENT_COUNT;

            constexpr auto GetCorrectIndex = [](const int componentType) -> int {
                if (componentType == CMP_SCRIPT) {
                    return SCRIPT_COMPONENT_INDEX;
                }

                return componentType - FIRST_UI_COMPONENT;
            };

            static int componentToAdd = GetCorrectIndex(CMP_UI_SPRITE);

            if (addingComponent) {
                ImGui::PushID("add_component_combo");

                std::array<std::string, compCount> componentNames;

                componentNames[GetCorrectIndex(CMP_UI_TRANSFORM)] = Get("component.ui_transform");
                componentNames[GetCorrectIndex(CMP_UI_SPRITE)] = Get("component.ui_sprite");
                componentNames[GetCorrectIndex(CMP_SCRIPT)] = Get("component.script").c_str();

                if (ImGui::BeginCombo(Get("component.component").c_str(), componentNames[componentToAdd].c_str())) {
                    for (int i = 0; i < compCount; i++) {
                        if (i == GetCorrectIndex(CMP_UI_TRANSFORM)) {
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
                    if (componentToAdd == GetCorrectIndex(CMP_UI_SPRITE) && !entity.HasComponent<ComponentUISprite>())
                        entity.AddComponent<ComponentUISprite>();
                    if (componentToAdd == GetCorrectIndex(CMP_SCRIPT) && !entity.HasComponent<ComponentScript>())
                        entity.AddComponent<ComponentScript>();

                    addingComponent = false;
                    componentToAdd = GetCorrectIndex(CMP_UI_SPRITE);
                }

                ImGui::SameLine();

                if (ImGui::Button(Get("common.cancel").c_str())) {
                    addingComponent = false;
                    componentToAdd = GetCorrectIndex(CMP_UI_SPRITE);
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

            if (entity.HasComponent<ComponentUITransform>())
                DrawComponentRow(Get("component.ui_transform").c_str(), CMP_UI_TRANSFORM);

            if (entity.HasComponent<ComponentUISprite>())
                DrawComponentRow(Get("component.ui_sprite").c_str(), CMP_UI_SPRITE);

            if (entity.HasComponent<ComponentScript>())
                DrawComponentRow(Get("component.script").c_str(), CMP_SCRIPT);

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
    void EditingComponent() {
        Entity *entityPtr = nullptr;
        Level &level = LevelManager::CurrentLevel();

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
                case CMP_UI_TRANSFORM:
                    componentName = Get("component.ui_transform");
                    break;
                case CMP_UI_SPRITE:
                    componentName = Get("component.ui_sprite");
                    break;
                    break;
                case CMP_SCRIPT:
                    componentName = Get("component.script");
                    break;
                default:
                    componentName = Get("bug.unknown");
                    break;
            }

            const std::string windowTitle = componentName + "##component_editor";

            ImGui::Begin(windowTitle.c_str(), &editingComponent);
            ImGui::PushID(selectedComponent);

            if (selectedComponent == CMP_UI_TRANSFORM) {
                auto *c = entity.GetComponent<ComponentUITransform>();

                if (c == nullptr) {
                    ImGui::Text("Transform component missing");
                } else {
                    float positionValues[2] = {c->position.x, c->position.y};
                    float scaleValues[2] = {c->scale.x, c->scale.y};

                    ImGui::Text("%s", Get("component.transform.position").c_str());
                    ImGui::Text("X    Y");
                    ImGui::SetNextItemWidth(220.0f);
                    ImGui::InputFloat2("##position", positionValues);

                    ImGui::Spacing();

                    ImGui::Text("%s", Get("component.transform.scale").c_str());
                    ImGui::Text("X    Y");
                    ImGui::SetNextItemWidth(220.0f);
                    ImGui::InputFloat2("##scale", scaleValues);

                    c->position = {positionValues[0], positionValues[1]};
                    c->scale = {scaleValues[0], scaleValues[1]};

                    ImGui::Spacing();

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentUITransform>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
            }
            else if (selectedComponent == CMP_UI_SPRITE) {
                auto *c = entity.GetComponent<ComponentUISprite>();

                if (c == nullptr) {
                    ImGui::Text("Sprite component missing");
                } else {
                    int textureIndex = c->textureIndex;

                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputInt(Get("component.sprite.texture_index").c_str(), &textureIndex);

                    c->textureIndex = textureIndex;

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentUISprite>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
            }
            else if (selectedComponent == CMP_SCRIPT) {
                auto *c = entity.GetComponent<ComponentScript>();

                if (c == nullptr) {
                    ImGui::Text("Script component missing");
                } else {
                    std::string scriptFile = c->fileName;
                    bool enabled = c->enabled;

                    ImGui::InputText(Get("component.script.file_name").c_str(), &scriptFile);
                    ImGui::Checkbox(Get("component.script.enabled").c_str(), &enabled);

                    c->fileName = scriptFile;
                    c->enabled = enabled;

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentScript>();
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
}


namespace MapEditorInternal {
    void DrawUIEditorUI() {
        using namespace Localisation;
        ImGui::Begin(Get("ui_editor.title").c_str());


        if (editingComponent && selectedComponent != -1) EditingComponent();
        if (editingEntity) EditingEntity();

        if (ImGui::Button(Get("editor.switch_to_map").c_str())) currentState = STATE_MAP;

        ImGui::End();
    }
}
