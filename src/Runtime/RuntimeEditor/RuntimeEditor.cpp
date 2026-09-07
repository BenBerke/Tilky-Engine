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
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <numbers>
#include <limits>
#include <optional>
#include <SDL3/SDL.h>
#include <string>

#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

namespace {
    IRenderer* runtimeRenderer = nullptr;
    ComponentCamera* camera = nullptr;
    ComponentTransform* transform = nullptr;

    // InputManager::SetRelativeMouseMode needs the window, and Shutdown() has
    // to release the cursor while the renderer reference is already being torn
    // down - so cache it in Start() rather than reaching through the renderer
    // at each call site.
    SDL_Window* editorWindow = nullptr;

    constexpr float MOUSE_SENSITIVITY = 0.5f;
    constexpr float BASE_MOVE_SPEED = 50.0f;
    constexpr float RAY_LENGTH = 10000.0f;

    // Texture units per pixel of left-drag. Walls and sector surfaces share it
    // so a drag feels the same on either.
    constexpr float UV_DRAG_SENSITIVITY = 0.08f;

    float moveSpeed = 50.0f;

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
    int selectedSectorFloor = -1;

    // Camera look and UV-offset dragging both consume relative mouse motion.
    // Keep the cursor locked while either interaction is active and release it
    // only after both have ended. Update() owns this; Draw() only reads it.
    bool cameraLooking = false;

    bool cursorLocked = false;
    Vector2 cursorBeforeLock = {};
    std::optional<SDL_Rect> mouseRectBeforeLock;

    // Set true to print the live payload next to the cursor while dragging.
    constexpr bool DEBUG_DRAG_DROP = false;

    // Indices, not pointers: the cache outlives the RayHit by a frame and the
    // level vectors can reallocate in between.
    struct SurfaceRef {
        RayHitType type = RayHitType::None;
        int wallIndex = -1;
        int sectorIndex = -1;
        int floorIndex = -1;
    };

    SurfaceRef hoveredSurface;

    // Captured on button-down so a drag that slides off the surface keeps
    // editing the one it started on.
    SurfaceRef uvDragSurface;
    bool draggingUv = false;
}

namespace {
    void ResetEntityInspectorState() {
        entityInspectorState = {};
    }

