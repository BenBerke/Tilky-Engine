//
// Created by berke on 5/17/2026.
//
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Editor/EditorInternal.hpp"
#include "Headers/UISystem.hpp"
#include "Headers/Objects/Entity.hpp"
// ASSUMPTION: same as UIEditorUI.cpp - adjust to wherever ComponentUIText /
// ComponentUISprite / ComponentUITransform actually live in your project.
#include "Headers/Objects/Components.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <string>
#include <unordered_map>

/// This script is responsible for drawing the UI objects onto the screen.
/// It should not change any values. Only read and draw.
///
/// Renders full-screen via direct SDL calls, the same way DrawGridDots() /
/// DrawDots() / DrawWalls() / DrawExistingSectors() / DrawEntities() render
/// the Map Editor's view in MapEditorDrawing.cpp.
///
/// Position/rotation math below is ported from UI_vs.glsl (the real runtime
/// vertex shader, via OpenGLUI.cpp's DrawUIRectangle): uPosition is the
/// rect's TOP-LEFT corner, not a pivot-relative point, and rotation is
/// always applied to the quad's local geometry BEFORE it's offset/scaled
/// from that top-left corner - i.e. always around the rect's own centre,
/// never around `pivot`. `pivot` only affects how the UI layout system
/// computed resolvedPosition in the first place (which point of the
/// element lands on the anchor); by the time resolvedPosition reaches
/// here, that's already resolved into a plain top-left corner, same as
/// what gets handed to DrawUIRectangle's `position` argument at runtime.
/// There's also no separate design/reference resolution: UI_vs.glsl's
/// uScreenSize is the actual current screen size, so the canvas boundary
/// here is always exactly screenWidth x screenHeight, not a configurable
/// value.
///
/// resolvedPosition/resolvedSize are computed by UISystem before the editor
/// reads them, using the same screen dimensions as the runtime renderer.

namespace {
    using namespace MapEditorInternal;

