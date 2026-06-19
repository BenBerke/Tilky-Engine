//
// Created by berke on 6/2/2026.
//

#include "Headers/Editor/ImGuiDrawFunctions.hpp"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <array>
#include <string>

#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Components.hpp"

#include "Headers/Objects/ComponentRegistry.hpp"
#include <type_traits>

#include "Headers/Engine/InputManager.hpp"

static const char *GetComponentLabelKey(const int componentType) {
    switch (componentType) {
#define COMPONENT_LABEL_CASE(Type, Bit, Storage, LabelKey) \
case Bit: return LabelKey;

        TILKY_COMPONENTS(COMPONENT_LABEL_CASE)

#undef COMPONENT_LABEL_CASE

        default:
            return "bug.unknown";
    }
}

static std::string GetComponentDisplayName(const int componentType) {
    return Localisation::Get(GetComponentLabelKey(componentType));
}

template<typename T>
static void AddEditorComponent(Entity &entity) {
    if constexpr (std::is_same_v<T, ComponentPlayerController>) {
        if (!entity.HasComponent<ComponentPlayerController>()) {
            auto *pc = entity.AddComponent<ComponentPlayerController>();

            if (!entity.HasComponent<ComponentRigidbody>())
                entity.AddComponent<ComponentRigidbody>();

            if (!entity.HasComponent<ComponentCollider>())
                entity.AddComponent<ComponentCollider>();

            if (!entity.HasComponent<ComponentCamera>())
                entity.AddComponent<ComponentCamera>();

            pc->isActive = true;
        }
    } else if constexpr (std::is_same_v<T, ComponentCamera>) {
        if (!entity.HasComponent<ComponentCamera>()) {
            auto *cam = entity.AddComponent<ComponentCamera>();
            cam->isActive = true;
        }
    } else {
        if (!entity.HasComponent<T>())
            entity.AddComponent<T>();
    }
}

static void AddEditorComponentByType(Entity &entity, const int componentType) {
    switch (componentType) {
#define ADD_EDITOR_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
case Bit: \
AddEditorComponent<Type>(entity); \
break;

        TILKY_NORMAL_COMPONENTS(ADD_EDITOR_COMPONENT_CASE)

#undef ADD_EDITOR_COMPONENT_CASE

        default:
            break;
    }
}

namespace {
    bool InputID(const char *label, ID &value) {
        int displayValue = (value == INVALID_ID) ? -1 : static_cast<int>(value);

        if (!ImGui::InputInt(label, &displayValue)) return false;

        value = (displayValue < 0) ? INVALID_ID : static_cast<ID>(displayValue);

        return true;
    }

    template<typename T>
    bool InputOrDrag(const char *label, T *value, bool draggable, float speed = 1.0f) {
        if constexpr (std::is_same_v<T, float>)
            return draggable ? ImGui::DragFloat(label, value, speed) : ImGui::InputFloat(label, value);
        else if constexpr (std::is_same_v<T, int>)
            return draggable ? ImGui::DragInt(label, value, speed) : ImGui::InputInt(label, value);

        return false;
    }

    bool InputOrDrag2(const char *label, float *value, const bool draggable, const float speed = 1.0f) {
        return draggable ? ImGui::DragFloat2(label, value, speed) : ImGui::InputFloat2(label, value);
    }

    bool InputOrDrag3(const char *label, float *value, const bool draggable, const float speed = 1.0f) {
        return draggable ? ImGui::DragFloat3(label, value, speed) : ImGui::InputFloat3(label, value);
    }

    bool InputOrDrag4(const char *label, float *value, const bool draggable, const float speed = 1.0f) {
        return draggable ? ImGui::DragFloat4(label, value, speed) : ImGui::InputFloat4(label, value);
    }
}

namespace ImGuiDrawFunctions {
    using namespace Localisation;

    void PutSpace(const int n) {
        for (int i = 0; i < n; i++) {
            ImGui::Spacing();
        }
    }

