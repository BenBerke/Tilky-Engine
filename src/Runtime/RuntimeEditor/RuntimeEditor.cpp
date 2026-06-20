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

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
    ComponentCamera* camera = nullptr;
    ComponentTransform* transform = nullptr;

    constexpr float MOUSE_SENSITIVITY = 0.5f;
    constexpr float MOVE_SPEED = 50.0f;
    constexpr float FAST_MOVE_SPEED = 175.0f;
    constexpr float RAY_LENGTH = 10000.0f;

    Vector3 GetFreecamForward() {
        const float yawRadians =
                camera->yaw * std::numbers::pi_v<float> / 180.0f;

        const float pitchRadians =
                camera->pitch * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        return Vector3Math::Normalized({
            yawSin * pitchCos,
            yawCos * pitchCos,
            pitchSin
        });
    }


    Vector3 GetMouseRayDirection(
        const ComponentCamera &camera,
        const Vector2 mousePosition,
        const Vector2 viewportSize
    ) {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
            return GetFreecamForward();
        }

        // Mouse position -> Normalized Device Coordinates
        // SDL mouse Y is top-to-bottom, so Y must be flipped.
        const float ndcX = (2.0f * mousePosition.x / viewportSize.x) - 1.0f;
        const float ndcY = 1.0f - (2.0f * mousePosition.y / viewportSize.y);

        const float yawRadians =
                camera.yaw * std::numbers::pi_v<float> / 180.0f;

        const float pitchRadians =
                camera.pitch * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const float pitchSin = std::sin(pitchRadians);
        const float pitchCos = std::cos(pitchRadians);

        const Vector3 forward = Vector3Math::Normalized({
            yawSin * pitchCos,
            yawCos * pitchCos,
            pitchSin
        });

        const Vector3 right = Vector3Math::Normalized({
            yawCos,
            -yawSin,
            0.0f
        });

        const Vector3 up = Vector3Math::Normalized(
            Vector3Math::Cross(right, forward)
        );

        const float aspect = viewportSize.x / viewportSize.y;

        const float fovRadians =
                camera.fov * std::numbers::pi_v<float> / 180.0f;

        const float tanHalfFov = std::tan(fovRadians * 0.5f);

        return Vector3Math::Normalized(
            forward +
            right * (-ndcX * tanHalfFov * aspect) +
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
        for (Entity& entity : level.entities) {
            if (entity.id == entityId) {
                return &entity;
            }
        }

        return nullptr;
    }

    int FindWallIndex(const Level& level, const Wall* wallToFind) {
        if (wallToFind == nullptr) return -1;

        for (int i = 0; i < static_cast<int>(level.walls.size()); i++) {
            if (&level.walls[i] == wallToFind) {
                return i;
            }
        }

        return -1;
    }

    int FindSectorIndex(Level& level, const Sector* sector) {
        if (sector == nullptr) return -1;

        for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i)
            if (&level.sectors[i] == sector) return i;

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
                ImGuiDrawFunctions::DrawEntityEditor(
                    *entityToEdit,
                    entityInspectorState,
                    &editingEntity, DRAGGABLE
                );

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
                ImGuiDrawFunctions::DrawWallEditor(
                    wall,
                    &editingWall,
                    selectedWall, DRAGGABLE
                );

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
            if (
                selectedSector < 0 ||
                selectedSector >= static_cast<int>(level.sectors.size())
            ) {
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
    }
}

namespace RuntimeEditor {
    void Start(Level& level, IRenderer& renderer) {
        (void)level;

        spdlog::info("Runtime editor started");

        renderer.SetUseEditorCamera(true);

        camera = renderer.GetEditorCamera();
        transform = renderer.GetEditorCameraTransform();

        if (camera == nullptr || transform == nullptr) {
            spdlog::error("RuntimeEditor::Start failed: editor camera or transform was not created");
            return;
        }

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

            camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
            camera->yaw = std::fmod(camera->yaw, 360.0f);
        }

        const float yawRadians = camera->yaw * std::numbers::pi_v<float> / 180.0f;

        const float yawSin = std::sin(yawRadians);
        const float yawCos = std::cos(yawRadians);

        const Vector2 forward = { yawSin, yawCos };
        const Vector2 right = { yawCos, -yawSin };

        //region movement

        Vector3 movement = { 0.0f, 0.0f, 0.0f };

        if (InputManager::GetKey(SDL_SCANCODE_W)) {
            movement.x += forward.x;
            movement.y += forward.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_S)) {
            movement.x -= forward.x;
            movement.y -= forward.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_A)) {
            movement.x += right.x;
            movement.y += right.y;
        }

        if (InputManager::GetKey(SDL_SCANCODE_D)) {
            movement.x -= right.x;
            movement.y -= right.y;
        }

        if (!keyboardBlockedByImGui) {
            if (InputManager::GetKey(SDL_SCANCODE_SPACE)) movement.z += 1.0f;
            if (InputManager::GetKey(SDL_SCANCODE_LCTRL)) movement.z -= 1.0f;
        }

        const float length = std::sqrt(
            movement.x * movement.x +
            movement.y * movement.y +
            movement.z * movement.z
        );

        if (length > 0.0f) {
            movement.x /= length;
            movement.y /= length;
            movement.z /= length;
        }

        const float speed = InputManager::GetKey(SDL_SCANCODE_LSHIFT)
            ? FAST_MOVE_SPEED
            : MOVE_SPEED;

        transform->AddPosition(movement * speed * GameTime::deltaTime);

        //endregion

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT) && !mouseBlockedByImGui) {
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

                    if (!selectedEntityId.has_value() || *selectedEntityId != hitEntityId) {
                        ResetEntityInspectorState();
                    }

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

        ImGuiDrawFunctions::SetImGuiFocus(!relativeMouseMod);
    } // Update

    void Draw(Level& level) {
        RuntimeEditorUi::Draw(level);
    }

    void Shutdown(const Level& level) {
        (void)level;

        camera = nullptr;
        transform = nullptr;

        spdlog::info("Runtime editor shut down");
    }
}