    Vector2 CurrentScreenResolution() {
        return {static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    }

    // ---------------------------------------------------------------------
    // Canvas chrome - checkerboard / grid / boundary / centre-lines / safe
    // area, bounded to the current screen rect (0,0)-(screenWidth,screenHeight).
    // ---------------------------------------------------------------------

    void DrawUICanvasCheckerboard() {
        const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 bottomRight = UICanvasToScreen(CurrentScreenResolution());

        constexpr float cell = 16.0f;
        const float cellScreen = cell * uiCanvasZoom;

        if (cellScreen > 3.0f) {
            int col = 0;
            for (float x = topLeft.x; x < bottomRight.x; x += cellScreen, ++col) {
                int row = 0;
                for (float y = topLeft.y; y < bottomRight.y; y += cellScreen, ++row) {
                    const bool dark = (col + row) % 2 == 0;
                    if (dark) SDL_SetRenderDrawColor(renderer, 54, 54, 60, 255);
                    else SDL_SetRenderDrawColor(renderer, 70, 70, 76, 255);

                    const SDL_FRect cellRect = {
                        x, y,
                        std::min(cellScreen, bottomRight.x - x),
                        std::min(cellScreen, bottomRight.y - y)
                    };
                    SDL_RenderFillRect(renderer, &cellRect);
                }
            }
        }
        else {
            SDL_SetRenderDrawColor(renderer, 62, 62, 68, 255);
            const SDL_FRect wholeRect = {
                topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y
            };
            SDL_RenderFillRect(renderer, &wholeRect);
        }
    }

    void DrawUICanvasGrid() {
        if (!showUIGrid) return;

        const float stepScreen = UI_CANVAS_GRID_STEP * uiCanvasZoom;
        if (stepScreen < 4.0f) return;

        const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 bottomRight = UICanvasToScreen(CurrentScreenResolution());

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 22);

        for (float x = topLeft.x; x <= bottomRight.x; x += stepScreen)
            SDL_RenderLine(renderer, x, topLeft.y, x, bottomRight.y);
        for (float y = topLeft.y; y <= bottomRight.y; y += stepScreen)
            SDL_RenderLine(renderer, topLeft.x, y, bottomRight.x, y);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawUICanvasBoundary() {
        const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 bottomRight = UICanvasToScreen(CurrentScreenResolution());

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
        const SDL_FRect boundary = {
            topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y
        };
        SDL_RenderRect(renderer, &boundary);
    }

    void DrawUICanvasCenterLines() {
        if (!showUICenterLines) return;

        const Vector2 screenRes = CurrentScreenResolution();
        const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 bottomRight = UICanvasToScreen(screenRes);
        const Vector2 center = UICanvasToScreen({screenRes.x * 0.5f, screenRes.y * 0.5f});

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 210, 90, 130);
        SDL_RenderLine(renderer, center.x, topLeft.y, center.x, bottomRight.y);
        SDL_RenderLine(renderer, topLeft.x, center.y, bottomRight.x, center.y);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawDashedLine(const Vector2& a, const Vector2& b) {
        constexpr float dash = 6.0f, gap = 4.0f;
        const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        if (length < 0.01f) return;

        const Vector2 dir = {(b.x - a.x) / length, (b.y - a.y) / length};
        for (float t = 0.0f; t < length; t += dash + gap) {
            const float segEnd = std::min(t + dash, length);
            SDL_RenderLine(renderer, a.x + dir.x * t, a.y + dir.y * t,
                            a.x + dir.x * segEnd, a.y + dir.y * segEnd);
        }
    }

    void DrawUICanvasSafeArea() {
        if (!showUISafeArea) return;

        const Vector2 screenRes = CurrentScreenResolution();
        const Vector2 inset = {screenRes.x * UI_SAFE_AREA_FRACTION, screenRes.y * UI_SAFE_AREA_FRACTION};
        const Vector2 topLeft = UICanvasToScreen(inset);
        const Vector2 bottomRight = UICanvasToScreen({screenRes.x - inset.x, screenRes.y - inset.y});

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 120, 220, 140, 200);
        DrawDashedLine({topLeft.x, topLeft.y}, {bottomRight.x, topLeft.y});
        DrawDashedLine({bottomRight.x, topLeft.y}, {bottomRight.x, bottomRight.y});
        DrawDashedLine({bottomRight.x, bottomRight.y}, {topLeft.x, bottomRight.y});
        DrawDashedLine({topLeft.x, bottomRight.y}, {topLeft.x, topLeft.y});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // ---------------------------------------------------------------------
    // Entity rendering + selection/hover gizmos
    // ---------------------------------------------------------------------

    // Screen-space corners of a UI entity's rect (TL, TR, BR, BL), plus its
    // rotation centre and where `pivot` currently sits - all matching
    // UI_vs.glsl's actual math (see the file comment above).
    struct UIEntityScreenQuad {
        Vector2 corners[4];
        Vector2 centerScreen;      // rotation centre - resolvedPosition + size*0.5, then rotated (invariant under its own rotation)
        Vector2 pivotMarkerScreen; // where `pivot` sits on the rect, for the visual marker only - not used for position/rotation
    };

    UIEntityScreenQuad ComputeUIEntityScreenQuad(const ComponentUITransform& transform) {
        const Vector2 size = transform.resolvedSize;

        // resolvedPosition is the TOP-LEFT corner (matches UI_vs.glsl's
        // uPosition) - the centre, which is what rotation is actually
        // applied around, is derived from it plus half the size.
        const Vector2 centerCanvas = {
            transform.resolvedPosition.x + size.x * 0.5f,
            transform.resolvedPosition.y + size.y * 0.5f
        };

        const float rad = transform.rotation * Constants::DegToRad;
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);

        const auto RotateAroundCenter = [&](const Vector2& offsetFromCenter) -> Vector2 {
            const Vector2 rotated = {
                offsetFromCenter.x * cosA - offsetFromCenter.y * sinA,
                offsetFromCenter.x * sinA + offsetFromCenter.y * cosA
            };
            return UICanvasToScreen({centerCanvas.x + rotated.x, centerCanvas.y + rotated.y});
        };

        const Vector2 localCornersFromCenter[4] = {
            {-size.x * 0.5f, -size.y * 0.5f},
            {size.x * 0.5f, -size.y * 0.5f},
            {size.x * 0.5f, size.y * 0.5f},
            {-size.x * 0.5f, size.y * 0.5f},
        };

        UIEntityScreenQuad quad;
        for (int i = 0; i < 4; ++i) quad.corners[i] = RotateAroundCenter(localCornersFromCenter[i]);

        quad.centerScreen = UICanvasToScreen(centerCanvas);

        const Vector2 pivotOffsetFromCenter = {
            transform.pivot.x * size.x - size.x * 0.5f,
            transform.pivot.y * size.y - size.y * 0.5f
        };
        quad.pivotMarkerScreen = RotateAroundCenter(pivotOffsetFromCenter);

        return quad;
    }

