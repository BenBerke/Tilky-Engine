//
// Created by berke on 5/16/2026.
//
#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/UISystem.hpp"
#include "src/Editor/EditorInternal.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Objects/Components.hpp"

#include <algorithm>
#include <cmath>

/// This script is responsible for handling keyboard/mouse input inside the UI Editor.
///
/// Structure mirrors HandleEditorInput()/UpdateEditorZoom() in
/// MapEditorInput.cpp. Hit-testing/handle-placement math is ported from
/// UI_vs.glsl the same way UIEditorDraw.cpp's is: resolvedPosition is the
/// rect's top-left corner, rotation is always around the rect's own centre
/// (never around `pivot`) - see the file comment at the top of
/// UIEditorDraw.cpp for the full explanation.
///
/// hoveredUIEntityID is owned entirely by this file: HandleUIEditorInput()
/// recomputes it unconditionally on every call, so nothing elsewhere should
/// also write to it.
namespace {
    using namespace MapEditorInternal;

    constexpr float UI_INPUT_PI = 3.14159265358979323846f;
    constexpr float UI_HANDLE_HIT_RADIUS = 8.0f;
    constexpr float UI_ROTATE_HANDLE_OFFSET = 24.0f; // screen pixels; must match DrawUIEntityGizmos() in UIEditorDraw.cpp

    enum class UIDragMode { None, Move, Rotate, Scale };

    UIDragMode uiDragMode = UIDragMode::None;
    Vector2 uiDragStartMouseCanvas{};
    Vector2 uiDragStartPosition{};
    float uiDragStartRotation = 0.0f;
    float uiDragStartAngleToMouse = 0.0f;

    // Mirrors UpdateEditorZoom() exactly: same keys/wheel, same
    // zoom-around-cursor compensation, just against uiCanvasPan/uiCanvasZoom
    // and ScreenToUICanvas() instead of cameraPos/editorZoom and ScreenToWorld().
    void UpdateUICanvasZoom() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 canvasBeforeZoom = ScreenToUICanvas(mouseScreen);

        bool zoomChanged = false;

        if (InputManager::GetKeyDown(SDL_SCANCODE_EQUALS) ||
            InputManager::GetKeyDown(SDL_SCANCODE_KP_PLUS) || InputManager::GetMouseWheelScrollUp()) {
            uiCanvasZoom *= 1.15f;
            zoomChanged = true;
        }

        if (InputManager::GetKeyDown(SDL_SCANCODE_MINUS) ||
            InputManager::GetKeyDown(SDL_SCANCODE_KP_MINUS) || InputManager::GetMouseWheelScrollDown()) {
            uiCanvasZoom /= 1.15f;
            zoomChanged = true;
        }

        uiCanvasZoom = std::clamp(uiCanvasZoom, MIN_UI_CANVAS_ZOOM, MAX_UI_CANVAS_ZOOM);

