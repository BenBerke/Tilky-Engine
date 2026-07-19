//
// Created by berke on 6/3/2026.
//

#include "Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp"

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"

#include "Headers/Runtime/Renderer/IRenderer.hpp"

#include "../../Editor/EditorInternal.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Editor/AssetBrowser.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <numbers>

namespace {
    IRenderer* runtimeRenderer = nullptr;
    ComponentCamera* camera = nullptr;
    ComponentTransform* transform = nullptr;

    constexpr float MOUSE_SENSITIVITY = 0.5f;
    constexpr float MOVE_SPEED = 50.0f;
    constexpr float FAST_MOVE_SPEED = 175.0f;
    constexpr float RAY_LENGTH = 10000.0f;

    Vector3 GetCameraForward(const ComponentCamera& camera) {
        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;
        const float pitchRadians = camera.pitch * std::numbers::pi_v<float> / 180.0f;
        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);
        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        return Vector3Math::Normalized({yawSin * pitchCos, pitchSin, yawCos * pitchCos});
    }

    Vector3 GetCameraLeft(const ComponentCamera& camera) {
        const float yawRadians = camera.yaw * std::numbers::pi_v<float> / 180.0f;
        return Vector3Math::Normalized({std::cos(yawRadians), 0.0f, -std::sin(yawRadians)});
    }

    Vector3 GetMouseRayDirection(const ComponentCamera& camera, const Vector2 mousePosition, const Vector2 viewportSize) {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return camera.forward;

        const float ndcX = (2.0f * mousePosition.x / viewportSize.x) - 1.0f;
        const float ndcY = 1.0f - (2.0f * mousePosition.y / viewportSize.y);

        const Vector3 forward = Vector3Math::Normalized(camera.forward);
        const Vector3 left = GetCameraLeft(camera);
        const Vector3 up = Vector3Math::Normalized(Vector3Math::Cross(forward, left));
        const float aspect = viewportSize.x / viewportSize.y;
        const float fovRadians = camera.fov * std::numbers::pi_v<float> / 180.0f;
        const float tanHalfFov = std::tan(fovRadians * 0.5f);

        return Vector3Math::Normalized(
            forward +
            left * (-ndcX * tanHalfFov * aspect) +
            up * (ndcY * tanHalfFov)
        );
    }

    ImGuiDrawFunctions::EntityInspectorState entityInspectorState;

    bool editingEntity = false;
    std::optional<ID> selectedEntityId;

    bool editingWall = false;
    int selectedWall = -1;

    bool editingSector = false;
    int selectedSector = -1;

    RayHitType selectedSectorSurface = RayHitType::None;
}

namespace {
    void ResetEntityInspectorState() {
        entityInspectorState = {};
    }

    Entity* FindEntityById(Level& level, const ID entityId) {
        for (Entity& entity : level.entities) if (entity.id == entityId) return &entity;

        return nullptr;
    }

    int FindWallIndex(const Level& level, const Wall* wallToFind) {
        if (wallToFind == nullptr) return -1;

        for (int i = 0; i < static_cast<int>(level.walls.size()); i++) if (&level.walls[i] == wallToFind) return i;

        return -1;
    }

    int FindSectorIndex(Level& level, const Sector* sector) {
        if (sector == nullptr) return -1;

        for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i) if (&level.sectors[i] == sector) return i;

        return -1;
    }
}

