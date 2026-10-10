//
// Created by berke on 5/17/2026.
//
#include "Headers/Editor/EditorTextureCache.hpp"
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

        constexpr float gridStep = 32.0f;
        const float stepScreen = gridStep * uiCanvasZoom;
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

        constexpr float marginFraction = 0.05f;
        const Vector2 screenRes = CurrentScreenResolution();
        const Vector2 inset = {screenRes.x * marginFraction, screenRes.y * marginFraction};
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

    void DrawUIEntityGizmos(
        const UIEntityScreenQuad& quad,
        const ComponentUITransform& transform,
        const bool selected,
        const bool hovered) {
        if (!selected && !hovered) return;

        if (selected) SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
        else SDL_SetRenderDrawColor(renderer, 255, 220, 80, 160);

        for (int i = 0; i < 4; ++i)
            DrawThickLine(renderer, quad.corners[i], quad.corners[(i + 1) % 4], selected ? 2.0f : 1.5f);

        if (!selected) return; // handles/markers are for the active selection only

        // Pivot marker - where `pivot` currently sits on the (rotated) rect;
        // visual reference only, not the rotation centre (see the file
        // comment at the top for why those differ now).
        SDL_SetRenderDrawColor(renderer, 90, 220, 255, 255);
        constexpr float pivotRadius = 4.0f;
        const SDL_FRect pivotRect = {
            quad.pivotMarkerScreen.x - pivotRadius, quad.pivotMarkerScreen.y - pivotRadius,
            pivotRadius * 2.0f, pivotRadius * 2.0f
        };
        SDL_RenderFillRect(renderer, &pivotRect);

        // Anchor markers - where anchorMin/anchorMax currently sit against
        // the full current-screen canvas (independent of this entity's own
        // rotation - anchors are always axis-aligned against the canvas).
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

        // Rotation handle - offset outward from the rect's own top-centre
        // edge, along the direction from the TRUE rotation centre (not the
        // pivot marker) to that edge midpoint, so it tracks the shape
        // correctly as it rotates.
        const Vector2 topCenter = {
            (quad.corners[0].x + quad.corners[1].x) * 0.5f,
            (quad.corners[0].y + quad.corners[1].y) * 0.5f
        };
        Vector2 outward = {topCenter.x - quad.centerScreen.x, topCenter.y - quad.centerScreen.y};
        const float outwardLen = std::sqrt(outward.x * outward.x + outward.y * outward.y);
        if (outwardLen > 0.0001f) {
            outward.x /= outwardLen;
            outward.y /= outwardLen;
        }
        else outward = {0.0f, -1.0f};

        const Vector2 rotationHandlePos = {topCenter.x + outward.x * 24.0f, topCenter.y + outward.y * 24.0f};

        SDL_SetRenderDrawColor(renderer, 170, 255, 130, 255);
        DrawThickLine(renderer, topCenter, rotationHandlePos, 1.5f);
        const SDL_FRect rotationHandleRect = {
            rotationHandlePos.x - 5.0f, rotationHandlePos.y - 5.0f, 10.0f, 10.0f
        };
        SDL_RenderFillRect(renderer, &rotationHandleRect);

        // Scale handle - the rect's own bottom-right corner (index 2).
        SDL_SetRenderDrawColor(renderer, 255, 120, 220, 255);
        const SDL_FRect scaleHandleRect = {
            quad.corners[2].x - 5.0f, quad.corners[2].y - 5.0f, 10.0f, 10.0f
        };
        SDL_RenderFillRect(renderer, &scaleHandleRect);
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

        // Draw sprites.
        for (const ComponentUISprite &sprite: level.ui_sprites.components) {
            if (sprite.texture.empty()) continue;

            const auto transformIt = transformsByOwner.find(sprite.ownerID);
            if (transformIt == transformsByOwner.end()) continue;

            SDL_Texture *texture = GetEditorTexture(sprite.texture);
            if (texture == nullptr) continue;

            const UIEntityScreenQuad quad = ComputeUIEntityScreenQuad(*transformIt->second);

            // Inactive sprites aren't drawn in game; show them faded so they can still be found.
            DrawUISpriteQuad(quad, texture, SDL_FColor{1.0f, 1.0f, 1.0f, sprite.isActive ? 1.0f : 0.25f});
        }

        // Draw text.
        for (const ComponentUIText &text: level.ui_texts.components) {
            const auto transformIt = transformsByOwner.find(text.ownerID);

            if (transformIt == transformsByOwner.end()) continue;

            DrawUITextEntity(text, *transformIt->second);
        }

        // Draw transform gizmos last.
        for (const ComponentUITransform &transform: level.ui_transforms.components) {
            const UIEntityScreenQuad quad = ComputeUIEntityScreenQuad(transform);

            DrawUIEntityGizmos(quad, transform, transform.ownerID == selectedUIEntityID, transform.ownerID == hoveredUIEntityID);
        }

        SDL_SetRenderClipRect(renderer, nullptr);

        // Overlays drawn last (on top of entity content) so they stay
        // visible regardless of what's rendered underneath.
        DrawUICanvasBoundary();
        DrawUICanvasCenterLines();
        DrawUICanvasSafeArea();
    }
} // namespace MapEditorInternal