        if (zoomChanged) {
            const Vector2 canvasAfterZoom = ScreenToUICanvas(mouseScreen);
            uiCanvasPan.x += canvasBeforeZoom.x - canvasAfterZoom.x;
            uiCanvasPan.y += canvasBeforeZoom.y - canvasAfterZoom.y;
        }
    }

    bool NearScreenPoint(const Vector2& a, const Vector2& b, const float radius) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return (dx * dx + dy * dy) <= radius * radius;
    }

    Vector2 UITransformCenterCanvas(const ComponentUITransform& transform) {
        return {
            transform.resolvedPosition.x + transform.resolvedSize.x * 0.5f,
            transform.resolvedPosition.y + transform.resolvedSize.y * 0.5f
        };
    }

    // Rotated-rect corners in CANVAS space, TL/TR/BR/BL. Mirrors
    // ComputeUIEntityScreenQuad()'s corner math in UIEditorDraw.cpp exactly
    // (kept in canvas space here rather than converting to screen inline, so
    // callers can convert only the points they actually need). Duplicated
    // rather than shared via the header because EditorInternal.hpp doesn't
    // know about ComponentUITransform's layout - if you hoist UI-transform-
    // aware helpers into a shared location later, this is the first
    // candidate to de-duplicate.
    void ComputeUIEntityCanvasCorners(const ComponentUITransform& transform, Vector2 outCorners[4]) {
        const Vector2 size = transform.resolvedSize;
        const Vector2 center = UITransformCenterCanvas(transform);

        const float rad = transform.rotation * (UI_INPUT_PI / 180.0f);
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);

        const Vector2 localCorners[4] = {
            {-size.x * 0.5f, -size.y * 0.5f},
            {size.x * 0.5f, -size.y * 0.5f},
            {size.x * 0.5f, size.y * 0.5f},
            {-size.x * 0.5f, size.y * 0.5f},
        };

        for (int i = 0; i < 4; ++i) {
            const Vector2 rotated = {
                localCorners[i].x * cosA - localCorners[i].y * sinA,
                localCorners[i].x * sinA + localCorners[i].y * cosA
            };
            outCorners[i] = {center.x + rotated.x, center.y + rotated.y};
        }
    }

    // Point-in-rotated-rect test: inverse-rotate the point into the
    // transform's own local space around its CENTRE (matching UI_vs.glsl -
    // see the file comment in UIEditorDraw.cpp), then test against the
    // (unrotated, by construction) centred local rect. Correct for any
    // rotation value, unlike a plain axis-aligned box test would be.
    bool CanvasPointInsideTransform(const Vector2& canvasPoint, const ComponentUITransform& transform) {
        const Vector2 size = transform.resolvedSize;
        const Vector2 center = UITransformCenterCanvas(transform);
        const Vector2 offset = {canvasPoint.x - center.x, canvasPoint.y - center.y};

        const float rad = -transform.rotation * (UI_INPUT_PI / 180.0f); // inverse rotation
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);
        const Vector2 local = {
            offset.x * cosA - offset.y * sinA,
            offset.x * sinA + offset.y * cosA
        };

        return local.x >= -size.x * 0.5f && local.x <= size.x * 0.5f && local.y >= -size.y * 0.5f && local.y <= size.y * 0.5f;
    }

    ID UIEntityAtCanvasPoint(Level& level, const Vector2& canvasPoint) {
        // Back-to-front so the last-drawn (topmost) entity wins, mirroring
        // DrawExistingSectors()'s hover scan in MapEditorDrawing.cpp.
        for (int i = static_cast<int>(level.entities.size()) - 1; i >= 0; --i) {
            Entity& entity = level.entities[i];
            auto* transform = entity.GetComponent<ComponentUITransform>();
            if (transform != nullptr && CanvasPointInsideTransform(canvasPoint, *transform)) return entity.id;
        }
        return INVALID_ID;
    }

    // Screen-space rotate/scale handle positions for a transform, matching
    // DrawUIEntityGizmos()'s placement in UIEditorDraw.cpp exactly, so
    // what's clickable matches what's drawn even when the element is
    // rotated.
    void GetUIHandleScreenPositions(const ComponentUITransform& transform, Vector2& outRotateHandle,
                                     Vector2& outScaleHandle) {
        Vector2 canvasCorners[4];
        ComputeUIEntityCanvasCorners(transform, canvasCorners);

        Vector2 screenCorners[4];
        for (int i = 0; i < 4; ++i) screenCorners[i] = UICanvasToScreen(canvasCorners[i]);
        const Vector2 centerScreen = UICanvasToScreen(UITransformCenterCanvas(transform));

        const Vector2 topCenterScreen = {
            (screenCorners[0].x + screenCorners[1].x) * 0.5f,
            (screenCorners[0].y + screenCorners[1].y) * 0.5f
        };

        Vector2 outward = {topCenterScreen.x - centerScreen.x, topCenterScreen.y - centerScreen.y};
        const float outwardLen = std::sqrt(outward.x * outward.x + outward.y * outward.y);
        if (outwardLen > 0.0001f) {
            outward.x /= outwardLen;
            outward.y /= outwardLen;
        }
        else outward = {0.0f, -1.0f};

        outRotateHandle = {
            topCenterScreen.x + outward.x * UI_ROTATE_HANDLE_OFFSET,
            topCenterScreen.y + outward.y * UI_ROTATE_HANDLE_OFFSET
        };
        outScaleHandle = screenCorners[2];
    }

    // Applies the active drag mode for the current frame. Does not itself
    // check whether the left button is still held or clear uiDragMode on
    // release - the caller (HandleUIEditorInput) does that, mirroring how
    // MapEditorInput.cpp separates holdingEntity's continuation from its
    // own release check.
    void ContinueUIDrag(Level& level, const Vector2& mouseCanvas) {
        Entity* entity = level.GetEntity(selectedUIEntityID);
        ComponentUITransform* transform = entity != nullptr ? entity->GetComponent<ComponentUITransform>() : nullptr;
        if (transform == nullptr) {
            uiDragMode = UIDragMode::None;
            return;
        }

        switch (uiDragMode) {
            case UIDragMode::Move: {
                const Vector2 delta = {
                    mouseCanvas.x - uiDragStartMouseCanvas.x,
                    mouseCanvas.y - uiDragStartMouseCanvas.y
                };
                transform->position = {uiDragStartPosition.x + delta.x, uiDragStartPosition.y + delta.y};
                break;
            }
            case UIDragMode::Scale: {
                // Solves for the scale that puts the bottom-right corner
                // under the mouse, using resolvedPosition (the fixed
                // top-left reference - unlike the rect's centre, it doesn't
                // move as scale changes) and inverse-rotating the mouse
                // around it. The real render rotation pivot is the rect's
                // centre, which itself shifts with scale - using
                // resolvedPosition here instead is an approximation that's
                // exact at rotation=0 and close enough for typical drag
                // ranges otherwise, rather than solving that circular
                // (centre depends on scale, scale depends on centre)
                // relationship exactly.
                const Vector2 offset = {
                    mouseCanvas.x - transform->resolvedPosition.x,
                    mouseCanvas.y - transform->resolvedPosition.y
                };
                const float rad = -transform->rotation * (UI_INPUT_PI / 180.0f);
                const float cosA = std::cos(rad);
                const float sinA = std::sin(rad);
                const Vector2 localMouse = {
                    offset.x * cosA - offset.y * sinA,
                    offset.x * sinA + offset.y * cosA
                };
                // On a non-stretched axis UISystem treats scale as the final
                // pixel size. Dividing by resolvedSize produced a ratio and
                // then resolved that ratio as a size on the following frame.
                if (transform->anchorMin.x == transform->anchorMax.x) transform->scale.x = localMouse.x;
                if (transform->anchorMin.y == transform->anchorMax.y) transform->scale.y = localMouse.y;
                break;
            }
            case UIDragMode::Rotate: {
                const Vector2 center = UITransformCenterCanvas(*transform);
                const Vector2 toMouse = {mouseCanvas.x - center.x, mouseCanvas.y - center.y};
                const float angleToMouse = std::atan2(toMouse.y, toMouse.x) * (180.0f / UI_INPUT_PI);
                transform->rotation = uiDragStartRotation + (angleToMouse - uiDragStartAngleToMouse);
                break;
            }
            case UIDragMode::None:
            default: break;
        }
    }

    void BeginUIDragOrSelect(Level& level, const Vector2& mouseScreen, const Vector2& mouseCanvas,
                              ComponentUITransform* selectedTransform) {
        if (selectedTransform != nullptr) {
            Vector2 rotateHandle{}, scaleHandle{};
            GetUIHandleScreenPositions(*selectedTransform, rotateHandle, scaleHandle);

            if (NearScreenPoint(mouseScreen, rotateHandle, UI_HANDLE_HIT_RADIUS)) {
                uiDragMode = UIDragMode::Rotate;
                uiDragStartRotation = selectedTransform->rotation;
                const Vector2 center = UITransformCenterCanvas(*selectedTransform);
                const Vector2 toMouse = {mouseCanvas.x - center.x, mouseCanvas.y - center.y};
                uiDragStartAngleToMouse = std::atan2(toMouse.y, toMouse.x) * (180.0f / UI_INPUT_PI);
                return;
            }

            if (NearScreenPoint(mouseScreen, scaleHandle, UI_HANDLE_HIT_RADIUS)) {
                uiDragMode = UIDragMode::Scale;
                return;
            }

            if (CanvasPointInsideTransform(mouseCanvas, *selectedTransform)) {
                uiDragMode = UIDragMode::Move;
                uiDragStartMouseCanvas = mouseCanvas;
                uiDragStartPosition = selectedTransform->position;
                return;
            }
        }

        // Not grabbing a handle or the current selection's body -> plain
        // click-to-select against whatever's under the cursor (or clear).
        selectedUIEntityID = UIEntityAtCanvasPoint(level, mouseCanvas);
    }
}