    void DrawUISpriteQuad(const UIEntityScreenQuad& quad, SDL_Texture* texture, const SDL_FColor tint) {
        SDL_Vertex vertices[4];
        vertices[0].position = {quad.corners[0].x, quad.corners[0].y};
        vertices[1].position = {quad.corners[1].x, quad.corners[1].y};
        vertices[2].position = {quad.corners[2].x, quad.corners[2].y};
        vertices[3].position = {quad.corners[3].x, quad.corners[3].y};

        vertices[0].tex_coord = {0.0f, 0.0f};
        vertices[1].tex_coord = {1.0f, 0.0f};
        vertices[2].tex_coord = {1.0f, 1.0f};
        vertices[3].tex_coord = {0.0f, 1.0f};

        for (SDL_Vertex& vertex : vertices) vertex.color = tint;

        constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer, texture, vertices, 4, indices, 6);
    }

    // ---------------------------------------------------------------------
    // Text preview fonts. Each UI Text is previewed in its own Font at its
    // Font Size, scaled like the game does (window height / the project's
    // UI Reference Height) and then by the canvas zoom. SDL_ttf caches
    // glyphs per size, so every (file, pixel size) pair gets its own
    // TTF_Font; the shared editor `font` is never resized.
    // ---------------------------------------------------------------------

    constexpr size_t MAX_PREVIEW_FONTS = 32;     // zooming walks through many sizes
    constexpr Uint64 FAILED_FONT_RETRY_MS = 2000; // a missing font may be imported meanwhile

    std::unordered_map<std::string, TTF_Font*> previewFonts;   // "path|size"
    std::unordered_map<std::string, Uint64> failedPreviewFonts; // path -> when it failed

    std::string PreviewFontPath(const std::string& reference) {
        const std::filesystem::path path = reference.empty()
            ? ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf")
            : ProjectManager::GetAssetsPath() / reference;
        return path.lexically_normal().generic_string();
    }

    // The font for `reference` at `pixelSize`, or the default font if it can't be opened.
    TTF_Font* GetPreviewFont(const std::string& reference, const int pixelSize) {
        const std::string path = PreviewFontPath(reference);
        const std::string key = path + "|" + std::to_string(pixelSize);

        if (const auto it = previewFonts.find(key); it != previewFonts.end()) return it->second;

        const auto fallback = [&]() { return reference.empty() ? nullptr : GetPreviewFont("", pixelSize); };

        if (const auto failed = failedPreviewFonts.find(path); failed != failedPreviewFonts.end()) {
            if (SDL_GetTicks() - failed->second < FAILED_FONT_RETRY_MS) return fallback();
            failedPreviewFonts.erase(failed);
        }

        if (previewFonts.size() >= MAX_PREVIEW_FONTS) DestroyUIPreviewFonts();

        TTF_Font* opened = TTF_OpenFont(path.c_str(), static_cast<float>(pixelSize));
        if (opened == nullptr) {
            spdlog::warn("UI Editor: could not open font '{}': {}", path, SDL_GetError());
            failedPreviewFonts[path] = SDL_GetTicks();
            return fallback();
        }

        // The game's text renderer doesn't kern either.
        TTF_SetFontKerning(opened, false);
        previewFonts.emplace(key, opened);
        return opened;
    }

    void DrawUITextEntity(const ComponentUIText& text, const ComponentUITransform& transform) {
        if (text.text.empty() || textEngine == nullptr || text.color.w <= 0.0f) return;

        const float windowScale = static_cast<float>(screenHeight) / std::max(1.0f, ProjectManager::GetUIReferenceHeight());
        const float pixelSize = text.fontSize * windowScale * uiCanvasZoom;
        if (!(pixelSize >= 0.5f)) return;

        TTF_Font* previewFont = GetPreviewFont(text.font, std::clamp(static_cast<int>(std::lround(pixelSize)), 1, 512));
        if (previewFont == nullptr) return;

        TTF_Text* renderedText = TTF_CreateText(textEngine, previewFont, text.text.c_str(), text.text.size());
        if (renderedText == nullptr) return;

        const auto channel = [](const float value) {
            return static_cast<Uint8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };
        TTF_SetTextColor(renderedText, channel(text.color.x), channel(text.color.y), channel(text.color.z), channel(text.color.w));

        const float padding = UI_TEXT_PADDING * windowScale;
        const Vector2 screenPos = UICanvasToScreen({
            transform.resolvedPosition.x + padding,
            transform.resolvedPosition.y + padding
        });

        TTF_DrawRendererText(renderedText, screenPos.x, screenPos.y);
        TTF_DestroyText(renderedText);
    }

    SDL_FColor ToFColor(const Vector3& color, const float alpha = 1.0f) {
        return {color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, alpha};
    }

    // A line `thickness` pixels wide in the current draw colour.
    // (MapEditorInternal::DrawThickLine always draws in the wall colour.)
    void DrawColoredThickLine(const Vector2& start, const Vector2& end, const float thickness) {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) return;

        const float normalX = -dy / length;
        const float normalY = dx / length;
        const int halfThickness = static_cast<int>(thickness * 0.5f);

        for (int i = -halfThickness; i <= halfThickness; ++i) {
            const float offsetX = normalX * static_cast<float>(i);
            const float offsetY = normalY * static_cast<float>(i);
            SDL_RenderLine(renderer, start.x + offsetX, start.y + offsetY, end.x + offsetX, end.y + offsetY);
        }
    }

    void DrawFilledQuad(const Vector2& center, const float half, const SDL_FColor fill, const Vector3& outline) {
        const SDL_FRect rect = {center.x - half, center.y - half, half * 2.0f, half * 2.0f};
        SDL_SetRenderDrawColorFloat(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, outline.x, outline.y, outline.z, 255);
        SDL_RenderRect(renderer, &rect);
    }

    // A thick shaft ending in a filled triangle head, in screen space.
    void DrawUIGizmoArrow(const Vector2& start, const Vector2& tip, const Vector3& color) {
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

        SDL_SetRenderDrawColor(renderer, color.x, color.y, color.z, 255);
        DrawColoredThickLine(start, headBase, SHAFT_THICKNESS);

        SDL_Vertex head[3];
        head[0].position = {tip.x, tip.y};
        head[1].position = {headBase.x - dirY * HEAD_HALF_WIDTH, headBase.y + dirX * HEAD_HALF_WIDTH};
        head[2].position = {headBase.x + dirY * HEAD_HALF_WIDTH, headBase.y - dirX * HEAD_HALF_WIDTH};
        for (SDL_Vertex& vertex : head) {
            vertex.color = ToFColor(color);
            vertex.tex_coord = {0.0f, 0.0f};
        }
        SDL_RenderGeometry(renderer, nullptr, head, 3, nullptr, 0);
    }

    // Outline of a selected (primary brighter) or hovered element.
    void DrawUIEntityOutline(const UIEntityScreenQuad& quad, const bool selected, const bool primary, const bool hovered) {
        if (!selected && !hovered) return;

        if (primary) SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
        else if (selected) SDL_SetRenderDrawColor(renderer, 80, 220, 255, 170);
        else SDL_SetRenderDrawColor(renderer, 255, 220, 80, 160);

        for (int i = 0; i < 4; ++i)
            DrawColoredThickLine(quad.corners[i], quad.corners[(i + 1) % 4], primary ? 2.0f : 1.5f);
    }

    // Pivot and anchor markers, resize/rotate handles and the move arrows of
    // the primary selection, laid out by GetUIGizmoLayout() so what's drawn
    // is what UIEditorInput.cpp picks.
    void DrawUIEntityGizmo(const UIEntityScreenQuad& quad, const ComponentUITransform& transform) {
        UIGizmoLayout layout;
        if (!GetUIGizmoLayout(&layout)) return;

        const UIGizmoPart highlighted = GetHighlightedUIGizmoPart();
        const auto colorFor = [highlighted](const UIGizmoPart part, const Vector3& color) -> Vector3 {
            return highlighted == part ? highlightedHandleColor : color;
        };

        // Pivot marker - where `pivot` currently sits on the (rotated) rect;
        // visual reference only, not the rotation centre (see the file
        // comment at the top for why those differ).
        SDL_SetRenderDrawColor(renderer, 90, 220, 255, 255);
        constexpr float pivotRadius = 3.0f;
        const SDL_FRect pivotRect = {
            quad.pivotMarkerScreen.x - pivotRadius, quad.pivotMarkerScreen.y - pivotRadius,
            pivotRadius * 2.0f, pivotRadius * 2.0f
        };
        SDL_RenderFillRect(renderer, &pivotRect);

        // Anchor markers - where anchorMin/anchorMax currently sit against
        // the full current-screen canvas (always axis-aligned against it).
        SDL_SetRenderDrawColor(renderer, 255, 150, 60, 255);
        const Vector2 screenRes = CurrentScreenResolution();
        const Vector2 anchorMinScreen = UICanvasToScreen({
            transform.anchorMin.x * screenRes.x, transform.anchorMin.y * screenRes.y
        });
        const Vector2 anchorMaxScreen = UICanvasToScreen({
            transform.anchorMax.x * screenRes.x, transform.anchorMax.y * screenRes.y
        });
        for (const Vector2& anchorPoint : {anchorMinScreen, anchorMaxScreen}) {
            const SDL_FRect marker = {anchorPoint.x - 4.0f, anchorPoint.y - 4.0f, 8.0f, 8.0f};
            SDL_RenderRect(renderer, &marker);
        }

        // Drawn bottom to top in the reverse of the order UIEditorInput.cpp
        // picks them in, so the part on top is the one a click grabs.

        // Move arrows and the free-move square, in the map editor gizmo's colours.
        constexpr float squareHalf = 7.0f; // must match UI_GIZMO_SQUARE_HALF in UIEditorInput.cpp
        DrawUIGizmoArrow({layout.center.x + squareHalf, layout.center.y}, layout.xTip, colorFor(UI_GIZMO_MOVE_X, gizmoXColor));
        DrawUIGizmoArrow({layout.center.x, layout.center.y + squareHalf}, layout.yTip, colorFor(UI_GIZMO_MOVE_Y, gizmoZColor));

        const auto drawMoveSquare = [&] {
            DrawFilledQuad(layout.center, squareHalf, ToFColor(colorFor(UI_GIZMO_MOVE_FREE, gizmoYColor)), handleOutlineColor);
        };
        if (!layout.moveSquareOnTop) drawMoveSquare();

        // Resize handles.
        for (int i = 0; i < 8; ++i) {
            if (!layout.resizeShown[i]) continue;
            const auto part = static_cast<UIGizmoPart>(UI_GIZMO_RESIZE_FIRST + i);
            DrawFilledQuad(layout.resizeHandles[i], 4.5f, ToFColor(colorFor(part, Vector3{245, 245, 245})), handleOutlineColor);
        }

        if (layout.moveSquareOnTop) drawMoveSquare();

        // Rotation handle, out from the middle of the top edge.
        const Vector3 rotateColor = colorFor(UI_GIZMO_ROTATE, Vector3{170, 255, 130});
        SDL_SetRenderDrawColor(renderer, rotateColor.x, rotateColor.y, rotateColor.z, 255);
        DrawColoredThickLine(layout.rotateStem, layout.rotateHandle, 2.0f);
        DrawFilledQuad(layout.rotateHandle, 5.0f, ToFColor(rotateColor), handleOutlineColor);
    }

    // Guides for the snaps the current drag landed on, across the whole window.
    void DrawUISnapGuides() {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 80, 200, 220);

        for (const UISnapGuide& guide : GetUISnapGuides()) {
            if (guide.vertical) {
                const float x = UICanvasToScreen({guide.canvasValue, 0.0f}).x;
                SDL_RenderLine(renderer, x, 0.0f, x, static_cast<float>(screenHeight));
            }
            else {
                const float y = UICanvasToScreen({0.0f, guide.canvasValue}).y;
                SDL_RenderLine(renderer, 0.0f, y, static_cast<float>(screenWidth), y);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawUIBoxSelection() {
        Vector2 start, end;
        if (!GetUIBoxSelection(&start, &end)) return;

        const SDL_FRect rect = {
            std::min(start.x, end.x), std::min(start.y, end.y), std::fabs(end.x - start.x), std::fabs(end.y - start.y)
        };

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 80, 160, 255, 40);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 80, 160, 255, 200);
        SDL_RenderRect(renderer, &rect);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // A small tag of text with a dark backdrop, its top-left at `screenPos`
    // (kept inside the window).
    void DrawCanvasTag(const std::string& text, Vector2 screenPos) {
        if (font == nullptr || textEngine == nullptr || text.empty()) return;

        TTF_Text* renderedText = TTF_CreateText(textEngine, font, text.c_str(), 0);
        if (renderedText == nullptr) return;

        int textWidth = 0, textHeight = 0;
        TTF_GetTextSize(renderedText, &textWidth, &textHeight);

        constexpr float padding = 4.0f;
        const float tagWidth = static_cast<float>(textWidth) + padding * 2.0f;
        const float tagHeight = static_cast<float>(textHeight) + padding * 2.0f;
        screenPos.x = std::clamp(screenPos.x, 2.0f, std::max(2.0f, static_cast<float>(screenWidth) - tagWidth - 2.0f));
        screenPos.y = std::clamp(screenPos.y, 2.0f, std::max(2.0f, static_cast<float>(screenHeight) - tagHeight - 2.0f));

        const SDL_FRect backdrop = {screenPos.x, screenPos.y, tagWidth, tagHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 20, 20, 26, 210);
        SDL_RenderFillRect(renderer, &backdrop);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        TTF_SetTextColor(renderedText, 235, 235, 240, 255);
        TTF_DrawRendererText(renderedText, screenPos.x + padding, screenPos.y + padding);
        TTF_DestroyText(renderedText);
    }

    // The hovered element's name above it, and the drag's readout by the cursor.
    void DrawUICanvasLabels(Level& level, const std::unordered_map<ID, const ComponentUITransform*>& transformsByOwner) {
        const std::string& readout = GetUIDragReadout();
        if (!readout.empty()) {
            const Vector2 mouse = InputManager::GetMousePosition();
            DrawCanvasTag(readout, {mouse.x + 16.0f, mouse.y + 16.0f});
            return;
        }

        if (hoveredUIEntityID == INVALID_ID || GetHighlightedUIGizmoPart() != UI_GIZMO_NONE) return;

        const Entity* entity = level.GetEntity(hoveredUIEntityID);
        const auto transformIt = transformsByOwner.find(hoveredUIEntityID);
        if (entity == nullptr || transformIt == transformsByOwner.end()) return;

        const UIEntityScreenQuad quad = ComputeUIEntityScreenQuad(*transformIt->second);
        float minX = quad.corners[0].x, minY = quad.corners[0].y;
        for (const Vector2& corner : quad.corners) {
            minX = std::min(minX, corner.x);
            minY = std::min(minY, corner.y);
        }

        const std::string name = entity->name.empty() ? "#" + std::to_string(entity->id) : entity->name;
        DrawCanvasTag(name, {minX, minY - 26.0f});
    }
}

