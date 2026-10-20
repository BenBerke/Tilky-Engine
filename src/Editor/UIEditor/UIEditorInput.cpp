//
// Created by berke on 5/16/2026.
//
#include "imgui.h"

#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/UISystem.hpp"
#include "src/Editor/EditorInternal.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Objects/Components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

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

    // All in screen pixels, so the gizmo is the same size at every zoom.
    // Drawn by DrawUIEntityGizmo() in UIEditorDraw.cpp from GetUIGizmoLayout().
    constexpr float UI_HANDLE_HIT_RADIUS = 7.0f;
    constexpr float UI_ROTATE_HANDLE_OFFSET = 24.0f;
    constexpr float UI_GIZMO_ARROW_LENGTH = 56.0f;
    constexpr float UI_GIZMO_SQUARE_HALF = 7.0f;
    constexpr float UI_GIZMO_ARROW_PICK = 6.0f;
    constexpr float UI_MIN_EDGE_HANDLE_LENGTH = 30.0f; // shorter edges show only their corner handles
    constexpr float UI_SMALL_ELEMENT_SIZE = 24.0f;     // below this the move square wins over the resize handles
    constexpr float UI_MIN_PICK_SIZE = 10.0f;          // tiny elements are still this easy to click
    constexpr float UI_SNAP_DISTANCE = 8.0f;
    constexpr float UI_CLICK_DRAG_THRESHOLD = 3.0f;    // a press that moves less than this is a click

    constexpr float UI_MIN_RESIZE_SIZE = 1.0f; // canvas pixels
    constexpr float UI_ROTATE_SNAP_STEP = 15.0f; // degrees
    constexpr float UI_NUDGE = 1.0f;
    constexpr float UI_BIG_NUDGE = 10.0f;
    constexpr float UI_DUPLICATE_OFFSET = 16.0f;
    constexpr float UI_FRAME_FILL = 0.6f; // how much of the window F frames the selection to

    constexpr Uint64 NUDGE_REPEAT_DELAY_MS = 400;
    constexpr Uint64 NUDGE_REPEAT_INTERVAL_MS = 40;
    constexpr Uint64 NUDGE_UNDO_MERGE_MS = 800; // nudges this close together are one undo step

    struct CanvasRect {
        float minX, minY, maxX, maxY;
    };

    enum class UIDragMode { None, PendingClick, Move, Rotate, Resize, BoxSelect };

    struct UIStartPose {
        ID id;
        Vector2 position;
        Vector2 scale;
        float rotation;
    };

    UIDragMode uiDragMode = UIDragMode::None;
    UIGizmoPart uiDragPart = UI_GIZMO_NONE;
    Vector2 uiDragStartMouseScreen{};
    Vector2 uiDragStartMouseCanvas{};
    std::vector<UIStartPose> uiDragStartPoses;
    CanvasRect uiDragStartBounds{};
    EntitiesSnapshot uiDragUndoBefore;

    // Resize: the point of the rect that stays put (the handle's opposite),
    // in canvas space, and the size the drag started from.
    Vector2 uiResizeFixedPoint{};
    Vector2 uiResizeStartSize{};
    int uiResizeHandle = 0;

    float uiRotateStartAngleToMouse = 0.0f;

    // A press on an element: what a click (no drag) does on release.
    ID uiPendingClickEntity = INVALID_ID;
    bool uiPendingClickCycles = false;

    // Clicking the same spot again selects the next element under it.
    Vector2 uiLastClickScreen{-1000.0f, -1000.0f};
    std::vector<ID> uiLastClickStack;

    // A right click this frame, for the context menu.
    bool uiContextMenuRequested = false;
    bool uiContextMenuOnElement = false;
    Vector2 uiContextMenuCanvasPoint{};

    std::vector<UISnapGuide> uiSnapGuides;
    std::string uiDragReadout;
    bool uiMouseBlockedLastFrame = false;

    Uint64 uiNudgeHeldSince[4] = {};
    Uint64 uiNudgeLastRepeat[4] = {};
    Uint64 uiLastNudgeTicks = 0;
    std::size_t uiLastNudgeUndoSize = 0;

    Vector2 ScreenSize() {
        return {static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    }

    bool NearScreenPoint(const Vector2& a, const Vector2& b, const float radius) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return (dx * dx + dy * dy) <= radius * radius;
    }

    float DistanceToSegment(const Vector2& point, const Vector2& a, const Vector2& b) {
        const float abX = b.x - a.x;
        const float abY = b.y - a.y;
        const float lengthSq = abX * abX + abY * abY;

        float t = lengthSq > 0.0f ? ((point.x - a.x) * abX + (point.y - a.y) * abY) / lengthSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        const float dx = point.x - (a.x + abX * t);
        const float dy = point.y - (a.y + abY * t);
        return std::sqrt(dx * dx + dy * dy);
    }

    Vector2 Rotate(const Vector2& v, const float degrees) {
        const float rad = degrees * (UI_INPUT_PI / 180.0f);
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);
        return {v.x * cosA - v.y * sinA, v.x * sinA + v.y * cosA};
    }

    ComponentUITransform* GetUITransform(Level& level, const ID id) {
        Entity* entity = level.GetEntity(id);
        return entity != nullptr ? entity->GetComponent<ComponentUITransform>() : nullptr;
    }

    Vector2 UITransformCenterCanvas(const ComponentUITransform& transform) {
        return {
            transform.resolvedPosition.x + transform.resolvedSize.x * 0.5f,
            transform.resolvedPosition.y + transform.resolvedSize.y * 0.5f
        };
    }

    // Rotated-rect corners in CANVAS space, TL/TR/BR/BL. Mirrors
    // ComputeUIEntityScreenQuad()'s corner math in UIEditorDraw.cpp exactly.
    void ComputeUIEntityCanvasCorners(const ComponentUITransform& transform, Vector2 outCorners[4]) {
        const Vector2 size = transform.resolvedSize;
        const Vector2 center = UITransformCenterCanvas(transform);

        const Vector2 localCorners[4] = {
            {-size.x * 0.5f, -size.y * 0.5f},
            {size.x * 0.5f, -size.y * 0.5f},
            {size.x * 0.5f, size.y * 0.5f},
            {-size.x * 0.5f, size.y * 0.5f},
        };

        for (int i = 0; i < 4; ++i) outCorners[i] = center + Rotate(localCorners[i], transform.rotation);
    }

    // Axis-aligned bounds of the rotated rect.
    CanvasRect TransformBounds(const ComponentUITransform& transform) {
        Vector2 corners[4];
        ComputeUIEntityCanvasCorners(transform, corners);

        CanvasRect bounds{corners[0].x, corners[0].y, corners[0].x, corners[0].y};
        for (const Vector2& corner : corners) {
            bounds.minX = std::min(bounds.minX, corner.x);
            bounds.minY = std::min(bounds.minY, corner.y);
            bounds.maxX = std::max(bounds.maxX, corner.x);
            bounds.maxY = std::max(bounds.maxY, corner.y);
        }
        return bounds;
    }

    // Bounds of every selected element together; false when nothing is selected.
    bool SelectionBounds(Level& level, CanvasRect& outBounds) {
        bool any = false;
        for (const ID id : selectedUIEntities) {
            const ComponentUITransform* transform = GetUITransform(level, id);
            if (transform == nullptr) continue;

            const CanvasRect bounds = TransformBounds(*transform);
            if (!any) outBounds = bounds;
            else {
                outBounds.minX = std::min(outBounds.minX, bounds.minX);
                outBounds.minY = std::min(outBounds.minY, bounds.minY);
                outBounds.maxX = std::max(outBounds.maxX, bounds.maxX);
                outBounds.maxY = std::max(outBounds.maxY, bounds.maxY);
            }
            any = true;
        }
        return any;
    }

    // Point-in-rotated-rect test: inverse-rotate the point into the
    // transform's own local space around its CENTRE (matching UI_vs.glsl),
    // then test against the centred local rect. Each half-extent is at
    // least half of UI_MIN_PICK_SIZE screen pixels, so tiny elements stay
    // clickable.
    bool CanvasPointInsideTransform(const Vector2& canvasPoint, const ComponentUITransform& transform) {
        const Vector2 size = transform.resolvedSize;
        const Vector2 local = Rotate(canvasPoint - UITransformCenterCanvas(transform), -transform.rotation);

        const float minHalf = UI_MIN_PICK_SIZE * 0.5f / uiCanvasZoom;
        const float halfX = std::max(std::fabs(size.x) * 0.5f, minHalf);
        const float halfY = std::max(std::fabs(size.y) * 0.5f, minHalf);

        return local.x >= -halfX && local.x <= halfX && local.y >= -halfY && local.y <= halfY;
    }

    // Every UI entity, topmost first in the order the canvas draws them:
    // Text over Sprites, and within each, later components over earlier
    // ones. Entities with neither are at the bottom.
    std::vector<ID> UIEntitiesTopmostFirst(Level& level) {
        std::unordered_map<ID, std::size_t> drawRank;

        const std::size_t spriteCount = level.ui_sprites.components.size();
        for (std::size_t i = 0; i < spriteCount; ++i) {
            std::size_t& rank = drawRank[level.ui_sprites.components[i].ownerID];
            rank = std::max(rank, i + 1);
        }
        for (std::size_t i = 0; i < level.ui_texts.components.size(); ++i) {
            std::size_t& rank = drawRank[level.ui_texts.components[i].ownerID];
            rank = std::max(rank, spriteCount + i + 1);
        }

        std::vector<ID> order;
        for (auto it = level.entities.rbegin(); it != level.entities.rend(); ++it)
            if (it->HasComponent<ComponentUITransform>()) order.push_back(it->id);

        std::ranges::stable_sort(order, [&drawRank](const ID a, const ID b) {
            const auto rankA = drawRank.find(a);
            const auto rankB = drawRank.find(b);
            return (rankA != drawRank.end() ? rankA->second : 0) > (rankB != drawRank.end() ? rankB->second : 0);
        });
        return order;
    }

    // Every UI entity under `canvasPoint`, topmost first.
    std::vector<ID> UIEntitiesAtCanvasPoint(Level& level, const Vector2& canvasPoint) {
        std::vector<ID> hits;
        for (const ID id : UIEntitiesTopmostFirst(level)) {
            const ComponentUITransform* transform = GetUITransform(level, id);
            if (transform != nullptr && CanvasPointInsideTransform(canvasPoint, *transform)) hits.push_back(id);
        }
        return hits;
    }

    // The `position` that puts the rect's top-left corner at `topLeft` with
    // `size` - UISystem::UpdateTransform() solved the other way round.
    Vector2 PositionForTopLeft(const ComponentUITransform& transform, const Vector2& topLeft, const Vector2& size) {
        const Vector2 screen = ScreenSize();
        const Vector2 anchorMinPx = {screen.x * transform.anchorMin.x, screen.y * transform.anchorMin.y};
        const Vector2 anchorMaxPx = {screen.x * transform.anchorMax.x, screen.y * transform.anchorMax.y};

        const bool stretchX = transform.anchorMin.x != transform.anchorMax.x;
        const bool stretchY = transform.anchorMin.y != transform.anchorMax.y;

        const Vector2 anchorPoint = {
            stretchX ? anchorMinPx.x + (anchorMaxPx.x - anchorMinPx.x) * transform.pivot.x : anchorMinPx.x,
            stretchY ? anchorMinPx.y + (anchorMaxPx.y - anchorMinPx.y) * transform.pivot.y : anchorMinPx.y
        };

        return {
            topLeft.x - anchorPoint.x + size.x * transform.pivot.x,
            topLeft.y - anchorPoint.y + size.y * transform.pivot.y
        };
    }

    bool IsStretchedX(const ComponentUITransform& transform) { return transform.anchorMin.x != transform.anchorMax.x; }
    bool IsStretchedY(const ComponentUITransform& transform) { return transform.anchorMin.y != transform.anchorMax.y; }

    // ---------------------------------------------------------------------
    // Snapping
    // ---------------------------------------------------------------------

    bool SnappingActive() {
        return uiSnapEnabled && !GetKeyBind(KEYBIND_UI_DISABLE_SNAP);
    }

    // The canvas lines an edge or centre can snap to on one axis
    // (vertical = lines of constant x).
    std::vector<float> SnapTargets(Level& level, const bool vertical) {
        const float extent = vertical ? ScreenSize().x : ScreenSize().y;

        std::vector<float> targets = {0.0f, extent * 0.5f, extent};

        if (showUISafeArea) {
            targets.push_back(extent * UI_SAFE_AREA_FRACTION);
            targets.push_back(extent * (1.0f - UI_SAFE_AREA_FRACTION));
        }

        if (uiSnapToElements) {
            for (const ComponentUITransform& transform : level.ui_transforms.components) {
                if (IsUIEntitySelected(transform.ownerID)) continue;

                const CanvasRect bounds = TransformBounds(transform);
                const float min = vertical ? bounds.minX : bounds.minY;
                const float max = vertical ? bounds.maxX : bounds.maxY;
                targets.push_back(min);
                targets.push_back((min + max) * 0.5f);
                targets.push_back(max);
            }
        }

        return targets;
    }

    // The smallest shift (within snapping distance) that puts one of
    // `values` on a target or, with the grid shown, a grid line. Adds a
    // guide for a target snap; the grid draws its own lines.
    float SnapOffset(Level& level, const std::vector<float>& values, const bool vertical) {
        const float threshold = UI_SNAP_DISTANCE / uiCanvasZoom;

        float best = 0.0f;
        float bestDistance = threshold;
        bool found = false;
        bool bestIsGuide = false;
        float bestGuide = 0.0f;

        const auto consider = [&](const float value, const float target, const bool guide) {
            const float distance = std::fabs(target - value);
            if (distance > bestDistance) return;
            // A guide line wins a tie with the grid, so it's shown.
            if (found && distance == bestDistance && !guide) return;

            best = target - value;
            bestDistance = distance;
            found = true;
            bestIsGuide = guide;
            bestGuide = target;
        };

        for (const float target : SnapTargets(level, vertical))
            for (const float value : values) consider(value, target, true);

        if (showUIGrid)
            for (const float value : values)
                consider(value, std::round(value / UI_CANVAS_GRID_STEP) * UI_CANVAS_GRID_STEP, false);

        if (!found) return 0.0f;

        if (bestIsGuide) uiSnapGuides.push_back({vertical, bestGuide});
        return best;
    }

    // ---------------------------------------------------------------------
    // Gizmo
    // ---------------------------------------------------------------------

    bool IsSmallOnScreen(const ComponentUITransform& transform) {
        return std::min(std::fabs(transform.resolvedSize.x), std::fabs(transform.resolvedSize.y)) * uiCanvasZoom < UI_SMALL_ELEMENT_SIZE;
    }

    UIGizmoPart PickUIGizmoPart(const Vector2& mouseScreen) {
        UIGizmoLayout layout;
        if (!GetUIGizmoLayout(&layout)) return UI_GIZMO_NONE;

        const bool onSquare = std::fabs(mouseScreen.x - layout.center.x) <= UI_GIZMO_SQUARE_HALF + 1.0f &&
                              std::fabs(mouseScreen.y - layout.center.y) <= UI_GIZMO_SQUARE_HALF + 1.0f;

        if (NearScreenPoint(mouseScreen, layout.rotateHandle, UI_HANDLE_HIT_RADIUS)) return UI_GIZMO_ROTATE;
        if (layout.moveSquareOnTop && onSquare) return UI_GIZMO_MOVE_FREE;

        for (int i = 0; i < 8; ++i)
            if (layout.resizeShown[i] && NearScreenPoint(mouseScreen, layout.resizeHandles[i], UI_HANDLE_HIT_RADIUS))
                return static_cast<UIGizmoPart>(UI_GIZMO_RESIZE_FIRST + i);

        if (onSquare) return UI_GIZMO_MOVE_FREE;

        const Vector2 xStart = {layout.center.x + UI_GIZMO_SQUARE_HALF, layout.center.y};
        const Vector2 yStart = {layout.center.x, layout.center.y + UI_GIZMO_SQUARE_HALF};
        if (DistanceToSegment(mouseScreen, xStart, layout.xTip) <= UI_GIZMO_ARROW_PICK) return UI_GIZMO_MOVE_X;
        if (DistanceToSegment(mouseScreen, yStart, layout.yTip) <= UI_GIZMO_ARROW_PICK) return UI_GIZMO_MOVE_Y;

        return UI_GIZMO_NONE;
    }

    // A resize cursor matching the handle's direction on screen.
    ImGuiMouseCursor ResizeCursorFor(const UIGizmoLayout& layout, const int handle) {
        const Vector2 dir = layout.resizeHandles[handle] - layout.center;
        float angle = std::atan2(dir.y, dir.x) * (180.0f / UI_INPUT_PI);
        if (angle < 0.0f) angle += 180.0f; // the cursor is the same both ways

        if (angle < 22.5f || angle >= 157.5f) return ImGuiMouseCursor_ResizeEW;
        if (angle < 67.5f) return ImGuiMouseCursor_ResizeNWSE;
        if (angle < 112.5f) return ImGuiMouseCursor_ResizeNS;
        return ImGuiMouseCursor_ResizeNESW;
    }

    void UpdateCanvasCursor() {
        const UIGizmoPart part = GetHighlightedUIGizmoPart();

        if (part >= UI_GIZMO_RESIZE_FIRST && part <= UI_GIZMO_RESIZE_LAST) {
            UIGizmoLayout layout;
            if (GetUIGizmoLayout(&layout)) ImGui::SetMouseCursor(ResizeCursorFor(layout, part - UI_GIZMO_RESIZE_FIRST));
        }
        else if (part == UI_GIZMO_MOVE_FREE || uiDragMode == UIDragMode::Move) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        else if (part == UI_GIZMO_MOVE_X) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        else if (part == UI_GIZMO_MOVE_Y) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        else if (part == UI_GIZMO_ROTATE) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // ---------------------------------------------------------------------
    // Drags
    // ---------------------------------------------------------------------

    void CaptureStartPoses(Level& level) {
        uiDragStartPoses.clear();
        for (const ID id : selectedUIEntities)
            if (const ComponentUITransform* transform = GetUITransform(level, id))
                uiDragStartPoses.push_back({id, transform->position, transform->scale, transform->rotation});

        if (!SelectionBounds(level, uiDragStartBounds)) uiDragStartBounds = {};
    }

    void RestoreStartPoses(Level& level) {
        for (const UIStartPose& pose : uiDragStartPoses) {
            if (ComponentUITransform* transform = GetUITransform(level, pose.id)) {
                transform->position = pose.position;
                transform->scale = pose.scale;
                transform->rotation = pose.rotation;
            }
        }
    }

    bool PosesChanged(Level& level) {
        for (const UIStartPose& pose : uiDragStartPoses) {
            const ComponentUITransform* transform = GetUITransform(level, pose.id);
            if (transform == nullptr) continue;

            if (transform->position.x != pose.position.x || transform->position.y != pose.position.y ||
                transform->scale.x != pose.scale.x || transform->scale.y != pose.scale.y ||
                transform->rotation != pose.rotation) return true;
        }
        return false;
    }

    void BeginDrag(Level& level, const UIDragMode mode, const UIGizmoPart part) {
        uiDragMode = mode;
        uiDragPart = part;
        uiDragUndoBefore = CaptureEntitiesSnapshot();
        CaptureStartPoses(level);

        ComponentUITransform* transform = GetUITransform(level, selectedUIEntityID);
        if (transform == nullptr) return;

        if (mode == UIDragMode::Rotate) {
            const Vector2 toMouse = uiDragStartMouseCanvas - UITransformCenterCanvas(*transform);
            uiRotateStartAngleToMouse = std::atan2(toMouse.y, toMouse.x) * (180.0f / UI_INPUT_PI);
        }
        else if (mode == UIDragMode::Resize) {
            uiResizeHandle = part - UI_GIZMO_RESIZE_FIRST;
            uiResizeStartSize = transform->resolvedSize;

            const int hx = UI_RESIZE_HANDLE_DIRECTIONS[uiResizeHandle][0];
            const int hy = UI_RESIZE_HANDLE_DIRECTIONS[uiResizeHandle][1];
            const Vector2 localFixed = {-hx * uiResizeStartSize.x * 0.5f, -hy * uiResizeStartSize.y * 0.5f};
            uiResizeFixedPoint = UITransformCenterCanvas(*transform) + Rotate(localFixed, transform->rotation);
        }
    }

    void EndDrag(Level& level) {
        const UIDragMode mode = uiDragMode;
        uiDragMode = UIDragMode::None;
        uiDragPart = UI_GIZMO_NONE;
        uiSnapGuides.clear();
        uiDragReadout.clear();

        if (mode != UIDragMode::Move && mode != UIDragMode::Resize && mode != UIDragMode::Rotate) return;
        if (!PosesChanged(level)) return;

        const char* label = mode == UIDragMode::Move ? "editor.history.ui_move"
                          : mode == UIDragMode::Resize ? "editor.history.ui_resize" : "editor.history.ui_rotate";
        PushEntitiesUndo(std::move(uiDragUndoBefore), label, static_cast<int>(uiDragStartPoses.size()));
        MarkUIUnsavedChanges();
    }

    void CancelDrag(Level& level) {
        if (uiDragMode == UIDragMode::Move || uiDragMode == UIDragMode::Resize || uiDragMode == UIDragMode::Rotate)
            RestoreStartPoses(level);

        uiDragMode = UIDragMode::None;
        uiDragPart = UI_GIZMO_NONE;
        uiSnapGuides.clear();
        uiDragReadout.clear();
    }

    void ContinueMove(Level& level, const Vector2& mouseCanvas) {
        Vector2 delta = mouseCanvas - uiDragStartMouseCanvas;
        if (uiDragPart == UI_GIZMO_MOVE_X) delta.y = 0.0f;
        if (uiDragPart == UI_GIZMO_MOVE_Y) delta.x = 0.0f;

        if (SnappingActive()) {
            const CanvasRect b = uiDragStartBounds;
            if (uiDragPart != UI_GIZMO_MOVE_Y)
                delta.x += SnapOffset(level, {b.minX + delta.x, (b.minX + b.maxX) * 0.5f + delta.x, b.maxX + delta.x}, true);
            if (uiDragPart != UI_GIZMO_MOVE_X)
                delta.y += SnapOffset(level, {b.minY + delta.y, (b.minY + b.maxY) * 0.5f + delta.y, b.maxY + delta.y}, false);
        }

        for (const UIStartPose& pose : uiDragStartPoses)
            if (ComponentUITransform* transform = GetUITransform(level, pose.id))
                transform->position = pose.position + delta;

        if (const ComponentUITransform* primary = GetUITransform(level, selectedUIEntityID)) {
            char text[64];
            std::snprintf(text, sizeof(text), "%.0f, %.0f", primary->position.x, primary->position.y);
            uiDragReadout = text;
        }
    }

    void ContinueResize(Level& level, const Vector2& mouseCanvas) {
        ComponentUITransform* transform = GetUITransform(level, selectedUIEntityID);
        if (transform == nullptr) return;

        const int hx = UI_RESIZE_HANDLE_DIRECTIONS[uiResizeHandle][0];
        const int hy = UI_RESIZE_HANDLE_DIRECTIONS[uiResizeHandle][1];
        const float rotation = transform->rotation;

        // The mouse in the rect's own axes, measured from the fixed point.
        const Vector2 local = Rotate(mouseCanvas - uiResizeFixedPoint, -rotation);

        Vector2 size = uiResizeStartSize;
        if (hx != 0) size.x = std::max(UI_MIN_RESIZE_SIZE, static_cast<float>(hx) * local.x);
        if (hy != 0) size.y = std::max(UI_MIN_RESIZE_SIZE, static_cast<float>(hy) * local.y);

        const bool keepAspect = GetKeyBind(KEYBIND_UI_KEEP_ASPECT) && uiResizeStartSize.x > 0.0f && uiResizeStartSize.y > 0.0f;
        const float wrapped = std::fmod(std::fabs(rotation), 360.0f);
        const bool axisAligned = wrapped < 0.01f || wrapped > 359.99f;

        if (keepAspect) {
            const float factorX = size.x / uiResizeStartSize.x;
            const float factorY = size.y / uiResizeStartSize.y;
            const float factor = hx == 0 ? factorY : hy == 0 ? factorX : std::max(factorX, factorY);
            size = {uiResizeStartSize.x * factor, uiResizeStartSize.y * factor};
        }
        else if (axisAligned && SnappingActive()) {
            // The moving edges, unrotated so canvas space and local space agree.
            if (hx != 0) {
                const float edge = uiResizeFixedPoint.x + static_cast<float>(hx) * size.x;
                size.x = std::max(UI_MIN_RESIZE_SIZE, size.x + static_cast<float>(hx) * SnapOffset(level, {edge}, true));
            }
            if (hy != 0) {
                const float edge = uiResizeFixedPoint.y + static_cast<float>(hy) * size.y;
                size.y = std::max(UI_MIN_RESIZE_SIZE, size.y + static_cast<float>(hy) * SnapOffset(level, {edge}, false));
            }
        }

        // A stretched axis takes its size from the anchors.
        if (IsStretchedX(*transform)) size.x = uiResizeStartSize.x;
        if (IsStretchedY(*transform)) size.y = uiResizeStartSize.y;

        const Vector2 center = uiResizeFixedPoint + Rotate({hx * size.x * 0.5f, hy * size.y * 0.5f}, rotation);
        const Vector2 topLeft = {center.x - size.x * 0.5f, center.y - size.y * 0.5f};

        if (!IsStretchedX(*transform)) transform->scale.x = size.x;
        if (!IsStretchedY(*transform)) transform->scale.y = size.y;
        transform->position = PositionForTopLeft(*transform, topLeft, size);

        char text[64];
        std::snprintf(text, sizeof(text), "%.0f x %.0f", size.x, size.y);
        uiDragReadout = text;
    }

    void ContinueRotate(Level& level, const Vector2& mouseCanvas) {
        ComponentUITransform* transform = GetUITransform(level, selectedUIEntityID);
        if (transform == nullptr || uiDragStartPoses.empty()) return;

        float startRotation = transform->rotation;
        for (const UIStartPose& pose : uiDragStartPoses)
            if (pose.id == selectedUIEntityID) startRotation = pose.rotation;

        const Vector2 toMouse = mouseCanvas - UITransformCenterCanvas(*transform);
        const float angleToMouse = std::atan2(toMouse.y, toMouse.x) * (180.0f / UI_INPUT_PI);

        float rotation = startRotation + (angleToMouse - uiRotateStartAngleToMouse);
        if (SnappingActive()) rotation = std::round(rotation / UI_ROTATE_SNAP_STEP) * UI_ROTATE_SNAP_STEP;
        transform->rotation = rotation;

        char text[64];
        std::snprintf(text, sizeof(text), "%.1f deg", rotation);
        uiDragReadout = text;
    }

    void FinishBoxSelection(Level& level, const Vector2& mouseCanvas) {
        const CanvasRect box = {
            std::min(uiDragStartMouseCanvas.x, mouseCanvas.x), std::min(uiDragStartMouseCanvas.y, mouseCanvas.y),
            std::max(uiDragStartMouseCanvas.x, mouseCanvas.x), std::max(uiDragStartMouseCanvas.y, mouseCanvas.y)
        };

        if (!GetKeyBind(KEYBIND_UI_ADD_TO_SELECTION)) SelectUIEntity(INVALID_ID);

        // Bottom first, so the topmost one in the box ends up the primary.
        std::vector<ID> order = UIEntitiesTopmostFirst(level);
        std::ranges::reverse(order);

        for (const ID id : order) {
            const ComponentUITransform* transform = GetUITransform(level, id);
            if (transform == nullptr || IsUIEntitySelected(id)) continue;

            const CanvasRect bounds = TransformBounds(*transform);
            if (bounds.maxX < box.minX || bounds.minX > box.maxX || bounds.maxY < box.minY || bounds.minY > box.maxY) continue;

            ToggleUIEntitySelection(id);
        }
    }

    void HandleMousePress(Level& level, const Vector2& mouseScreen, const Vector2& mouseCanvas) {
        uiDragStartMouseScreen = mouseScreen;
        uiDragStartMouseCanvas = mouseCanvas;
        uiPendingClickEntity = INVALID_ID;
        uiPendingClickCycles = false;

        const UIGizmoPart part = PickUIGizmoPart(mouseScreen);
        if (part == UI_GIZMO_ROTATE) {
            BeginDrag(level, UIDragMode::Rotate, part);
            return;
        }
        if (part >= UI_GIZMO_RESIZE_FIRST && part <= UI_GIZMO_RESIZE_LAST) {
            BeginDrag(level, UIDragMode::Resize, part);
            return;
        }
        // With the add-to-selection key held, a click on the move gizmo
        // toggles the element under it instead.
        const bool addingToSelection = GetKeyBind(KEYBIND_UI_ADD_TO_SELECTION);
        if (!addingToSelection && (part == UI_GIZMO_MOVE_X || part == UI_GIZMO_MOVE_Y || part == UI_GIZMO_MOVE_FREE)) {
            BeginDrag(level, UIDragMode::Move, part);
            return;
        }

        const std::vector<ID> stack = UIEntitiesAtCanvasPoint(level, mouseCanvas);

        // Empty canvas: a click clears the selection, a drag box selects.
        if (stack.empty()) {
            uiDragMode = UIDragMode::PendingClick;
            return;
        }

        if (addingToSelection) {
            ToggleUIEntitySelection(stack.front());
            return;
        }

        // Pressing on something already selected drags the whole selection;
        // otherwise the topmost element is selected and can be dragged at once.
        ID pressed = INVALID_ID;
        if (std::ranges::find(stack, selectedUIEntityID) != stack.end()) pressed = selectedUIEntityID;
        else
            for (const ID id : stack)
                if (IsUIEntitySelected(id)) { pressed = id; break; }

        if (pressed == INVALID_ID) {
            SelectUIEntity(stack.front());
            pressed = stack.front();
        }
        else {
            uiPendingClickCycles = stack.size() > 1 && stack == uiLastClickStack &&
                                   NearScreenPoint(mouseScreen, uiLastClickScreen, UI_CLICK_DRAG_THRESHOLD * 2.0f);
        }

        uiPendingClickEntity = pressed;
        uiDragMode = UIDragMode::PendingClick;
        uiDragPart = UI_GIZMO_BODY;
    }

    // A press released without dragging.
    void HandleClick(Level& level, const Vector2& mouseScreen, const Vector2& mouseCanvas) {
        const std::vector<ID> stack = UIEntitiesAtCanvasPoint(level, mouseCanvas);

        if (uiPendingClickEntity == INVALID_ID) {
            if (!GetKeyBind(KEYBIND_UI_ADD_TO_SELECTION)) SelectUIEntity(INVALID_ID);
        }
        else if (uiPendingClickCycles) {
            // The next element under the cursor, below the current one.
            auto it = std::ranges::find(stack, uiPendingClickEntity);
            const ID next = it == stack.end() || std::next(it) == stack.end() ? stack.front() : *std::next(it);
            SelectUIEntity(next);
        }
        else SelectUIEntity(uiPendingClickEntity);

        uiLastClickStack = stack;
        uiLastClickScreen = mouseScreen;
    }

    void HandleCanvasMouse(Level& level, const Vector2& mouseScreen, const Vector2& mouseCanvas) {
        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) HandleMousePress(level, mouseScreen, mouseCanvas);

        if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) {
            if (uiDragMode == UIDragMode::PendingClick &&
                !NearScreenPoint(mouseScreen, uiDragStartMouseScreen, UI_CLICK_DRAG_THRESHOLD)) {
                if (uiPendingClickEntity != INVALID_ID) BeginDrag(level, UIDragMode::Move, UI_GIZMO_BODY);
                else uiDragMode = UIDragMode::BoxSelect;
            }

            uiSnapGuides.clear();
            switch (uiDragMode) {
                case UIDragMode::Move: ContinueMove(level, mouseCanvas); break;
                case UIDragMode::Resize: ContinueResize(level, mouseCanvas); break;
                case UIDragMode::Rotate: ContinueRotate(level, mouseCanvas); break;
                default: break;
            }
        }

        if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
            if (uiDragMode == UIDragMode::PendingClick) {
                uiDragMode = UIDragMode::None;
                HandleClick(level, mouseScreen, mouseCanvas);
            }
            else if (uiDragMode == UIDragMode::BoxSelect) {
                uiDragMode = UIDragMode::None;
                FinishBoxSelection(level, mouseCanvas);
            }
            else EndDrag(level);
        }

        // Right click: the canvas context menu (drawn by UIEditorUI.cpp). On
        // an element that isn't selected it selects that element first.
        if (uiDragMode == UIDragMode::None && InputManager::GetMouseButtonDown(SDL_BUTTON_RIGHT)) {
            const std::vector<ID> stack = UIEntitiesAtCanvasPoint(level, mouseCanvas);

            uiContextMenuOnElement = !stack.empty();
            if (uiContextMenuOnElement && !IsUIEntitySelected(stack.front())) {
                ID target = stack.front();
                for (const ID id : stack)
                    if (IsUIEntitySelected(id)) { target = id; break; }
                if (!IsUIEntitySelected(target)) SelectUIEntity(target);
            }

            uiContextMenuCanvasPoint = mouseCanvas;
            uiContextMenuRequested = true;
        }
    }

    // ---------------------------------------------------------------------
    // View
    // ---------------------------------------------------------------------

    void ZoomAround(const Vector2& screenPoint, const float factor) {
        const Vector2 before = ScreenToUICanvas(screenPoint);
        uiCanvasZoom = std::clamp(uiCanvasZoom * factor, MIN_UI_CANVAS_ZOOM, MAX_UI_CANVAS_ZOOM);
        const Vector2 after = ScreenToUICanvas(screenPoint);
        uiCanvasPan.x += before.x - after.x;
        uiCanvasPan.y += before.y - after.y;
    }

    // F: the selection (or the whole screen) centred and filling most of the window.
    void FrameSelection(Level& level) {
        CanvasRect bounds{};
        if (!SelectionBounds(level, bounds)) bounds = {0.0f, 0.0f, ScreenSize().x, ScreenSize().y};

        const float width = std::max(bounds.maxX - bounds.minX, 1.0f);
        const float height = std::max(bounds.maxY - bounds.minY, 1.0f);

        uiCanvasPan = {(bounds.minX + bounds.maxX) * 0.5f, (bounds.minY + bounds.maxY) * 0.5f};
        uiCanvasZoom = std::clamp(std::min(ScreenSize().x * UI_FRAME_FILL / width, ScreenSize().y * UI_FRAME_FILL / height),
                                  MIN_UI_CANVAS_ZOOM, MAX_UI_CANVAS_ZOOM);
    }

    // ---------------------------------------------------------------------
    // Keyboard actions
    // ---------------------------------------------------------------------

    void NudgeSelection(Level& level, const Vector2& delta) {
        if (selectedUIEntities.empty()) return;

        // Taps in quick succession are one undo step.
        const Uint64 now = SDL_GetTicks();
        const bool merge = now - uiLastNudgeTicks < NUDGE_UNDO_MERGE_MS && !undoSteps.empty() &&
                           undoSteps.size() == uiLastNudgeUndoSize &&
                           std::strcmp(undoSteps.back().labelKey, "editor.history.ui_nudge") == 0;
        if (!merge) PushEntitiesUndo(CaptureEntitiesSnapshot(), "editor.history.ui_nudge", static_cast<int>(selectedUIEntities.size()));

        for (const ID id : selectedUIEntities)
            if (ComponentUITransform* transform = GetUITransform(level, id)) transform->position = transform->position + delta;

        uiLastNudgeTicks = now;
        uiLastNudgeUndoSize = undoSteps.size();
        MarkUIUnsavedChanges();
    }

    void HandleNudgeKeys(Level& level) {
        constexpr EditorKeyBind binds[4] = {
            KEYBIND_UI_NUDGE_UP, KEYBIND_UI_NUDGE_DOWN, KEYBIND_UI_NUDGE_LEFT, KEYBIND_UI_NUDGE_RIGHT
        };
        const Vector2 directions[4] = {{0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}};

        const float step = GetKeyBind(KEYBIND_UI_BIG_NUDGE) ? UI_BIG_NUDGE : UI_NUDGE;
        const Uint64 now = SDL_GetTicks();

        for (int i = 0; i < 4; ++i) {
            bool nudge = false;

            if (GetKeyBindDown(binds[i])) {
                uiNudgeHeldSince[i] = now;
                uiNudgeLastRepeat[i] = now;
                nudge = true;
            }
            else if (GetKeyBind(binds[i]) && now - uiNudgeHeldSince[i] >= NUDGE_REPEAT_DELAY_MS &&
                     now - uiNudgeLastRepeat[i] >= NUDGE_REPEAT_INTERVAL_MS) {
                uiNudgeLastRepeat[i] = now;
                nudge = true;
            }

            if (nudge) NudgeSelection(level, {directions[i].x * step, directions[i].y * step});
        }
    }

    void DeleteSelection(Level& level) {
        if (selectedUIEntities.empty()) return;

        PushEntitiesUndo(CaptureEntitiesSnapshot(), "editor.history.ui_delete", static_cast<int>(selectedUIEntities.size()));

        const std::vector<ID> toDelete = selectedUIEntities;
        SelectUIEntity(INVALID_ID);
        for (const ID id : toDelete) level.DestroyEntity(id);

        MarkUIUnsavedChanges();
    }

    void DuplicateSelection(Level& level) {
        if (selectedUIEntities.empty()) return;

        EntitiesSnapshot before = CaptureEntitiesSnapshot();

        // The primary is copied last, so its copy becomes the new primary.
        std::vector<ID> originals;
        for (const ID id : selectedUIEntities)
            if (id != selectedUIEntityID) originals.push_back(id);
        originals.push_back(selectedUIEntityID);

        std::vector<ID> copies;
        for (const ID id : originals) {
            const Entity* original = level.GetEntity(id);
            if (original == nullptr) continue;

            // CreateEntity can grow the entity list, so copy before calling it.
            Entity copy = *original;
            const ID copyID = level.CreateEntity(copy);

            if (ComponentUITransform* transform = GetUITransform(level, copyID))
                transform->position = {transform->position.x + UI_DUPLICATE_OFFSET, transform->position.y + UI_DUPLICATE_OFFSET};

            copies.push_back(copyID);
        }

        if (copies.empty()) return;

        PushEntitiesUndo(std::move(before), "editor.history.ui_duplicate", static_cast<int>(copies.size()));

        selectedUIEntityID = copies.back();
        selectedUIEntities = copies;
        MarkUIUnsavedChanges();
    }

    void SelectAllUIEntities(Level& level) {
        SelectUIEntity(INVALID_ID);

        std::vector<ID> order = UIEntitiesTopmostFirst(level);
        std::ranges::reverse(order);
        for (const ID id : order) ToggleUIEntitySelection(id);
    }

    void AfterUndoOrRedo(Level& level) {
        CancelDrag(level);
        MarkUIUnsavedChanges();
    }

    void HandleCanvasKeys(Level& level) {
        if (uiDragMode == UIDragMode::None) {
            HandleNudgeKeys(level);

            if (GetKeyBindDown(KEYBIND_UI_DELETE)) DeleteSelection(level);
            if (GetComboDown(COMBO_UI_DUPLICATE)) DuplicateSelection(level);
            if (GetComboDown(COMBO_UI_SELECT_ALL)) SelectAllUIEntities(level);
        }

        if (GetComboDown(COMBO_UI_UNDO)) {
            CancelDrag(level);
            UndoLastAction();
            AfterUndoOrRedo(level);
        }
        else if (GetComboDown(COMBO_UI_REDO)) {
            CancelDrag(level);
            RedoLastAction();
            AfterUndoOrRedo(level);
        }

        if (GetKeyBindDown(KEYBIND_UI_FOCUS_SELECTION)) FrameSelection(level);
        if (GetKeyBindDown(KEYBIND_UI_TOGGLE_SNAP)) uiSnapEnabled = !uiSnapEnabled;

        const Vector2 windowCenter = {ScreenSize().x * 0.5f, ScreenSize().y * 0.5f};
        if (GetKeyBindDown(KEYBIND_UI_ZOOM_IN)) ZoomAround(windowCenter, 1.15f);
        if (GetKeyBindDown(KEYBIND_UI_ZOOM_OUT)) ZoomAround(windowCenter, 1.0f / 1.15f);

        // Escape stays fixed, like everywhere else: cancel a drag, else deselect.
        if (InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE)) {
            if (uiDragMode != UIDragMode::None) CancelDrag(level);
            else SelectUIEntity(INVALID_ID);
        }
    }

    // Keeps selectedUIEntities in step with selectedUIEntityID, which other
    // code sets directly (see EditorInternal.hpp).
    void SyncUISelection(Level& level) {
        std::erase_if(selectedUIEntities, [&level](const ID id) { return level.GetEntity(id) == nullptr; });

        if (selectedUIEntityID != INVALID_ID && level.GetEntity(selectedUIEntityID) == nullptr)
            selectedUIEntityID = selectedUIEntities.empty() ? INVALID_ID : selectedUIEntities.back();

        if (selectedUIEntityID == INVALID_ID) selectedUIEntities.clear();
        else if (!IsUIEntitySelected(selectedUIEntityID)) selectedUIEntities = {selectedUIEntityID};
    }
}