    // Relative mode supplies motion without moving the OS pointer. The
    // one-pixel rectangle fixes its absolute position as well. Restore that
    // position before leaving relative mode so the cursor reappears exactly
    // where the interaction started.
    void SetCursorLocked(const bool locked) {
        if (editorWindow == nullptr) return;

        if (locked) {
            if (cursorLocked) return;

            SDL_GetMouseState(&cursorBeforeLock.x, &cursorBeforeLock.y);

            const SDL_Rect* previousRect = SDL_GetWindowMouseRect(editorWindow);
            mouseRectBeforeLock = previousRect != nullptr
                ? std::optional<SDL_Rect>{*previousRect}
                : std::nullopt;

            InputManager::SetRelativeMouseMode(editorWindow, true);

            const SDL_Rect lockRect = {
                static_cast<int>(cursorBeforeLock.x),
                static_cast<int>(cursorBeforeLock.y),
                1, 1
            };

            if (!SDL_SetWindowMouseRect(editorWindow, &lockRect))
                spdlog::warn("Runtime editor could not fix the cursor position: {}", SDL_GetError());

            cursorLocked = true;
            return;
        }

        if (cursorLocked) {
            if (!SDL_SetWindowMouseRect(
                    editorWindow,
                    mouseRectBeforeLock.has_value() ? &*mouseRectBeforeLock : nullptr))
                spdlog::warn("Runtime editor could not restore the mouse rectangle: {}", SDL_GetError());

            // Do not move the pointer over another application after focus loss.
            if (SDL_GetMouseFocus() == editorWindow)
                SDL_WarpMouseInWindow(editorWindow, cursorBeforeLock.x, cursorBeforeLock.y);

            cursorLocked = false;
            mouseRectBeforeLock.reset();
        }

        InputManager::SetRelativeMouseMode(editorWindow, false);
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

    // The browser sends an absolute path; the level stores an asset reference
    // (assets-root-relative, generic separators). ToAssetReference is the one
    // function that performs that conversion, and it is what DrawAssetField
    // already calls on drop - so going through it here guarantees the runtime
    // editor and the inspector can never write two different strings for the
    // same file. A bare filename does not resolve, which is why a hand-rolled
    // conversion rendered black.
    std::string PayloadToTextureReference(const ImGuiPayload* payload) {
        if (payload == nullptr || payload->Data == nullptr || payload->DataSize <= 0) return {};

        const char* raw = static_cast<const char*>(payload->Data);
        const std::string text(raw, strnlen(raw, static_cast<size_t>(payload->DataSize)));

        if (text.empty()) return {};

        return AssetBrowser::ToAssetReference(std::filesystem::path(text), AssetKind::Texture);
    }

    std::string DescribeHoveredSurface() {
        switch (hoveredSurface.type) {
            case RayHitType::Wall:
                return "wall #" + std::to_string(hoveredSurface.wallIndex);

            case RayHitType::SectorFloor:
                return "floor of sector #" + std::to_string(hoveredSurface.sectorIndex);

            case RayHitType::SectorCeiling:
                return "ceiling of sector #" + std::to_string(hoveredSurface.sectorIndex);

            default:
                return {};
        }
    }

    // What the surface is showing right now. A texture that already renders
    // correctly is the ground truth for the string format the engine expects.
    std::string CurrentTextureOfHoveredSurface(Level& level) {
        switch (hoveredSurface.type) {
            case RayHitType::Wall: {
                if (hoveredSurface.wallIndex < 0 ||
                    hoveredSurface.wallIndex >= static_cast<int>(level.walls.size())) return {};

                return level.walls[hoveredSurface.wallIndex].textureFileName;
            }

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (hoveredSurface.sectorIndex < 0 ||
                    hoveredSurface.sectorIndex >= static_cast<int>(level.sectors.size())) return {};

                Sector& sector = level.sectors[hoveredSurface.sectorIndex];

                if (hoveredSurface.floorIndex < 0 ||
                    hoveredSurface.floorIndex >= static_cast<int>(sector.floors.size())) return {};

                const SectorFloor& sectorFloor = sector.floors[hoveredSurface.floorIndex];

                return hoveredSurface.type == RayHitType::SectorFloor
                    ? sectorFloor.floor.texture
                    : sectorFloor.ceiling.texture;
            }

            default:
                return {};
        }
    }