namespace RuntimeEditorUi {
    constexpr bool DRAGGABLE = true;
    void Draw(Level& level) {
        if (editingEntity) {
            if (!selectedEntityId.has_value()) {
                editingEntity = false;
                ResetEntityInspectorState();
                return;
            }

            Entity* entityToEdit = FindEntityById(level, *selectedEntityId);

            if (entityToEdit == nullptr) {
                editingEntity = false;
                selectedEntityId.reset();
                ResetEntityInspectorState();
                return;
            }

            const bool deleteRequested =
                ImGuiDrawFunctions::DrawEntityEditor(*entityToEdit,entityInspectorState, &editingEntity, DRAGGABLE);

            if (deleteRequested) {
                const ID idToDelete = entityToEdit->id;

                editingEntity = false;
                selectedEntityId.reset();
                ResetEntityInspectorState();

                level.DestroyEntity(idToDelete);
                return;
            }

            if (!editingEntity) {
                selectedEntityId.reset();
                ResetEntityInspectorState();
                return;
            }

            if (entityInspectorState.editingComponent && entityInspectorState.selectedComponent != -1) {
                ImGuiDrawFunctions::DrawComponentEditor(
                    *entityToEdit,
                    entityInspectorState,
                    &entityInspectorState.editingComponent, DRAGGABLE
                );
            }
        }

        if (editingWall) {
            if (selectedWall < 0 || selectedWall >= static_cast<int>(level.walls.size())) {
                editingWall = false;
                selectedWall = -1;
                return;
            }

            Wall& wall = level.walls[selectedWall];

            const bool deleteRequested =
                ImGuiDrawFunctions::DrawWallEditor(wall, &editingWall, selectedWall, DRAGGABLE);

            if (deleteRequested) {
                level.walls.erase(level.walls.begin() + selectedWall);

                editingWall = false;
                selectedWall = -1;
                return;
            }

            if (!editingWall) {
                selectedWall = -1;
                return;
            }
        }

        if (editingSector) {
            if (selectedSector < 0 || selectedSector >= static_cast<int>(level.sectors.size())) {
                editingSector = false;
                selectedSector = -1;
                selectedSectorSurface = RayHitType::None;
            }
            else {
                bool open = true;

                ImGuiDrawFunctions::DrawSectorEditor(
                    level.sectors[selectedSector],
                    &open,
                    selectedSector,
                    true
                );

                if (!open) {
                    editingSector = false;
                    selectedSector = -1;
                    selectedSectorSurface = RayHitType::None;
                }
            }
        }

        if (!MapEditorInternal::assetBrowserInitialized) {
             MapEditorInternal::assetBrowser.SetRootDirectory(ProjectManager::GetAssetsPath());
              MapEditorInternal::assetBrowserInitialized = true;
        }

        ImGui::Begin("Asset Browser##RuntimeEditor");

        if (ImGui::Button("Refresh"))  MapEditorInternal::assetBrowser.Refresh();

        ImGui::Spacing();

        MapEditorInternal::assetBrowser.Draw([](const std::string& fileName) {
            if (runtimeRenderer == nullptr) return ImTextureID{};
            return runtimeRenderer->GetImGuiTextureID(fileName);
        });

        ImGui::End();

        // Crosshair, useless because mouse position is used for selection
        // const ImGuiViewport* viewport = ImGui::GetMainViewport();
        // const ImVec2 center = viewport->GetCenter();
        //
        // constexpr float size = 6.0f;
        //
        // ImGui::GetForegroundDrawList()->AddRectFilled(
        //     {center.x - size * 0.5f, center.y - size * 0.5f},
        //     {center.x + size * 0.5f, center.y + size * 0.5f},
        //     IM_COL32(255, 255, 255, 255)
        // );
    }
}

namespace RuntimeEditor {
    void Start(Level& level, IRenderer& renderer) {
        (void)level;

        spdlog::info("Runtime editor started");

        runtimeRenderer = &renderer;

        renderer.SetUseEditorCamera(true);

        camera = renderer.GetEditorCamera();
        transform = renderer.GetEditorCameraTransform();

        if (camera == nullptr || transform == nullptr) {
            spdlog::error("RuntimeEditor::Start failed: editor camera or transform was not created");
            return;
        }

        camera->forward = GetCameraForward(*camera);

        spdlog::info("Runtime editor is using renderer editor-only camera");
    }

    void Update(Level& level,
        IRenderer& renderer,
        const bool relativeMouseMod,
        const bool mouseBlockedByImGui,
        const bool keyboardBlockedByImGui,
        const float screenWidth,
        const float screenHeight) {
        if (!renderer.IsUsingEditorCamera()) renderer.SetUseEditorCamera(true);

        if (camera == nullptr || transform == nullptr) {
            camera = renderer.GetEditorCamera();
            transform = renderer.GetEditorCameraTransform();
        }

        if (camera == nullptr || transform == nullptr) return;

        if (relativeMouseMod && !mouseBlockedByImGui) {
            camera->yaw -= InputManager::GetMouseDelta().x * MOUSE_SENSITIVITY;
            camera->pitch -= InputManager::GetMouseDelta().y * MOUSE_SENSITIVITY;
        }

        camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
        camera->yaw = std::fmod(camera->yaw, 360.0f);
        if (camera->yaw < 0.0f) camera->yaw += 360.0f;

        // Runtime editor cameras are not updated by the normal gameplay
        // CameraSystem, so keep the renderer-facing direction synchronized.
        camera->forward = GetCameraForward(*camera);

        //region movement

        Vector3 movement = {0.0f, 0.0f, 0.0f};

        if (!keyboardBlockedByImGui) {
            const Vector3 forward = camera->forward;
            const Vector3 left = GetCameraLeft(*camera);

            if (InputManager::GetKey(SDL_SCANCODE_W)) movement = movement + forward;
            if (InputManager::GetKey(SDL_SCANCODE_S)) movement = movement - forward;
            if (InputManager::GetKey(SDL_SCANCODE_A)) movement = movement + left;
            if (InputManager::GetKey(SDL_SCANCODE_D)) movement = movement - left;
            if (InputManager::GetKey(SDL_SCANCODE_SPACE)) movement.y += 1.0f;
            if (InputManager::GetKey(SDL_SCANCODE_LCTRL)) movement.y -= 1.0f;

            const float movementLengthSq =
                movement.x * movement.x +
                movement.y * movement.y +
                movement.z * movement.z;

            if (movementLengthSq > 0.0f) {
                movement = movement * (1.0f / std::sqrt(movementLengthSq));
                const float speed = InputManager::GetKey(SDL_SCANCODE_LSHIFT) ? FAST_MOVE_SPEED : MOVE_SPEED;
                transform->AddPosition(movement * speed * GameTime::deltaTime);
            }
        }

        //endregion

        const Vector3 rayOrigin = transform->position;

        const Vector2 mousePosition = InputManager::GetMousePosition();

        const Vector2 viewportSize = {
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight)
        };