namespace MapEditorInternal {
    bool IsUIEntitySelected(const ID id) {
        return id != INVALID_ID && std::ranges::find(selectedUIEntities, id) != selectedUIEntities.end();
    }

    void SelectUIEntity(const ID id) {
        selectedUIEntityID = id;
        selectedUIEntities.clear();
        if (id != INVALID_ID) selectedUIEntities.push_back(id);
    }

    void ToggleUIEntitySelection(const ID id) {
        if (id == INVALID_ID) return;

        if (IsUIEntitySelected(id)) {
            std::erase(selectedUIEntities, id);
            if (selectedUIEntityID == id) selectedUIEntityID = selectedUIEntities.empty() ? INVALID_ID : selectedUIEntities.back();
        }
        else {
            selectedUIEntities.push_back(id);
            selectedUIEntityID = id;
        }
    }

    bool GetUIGizmoLayout(UIGizmoLayout* layout) {
        const ComponentUITransform* transform = GetUITransform(LevelManager::CurrentLevel(), selectedUIEntityID);
        if (transform == nullptr) return false;

        Vector2 canvasCorners[4];
        ComputeUIEntityCanvasCorners(*transform, canvasCorners);
        for (int i = 0; i < 4; ++i) layout->corners[i] = UICanvasToScreen(canvasCorners[i]);

        const Vector2 centerCanvas = UITransformCenterCanvas(*transform);
        layout->center = UICanvasToScreen(centerCanvas);

        // Along the canvas axes, which is what position moves along.
        layout->xTip = {layout->center.x + UI_GIZMO_ARROW_LENGTH, layout->center.y};
        layout->yTip = {layout->center.x, layout->center.y + UI_GIZMO_ARROW_LENGTH};

        // Out from the middle of the top edge, so it turns with the element.
        layout->rotateStem = (layout->corners[0] + layout->corners[1]) * 0.5f;
        Vector2 outward = layout->rotateStem - layout->center;
        const float outwardLength = std::sqrt(outward.x * outward.x + outward.y * outward.y);
        outward = outwardLength > 0.0001f ? Vector2{outward.x / outwardLength, outward.y / outwardLength}
                                          : Rotate({0.0f, -1.0f}, transform->rotation);
        layout->rotateHandle = {
            layout->rotateStem.x + outward.x * UI_ROTATE_HANDLE_OFFSET,
            layout->rotateStem.y + outward.y * UI_ROTATE_HANDLE_OFFSET
        };

        const Vector2 size = transform->resolvedSize;
        const bool stretchX = IsStretchedX(*transform);
        const bool stretchY = IsStretchedY(*transform);
        const float widthOnScreen = std::fabs(size.x) * uiCanvasZoom;
        const float heightOnScreen = std::fabs(size.y) * uiCanvasZoom;

        for (int i = 0; i < 8; ++i) {
            const int hx = UI_RESIZE_HANDLE_DIRECTIONS[i][0];
            const int hy = UI_RESIZE_HANDLE_DIRECTIONS[i][1];

            const Vector2 local = {hx * size.x * 0.5f, hy * size.y * 0.5f};
            layout->resizeHandles[i] = UICanvasToScreen(centerCanvas + Rotate(local, transform->rotation));

            bool shown = !(hx != 0 && stretchX) && !(hy != 0 && stretchY);
            if (hx == 0 && widthOnScreen < UI_MIN_EDGE_HANDLE_LENGTH) shown = false;
            if (hy == 0 && heightOnScreen < UI_MIN_EDGE_HANDLE_LENGTH) shown = false;
            layout->resizeShown[i] = shown;
        }

        layout->moveSquareOnTop = IsSmallOnScreen(*transform);

        return true;
    }

