// The UI editor canvas as an Editor2D::Frame for the OpenGL renderer: the
// same picture UIEditorDraw.cpp draws with SDL_Renderer, function for
// function. There are no level passes here; everything is overlay triangles
// and labels in three layers:
//   Background   - checkerboard, grid
//   AfterSectors - the UI's sprites and texts, cut to the screen rect
//   Top          - boundary, centre lines, safe area, snap guides, outlines,
//                  gizmo, box selection
// and the hover / drag tag after everything.

#include "../EditorInternal.hpp"
#include "../Editor2DCanvas.hpp"

#include "Headers/Runtime/Gameplay/UIButtonSystem.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/UISystem.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace MapEditorInternal {
    // Named, not anonymous: unity builds put several of these files together.
    namespace UICanvasFrame {
        using Editor2D::Frame;
        using Editor2D::Layer;
        using Editor2DCanvas::Canvas;

        Canvas canvas;

        Vector2 ScreenResolution() {
            return {static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
        }

        Vector4 Rgba01(const Vector3& color, const float alpha = 1.0f) {
            return {color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, alpha};
        }

        // ---- Canvas chrome -------------------------------------------------

        void Checkerboard() {
            const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
            const Vector2 bottomRight = UICanvasToScreen(ScreenResolution());

            constexpr float cell = 16.0f;
            const float cellScreen = cell * uiCanvasZoom;

            if (cellScreen > 3.0f) {
                int col = 0;
                for (float x = topLeft.x; x < bottomRight.x; x += cellScreen, ++col) {
                    int row = 0;
                    for (float y = topLeft.y; y < bottomRight.y; y += cellScreen, ++row) {
                        if ((col + row) % 2 == 0) canvas.SetColor(54, 54, 60, 255);
                        else canvas.SetColor(70, 70, 76, 255);
                        canvas.FillRect({x, y, std::min(cellScreen, bottomRight.x - x), std::min(cellScreen, bottomRight.y - y)});
                    }
                }
            }
            else {
                canvas.SetColor(62, 62, 68, 255);
                canvas.FillRect({topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y});
            }
        }

        void Grid() {
            if (!showUIGrid) return;

            const float stepScreen = UI_CANVAS_GRID_STEP * uiCanvasZoom;
            if (stepScreen < 4.0f) return;

            const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
            const Vector2 bottomRight = UICanvasToScreen(ScreenResolution());

            canvas.SetBlend(true);
            canvas.SetColor(255, 255, 255, 22);

            for (float x = topLeft.x; x <= bottomRight.x; x += stepScreen) canvas.Line({x, topLeft.y}, {x, bottomRight.y});
            for (float y = topLeft.y; y <= bottomRight.y; y += stepScreen) canvas.Line({topLeft.x, y}, {bottomRight.x, y});

            canvas.SetBlend(false);
        }

        void Boundary() {
            const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
            const Vector2 bottomRight = UICanvasToScreen(ScreenResolution());

            canvas.SetColor(255, 255, 255, 180);
            canvas.Rect({topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y});
        }

        void CenterLines() {
            if (!showUICenterLines) return;

            const Vector2 screenRes = ScreenResolution();
            const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
            const Vector2 bottomRight = UICanvasToScreen(screenRes);
            const Vector2 center = UICanvasToScreen({screenRes.x * 0.5f, screenRes.y * 0.5f});

            canvas.SetBlend(true);
            canvas.SetColor(255, 210, 90, 130);
            canvas.Line({center.x, topLeft.y}, {center.x, bottomRight.y});
            canvas.Line({topLeft.x, center.y}, {bottomRight.x, center.y});
            canvas.SetBlend(false);
        }

        void DashedLine(const Vector2& a, const Vector2& b) {
            constexpr float dash = 6.0f, gap = 4.0f;
            const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            if (length < 0.01f) return;

            const Vector2 dir = {(b.x - a.x) / length, (b.y - a.y) / length};
            for (float t = 0.0f; t < length; t += dash + gap) {
                const float segEnd = std::min(t + dash, length);
                canvas.Line({a.x + dir.x * t, a.y + dir.y * t}, {a.x + dir.x * segEnd, a.y + dir.y * segEnd});
            }
        }

        void SafeArea() {
            if (!showUISafeArea) return;

            const Vector2 screenRes = ScreenResolution();
            const Vector2 inset = {screenRes.x * UI_SAFE_AREA_FRACTION, screenRes.y * UI_SAFE_AREA_FRACTION};
            const Vector2 topLeft = UICanvasToScreen(inset);
            const Vector2 bottomRight = UICanvasToScreen({screenRes.x - inset.x, screenRes.y - inset.y});

            canvas.SetBlend(true);
            canvas.SetColor(120, 220, 140, 200);
            DashedLine({topLeft.x, topLeft.y}, {bottomRight.x, topLeft.y});
            DashedLine({bottomRight.x, topLeft.y}, {bottomRight.x, bottomRight.y});
            DashedLine({bottomRight.x, bottomRight.y}, {topLeft.x, bottomRight.y});
            DashedLine({topLeft.x, bottomRight.y}, {topLeft.x, topLeft.y});
            canvas.SetBlend(false);
        }

        // ---- Entities ------------------------------------------------------

        // Ported from UI_vs.glsl (the game's UI vertex shader, via OpenGLUI.cpp's
        // DrawUIRectangle): resolvedPosition is the rect's TOP-LEFT corner, and
        // rotation turns the rect about its own centre, never about `pivot`.
        // `pivot` only decided which point of the element lands on the anchor
        // when UISystem resolved the position; here it is a marker. There is no
        // separate design resolution: the canvas is the screen size the game
        // sends as uScreenSize.
        struct ScreenQuad {
            Vector2 corners[4];
            Vector2 pivotMarkerScreen;
        };

        ScreenQuad ComputeScreenQuad(const ComponentUITransform& transform) {
            const Vector2 size = transform.resolvedSize;
            const Vector2 centerCanvas = {transform.resolvedPosition.x + size.x * 0.5f, transform.resolvedPosition.y + size.y * 0.5f};

            const float rad = transform.rotation * Constants::DegToRad;
            const float cosA = std::cos(rad);
            const float sinA = std::sin(rad);

            const auto rotateAroundCenter = [&](const Vector2& offset) -> Vector2 {
                return UICanvasToScreen({centerCanvas.x + offset.x * cosA - offset.y * sinA, centerCanvas.y + offset.x * sinA + offset.y * cosA});
            };

            ScreenQuad quad{};
            quad.corners[0] = rotateAroundCenter({-size.x * 0.5f, -size.y * 0.5f});
            quad.corners[1] = rotateAroundCenter({size.x * 0.5f, -size.y * 0.5f});
            quad.corners[2] = rotateAroundCenter({size.x * 0.5f, size.y * 0.5f});
            quad.corners[3] = rotateAroundCenter({-size.x * 0.5f, size.y * 0.5f});
            quad.pivotMarkerScreen = rotateAroundCenter({transform.pivot.x * size.x - size.x * 0.5f, transform.pivot.y * size.y - size.y * 0.5f});
            return quad;
        }

        void TextEntity(Frame& frame, const ComponentUIText& text, const std::string& shown, const ComponentUITransform& transform) {
            if (shown.empty() || text.color.w <= 0.0f) return;

            const float windowScale = static_cast<float>(screenHeight) / std::max(1.0f, ProjectManager::GetUIReferenceHeight());
            const float pixelSize = text.fontSize * windowScale * uiCanvasZoom;
            if (!(pixelSize >= 0.5f)) return;

            // Whole-byte colours, as the editor's old SDL_ttf text had.
            const auto channel = [](const float value) { return static_cast<float>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)) / 255.0f; };

            const float padding = UI_TEXT_PADDING * windowScale;
            const Vector2 screenPos = UICanvasToScreen({transform.resolvedPosition.x + padding, transform.resolvedPosition.y + padding});

            Editor2D::Label label;
            label.text = shown;
            label.anchor = screenPos;
            label.color = {channel(text.color.x), channel(text.color.y), channel(text.color.z), channel(text.color.w)};
            label.pixelSize = static_cast<float>(std::clamp(static_cast<int>(std::lround(pixelSize)), 1, 512));
            label.style = Editor2D::LabelStyle::TopLeft;
            label.font = text.font;
            label.layer = Layer::AfterSectors;
            label.clip = true;
            frame.labels.push_back(std::move(label));
        }

        std::unordered_map<ID, const ComponentUITransform*> Content(Frame& frame, const Level& level) {
            std::unordered_map<ID, const ComponentUITransform*> transformsByOwner;
            transformsByOwner.reserve(level.ui_transforms.components.size());
            for (const ComponentUITransform& transform : level.ui_transforms.components) transformsByOwner.emplace(transform.ownerID, &transform);

            canvas.layer = Layer::AfterSectors;
            canvas.clip = true;
            canvas.SetBlend(true);

            UISystem::ForEachSpriteInDrawOrder(level, [&](const ComponentUISprite& sprite) {
                const auto transformIt = transformsByOwner.find(sprite.ownerID);
                if (transformIt == transformsByOwner.end()) return;

                // A UI Button's first sprite shows the look the game starts it with.
                Vector4 color = sprite.color;
                const std::string* textureName = &sprite.texture;
                const ComponentUIButton* button = level.ui_buttons.Get(sprite.ownerID);
                if (button != nullptr && level.ui_sprites.Get(sprite.ownerID) == &sprite) {
                    const Vector4& stateColor = UIButtonSystem::StateColor(*button);
                    color = {color.x * stateColor.x, color.y * stateColor.y, color.z * stateColor.z, color.w * stateColor.w};
                    if (const std::string& stateTexture = UIButtonSystem::StateTexture(*button); !stateTexture.empty()) textureName = &stateTexture;
                }

                if (textureName->empty() && !sprite.fillWhenEmpty) return;
                if (!textureName->empty() && GetEditorTexture(*textureName) == nullptr) return;

                const ScreenQuad quad = ComputeScreenQuad(*transformIt->second);

                // Inactive sprites aren't drawn in game; shown faded so they can still be found.
                const Vector4 tint = {
                    std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f), std::clamp(color.z, 0.0f, 1.0f),
                    std::clamp(color.w, 0.0f, 1.0f) * (sprite.isActive ? 1.0f : 0.25f)
                };

                // A texture that fails to load leaves the sprite out (the renderer skips the run).
                canvas.TexturedQuad(quad.corners, *textureName, tint);
            });

            canvas.SetBlend(false);
            canvas.clip = false;

            for (const ComponentUIText& text : level.ui_texts.components) {
                const auto transformIt = transformsByOwner.find(text.ownerID);
                if (transformIt == transformsByOwner.end()) continue;
                TextEntity(frame, text, level.ShownUIText(text), *transformIt->second);
            }

            return transformsByOwner;
        }

        // ---- Selection and gizmo -------------------------------------------

        void FilledQuad(const Vector2& center, const float half, const Vector4& fill, const Vector3& outline) {
            const SDL_FRect rect = {center.x - half, center.y - half, half * 2.0f, half * 2.0f};
            canvas.SetColorFloat(fill.x, fill.y, fill.z, fill.w);
            canvas.FillRect(rect);
            canvas.SetColor(outline.x, outline.y, outline.z, 255);
            canvas.Rect(rect);
        }

        void GizmoArrow(const Vector2& start, const Vector2& tip, const Vector3& color) {
            constexpr float SHAFT_THICKNESS = 3.0f;
            constexpr float HEAD_LENGTH = 12.0f;
            constexpr float HEAD_HALF_WIDTH = 6.0f;

            const float dx = tip.x - start.x;
            const float dy = tip.y - start.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= HEAD_LENGTH) return;

            const float dirX = dx / length;
            const float dirY = dy / length;
            const Vector2 headBase = {tip.x - dirX * HEAD_LENGTH, tip.y - dirY * HEAD_LENGTH};

            canvas.SetColor(color.x, color.y, color.z, 255);
            canvas.ThickLine(start, headBase, SHAFT_THICKNESS);

            canvas.GeometryTriangle(tip, {headBase.x - dirY * HEAD_HALF_WIDTH, headBase.y + dirX * HEAD_HALF_WIDTH},
                                    {headBase.x + dirY * HEAD_HALF_WIDTH, headBase.y - dirX * HEAD_HALF_WIDTH}, Rgba01(color));
        }

        void EntityOutline(const ScreenQuad& quad, const bool selected, const bool primary, const bool hovered) {
            if (!selected && !hovered) return;

            if (primary) canvas.SetColor(80, 220, 255, 255);
            else if (selected) canvas.SetColor(80, 220, 255, 170);
            else canvas.SetColor(255, 220, 80, 160);

            for (int i = 0; i < 4; ++i) canvas.ThickLine(quad.corners[i], quad.corners[(i + 1) % 4], primary ? 2.0f : 1.5f);
        }

        void EntityGizmo(const ScreenQuad& quad, const ComponentUITransform& transform) {
            UIGizmoLayout layout;
            if (!GetUIGizmoLayout(&layout)) return;

            const UIGizmoPart highlighted = GetHighlightedUIGizmoPart();
            const auto colorFor = [highlighted](const UIGizmoPart part, const Vector3& color) -> Vector3 {
                return highlighted == part ? highlightedHandleColor : color;
            };

            canvas.SetColor(90, 220, 255, 255);
            constexpr float pivotRadius = 3.0f;
            canvas.FillRect({quad.pivotMarkerScreen.x - pivotRadius, quad.pivotMarkerScreen.y - pivotRadius, pivotRadius * 2.0f, pivotRadius * 2.0f});

            canvas.SetColor(255, 150, 60, 255);
            const Vector2 screenRes = ScreenResolution();
            const Vector2 anchorMinScreen = UICanvasToScreen({transform.anchorMin.x * screenRes.x, transform.anchorMin.y * screenRes.y});
            const Vector2 anchorMaxScreen = UICanvasToScreen({transform.anchorMax.x * screenRes.x, transform.anchorMax.y * screenRes.y});
            for (const Vector2& anchorPoint : {anchorMinScreen, anchorMaxScreen})
                canvas.Rect({anchorPoint.x - 4.0f, anchorPoint.y - 4.0f, 8.0f, 8.0f});

            constexpr float squareHalf = 7.0f; // UI_GIZMO_SQUARE_HALF in UIEditorInput.cpp
            GizmoArrow({layout.center.x + squareHalf, layout.center.y}, layout.xTip, colorFor(UI_GIZMO_MOVE_X, gizmoXColor));
            GizmoArrow({layout.center.x, layout.center.y + squareHalf}, layout.yTip, colorFor(UI_GIZMO_MOVE_Y, gizmoZColor));

            const auto moveSquare = [&] { FilledQuad(layout.center, squareHalf, Rgba01(colorFor(UI_GIZMO_MOVE_FREE, gizmoYColor)), handleOutlineColor); };
            if (!layout.moveSquareOnTop) moveSquare();

            for (int i = 0; i < 8; ++i) {
                if (!layout.resizeShown[i]) continue;
                const auto part = static_cast<UIGizmoPart>(UI_GIZMO_RESIZE_FIRST + i);
                FilledQuad(layout.resizeHandles[i], 4.5f, Rgba01(colorFor(part, Vector3{245, 245, 245})), handleOutlineColor);
            }

            if (layout.moveSquareOnTop) moveSquare();

            const Vector3 rotateColor = colorFor(UI_GIZMO_ROTATE, Vector3{170, 255, 130});
            canvas.SetColor(rotateColor.x, rotateColor.y, rotateColor.z, 255);
            canvas.ThickLine(layout.rotateStem, layout.rotateHandle, 2.0f);
            FilledQuad(layout.rotateHandle, 5.0f, Rgba01(rotateColor), handleOutlineColor);
        }

        void SnapGuides() {
            canvas.SetBlend(true);
            canvas.SetColor(255, 80, 200, 220);

            for (const UISnapGuide& guide : GetUISnapGuides()) {
                if (guide.vertical) {
                    const float x = UICanvasToScreen({guide.canvasValue, 0.0f}).x;
                    canvas.Line({x, 0.0f}, {x, static_cast<float>(screenHeight)});
                }
                else {
                    const float y = UICanvasToScreen({0.0f, guide.canvasValue}).y;
                    canvas.Line({0.0f, y}, {static_cast<float>(screenWidth), y});
                }
            }

            canvas.SetBlend(false);
        }

        void BoxSelection() {
            Vector2 start, end;
            if (!GetUIBoxSelection(&start, &end)) return;

            const SDL_FRect rect = {std::min(start.x, end.x), std::min(start.y, end.y), std::fabs(end.x - start.x), std::fabs(end.y - start.y)};

            canvas.SetBlend(true);
            canvas.SetColor(80, 160, 255, 40);
            canvas.FillRect(rect);
            canvas.SetColor(80, 160, 255, 200);
            canvas.Rect(rect);
            canvas.SetBlend(false);
        }

        void Tag(Frame& frame, const std::string& text, const Vector2 screenPos) {
            if (text.empty()) return;
            frame.labels.push_back({text, screenPos, {235.0f / 255.0f, 235.0f / 255.0f, 240.0f / 255.0f, 1.0f}, UI_FONT_SIZE, Editor2D::LabelStyle::Tag});
        }

        void CanvasLabels(Frame& frame, Level& level, const std::unordered_map<ID, const ComponentUITransform*>& transformsByOwner) {
            if (const std::string& readout = GetUIDragReadout(); !readout.empty()) {
                const Vector2 mouse = MouseInEditorWindow();
                Tag(frame, readout, {mouse.x + 16.0f, mouse.y + 16.0f});
                return;
            }

            if (hoveredUIEntityID == INVALID_ID || GetHighlightedUIGizmoPart() != UI_GIZMO_NONE) return;

            const Entity* entity = level.GetEntity(hoveredUIEntityID);
            const auto transformIt = transformsByOwner.find(hoveredUIEntityID);
            if (entity == nullptr || transformIt == transformsByOwner.end()) return;

            const ScreenQuad quad = ComputeScreenQuad(*transformIt->second);
            float minX = quad.corners[0].x, minY = quad.corners[0].y;
            for (const Vector2& corner : quad.corners) {
                minX = std::min(minX, corner.x);
                minY = std::min(minY, corner.y);
            }

            Tag(frame, entity->name.empty() ? "#" + std::to_string(entity->id) : entity->name, {minX, minY - 26.0f});
        }
    }

    void BuildUIEditorFrame(Editor2D::Frame& frame) {
        using namespace UICanvasFrame;

        frame.Clear();
        frame.view = {{0.0f, 0.0f}, 1.0f, screenWidth, screenHeight};
        frame.style = {};
        frame.style.clearColor = Rgba01(backgroundColor);

        canvas = {};
        canvas.frame = &frame;

        // Only the UI being edited is shown: a UI layout, or the level's own UI.
        Level* editedLayout = UIEditedLayout();
        Level& level = editedLayout != nullptr ? *editedLayout : EditorLevel();
        LevelManager::ScopedCurrentLevel editedScope(level);

        // Resolved against the screen size the game sends to uScreenSize.
        UISystem::UpdateAllTransforms(level, static_cast<float>(screenWidth), static_cast<float>(screenHeight));

        canvas.layer = Layer::Background;
        Checkerboard();
        Grid();

        // Content is cut to the screen rect, as SDL_SetRenderClipRect did.
        const Vector2 clipTopLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 clipBottomRight = UICanvasToScreen(ScreenResolution());
        frame.clipRect = {
            static_cast<int>(clipTopLeft.x), static_cast<int>(clipTopLeft.y),
            static_cast<int>(clipBottomRight.x - clipTopLeft.x), static_cast<int>(clipBottomRight.y - clipTopLeft.y)
        };

        const std::unordered_map<ID, const ComponentUITransform*> transformsByOwner = Content(frame, level);

        canvas.layer = Layer::Top;
        Boundary();
        CenterLines();
        SafeArea();
        SnapGuides();

        canvas.SetBlend(true);
        const ComponentUITransform* primaryTransform = nullptr;
        for (const ComponentUITransform& transform : level.ui_transforms.components) {
            const bool primary = transform.ownerID == selectedUIEntityID;
            if (primary) primaryTransform = &transform;
            EntityOutline(ComputeScreenQuad(transform), IsUIEntitySelected(transform.ownerID), primary, transform.ownerID == hoveredUIEntityID);
        }
        if (primaryTransform != nullptr) EntityGizmo(ComputeScreenQuad(*primaryTransform), *primaryTransform);
        canvas.SetBlend(false);

        BoxSelection();
        CanvasLabels(frame, level, transformsByOwner);
    }
}
