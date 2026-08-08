//
// Created by berke on 5/16/2026.
//
#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Editor/EditorInternal.hpp"
#include "Headers/Objects/Entity.hpp"
// ASSUMPTION: same as UIEditorUI.cpp/UIEditorDraw.cpp - adjust to wherever
// ComponentUITransform actually lives in your project.
#include "Headers/Objects/Components.hpp"

#include <algorithm>
#include <cmath>

/// This script is responsible for handling keyboard/mouse input inside the UI Editor.
///
/// Structure mirrors HandleEditorInput()/UpdateEditorZoom() in
/// MapEditorInput.cpp directly: mouse-wheel/keyboard zoom around the cursor,
/// middle-mouse pan, all gated behind `!mouseBlockedByImGui`, since the
/// canvas is now a full-screen view (see UIEditorDraw.cpp) rather than a
/// bounded panel - there's no "is the mouse over some rect" check needed
/// any more than there is for the Map Editor's own equivalent.
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

    // Rotated-rect corners in CANVAS space, TL/TR/BR/BL. Mirrors
    // ComputeUIEntityScreenQuad()'s corner math in UIEditorDraw.cpp exactly
    // (kept in canvas space here rather than converting to screen inline, so
    // callers can convert only the points they actually need). Duplicated
    // rather than shared via the header because EditorInternal.hpp doesn't
    // know about ComponentUITransform's layout - if you hoist UI-transform-
    // aware helpers into a shared location later, this is the first
    // candidate to de-duplicate.
    void ComputeUIEntityCanvasCorners(const ComponentUITransform& transform, Vector2 outCorners[4]) {
        const Vector2 size = {
            transform.resolvedSize.x * transform.scale.x,
            transform.resolvedSize.y * transform.scale.y
        };

        const float rad = transform.rotation * (UI_INPUT_PI / 180.0f);
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);

        const Vector2 localCorners[4] = {
            {-transform.pivot.x * size.x, -transform.pivot.y * size.y},
            {(1.0f - transform.pivot.x) * size.x, -transform.pivot.y * size.y},
            {(1.0f - transform.pivot.x) * size.x, (1.0f - transform.pivot.y) * size.y},
            {-transform.pivot.x * size.x, (1.0f - transform.pivot.y) * size.y},
        };

        for (int i = 0; i < 4; ++i) {
            const Vector2 rotated = {
                localCorners[i].x * cosA - localCorners[i].y * sinA,
                localCorners[i].x * sinA + localCorners[i].y * cosA
            };
            outCorners[i] = {transform.resolvedPosition.x + rotated.x, transform.resolvedPosition.y + rotated.y};
        }
    }

    // Point-in-rotated-rect test: inverse-rotate the point into the
    // transform's own local space around resolvedPosition, then test against
    // the (unrotated, by construction) local rect. Correct for any rotation
    // value, unlike a plain axis-aligned box test against resolvedPosition/
    // resolvedSize would be.
    bool CanvasPointInsideTransform(const Vector2& canvasPoint, const ComponentUITransform& transform) {
        const Vector2 size = {
            transform.resolvedSize.x * transform.scale.x,
            transform.resolvedSize.y * transform.scale.y
        };
        const Vector2 offset = {
            canvasPoint.x - transform.resolvedPosition.x,
            canvasPoint.y - transform.resolvedPosition.y
        };

        const float rad = -transform.rotation * (UI_INPUT_PI / 180.0f); // inverse rotation
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);
        const Vector2 local = {
            offset.x * cosA - offset.y * sinA,
            offset.x * sinA + offset.y * cosA
        };

        return local.x >= -transform.pivot.x * size.x && local.x <= (1.0f - transform.pivot.x) * size.x &&
               local.y >= -transform.pivot.y * size.y && local.y <= (1.0f - transform.pivot.y) * size.y;
    }

    ID UIEntityAtCanvasPoint(Level& level, const Vector2& canvasPoint) {
        // Back-to-front so the last-drawn (topmost) entity wins, mirroring
        // DrawExistingSectors()'s hover scan in MapEditorDrawing.cpp.
        for (int i = static_cast<int>(level.entities.size()) - 1; i >= 0; --i) {
            Entity& entity = level.entities[i];
            ComponentUITransform* transform = entity.GetComponent<ComponentUITransform>();
            if (transform != nullptr && CanvasPointInsideTransform(canvasPoint, *transform))
                return entity.id;
        }
        return INVALID_ID;
    }

    // Screen-space rotate/scale handle positions for a transform, matching
    // DrawUIEntityGizmos()'s placement in UIEditorDraw.cpp exactly (same
    // corner math, same top-centre-outward / bottom-right-corner rule, same
    // fixed screen-pixel offset) so what's clickable matches what's drawn
    // even when the element is rotated.
    void GetUIHandleScreenPositions(const ComponentUITransform& transform, Vector2& outRotateHandle,
                                     Vector2& outScaleHandle) {
        Vector2 canvasCorners[4];
        ComputeUIEntityCanvasCorners(transform, canvasCorners);

        Vector2 screenCorners[4];
        for (int i = 0; i < 4; ++i) screenCorners[i] = UICanvasToScreen(canvasCorners[i]);
        const Vector2 pivotScreen = UICanvasToScreen(transform.resolvedPosition);

        const Vector2 topCenterScreen = {
            (screenCorners[0].x + screenCorners[1].x) * 0.5f,
            (screenCorners[0].y + screenCorners[1].y) * 0.5f
        };

        Vector2 outward = {topCenterScreen.x - pivotScreen.x, topCenterScreen.y - pivotScreen.y};
        const float outwardLen = std::sqrt(outward.x * outward.x + outward.y * outward.y);
        if (outwardLen > 0.0001f) {
            outward.x /= outwardLen;
            outward.y /= outwardLen;
        } else {
            outward = {0.0f, -1.0f};
        }

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
                // Anchored at the pivot: solve for the scale that makes the
                // (unrotated-local) bottom-right corner land under the
                // mouse, expressed in the transform's own rotated frame so a
                // rotated element still scales along its own axes rather
                // than the canvas's.
                const Vector2 extent = {
                    (1.0f - transform->pivot.x) * transform->resolvedSize.x,
                    (1.0f - transform->pivot.y) * transform->resolvedSize.y
                };
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
                if (std::abs(extent.x) > 0.01f) transform->scale.x = localMouse.x / extent.x;
                if (std::abs(extent.y) > 0.01f) transform->scale.y = localMouse.y / extent.y;
                break;
            }
            case UIDragMode::Rotate: {
                const Vector2 toMouse = {
                    mouseCanvas.x - transform->resolvedPosition.x,
                    mouseCanvas.y - transform->resolvedPosition.y
                };
                const float angleToMouse = std::atan2(toMouse.y, toMouse.x) * (180.0f / UI_INPUT_PI);
                transform->rotation = uiDragStartRotation + (angleToMouse - uiDragStartAngleToMouse);
                break;
            }
            case UIDragMode::None:
            default:
                break;
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
                const Vector2 toMouse = {
                    mouseCanvas.x - selectedTransform->resolvedPosition.x,
                    mouseCanvas.y - selectedTransform->resolvedPosition.y
                };
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
                Entity* uiSelectedEntity = selectedUIEntityID != INVALID_ID
                    ? level.GetEntity(selectedUIEntityID) : nullptr;
                ComponentUITransform* uiSelectedTransform = uiSelectedEntity != nullptr
                    ? uiSelectedEntity->GetComponent<ComponentUITransform>() : nullptr;

                BeginUIDragOrSelect(level, mouseScreen, mouseCanvas, uiSelectedTransform);
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && uiDragMode != UIDragMode::None)
                ContinueUIDrag(level, mouseCanvas);

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT))
                uiDragMode = UIDragMode::None;

            UpdateUICanvasZoom();
        }
    }
} // namespace MapEditorInternal