    UIGizmoPart GetHighlightedUIGizmoPart() {
        if (uiDragMode == UIDragMode::Move || uiDragMode == UIDragMode::Resize || uiDragMode == UIDragMode::Rotate)
            return uiDragPart;
        if (uiDragMode != UIDragMode::None || uiMouseBlockedLastFrame) return UI_GIZMO_NONE;

        return PickUIGizmoPart(InputManager::GetMousePosition());
    }

    const std::vector<UISnapGuide>& GetUISnapGuides() {
        return uiSnapGuides;
    }

    bool GetUIBoxSelection(Vector2* startScreen, Vector2* endScreen) {
        if (uiDragMode != UIDragMode::BoxSelect) return false;

        *startScreen = UICanvasToScreen(uiDragStartMouseCanvas);
        *endScreen = InputManager::GetMousePosition();
        return true;
    }

    const std::string& GetUIDragReadout() {
        return uiDragReadout;
    }

    void AlignSelectedUIEntities(const UIAlignment alignment) {
        Level& level = LevelManager::CurrentLevel();

        CanvasRect target{};
        if (selectedUIEntities.size() == 1) target = {0.0f, 0.0f, ScreenSize().x, ScreenSize().y};
        else if (!SelectionBounds(level, target)) return;

        EntitiesSnapshot before = CaptureEntitiesSnapshot();
        int moved = 0;

        for (const ID id : selectedUIEntities) {
            ComponentUITransform* transform = GetUITransform(level, id);
            if (transform == nullptr) continue;

            const CanvasRect bounds = TransformBounds(*transform);
            Vector2 delta{};
            switch (alignment) {
                case UI_ALIGN_LEFT: delta.x = target.minX - bounds.minX; break;
                case UI_ALIGN_CENTER_X: delta.x = (target.minX + target.maxX - bounds.minX - bounds.maxX) * 0.5f; break;
                case UI_ALIGN_RIGHT: delta.x = target.maxX - bounds.maxX; break;
                case UI_ALIGN_TOP: delta.y = target.minY - bounds.minY; break;
                case UI_ALIGN_CENTER_Y: delta.y = (target.minY + target.maxY - bounds.minY - bounds.maxY) * 0.5f; break;
                case UI_ALIGN_BOTTOM: delta.y = target.maxY - bounds.maxY; break;
            }

            if (delta.x == 0.0f && delta.y == 0.0f) continue;
            transform->position = transform->position + delta;
            ++moved;
        }

        if (moved == 0) return;
        PushEntitiesUndo(std::move(before), "editor.history.ui_align", moved);
        MarkUIUnsavedChanges();
    }