namespace MapEditorInternal {
    void DestroyUIPreviewFonts() {
        for (TTF_Font* previewFont : previewFonts | std::views::values) TTF_CloseFont(previewFont);
        previewFonts.clear();
    }

    void UIEditorDraw() {
        Level& level = LevelManager::CurrentLevel();

        // Resolve against the same dimensions sent to uScreenSize at runtime.
        // This also makes inspector changes visible immediately instead of
        // drawing stale resolvedPosition/resolvedSize values.
        UISystem::UpdateAllTransforms(
            level,
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight)
        );

        DrawUICanvasCheckerboard();
        DrawUICanvasGrid();

        // Clip entity rendering to the current-screen boundary (not
        // whatever the editor's own viewport happens to show at the
        // current zoom/pan), so an element positioned outside the actual
        // screen doesn't bleed across the rest of the editor view.
        const Vector2 clipTopLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 clipBottomRight = UICanvasToScreen(CurrentScreenResolution());
        const SDL_Rect clipRect = {
            static_cast<int>(clipTopLeft.x), static_cast<int>(clipTopLeft.y),
            static_cast<int>(clipBottomRight.x - clipTopLeft.x), static_cast<int>(clipBottomRight.y - clipTopLeft.y)
        };
        SDL_SetRenderClipRect(renderer, &clipRect);

