#include "../EditorInternal.hpp"

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
#include "../../../Headers/Engine/Local/Local.hpp"
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "misc/cpp/imgui_stdlib.h"

namespace Editor {
    void RefreshLevelSoundsFromFolder() {
        Level &level = LevelManager::CurrentLevel();
        level.sounds.clear();

        const std::filesystem::path soundsPath = ProjectManager::GetSoundsPath();

        if (!std::filesystem::exists(soundsPath)) {
            std::filesystem::create_directories(soundsPath);
            spdlog::warn("Created missing Sounds folder: {}", soundsPath.string());
            return;
        }

        if (!std::filesystem::is_directory(soundsPath)) {
            spdlog::error("Sounds path is not a directory: {}", soundsPath.string());
            return;
        }

        for (const auto &entry: std::filesystem::directory_iterator(soundsPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path path = entry.path();

            std::string extension = path.extension().string();

            std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (extension != ".wav") {
                continue;
            }

            Sound sound;
            sound.fileName = path.stem().string(); // "Shoot.wav" -> "Shoot"

            level.sounds.push_back(sound);
        }

        std::ranges::sort(level.sounds, [](const Sound &a, const Sound &b) {
            return a.fileName < b.fileName;
        });

        spdlog::info("Refreshed {} level sound(s)", level.sounds.size());
    }
}

namespace {
    using namespace Localisation;
    using namespace MapEditorInternal;
    bool addingComponent = false;
    int selectedComponent = -1;

    std::optional<std::string> pendingLevelToLoad;

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

    void DrawSoundCategory() {
        const Level &level = LevelManager::CurrentLevel();

        if (ImGui::Button(Get("editor.refresh_sounds").c_str())) {
            Editor::RefreshLevelSoundsFromFolder();
        }

        for (int i = 0; i < static_cast<int>(level.sounds.size()); i++) {
            ImGui::PushID(i);

            ImGui::Text("%d: %s", i, level.sounds[i].fileName.c_str());

            ImGui::PopID();
        }

        PutSpace(2);
    }

    void DrawWorldSettings() {
        Level &level = LevelManager::CurrentLevel();
        ListenerSettings &settings = level.listenerSettings;

        ImGui::Begin(Get("editor.world_settings").c_str());

        ImGui::SliderFloat(
            Get("settings.audio.master_gain").c_str(),
            &settings.masterGain,
            0.0f,
            2.0f
        );

        ImGui::Separator();
        ImGui::TextDisabled("%s", Get("settings.audio.global_physics_header").c_str());

        if (ImGui::DragFloat(
            Get("settings.audio.doppler_factor").c_str(),
            &settings.dopplerFactor,
            0.01f,
            0.0f,
            10.0f
        )) {
            // SoundManager::SetListenerDopplerFactor(settings.dopplerFactor);
        }

        if (ImGui::InputFloat(
            Get("settings.audio.speed_of_sound").c_str(),
            &settings.speedOfSound
        )) {
            settings.speedOfSound = std::max(1.0f, settings.speedOfSound);
            // SoundManager::SetListenerSpeedOfSound(settings.speedOfSound);
        }

        const std::array<std::string, 5> modelLabels = {
            Get("settings.audio.distance_model.inverse"),
            Get("settings.audio.distance_model.inverse_clamped"),
            Get("settings.audio.distance_model.linear"),
            Get("settings.audio.distance_model.linear_clamped"),
            Get("settings.audio.distance_model.none")
        };

        const char *models[] = {
            modelLabels[0].c_str(),
            modelLabels[1].c_str(),
            modelLabels[2].c_str(),
            modelLabels[3].c_str(),
            modelLabels[4].c_str()
        };

        const ALenum alModels[] = {
            AL_INVERSE_DISTANCE,
            AL_INVERSE_DISTANCE_CLAMPED,
            AL_LINEAR_DISTANCE,
            AL_LINEAR_DISTANCE_CLAMPED,
            AL_NONE
        };

        auto DistanceModelToIndex = [](const ALenum model) -> int {
            switch (model) {
                case AL_INVERSE_DISTANCE:
                    return 0;
                case AL_INVERSE_DISTANCE_CLAMPED:
                    return 1;
                case AL_LINEAR_DISTANCE:
                    return 2;
                case AL_LINEAR_DISTANCE_CLAMPED:
                    return 3;
                case AL_NONE:
                    return 4;
                default:
                    return 1;
            }
        };

        int currentModel = DistanceModelToIndex(settings.distanceModel);

        if (ImGui::Combo(
            Get("settings.audio.distance_model").c_str(),
            &currentModel,
            models,
            IM_ARRAYSIZE(models)
        )) {
            settings.distanceModel = alModels[currentModel];
            // SoundManager::SetListenerDistanceModel(settings.distanceModel);
        }

        PutSpace(5);

        ImGui::InputInt(Get("editor.background_texture").c_str(), &Editor::backgroundTextureIndex);

        ImGui::End();
    }

