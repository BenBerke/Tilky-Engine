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

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
    ComponentCamera* camera = nullptr;
    ComponentTransform* transform = nullptr;

    constexpr float MOUSE_SENSITIVITY = 0.5f;
    constexpr float MOVE_SPEED = 50.0f;
    constexpr float FAST_MOVE_SPEED = 75.0f;
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

    ImGuiDrawFunctions::EntityInspectorState entityInspectorState;

    bool editingEntity = false;
    std::optional<EntityID> selectedEntityId;

    bool editingWall = false;
    int selectedWall = -1;

}

namespace {
    void ResetEntityInspectorState() {
        entityInspectorState = {};
    }

    Entity* FindEntityById(Level& level, const EntityID entityId) {
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
}

namespace RuntimeEditorUi {
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
                    &editingEntity
                );

            if (deleteRequested) {
                const EntityID idToDelete = entityToEdit->id;

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

            if (
                entityInspectorState.editingComponent &&
                entityInspectorState.selectedComponent != -1
            ) {
                ImGuiDrawFunctions::DrawComponentEditor(
                    *entityToEdit,
                    entityInspectorState,
                    &entityInspectorState.editingComponent
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
                    selectedWall
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
    }
}

namespace RuntimeEditor {
    void Start(Level& level) {
        spdlog::info("Runtime editor started");

        camera = nullptr;
        transform = nullptr;

        for (ComponentCamera& _camera : level.cameras.components) {
            if (_camera.isActive) {
                camera = &_camera;
                transform = level.transforms.Get(camera->ownerID);
                break;
            }
        }

        if (camera == nullptr || transform == nullptr) {
            spdlog::error("RuntimeEditor::Start failed: no active camera or camera transform");
        }
    }

    void Update(Level& level) {
        if (camera == nullptr || transform == nullptr) return;

        camera->yaw -= InputManager::GetMouseDelta().x * MOUSE_SENSITIVITY;
        camera->pitch -= InputManager::GetMouseDelta().y * MOUSE_SENSITIVITY;

        camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
        camera->yaw = std::fmod(camera->yaw, 360.0f);

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

        if (InputManager::GetKey(SDL_SCANCODE_SPACE)) movement.z += 1.0f;
        if (InputManager::GetKey(SDL_SCANCODE_LCTRL)) movement.z -= 1.0f;

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

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
            const Vector3 rayOrigin = transform->position;
            const Vector3 rayDirection = GetFreecamForward();


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
                "Runtime editor ray hit. entity={}, wall={}",
                hit->entity != nullptr,
                hit->wall != nullptr
            );

            if (hit->entity != nullptr) {
                const EntityID hitEntityId = hit->entity->id;

                spdlog::info("Selected entity {}", hitEntityId);

                if (!selectedEntityId.has_value() || *selectedEntityId != hitEntityId) {
                    ResetEntityInspectorState();
                }

                editingEntity = true;
                selectedEntityId = hitEntityId;

                editingWall = false;
                selectedWall = -1;

                spdlog::info("Entity selection finished");
            }
            else if (hit->wall != nullptr) {
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

                spdlog::info("Selected wall {}", selectedWall);
            }
        }
    } // Update

    void Draw(Level& level) {
        RuntimeEditorUi::Draw(level);
    }

    void Shutdown(Level& level) {
        camera = nullptr;
        transform = nullptr;

        spdlog::info("Runtime editor shut down");
    }
}