    bool ApplyTextureToHoveredSurface(Level& level, const std::string& textureFileName) {
        switch (hoveredSurface.type) {
            case RayHitType::Wall: {
                if (hoveredSurface.wallIndex < 0 ||
                    hoveredSurface.wallIndex >= static_cast<int>(level.walls.size())) return false;

                level.walls[hoveredSurface.wallIndex].textureFileName = textureFileName;
                return true;
            }

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (hoveredSurface.sectorIndex < 0 ||
                    hoveredSurface.sectorIndex >= static_cast<int>(level.sectors.size())) return false;

                Sector& sector = level.sectors[hoveredSurface.sectorIndex];

                if (hoveredSurface.floorIndex < 0 ||
                    hoveredSurface.floorIndex >= static_cast<int>(sector.floors.size())) return false;

                SectorFloor& sectorFloor = sector.floors[hoveredSurface.floorIndex];

                // Walls name this field textureFileName; SectorSurface calls it
                // texture. Same contents - a bare asset file name.
                if (hoveredSurface.type == RayHitType::SectorFloor)
                    sectorFloor.floor.texture = textureFileName;
                else
                    sectorFloor.ceiling.texture = textureFileName;

                return true;
            }

            default:
                return false;
        }
    }

    // Wall::textureOffset and SectorSurface::textureOffset are the same idea
    // living in two structs, so the bounds checks happen here once instead of
    // at the call site. Returns false for entities and for stale indices.
    bool AddUvOffset(Level& level, const SurfaceRef& surface, const Vector2 delta) {
        switch (surface.type) {
            case RayHitType::Wall: {
                if (surface.wallIndex < 0 ||
                    surface.wallIndex >= static_cast<int>(level.walls.size())) return false;

                Wall& wall = level.walls[surface.wallIndex];

                wall.textureOffset.x += delta.x;
                wall.textureOffset.y += delta.y;

                return true;
            }

            case RayHitType::SectorFloor:
            case RayHitType::SectorCeiling: {
                if (surface.sectorIndex < 0 ||
                    surface.sectorIndex >= static_cast<int>(level.sectors.size())) return false;

                Sector& sector = level.sectors[surface.sectorIndex];

                if (surface.floorIndex < 0 ||
                    surface.floorIndex >= static_cast<int>(sector.floors.size())) return false;

                SectorFloor& sectorFloor = sector.floors[surface.floorIndex];

                SectorSurface& target = surface.type == RayHitType::SectorFloor
                    ? sectorFloor.floor
                    : sectorFloor.ceiling;

                target.textureOffset.x += delta.x;
                target.textureOffset.y += delta.y;

                return true;
            }

            default:
                return false;
        }
    }

    bool IsUvEditableSurface(const RayHitType type) {
        return type == RayHitType::Wall ||
               type == RayHitType::SectorFloor ||
               type == RayHitType::SectorCeiling;
    }

    void DrawDropHint(const std::string& text) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        const ImVec2 mousePosition = ImGui::GetMousePos();
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 origin = {mousePosition.x + 24.0f, mousePosition.y + 24.0f};

        drawList->AddRectFilled(
            {origin.x - 6.0f, origin.y - 4.0f},
            {origin.x + textSize.x + 6.0f, origin.y + textSize.y + 4.0f},
            IM_COL32(20, 20, 20, 200),
            4.0f
        );

        drawList->AddText(origin, IM_COL32(255, 255, 255, 255), text.c_str());
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
                selectedSectorFloor = -1;
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
                    selectedSectorFloor = -1;
                }
            }
        }

        // The cursor is free unless the middle button is down, so the browser
        // is up whenever it could actually be clicked.
        if (!cameraLooking) {
            ImGui::Begin("Asset Browser##RuntimeEditor");

            if (ImGui::Button("Refresh"))  MapEditorInternal::assetBrowser.Refresh();

            ImGui::Spacing();

            // Same resolver DrawAssetField uses, so the browser thumbnails and
            // the inspector previews can never disagree about what a texture
            // name maps to. Routes back through previewTextureProvider, which
            // Start() pointed at this renderer.
            MapEditorInternal::assetBrowser.Draw(&MapEditorInternal::GetPreviewTextureID);

            ImGui::End();
        }

        // ── Drop textures straight onto geometry ─────────────────────────────
        // The 3D view is not an ImGui window, so there is no item to hang a
        // BeginDragDropTarget() off. Watch the live payload instead and apply it
        // on release outside every ImGui window. ImGui keeps the payload alive
        // for the frame the button goes up, so this fires exactly once.
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            payload != nullptr &&
            payload->IsDataType(AssetBrowser::DragDropPayloadTypeFor(AssetKind::Texture))) {
            // AllowWhenBlockedByActiveItem is required: the drag source owns the
            // active id, so a plain AnyWindow query always answers false.
            constexpr ImGuiHoveredFlags hoverFlags =
                ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;

            const bool overViewport = !ImGui::IsWindowHovered(hoverFlags);
            const std::string textureReference = PayloadToTextureReference(payload);
            const std::string surfaceName = DescribeHoveredSurface();

            if (DEBUG_DRAG_DROP) {
                DrawDropHint(
                    "would write=" + (textureReference.empty() ? std::string("<none>") : textureReference) +
                    "\ncurrently   =" + [&] {
                        const std::string current = CurrentTextureOfHoveredSurface(level);
                        return current.empty() ? std::string("<empty>") : current;
                    }() +
                    "\nsurface=" + (surfaceName.empty() ? "<none>" : surfaceName) +
                    "  viewport=" + (overViewport ? "yes" : "no")
                );
            }
            else if (overViewport && !textureReference.empty() && !surfaceName.empty()) {
                DrawDropHint(textureReference + "  ->  " + surfaceName);
            }

            if (overViewport &&
                !textureReference.empty() &&
                !surfaceName.empty() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                ApplyTextureToHoveredSurface(level, textureReference)) {
                if (runtimeRenderer != nullptr) runtimeRenderer->RefreshTexturesFromLevel();

                EditorFunctions::Print("Applied " + textureReference + " to " + surfaceName);
            }
        }

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

        if (runtimeRenderer == nullptr) {
            spdlog::critical("Runtime renderer could not be found");
            return;
        }

        renderer.SetUseEditorCamera(true);

        camera = renderer.GetEditorCamera();
        transform = renderer.GetEditorCameraTransform();

        if (camera == nullptr || transform == nullptr) {
            spdlog::error("RuntimeEditor::Start failed: editor camera or transform was not created");
            return;
        }