    bool DrawSectorEditor(Sector &sector, bool *open, const int sectorId, const bool draggable) {
        bool deleteRequested = false;

        if (!ImGui::Begin(Get("sector.title").c_str(), open)) {
            ImGui::End();
            return false;
        }

        InputOrDrag(Get("sector.ceil_height").c_str(), &sector.ceilingHeight, draggable);
        InputOrDrag(Get("sector.floor_height").c_str(), &sector.floorHeight, draggable);
        InputOrDrag(Get("sector.floor_count").c_str(), &sector.floorCount, draggable);

        sector.floorCount = std::clamp(sector.floorCount, 1, MAX_FLOOR_COUNT);

        InputOrDrag(Get("sector.ground_floor_texture").c_str(), &sector.floorTextureIndex, draggable);

        for (int i = 0; i < sector.floorCount; ++i) {
            const std::string label =
                    Get("sector.ceiling_texture") + " " + std::to_string(i + 1);

            InputOrDrag(label.c_str(), &sector.ceilingTextureIndices[i], draggable);
        }

        InputOrDrag3(Get("sector.ceiling_color").c_str(), &sector.ceilingColor.x, draggable);
        InputOrDrag3(Get("sector.floor_color").c_str(), &sector.floorColor.x, draggable);

        if (ImGui::Button(Get("common.delete").c_str())) {
            deleteRequested = true;
            if (open != nullptr) {
                *open = false;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(Get("common.close").c_str())) {
            if (open != nullptr) {
                *open = false;
            }
        }

        if (sectorId >= 0) ImGui::Text("%s: %d", Get("common.id").c_str(), sector.id);

        ImGui::End();

        return deleteRequested;
    }

    bool DrawWallEditor(Wall &wall, bool *open, const int wallId, const bool draggable) {
        bool deleteRequested = false;

        if (!ImGui::Begin(Get("wall.title").c_str(), open)) {
            ImGui::End();
            return false;
        }

        bool wallSectorChanged = false;

        wallSectorChanged |= InputID(Get("wall.front_sector").c_str(), wall.frontSector);
        wallSectorChanged |= InputID(Get("wall.back_sector").c_str(), wall.backSector);

        ImGui::InputInt(Get("wall.texture_index").c_str(), &wall.textureIndex, 1);
        InputOrDrag(Get("wall.floor").c_str(), &wall.floor, draggable);
        InputOrDrag4(Get("wall.color").c_str(), &wall.color.x, draggable);
        InputOrDrag2(Get("wall.texture_offset").c_str(), &wall.textureOffset.x, draggable);

        if (wallSectorChanged) MapQueries::RebuildSectorRuntimeLinks(LevelManager::CurrentLevel());

        if (ImGui::Button(Get("common.delete").c_str())) {
            deleteRequested = true;
            if (open != nullptr) {
                *open = false;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(Get("common.close").c_str())) {
            if (open != nullptr) {
                *open = false;
            }
        }

        if (wallId >= 0) ImGui::Text("%s: %d", Get("common.id").c_str(), wall.id);

        ImGui::End();

        return deleteRequested;
    }

    bool DrawEntityEditor(Entity &entity, EntityInspectorState &state, bool *open, const bool draggable) {
        bool deleteRequested = false;

        if (!ImGui::Begin(Get("entity.title").c_str(), open)) {
            ImGui::End();
            return false;
        }

        ImGui::Text("%u", entity.id);
        ImGui::SameLine();
        ImGui::InputText(Get("entity.name").c_str(), &entity.name);

        if (ImGui::Button(Get("entity.add_component").c_str()))
            state.addingComponent = !state.addingComponent;

        if (state.addingComponent) {
            ImGui::PushID("add_component_combo");

            std::array<std::string, CMP_NORMAL_COUNT> componentNames{};

#define FILL_COMPONENT_NAME(Type, Bit, Storage, LabelKey) \
componentNames[Bit] = Get(LabelKey);

            TILKY_NORMAL_COMPONENTS(FILL_COMPONENT_NAME)

#undef FILL_COMPONENT_NAME

            if (ImGui::BeginCombo(
                Get("component.component").c_str(),
                componentNames[state.componentToAdd].c_str()
            )) {
                for (int i = 0; i < CMP_NORMAL_COUNT; i++) {
                    if (i == CMP_TRANSFORM) continue;

                    const bool isSelected = state.componentToAdd == i;

                    if (ImGui::Selectable(componentNames[i].c_str(), isSelected))
                        state.componentToAdd = i;

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            if (ImGui::Button(Get("common.add").c_str())) {
                AddEditorComponentByType(entity, state.componentToAdd);

                state.addingComponent = false;
                state.componentToAdd = CMP_SPRITE;
            }

            ImGui::SameLine();

            if (ImGui::Button(Get("common.cancel").c_str())) {
                state.addingComponent = false;
                state.componentToAdd = CMP_SPRITE;
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
                state.selectedComponent = componentType;
                state.editingComponent = true;
            }

            ImGui::PopID();
        };

#define DRAW_ENTITY_COMPONENT_ROW(Type, Bit, Storage, LabelKey) \
if (entity.HasComponent<Type>()) \
DrawComponentRow(Get(LabelKey).c_str(), Bit);

        TILKY_NORMAL_COMPONENTS(DRAW_ENTITY_COMPONENT_ROW)

#undef DRAW_ENTITY_COMPONENT_ROW

        PutSpace(2);

        ImGui::PushID("entity_buttons");

        if (ImGui::Button(Get("common.delete").c_str()) || InputManager::GetKeyDown(SDL_SCANCODE_DELETE)) {
            deleteRequested = true;
            state.editingComponent = false;
            state.selectedComponent = -1;

            if (open != nullptr) *open = false;
        }

        ImGui::SameLine();

        if (ImGui::Button(Get("common.close").c_str())) {
            state.editingComponent = false;
            state.selectedComponent = -1;

            if (open != nullptr) *open = false;
        }

        ImGui::PopID();

        ImGui::End();

        return deleteRequested;
    }

    void DrawComponentEditor(Entity &entity, EntityInspectorState &state, bool *open, const bool draggable) {
        if (state.selectedComponent == -1) {
            state.editingComponent = false;

            if (open != nullptr) {
                *open = false;
            }

            return;
        }

        const std::string componentName = GetComponentDisplayName(state.selectedComponent);

        const std::string windowTitle = componentName + "##component_editor";

        bool fallbackOpen = true;
        bool *windowOpen = open != nullptr ? open : &fallbackOpen;

        if (!ImGui::Begin(windowTitle.c_str(), windowOpen)) {
            ImGui::End();

            if (!*windowOpen) {
                state.editingComponent = false;
                state.selectedComponent = -1;
            }

            return;
        }

        ImGui::PushID(state.selectedComponent);

        bool closeRequested = false;

        auto CloseEditor = [&]() {
            closeRequested = true;
        };

        if (state.selectedComponent == CMP_TRANSFORM) {
            auto *c = entity.GetComponent<ComponentTransform>();

            if (c != nullptr) [[likely]] {
                ImGui::Text("%s", Get("component.transform.position").c_str());
                ImGui::Text("X       Y");

                ImGui::SetNextItemWidth(200.0f);
                InputOrDrag("##positionx", &c->position.x, draggable);

                ImGui::SetNextItemWidth(200.0f);
                ImGui::SameLine();
                InputOrDrag("##positiony", &c->position.y, draggable);

                ImGui::Text("Height");

                ImGui::SetNextItemWidth(200.0f);
                InputOrDrag("##relativeHeight", &c->position.z, draggable);

                ImGui::Spacing();

                ImGui::Text("%s", Get("component.transform.scale").c_str());
                ImGui::Text("X    Y");

                ImGui::SetNextItemWidth(220.0f);
                InputOrDrag2("##scale", &c->scale.x, draggable);

                ImGui::Spacing();

                ImGui::SetNextItemWidth(120.0f);
                InputOrDrag(Get("component.transform.floor").c_str(), &c->floor, draggable);

                ImGui::Spacing();

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentTransform>();
                    CloseEditor();
                }
            } else ImGui::Text("Transform component missing");
        } else if (state.selectedComponent == CMP_SPRITE) {
            auto *c = entity.GetComponent<ComponentSprite>();

            if (c != nullptr) [[likely]] {
                ImGui::SetNextItemWidth(120.0f);
                InputOrDrag(Get("component.sprite.texture_index").c_str(), &c->textureIndex, draggable);

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentSprite>();
                    CloseEditor();
                }
            } else [[unlikely]] ImGui::Text("Sprite component missing");
        } else if (state.selectedComponent == CMP_DECAL) {
            auto *c = entity.GetComponent<ComponentDecal>();

            if (c != nullptr) [[likely]] {
                InputOrDrag(Get("component.decal.attached_wall").c_str(), &c->wallIndex, draggable);
                InputOrDrag(Get("component.decal.wall_offset").c_str(), &c->horizontalPos, draggable);
                InputOrDrag(Get("component.decal.wall_normal_offset").c_str(), &c->wallNormalOffset, draggable);
                InputOrDrag(Get("component.decal.z_offset").c_str(), &c->verticalPos, draggable);
                InputOrDrag("Wall T", &c->wallT, draggable);
                InputOrDrag("Base Height", &c->baseHeight, draggable);
                ImGui::Checkbox(Get("component.decal.abs_height").c_str(), &c->absHeight);

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentDecal>();
                    CloseEditor();
                }
            } else [[unlikely]] ImGui::Text("Decal component missing");
        } else if (state.selectedComponent == CMP_AUDIO_SOURCE) {
            auto *c = entity.GetComponent<ComponentAudioSource>();

            if (c != nullptr) [[likely]] {
                InputOrDrag("Sound Index", &c->soundIndex, draggable);
                InputOrDrag(Get("component.audio_source.pitch").c_str(), &c->pitch, draggable);
                InputOrDrag(Get("component.audio_source.gain").c_str(), &c->gain, draggable);
                ImGui::Checkbox(Get("component.audio_source.looping").c_str(), &c->looping);
                ImGui::Checkbox(Get("component.audio_source.play_on_start").c_str(), &c->playOnStart);

                ImGui::Separator();
                ImGui::TextDisabled("%s", Get("component.audio_source.attenuation_header").c_str());

                InputOrDrag(Get("component.audio_source.ref_distance").c_str(), &c->referenceDistance, draggable, 0.1f);
                InputOrDrag(Get("component.audio_source.max_distance").c_str(), &c->maxDistance, draggable, 1.0f);
                InputOrDrag(Get("component.audio_source.rolloff").c_str(), &c->rollOffFactor, draggable, 0.01f);

                ImGui::Separator();
                ImGui::TextDisabled("%s", Get("component.audio_source.cone_header").c_str());

                InputOrDrag(Get("component.audio_source.inner_angle").c_str(), &c->innerConeAngle, draggable, 1.0f);
                InputOrDrag(Get("component.audio_source.outer_angle").c_str(), &c->outerConeAngle, draggable, 1.0f);
                InputOrDrag(Get("component.audio_source.outer_gain").c_str(), &c->outerGain, draggable, 0.01f);

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentAudioSource>();
                    CloseEditor();
                }
            } else [[unlikely]] ImGui::Text("Audio component missing");
        } else if (state.selectedComponent == CMP_SCRIPT) {
            auto *c = entity.GetComponent<ComponentScript>();

            if (c != nullptr) [[likely]] {
                ImGui::InputText(Get("component.script.file_name").c_str(), &c->fileName);
                ImGui::Checkbox(Get("component.script.enabled").c_str(), &c->enabled);

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentScript>();
                    CloseEditor();
                }
            } else [[unlikely]] ImGui::Text("Script component missing");
        } else if (state.selectedComponent == CMP_PLAYER_CONTROLLER) {
            auto *c = entity.GetComponent<ComponentPlayerController>();

            if (c != nullptr) [[likely]] {
                InputOrDrag(Get("component.player_controller.speed").c_str(), &c->speed, draggable);
                InputOrDrag(Get("component.player_controller.running_speed").c_str(), &c->runningSpeed, draggable);
                InputOrDrag(Get("component.player_controller.jump_power").c_str(), &c->jumpPower, draggable);
                InputOrDrag(Get("component.player_controller.eye_height").c_str(), &c->eyeHeight, draggable);

                ImGui::SliderFloat(Get("component.player_controller.friction").c_str(), &c->friction, 0.0f, 1.0f);
                ImGui::SliderFloat(
                    Get("component.player_controller.sensitivity_x").c_str(),
                    &c->sensitivityX,
                    0.001f,
                    2.0f
                );

                ImGui::SliderFloat(
                    Get("component.player_controller.sensitivity_y").c_str(),
                    &c->sensitivityY,
                    0.001f,
                    2.0f
                );

                ImGui::Checkbox(Get("component.player_controller.no_clip").c_str(), &c->noClip);
                ImGui::Checkbox(Get("component.player_controller.is_active").c_str(), &c->isActive);

                ImGui::Separator();

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentPlayerController>();
                    CloseEditor();
                }
            } else [[unlikely]] ImGui::Text("Player Controller component missing");
        } else if (state.selectedComponent == CMP_CAMERA) {
            auto *c = entity.GetComponent<ComponentCamera>();

            if (c != nullptr) [[likely]] {
                ImGui::SliderFloat(Get("component.camera.fov").c_str(), &c->fov, 1.0f, 179.0f);
                InputOrDrag(Get("component.camera.aspect_ratio").c_str(), &c->aspectRatio, draggable);
                InputOrDrag(Get("component.camera.near_plane").c_str(), &c->nearPlane, draggable);
                InputOrDrag(Get("component.camera.far_plane").c_str(), &c->farPlane, draggable);
                ImGui::Checkbox(Get("component.camera.is_active").c_str(), &c->isActive);

                ImGui::Separator();

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentCamera>();
                    CloseEditor();
                }
            } else [[unlikely]] ImGui::Text("Camera component missing");
        } else if (state.selectedComponent == CMP_COLLIDER) {
            auto *c = entity.GetComponent<ComponentCollider>();

            if (c != nullptr) [[likely]] {
                const std::array<std::string, 2> colliderTypeLabels = {
                    Get("component.collider.type.sphere"),
                    Get("component.collider.type.box")
                };

                const char *items[] = {
                    colliderTypeLabels[0].c_str(),
                    colliderTypeLabels[1].c_str()
                };

                static int selectedColliderIndex = 0;

                ImGui::Combo(
                    Get("component.collider.type").c_str(),
                    &selectedColliderIndex,
                    items,
                    IM_ARRAYSIZE(items)
                );

                if (selectedColliderIndex == 0) {
                    InputOrDrag(
                        Get("component.collider.sphere.scale").c_str(),
                        &c->scale.x,
                        draggable
                    );
                } else if (selectedColliderIndex == 1) {
                    ImGui::Text("%s", Get("component.collider.box.scale").c_str());
                    ImGui::Text("X           Y           Z");

                    ImGui::SetNextItemWidth(200.0f);
                    InputOrDrag3("##collider_scale", &c->scale.x, draggable);
                }

                ImGui::Checkbox(Get("component.collider.is_trigger").c_str(), &c->isTrigger);
                ImGui::Checkbox(Get("component.collider.is_active").c_str(), &c->isActive);

                InputOrDrag(Get("component.collider.step_size").c_str(), &c->stepSize, draggable);
            } else [[unlikely]] ImGui::Text("Collider component missing");

            if (ImGui::Button(Get("common.delete").c_str())) {
                entity.RemoveComponent<ComponentCollider>();
                CloseEditor();
            }
        } else if (state.selectedComponent == CMP_RIGIDBODY) {
            auto *c = entity.GetComponent<ComponentRigidbody>();

            if (c != nullptr) [[likely]] {
                ImGui::Checkbox(Get("component.rigidbody.is_static").c_str(), &c->isStatic);
                InputOrDrag(Get("component.rigidbody.mass").c_str(), &c->mass, draggable);
                InputOrDrag(Get("component.rigidbody.gravityScale").c_str(), &c->gravityScale, draggable);
                InputOrDrag(Get("component.rigidbody.friction").c_str(), &c->friction, draggable);
            } else [[unlikely]] ImGui::Text("Rigidbody component missing");

            if (ImGui::Button(Get("common.delete").c_str())) {
                entity.RemoveComponent<ComponentRigidbody>();
                CloseEditor();
            }
        }

        if (!closeRequested) {
            ImGui::SameLine();

            if (ImGui::Button(Get("common.close").c_str())) CloseEditor();
        }

        if (closeRequested || !*windowOpen) {
            state.editingComponent = false;
            state.selectedComponent = -1;

            if (open != nullptr) {
                *open = false;
            }
        }

        ImGui::PopID();
        ImGui::End();
    }

    void SetImGuiFocus(const bool focus) {
        ImGuiIO &io = ImGui::GetIO();
        if (focus) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        else io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }
}