namespace MapEditorInternal {
    void HandleUIEditorInput(const bool mouseBlockedByImGui, const bool /*keyboardBlockedByImgui*/) {
        Level& level = LevelManager::CurrentLevel();

        // Hit testing and handle placement must use the same current layout
        // values as both the editor drawing pass and the runtime renderer.
        UISystem::UpdateAllTransforms(
            level,
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight)
        );

        // Defensively drop a selection that no longer resolves (e.g. deleted
        // from outside the normal path) - mirrors ValidateSelections() in
        // MapEditorInput.cpp for the Map Editor's own selections.
        if (selectedUIEntityID != INVALID_ID && level.GetEntity(selectedUIEntityID) == nullptr)
            selectedUIEntityID = INVALID_ID;

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseCanvas = ScreenToUICanvas(mouseScreen);

        hoveredUIEntityID = UIEntityAtCanvasPoint(level, mouseCanvas);

        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();
                uiCanvasPan.x -= mouseDelta.x / uiCanvasZoom;
                uiCanvasPan.y -= mouseDelta.y / uiCanvasZoom;
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                Entity* uiSelectedEntity = selectedUIEntityID != INVALID_ID ? level.GetEntity(selectedUIEntityID) : nullptr;
                ComponentUITransform* uiSelectedTransform = uiSelectedEntity != nullptr
                    ? uiSelectedEntity->GetComponent<ComponentUITransform>() : nullptr;

                BeginUIDragOrSelect(level, mouseScreen, mouseCanvas, uiSelectedTransform);
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && uiDragMode != UIDragMode::None)
                ContinueUIDrag(level, mouseCanvas);
            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) uiDragMode = UIDragMode::None;

            UpdateUICanvasZoom();
        }
    }
} // namespace MapEditorInternal