#ifndef TILKY_STANDALONE
        transform->position = level.runtimeCamPos;
        camera->pitch = level.runtimeCamRot.x;
        camera->yaw = level.runtimeCamRot.y;
#endif
        camera->forward = GetCameraForward(*camera);

        editorWindow = renderer.GetWindow();

        if (editorWindow == nullptr) spdlog::error("Runtime editor could not get the window; cursor lock is disabled");

        // Unlocked by default. Middle-button camera look and left-button UV
        // dragging decide the lock state fresh every Update().
        cameraLooking = false;
        draggingUv = false;
        uvDragSurface = {};
        SetCursorLocked(false);

        spdlog::info("Runtime editor is using renderer editor-only camera");

        if (!MapEditorInternal::assetBrowserInitialized) {
            MapEditorInternal::assetBrowser.SetRootDirectory(ProjectManager::GetAssetsPath());
            MapEditorInternal::assetBrowserInitialized = true;
        }

        // The shared inspector widgets (DrawAssetField and the thumbnail
        // helpers, all defined in MapEditorUI.cpp) resolve previews through the
        // Map Editor's SDL_Renderer, which does not exist in this process - so
        // every asset field would draw an empty box. Point them at the live
        // renderer instead, which hands back IDs the active ImGui backend can
        // actually bind. Cleared again in Shutdown().
        MapEditorInternal::previewTextureProvider = [](const std::string& fileName) -> ImTextureID {
            return runtimeRenderer != nullptr ? runtimeRenderer->GetImGuiTextureID(fileName) : ImTextureID{};
        };
    }

    void Update(Level& level,
        IRenderer& renderer,
        const bool relativeMouseMod,
        const bool mouseBlockedByImGui,
        const bool keyboardBlockedByImGui,
        const float screenWidth,
        const float screenHeight) {
        // Still in the signature so the call site does not have to change, but
        // the runtime editor owns its cursor state now.
        (void)relativeMouseMod;

        if (!renderer.IsUsingEditorCamera()) renderer.SetUseEditorCamera(true);

        if (camera == nullptr || transform == nullptr) {
            camera = renderer.GetEditorCamera();
            transform = renderer.GetEditorCameraTransform();
        }

        if (camera == nullptr || transform == nullptr) return;

        //region look

        const bool middleHeld = InputManager::GetMouseButton(SDL_BUTTON_MIDDLE);

        // The ImGui test gates only the *start* of a look. Once the cursor is
        // hidden ImGui keeps reporting the position it froze at, so re-testing
        // every frame would drop the drag the moment you swing past a panel.
        const bool windowFocused = editorWindow != nullptr && SDL_GetKeyboardFocus() == editorWindow;
        const bool wantLook =
            windowFocused &&
            middleHeld &&
            !draggingUv &&
            (cameraLooking || !mouseBlockedByImGui);

        if (wantLook != cameraLooking) {
            cameraLooking = wantLook;
            SetCursorLocked(cameraLooking || draggingUv);
        }

        if (cameraLooking) {
            camera->yaw -= InputManager::GetMouseDelta().x * MOUSE_SENSITIVITY;
            camera->pitch -= InputManager::GetMouseDelta().y * MOUSE_SENSITIVITY;
        }

        //endregion

        camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
        camera->yaw = std::fmod(camera->yaw, 360.0f);
        if (camera->yaw < 0.0f) camera->yaw += 360.0f;

        // Runtime editor cameras are not updated by the normal gameplay
        // CameraSystem, so keep the renderer-facing direction synchronized.
        camera->forward = GetCameraForward(*camera);

        //region movement

        Vector3 movement = {0.0f, 0.0f, 0.0f};

        //if (!keyboardBlockedByImGui) {
            const Vector3 forward = camera->forward;
            const Vector3 left = GetCameraLeft(*camera);

            if (InputManager::GetKey(SDL_SCANCODE_W)) movement = movement + forward;
            if (InputManager::GetKey(SDL_SCANCODE_S)) movement = movement - forward;
            if (InputManager::GetKey(SDL_SCANCODE_A)) movement = movement + left;
            if (InputManager::GetKey(SDL_SCANCODE_D)) movement = movement - left;
            if (InputManager::GetKey(SDL_SCANCODE_SPACE)) movement.y += 1.0f;
            if (InputManager::GetKey(SDL_SCANCODE_LCTRL)) movement.y -= 1.0f;

            if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) moveSpeed += 3.0f;
            if (InputManager::GetKeyDown(SDL_SCANCODE_E)) moveSpeed -= 3.0f;
            if (InputManager::GetKey(SDL_SCANCODE_LSHIFT)) moveSpeed = BASE_MOVE_SPEED;

            moveSpeed = std::fmax(moveSpeed, 0.0001f);

            const float movementLengthSq = movement.x * movement.x + movement.y * movement.y + movement.z * movement.z;

            if (movementLengthSq > 0.0f) {
                movement = movement * (1.0f / std::sqrt(movementLengthSq));
                transform->AddPosition(movement * moveSpeed * GameTime::deltaTime);
            }
       // }

        //endregion

        const Vector3 rayOrigin = transform->position;

        const Vector2 viewportSize = {
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight)
        };

        // Camera look aims through the centre. A UV drag keeps using the point
        // at which its cursor was locked, so hidden relative input cannot move
        // the ray's absolute screen position.
        const Vector2 rayScreenPosition = cameraLooking
            ? Vector2{viewportSize.x * 0.5f, viewportSize.y * 0.5f}
            : draggingUv
                ? cursorBeforeLock
                : InputManager::GetMousePosition();

        const Vector3 rayDirection = GetMouseRayDirection(
            *camera,
            rayScreenPosition,
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

        // Cached for Draw(), which needs a drop target but has no viewport size
        // to build a ray from. Recomputed every frame whether or not anything is
        // being dragged, so the drop path stays a pure lookup.
        hoveredSurface = {};

        if (hit.has_value()) {
            hoveredSurface.type = hit->type;

            if (hit->type == RayHitType::Wall) {
                hoveredSurface.wallIndex = FindWallIndex(level, hit->wall);
            }
            else if (hit->type == RayHitType::SectorFloor || hit->type == RayHitType::SectorCeiling) {
                hoveredSurface.sectorIndex = FindSectorIndex(level, hit->sector);
                hoveredSurface.floorIndex = hit->sectorFloorIndex;
            }
        }

        //region uv drag

        // The asset browser's drop path also rides the left button, so stay out
        // of the way while a payload is live - otherwise dragging a texture in
        // smears the UVs on the way to the surface.
        const bool dragDropActive = ImGui::GetDragDropPayload() != nullptr;

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) &&
            windowFocused &&
            !mouseBlockedByImGui &&
            !dragDropActive &&
            !cameraLooking &&
            IsUvEditableSurface(hoveredSurface.type)) {
            uvDragSurface = hoveredSurface;
            draggingUv = true;
            SetCursorLocked(cameraLooking || draggingUv);
        }

        if (draggingUv &&
            (!InputManager::GetMouseButton(SDL_BUTTON_LEFT) || !windowFocused)) {
            draggingUv = false;
            uvDragSurface = {};
            SetCursorLocked(cameraLooking || draggingUv);
        }

        if (draggingUv) {
            const Vector2 mouseDelta = InputManager::GetMouseDelta();

            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                // Negated so the texture tracks the cursor rather than running
                // from it. Flip the signs if the sampler wants the opposite.
                AddUvOffset(level, uvDragSurface, {
                    mouseDelta.x * UV_DRAG_SENSITIVITY,
                    -mouseDelta.y * UV_DRAG_SENSITIVITY
                });
            }
        }

        //endregion

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
                    selectedSectorFloor = -1;

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
                    selectedSectorFloor = -1;

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
                        selectedSectorFloor = -1;
                        return;
                    }

                    editingSector = true;
                    selectedSector = sectorIndex;
                    selectedSectorSurface = hit->type;
                    selectedSectorFloor = hit->sectorFloorIndex;

                    editingEntity = false;
                    selectedEntityId.reset();
                    ResetEntityInspectorState();

                    editingWall = false;
                    selectedWall = -1;

                    spdlog::info(
                        "Selected sector {} floor={} surface={}",
                        selectedSector,
                        selectedSectorFloor,
                        hit->type == RayHitType::SectorFloor ? "floor" : "ceiling"
                    );

                    break;
                }

                case RayHitType::None:
                default: spdlog::warn("Runtime editor ray hit had invalid hit type"); break;
            }
        }

        const float wheel = InputManager::GetMouseWheelScroll();

        if (wheel != 0.0f && !mouseBlockedByImGui && hit.has_value()) {
            constexpr float HEIGHT_STEP = 1.0f;
            constexpr float UV_SCALE_STEP = 0.01f;
            constexpr float MIN_ROOM_HEIGHT = 1.0f;
            const float heightDelta = wheel * HEIGHT_STEP;
            const float uvScaleDelta = wheel * UV_SCALE_STEP;

            if (hit->type == RayHitType::Wall && hit->wall != nullptr) {
                Wall& wall = *hit->wall;
                wall.textureScale.x += uvScaleDelta;
                wall.textureScale.y += uvScaleDelta;
            }
            else if ((hit->type == RayHitType::SectorFloor ||
                      hit->type == RayHitType::SectorCeiling) &&
                     hit->sector != nullptr &&
                     hit->sectorFloorIndex >= 0 &&
                     hit->sectorFloorIndex < static_cast<int>(hit->sector->floors.size())) {
                Sector& sector = *hit->sector;
                const int floorIndex = hit->sectorFloorIndex;
                SectorFloor& floor = sector.floors[floorIndex];

                if (hit->type == RayHitType::SectorFloor) {
                    const float minimumHeight =
                        floorIndex > 0 ? sector.floors[floorIndex - 1].ceiling.height : std::numeric_limits<float>::lowest();

                    const float maximumHeight = floor.ceiling.height - MIN_ROOM_HEIGHT;

                    if (minimumHeight <= maximumHeight) {
                        floor.floor.height = std::clamp(
                            floor.floor.height + heightDelta,
                            minimumHeight,
                            maximumHeight
                        );
                    }
                }
                else if (hit->type == RayHitType::SectorCeiling) {
                    const float minimumHeight = floor.floor.height + MIN_ROOM_HEIGHT;

                    const float maximumHeight =
                        floorIndex + 1 < static_cast<int>(sector.floors.size())
                            ? sector.floors[floorIndex + 1].floor.height
                            : std::numeric_limits<float>::max();

                    if (minimumHeight <= maximumHeight) {
                        floor.ceiling.height = std::clamp(
                            floor.ceiling.height + heightDelta,
                            minimumHeight,
                            maximumHeight
                        );
                    }
                }
            }
        }

        ImGuiDrawFunctions::SetImGuiFocus(!cameraLooking && !draggingUv);

        if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) runtimeRenderer->RefreshTexturesFromLevel();
    } // Update

    void Draw(Level &level) {
        RuntimeEditorUi::Draw(level);
    }

    void Shutdown(Level& level) {
        (void)level;
#ifndef TILKY_STANDALONE
        level.runtimeCamPos = transform->position;
        level.runtimeCamRot.x = camera->pitch;
        level.runtimeCamRot.y = camera->yaw;
#endif
        // Must be cleared before control can return to the Map Editor: leaving
        // it set would feed that editor's SDL backend renderer-specific IDs.
        MapEditorInternal::previewTextureProvider = nullptr;

        runtimeRenderer = nullptr;
        camera = nullptr;
        transform = nullptr;

        editingEntity = false;
        selectedEntityId.reset();
        editingWall = false;
        selectedWall = -1;
        editingSector = false;
        selectedSector = -1;
        selectedSectorSurface = RayHitType::None;
        selectedSectorFloor = -1;

        // Release both relative mode and our cursor rectangle before switching editors.
        SetCursorLocked(false);

        editorWindow = nullptr;
        cameraLooking = false;
        draggingUv = false;
        uvDragSurface = {};
        hoveredSurface = {};

        spdlog::info("Runtime editor shut down");
    }
}