        const Vector3 rayDirection = GetMouseRayDirection(
            *camera,
            mousePosition,
            viewportSize
        );

        const std::optional<RayHit> hit = GameFunctions::Raycast(
            level,
            rayOrigin,
            rayDirection,
            RAY_LENGTH,
            camera->ownerID,
            false
        );

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT) && !mouseBlockedByImGui) {
            if (!hit.has_value()) {
                spdlog::info("Runtime editor ray missed");
                return;
            }

            spdlog::info(
                "Runtime editor ray hit. type={}, entity={}, wall={}, sector={}",
                static_cast<int>(hit->type),
                hit->entity != nullptr,
                hit->wall != nullptr,
                hit->sector != nullptr
            );

            switch (hit->type) {
                case RayHitType::Entity: {
                    if (hit->entity == nullptr) {
                        spdlog::error("RayHitType::Entity had null entity pointer");
                        return;
                    }

                    const ID hitEntityId = hit->entity->id;

                    spdlog::info("Selected entity {}", hitEntityId);

                    if (!selectedEntityId.has_value() || *selectedEntityId != hitEntityId) ResetEntityInspectorState();

                    editingEntity = true;
                    selectedEntityId = hitEntityId;

                    editingWall = false;
                    selectedWall = -1;

                    editingSector = false;
                    selectedSector = -1;
                    selectedSectorSurface = RayHitType::None;

                    spdlog::info("Entity selection finished");
                    break;
                }

                case RayHitType::Wall: {
                    if (hit->wall == nullptr) {
                        spdlog::error("RayHitType::Wall had null wall pointer");
                        return;
                    }

                    const int wallIndex = FindWallIndex(level, hit->wall);

                    if (wallIndex == -1) {
                        spdlog::error("Selected wall was not found in level.walls");
                        editingWall = false;
                        selectedWall = -1;
                        return;
                    }

                    editingWall = true;
                    selectedWall = wallIndex;

                    editingEntity = false;
                    selectedEntityId.reset();
                    ResetEntityInspectorState();

                    editingSector = false;
                    selectedSector = -1;
                    selectedSectorSurface = RayHitType::None;

                    spdlog::info("Selected wall {}", selectedWall);
                    break;
                }

                case RayHitType::SectorFloor:
                case RayHitType::SectorCeiling: {
                    if (hit->sector == nullptr) {
                        spdlog::error("Sector ray hit had null sector pointer");
                        return;
                    }

                    const int sectorIndex = FindSectorIndex(level, hit->sector);

                    if (sectorIndex == -1) {
                        spdlog::error("Selected sector was not found in level.sectors");
                        editingSector = false;
                        selectedSector = -1;
                        selectedSectorSurface = RayHitType::None;
                        return;
                    }

                    editingSector = true;
                    selectedSector = sectorIndex;
                    selectedSectorSurface = hit->type;

                    editingEntity = false;
                    selectedEntityId.reset();
                    ResetEntityInspectorState();

                    editingWall = false;
                    selectedWall = -1;

                    spdlog::info(
                        "Selected sector {} surface={}",
                        selectedSector,
                        hit->type == RayHitType::SectorFloor ? "floor" : "ceiling"
                    );

                    break;
                }

                case RayHitType::None:
                default:
                    spdlog::warn("Runtime editor ray hit had invalid hit type");
                    break;
            }
        }

        const float wheel = InputManager::GetMouseWheelScroll();

        if (wheel != 0.0f && !mouseBlockedByImGui && hit.has_value() && hit->sector != nullptr) {
            constexpr float HEIGHT_STEP = 1.0f;
            constexpr float MIN_SECTOR_CLEARANCE = 1.0f;

            Sector& sector = *hit->sector;
            const float heightDelta = wheel * HEIGHT_STEP;

            if (hit->type == RayHitType::SectorCeiling) {
                sector.ceilingHeight = std::max(
                    sector.floorHeight + MIN_SECTOR_CLEARANCE,
                    sector.ceilingHeight + heightDelta
                );
            }
            else if (hit->type == RayHitType::SectorFloor) {
                sector.floorHeight = std::min(
                    sector.ceilingHeight - MIN_SECTOR_CLEARANCE,
                    sector.floorHeight + heightDelta
                );
            }
        }

        ImGuiDrawFunctions::SetImGuiFocus(!relativeMouseMod);
    } // Update

    void Draw(Level& level) {
        RuntimeEditorUi::Draw(level);
    }

    void Shutdown(const Level& level) {
        (void)level;

        runtimeRenderer = nullptr;
        camera = nullptr;
        transform = nullptr;

        spdlog::info("Runtime editor shut down");
    }
}