        std::unordered_map<ID, const ComponentUITransform *> transformsByOwner;
        transformsByOwner.reserve(level.ui_transforms.components.size());

        for (const ComponentUITransform &transform:
             level.ui_transforms.components) {
            transformsByOwner.emplace(transform.ownerID, &transform);
        }

        // Draw sprites. One without a texture is a solid rectangle of its colour.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const ComponentUISprite &sprite: level.ui_sprites.components) {
            const auto transformIt = transformsByOwner.find(sprite.ownerID);
            if (transformIt == transformsByOwner.end()) continue;

            SDL_Texture *texture = sprite.texture.empty() ? nullptr : GetEditorTexture(sprite.texture);
            if (!sprite.texture.empty() && texture == nullptr) continue;

            const UIEntityScreenQuad quad = ComputeUIEntityScreenQuad(*transformIt->second);

            // Inactive sprites aren't drawn in game; show them faded so they can still be found.
            const SDL_FColor tint = {
                std::clamp(sprite.color.x, 0.0f, 1.0f), std::clamp(sprite.color.y, 0.0f, 1.0f),
                std::clamp(sprite.color.z, 0.0f, 1.0f), std::clamp(sprite.color.w, 0.0f, 1.0f) * (sprite.isActive ? 1.0f : 0.25f)
            };
            DrawUISpriteQuad(quad, texture, tint);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        // Draw text.
        for (const ComponentUIText &text: level.ui_texts.components) {
            const auto transformIt = transformsByOwner.find(text.ownerID);

            if (transformIt == transformsByOwner.end()) continue;

            DrawUITextEntity(text, *transformIt->second);
        }

        SDL_SetRenderClipRect(renderer, nullptr);

        // Overlays drawn last (on top of entity content) so they stay
        // visible regardless of what's rendered underneath.
        DrawUICanvasBoundary();
        DrawUICanvasCenterLines();
        DrawUICanvasSafeArea();
        DrawUISnapGuides();

        // Outlines and the gizmo aren't clipped, so handles on an element at
        // the screen's edge can still be grabbed.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        const ComponentUITransform* primaryTransform = nullptr;
        for (const ComponentUITransform &transform: level.ui_transforms.components) {
            const bool primary = transform.ownerID == selectedUIEntityID;
            if (primary) primaryTransform = &transform;

            DrawUIEntityOutline(ComputeUIEntityScreenQuad(transform), IsUIEntitySelected(transform.ownerID), primary,
                                transform.ownerID == hoveredUIEntityID);
        }
        if (primaryTransform != nullptr) DrawUIEntityGizmo(ComputeUIEntityScreenQuad(*primaryTransform), *primaryTransform);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        DrawUIBoxSelection();
        DrawUICanvasLabels(level, transformsByOwner);
    }
} // namespace MapEditorInternal