    void DrawMode() {
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

        auto GetModeName = [](const int mode) -> std::string {
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
    }

    void EditingSector() {
        Level &level = LevelManager::CurrentLevel();
        if (selectedSector < 0 ||
            selectedSector >= static_cast<int>(level.sectors.size())) {
            editingSector = false;
        } else {
            ImGui::Begin(Get("sector.title").c_str(), &editingSector);

            auto &sector = level.sectors[selectedSector];

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

    void EditingWall() {
        Level &level = LevelManager::CurrentLevel();
        if (selectedWall < 0 ||
            selectedWall >= static_cast<int>(level.walls.size())) {
            editingWall = false;
        } else {
            ImGui::Begin(Get("wall.title").c_str(), &editingWall);

            auto &wall = level.walls[selectedWall];

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

                std::array<std::string, CMP_NORMAL_COUNT> componentNames;

                componentNames[CMP_TRANSFORM] = Get("component.transform");
                componentNames[CMP_SPRITE] = Get("component.sprite");
                componentNames[CMP_DECAL] = Get("component.decal");
                componentNames[CMP_AUDIO_SOURCE] = Get("component.audio_source");
                componentNames[CMP_SCRIPT] = Get("component.script");
                componentNames[CMP_PLAYER_CONTROLLER] = Get("component.player_controller");
                componentNames[CMP_CAMERA] = Get("component.camera");
                componentNames[CMP_SPHERE_COLLIDER] = Get("component.sphere_collider");
                componentNames[CMP_RIGIDBODY] = Get("component.rigidbody");

                if (ImGui::BeginCombo(Get("component.component").c_str(), componentNames[componentToAdd].c_str())) {
                    for (int i = 0; i < CMP_NORMAL_COUNT; i++) {
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
                    switch (componentToAdd) {
                        case CMP_SPRITE:
                            if (!entity.HasComponent<ComponentSprite>()) [[likely]]
                                entity.AddComponent<ComponentSprite>();
                            break;

                        case CMP_DECAL:
                            if (!entity.HasComponent<ComponentDecal>()) [[likely]]
                                entity.AddComponent<ComponentDecal>();
                            break;

                        case CMP_AUDIO_SOURCE:
                            if (!entity.HasComponent<ComponentAudioSource>()) [[likely]]
                                entity.AddComponent<ComponentAudioSource>();
                            break;

                        case CMP_SCRIPT:
                            if (!entity.HasComponent<ComponentScript>()) [[likely]]
                                entity.AddComponent<ComponentScript>();
                            break;

                        case CMP_PLAYER_CONTROLLER:
                            if (!entity.HasComponent<ComponentPlayerController>()) [[likely]] {
                                auto* pc = entity.AddComponent<ComponentPlayerController>();
                                pc->isActive = true;
                            }
                            break;

                        case CMP_CAMERA:
                            if (!entity.HasComponent<ComponentCamera>()) [[likely]] {
                                auto* cam = entity.AddComponent<ComponentCamera>();
                                cam->isActive = true;
                            }
                            break;

                        case CMP_SPHERE_COLLIDER:
                            if (!entity.HasComponent<ComponentSphereCollider>()) [[likely]]
                                entity.AddComponent<ComponentSphereCollider>();
                            break;

                        case CMP_RIGIDBODY:
                            if (!entity.HasComponent<ComponentRigidbody>()) [[likely]]
                                entity.AddComponent<ComponentRigidbody>();
                        default:
                            spdlog::error("Unknown component type");
                            break;
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

            if (entity.HasComponent<ComponentTransform>()) [[likely]]
                DrawComponentRow(Get("component.transform").c_str(), CMP_TRANSFORM);
            if (entity.HasComponent<ComponentSprite>())
                DrawComponentRow(Get("component.sprite").c_str(), CMP_SPRITE);
            if (entity.HasComponent<ComponentDecal>())
                DrawComponentRow(Get("component.decal").c_str(), CMP_DECAL);
            if (entity.HasComponent<ComponentAudioSource>())
                DrawComponentRow(Get("component.audio_source").c_str(), CMP_AUDIO_SOURCE);
            if (entity.HasComponent<ComponentScript>())
                DrawComponentRow(Get("component.script").c_str(), CMP_SCRIPT);
            if (entity.HasComponent<ComponentPlayerController>())
                DrawComponentRow(Get("component.player_controller").c_str(), CMP_PLAYER_CONTROLLER);
            if (entity.HasComponent<ComponentCamera>())
                DrawComponentRow(Get("component.camera").c_str(), CMP_CAMERA);
            if (entity.HasComponent<ComponentSphereCollider>())
                DrawComponentRow(Get("component.sphere_collider").c_str(), CMP_SPHERE_COLLIDER);
            if (entity.HasComponent<ComponentRigidbody>())
                DrawComponentRow(Get("component.rigidbody").c_str(), CMP_RIGIDBODY);

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
                case CMP_TRANSFORM:
                    componentName = Get("component.transform");
                    break;
                case CMP_SPRITE:
                    componentName = Get("component.sprite");
                    break;
                case CMP_DECAL:
                    componentName = Get("component.decal");
                    break;
                case CMP_AUDIO_SOURCE:
                    componentName = Get("component.audio_source");
                    break;
                case CMP_SCRIPT:
                    componentName = Get("component.script");
                    break;
                case CMP_PLAYER_CONTROLLER:
                    componentName = Get("component.player_controller");
                    break;
                case CMP_CAMERA:
                    componentName = Get("component.camera");
                    break;
                case CMP_SPHERE_COLLIDER:
                    componentName = Get("component.sphere_collider");
                    break;
                case CMP_RIGIDBODY:
                    componentName = Get("component.rigidbody");
                    break;
                default:
                    componentName = Get("bug.unknown");
                    break;
            }

            const std::string windowTitle = componentName + "##component_editor";

            ImGui::Begin(windowTitle.c_str(), &editingComponent);
            ImGui::PushID(selectedComponent);

            if (selectedComponent == CMP_TRANSFORM) {
                auto *c = entity.GetComponent<ComponentTransform>();

                if (c != nullptr) [[likely]] {
                    ImGui::Text("%s", Get("component.transform.position").c_str());
                    ImGui::Text("X       Y");
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##positionx", &c->position.x);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::SameLine();
                    ImGui::InputFloat("##positiony", &c->position.y);

                    ImGui::Text("Height");
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##positionz", &c->position.z);

                    ImGui::Spacing();

                    ImGui::Text("%s", Get("component.transform.scale").c_str());
                    ImGui::Text("X    Y");
                    ImGui::SetNextItemWidth(220.0f);
                    ImGui::InputFloat2("##scale", &c->scale.x);

                    ImGui::Spacing();

                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputInt(Get("component.transform.floor").c_str(), &c->floor);

                    ImGui::Spacing();

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentTransform>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else {
                    ImGui::Text("Transform component missing");
                }
            }
            else if (selectedComponent == CMP_SPRITE) {
                auto *c = entity.GetComponent<ComponentSprite>();

                if (c != nullptr) [[likely]] {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputInt(Get("component.sprite.texture_index").c_str(), &c->textureIndex);

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentSprite>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else [[unlikely]] {
                    ImGui::Text("Sprite component missing");
                }
            }
            else if (selectedComponent == CMP_DECAL) {
                auto *c = entity.GetComponent<ComponentDecal>();

                if (c != nullptr) [[likely]] {
                    ImGui::InputInt(Get("component.decal.attached_wall").c_str(), &c->wallIndex);
                    ImGui::InputFloat(Get("component.decal.wall_offset").c_str(), &c->horizontalPos);
                    ImGui::InputFloat(Get("component.decal.wall_normal_offset").c_str(), &c->wallNormalOffset);
                    ImGui::InputFloat(Get("component.decal.z_offset").c_str(), &c->verticalPos);
                    ImGui::InputFloat("Wall T", &c->wallT);
                    ImGui::InputFloat("Base Height", &c->baseHeight);
                    ImGui::Checkbox(Get("component.decal.abs_height").c_str(), &c->absHeight);

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentDecal>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else [[unlikely]] {
                    ImGui::Text("Decal component missing");
                }
            }
            else if (selectedComponent == CMP_AUDIO_SOURCE) {
                auto *c = entity.GetComponent<ComponentAudioSource>();

                if (c != nullptr) [[likely]] {
                    ImGui::InputInt("Sound Index", &c->soundIndex);
                    ImGui::InputFloat(Get("component.audio_source.pitch").c_str(), &c->pitch);
                    ImGui::InputFloat(Get("component.audio_source.gain").c_str(), &c->gain);
                    ImGui::Checkbox(Get("component.audio_source.looping").c_str(), &c->looping);
                    ImGui::Checkbox(Get("component.audio_source.play_on_start").c_str(), &c->playOnStart);

                    ImGui::Separator();
                    ImGui::TextDisabled("%s", Get("component.audio_source.attenuation_header").c_str());

                    ImGui::DragFloat(Get("component.audio_source.ref_distance").c_str(), &c->referenceDistance, 0.1f,
                                     0.0f, 1000.0f);
                    ImGui::DragFloat(Get("component.audio_source.max_distance").c_str(), &c->maxDistance, 1.0f, 0.0f,
                                     10000.0f);
                    ImGui::DragFloat(Get("component.audio_source.rolloff").c_str(), &c->rollOffFactor, 0.01f, 0.0f,
                                     10.0f);

                    ImGui::Separator();
                    ImGui::TextDisabled("%s", Get("component.audio_source.cone_header").c_str());

                    ImGui::DragFloat(Get("component.audio_source.inner_angle").c_str(), &c->innerConeAngle, 1.0f, 0.0f,
                                     360.0f);
                    ImGui::DragFloat(Get("component.audio_source.outer_angle").c_str(), &c->outerConeAngle, 1.0f, 0.0f,
                                     360.0f);
                    ImGui::DragFloat(Get("component.audio_source.outer_gain").c_str(), &c->outerGain, 0.01f, 0.0f,
                                     1.0f);

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentAudioSource>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else [[unlikely]] {
                    ImGui::Text("Audio component missing");
                }
            }
            else if (selectedComponent == CMP_SCRIPT) {
                auto *c = entity.GetComponent<ComponentScript>();

                if (c != nullptr) [[likely]] {
                    ImGui::InputText(Get("component.script.file_name").c_str(), &c->fileName);
                    ImGui::Checkbox(Get("component.script.enabled").c_str(), &c->enabled);

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentScript>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else [[unlikely]] {
                    ImGui::Text("Script component missing");
                }
            }
            else if (selectedComponent == CMP_PLAYER_CONTROLLER) {
                auto *c = entity.GetComponent<ComponentPlayerController>();

                if (c != nullptr) [[likely]] {
                    ImGui::InputFloat(Get("component.player_controller.speed").c_str(), &c->speed);
                    ImGui::InputFloat(Get("component.player_controller.running_speed").c_str(), &c->runningSpeed);
                    ImGui::InputFloat(Get("component.player_controller.eye_height").c_str(), &c->eyeHeight);
                    ImGui::InputFloat(Get("component.player_controller.step_size").c_str(), &c->stepSize);

                    ImGui::SliderFloat(Get("component.player_controller.friction").c_str(), &c->friction, 0.0f, 1.0f);
                    ImGui::SliderFloat(Get("component.player_controller.sensitivity_x").c_str(), &c->sensitivityX,
                                       0.001f, 2.0f);
                    ImGui::SliderFloat(Get("component.player_controller.sensitivity_y").c_str(), &c->sensitivityY,
                                       0.001f, 2.0f);

                    ImGui::Checkbox(Get("component.player_controller.no_clip").c_str(), &c->noClip);
                    ImGui::Checkbox(Get("component.player_controller.is_active").c_str(), &c->isActive);

                    ImGui::Separator();

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentPlayerController>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else [[unlikely]] {
                    ImGui::Text("Player Controller component missing");
                }
            }
            else if (selectedComponent == CMP_CAMERA) {
                auto *c = entity.GetComponent<ComponentCamera>();

                if (c != nullptr) [[likely]] {
                    ImGui::SliderFloat(Get("component.camera.fov").c_str(), &c->fov, 1.0f, 179.0f);
                    ImGui::InputFloat(Get("component.camera.aspect_ratio").c_str(), &c->aspectRatio);
                    ImGui::InputFloat(Get("component.camera.near_plane").c_str(), &c->nearPlane);
                    ImGui::InputFloat(Get("component.camera.far_plane").c_str(), &c->farPlane);
                    ImGui::Checkbox(Get("component.camera.is_active").c_str(), &c->isActive);

                    ImGui::Separator();

                    if (ImGui::Button(Get("common.delete").c_str())) {
                        entity.RemoveComponent<ComponentCamera>();
                        editingComponent = false;
                        selectedComponent = -1;
                    }
                }
                else [[unlikely]]{
                    ImGui::Text("Camera component missing");
                }
            }
            else if (selectedComponent == CMP_SPHERE_COLLIDER) {
                auto *c = entity.GetComponent<ComponentSphereCollider>();

                if (c != nullptr) [[likely]] {
                    ImGui::DragFloat(Get("component.sphere_collider.size").c_str(), &c->size);
                    ImGui::Checkbox(Get("component.sphere_collider.is_trigger").c_str(), &c->isTrigger);
                    ImGui::Checkbox(Get("component.sphere_collider.is_active").c_str(), &c->isActive);
                }
                else [[unlikely]] {
                    ImGui::Text("Sphere Collider component missing");
                }

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentSphereCollider>();
                    editingComponent = false;
                    selectedComponent = -1;
                }
            }
            else if (selectedComponent == CMP_RIGIDBODY) {
                auto *c = entity.GetComponent<ComponentRigidbody>();

                if (c != nullptr) [[likely]] {
                    ImGui::Checkbox(Get("component.rigidbody.is_static").c_str(), &c->isStatic);
                    ImGui::InputFloat(Get("component.rigidbody.mass").c_str(), &c->mass);
                }

                if (ImGui::Button(Get("common.delete").c_str())) {
                    entity.RemoveComponent<ComponentRigidbody>();
                    editingComponent = false;
                    selectedComponent = -1;
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
    using namespace Localisation;

    void ChangeMode() {
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    }

    void QueueLevelLoad(const std::string &levelName) {
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

        Editor::LoadLevel(levelToLoad);

        return true;
    }

    void DrawEditorUI() {
        ImGui::Begin(Get("editor.title").c_str());

        DrawMode();

        if (editingSector && currentMode == MODE_SECTOR && selectedSector != -1) EditingSector();
        if (editingWall && currentMode == MODE_WALL && selectedWall != -1) EditingWall();
        if (editingEntity && currentMode == MODE_ENTITY) EditingEntity();
        if (editingComponent && currentMode == MODE_ENTITY && selectedComponent != -1) EditingComponent();
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

        DrawSoundCategory();

        PutSpace(2);

        DrawTextureCategory();

        ImGui::InputInt(Get("editor.floor").c_str(), &currentFloor);

        if (ImGui::Button(Get("editor.save").c_str())) {
            Save(Editor::currentMap);
        }

        PutSpace(1);

        if (ImGui::Button(Get("editor.save_and_play").c_str())) {
            if (Save(Editor::currentMap)) {
                SDL_Log("%s", Editor::currentMap.c_str());
                quit = true;
                play = true;
            }
        }
        if (ImGui::Button(Get("editor.save_and_quit").c_str())) {
            if (Save(Editor::currentMap)) {
                SDL_Log("%s", Editor::currentMap.c_str());
                quit = true;
            }
        }

        PutSpace(3);

        if (ImGui::Button(Get("editor.shutdown").c_str())) {
            shutdown = true;
            quit = true;
        }

        static char buf[64] = "";

        if (buf[0] == '\0' && !Editor::currentMap.empty()) {
            std::strncpy(buf, Editor::currentMap.c_str(), sizeof(buf) - 1);
        }

        if (ImGui::InputText(Get("editor.level_name").c_str(), buf, IM_ARRAYSIZE(buf))) {
            Editor::currentMap = buf;
        }

        if (ImGui::Button(Get("editor.switch_to_ui").c_str())) currentState = STATE_UI;

        ImGui::End(); // Editor

        ImGui::Begin(Get("levels.title").c_str());

        for (int i = 0; i < static_cast<int>(Editor::maps.size()); ++i) {
            ImGui::PushID(i);

            std::string cleanName = Editor::maps[i];

            ImGui::Text("%s", cleanName.c_str());

            if (ImGui::Button(Get("levels.load").c_str())) {
                if (!Editor::currentMap.empty()) {
                    Save(Editor::currentMap);
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

                        if (Editor::currentMap == cleanName) {
                            Editor::currentMap = "";
                        }

                        UpdateLevels();
                    } else spdlog::error("Failed to delete level, file may not exist: {}", path.string());
                } catch (const std::filesystem::filesystem_error &e) {
                    spdlog::error("Failed to delete level: {}", e.what());
                }
            }

            ImGui::PopID();
        }

        DrawWorldSettings();

        ImGui::End(); // Levels
    }
}