    void DistributeSelectedUIEntities(const bool horizontally) {
        Level& level = LevelManager::CurrentLevel();

        struct Item {
            ComponentUITransform* transform;
            float center;
        };

        std::vector<Item> items;
        for (const ID id : selectedUIEntities) {
            if (ComponentUITransform* transform = GetUITransform(level, id)) {
                const CanvasRect bounds = TransformBounds(*transform);
                items.push_back({transform, horizontally ? (bounds.minX + bounds.maxX) * 0.5f : (bounds.minY + bounds.maxY) * 0.5f});
            }
        }
        if (items.size() < 3) return;

        std::ranges::sort(items, {}, &Item::center);

        EntitiesSnapshot before = CaptureEntitiesSnapshot();

        const float first = items.front().center;
        const float spacing = (items.back().center - first) / static_cast<float>(items.size() - 1);
        int moved = 0;

        for (std::size_t i = 1; i + 1 < items.size(); ++i) {
            const float shift = first + spacing * static_cast<float>(i) - items[i].center;
            if (shift == 0.0f) continue;

            if (horizontally) items[i].transform->position.x += shift;
            else items[i].transform->position.y += shift;
            ++moved;
        }

        if (moved == 0) return;
        PushEntitiesUndo(std::move(before), "editor.history.ui_distribute", moved);
        MarkUIUnsavedChanges();
    }

    void FrameUISelection() {
        FrameSelection(LevelManager::CurrentLevel());
    }

    void DeleteSelectedUIEntities() {
        DeleteSelection(LevelManager::CurrentLevel());
    }

    void DuplicateSelectedUIEntities() {
        DuplicateSelection(LevelManager::CurrentLevel());
    }

    bool ConsumeUICanvasContextMenuRequest(Vector2* canvasPoint, bool* onElement) {
        if (!uiContextMenuRequested) return false;

        uiContextMenuRequested = false;
        *canvasPoint = uiContextMenuCanvasPoint;
        *onElement = uiContextMenuOnElement;
        return true;
    }

    void HandleUIEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        // Hit testing and handle placement must use the same current layout
        // values as both the editor drawing pass and the runtime renderer.
        UISystem::UpdateAllTransforms(
            level,
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight)
        );

        SyncUISelection(level);

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseCanvas = ScreenToUICanvas(mouseScreen);

        // A drag keeps going over a panel, so releasing there still ends it.
        const bool dragging = uiDragMode != UIDragMode::None;
        uiMouseBlockedLastFrame = mouseBlockedByImGui && !dragging;

        if (uiDragMode == UIDragMode::None) {
            const std::vector<ID> stack = mouseBlockedByImGui ? std::vector<ID>{} : UIEntitiesAtCanvasPoint(level, mouseCanvas);
            hoveredUIEntityID = stack.empty() ? INVALID_ID : stack.front();
        }
        else if (uiDragMode != UIDragMode::PendingClick) hoveredUIEntityID = INVALID_ID;

        if (!keyboardBlockedByImgui) HandleCanvasKeys(level);

        if (!mouseBlockedByImGui || dragging) {
            if (!dragging && InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();
                uiCanvasPan.x -= mouseDelta.x / uiCanvasZoom;
                uiCanvasPan.y -= mouseDelta.y / uiCanvasZoom;
            }
            else HandleCanvasMouse(level, mouseScreen, mouseCanvas);

            if (!mouseBlockedByImGui) {
                if (InputManager::GetMouseWheelScrollUp()) ZoomAround(mouseScreen, 1.15f);
                if (InputManager::GetMouseWheelScrollDown()) ZoomAround(mouseScreen, 1.0f / 1.15f);
                UpdateCanvasCursor();
            }
        }
    }
} // namespace MapEditorInternal
