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

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

/// This script is responsible for drawing the UI objects onto the screen.
/// It should not change any values. Only read and draw.
///
/// Renders full-screen via direct SDL calls, the same way DrawGridDots() /
/// DrawDots() / DrawWalls() / DrawExistingSectors() / DrawEntities() render
/// the Map Editor's view in MapEditorDrawing.cpp - there is no bounded
/// ImGui child panel here, so this mirrors that file's structure closely:
/// a handful of focused Draw*() functions, called in back-to-front order
/// from UIEditorDraw() below, each doing its own SDL_SetRenderDrawColor +
/// immediate-mode SDL calls.
///
/// resolvedPosition/resolvedSize are read, never computed, here - per spec
/// that belongs to the UI layout system (UISystem.hpp, already included by
/// the original stub). See the ASSUMPTION note inside UIEditorDraw() below.

namespace {
    using namespace MapEditorInternal;

    constexpr float UI_DRAW_PI = 3.14159265358979323846f;

    // ---------------------------------------------------------------------
    // Canvas chrome - checkerboard / grid / boundary / centre-lines / safe
    // area, bounded to the uiTargetResolution rect (not the whole screen).
    // ---------------------------------------------------------------------

    void DrawUICanvasCheckerboard() {
        const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 bottomRight = UICanvasToScreen(uiTargetResolution);

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
        } else {
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
        const Vector2 bottomRight = UICanvasToScreen(uiTargetResolution);

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
        const Vector2 bottomRight = UICanvasToScreen(uiTargetResolution);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
        const SDL_FRect boundary = {
            topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y
        };
        SDL_RenderRect(renderer, &boundary);
    }

    void DrawUICanvasCenterLines() {
        if (!showUICenterLines) return;

        const Vector2 topLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 bottomRight = UICanvasToScreen(uiTargetResolution);
        const Vector2 center = UICanvasToScreen({uiTargetResolution.x * 0.5f, uiTargetResolution.y * 0.5f});

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

        // ASSUMPTION: no device safe-area API was available in the reference
        // material, so this approximates it as a flat 5% inset margin. Wire
        // this up to real per-device insets if your engine has them.
        constexpr float marginFraction = 0.05f;
        const Vector2 inset = {uiTargetResolution.x * marginFraction, uiTargetResolution.y * marginFraction};
        const Vector2 topLeft = UICanvasToScreen(inset);
        const Vector2 bottomRight = UICanvasToScreen({uiTargetResolution.x - inset.x, uiTargetResolution.y - inset.y});

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

    // Screen-space corners of a UI entity's rect, honouring pivot/rotation/
    // scale (but not anchors directly - those already fed into
    // resolvedPosition/resolvedSize upstream).
    struct UIEntityScreenQuad {
        Vector2 corners[4]; // TL, TR, BR, BL
        Vector2 pivotScreen;
    };

    UIEntityScreenQuad ComputeUIEntityScreenQuad(const ComponentUITransform& transform) {
        const Vector2 size = {
            transform.resolvedSize.x * transform.scale.x,
            transform.resolvedSize.y * transform.scale.y
        };

        const float rad = transform.rotation * (UI_DRAW_PI / 180.0f);
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);

        const Vector2 localCorners[4] = {
            {-transform.pivot.x * size.x, -transform.pivot.y * size.y},
            {(1.0f - transform.pivot.x) * size.x, -transform.pivot.y * size.y},
            {(1.0f - transform.pivot.x) * size.x, (1.0f - transform.pivot.y) * size.y},
            {-transform.pivot.x * size.x, (1.0f - transform.pivot.y) * size.y},
        };

        UIEntityScreenQuad quad;
        for (int i = 0; i < 4; ++i) {
            const Vector2 rotated = {
                localCorners[i].x * cosA - localCorners[i].y * sinA,
                localCorners[i].x * sinA + localCorners[i].y * cosA
            };
            quad.corners[i] = UICanvasToScreen({
                transform.resolvedPosition.x + rotated.x,
                transform.resolvedPosition.y + rotated.y
            });
        }
        quad.pivotScreen = UICanvasToScreen(transform.resolvedPosition);
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

    void DrawUITextEntity(const ComponentUIText& text, const ComponentUITransform& transform) {
        if (text.text.empty() || textEngine == nullptr || font == nullptr) return;

        // ASSUMPTION: SDL3_ttf's TTF_Text object API (TTF_CreateText /
        // TTF_SetTextColor / TTF_DrawRendererText / TTF_DestroyText), matching
        // the already-declared `textEngine`/`font` externs. This renders
        // axis-aligned only - SDL_ttf's renderer-text draw doesn't take a
        // rotation angle the way SDL_RenderTextureRotated does for sprites,
        // so `rotation`/`scale` aren't applied to text, only `position`
        // (adjusted for pivot). Also uncached (creates/destroys a TTF_Text
        // per entity per frame) - fine for a moderate entity count, but
        // worth caching per-entity if this becomes a hot path for you.
        TTF_Text* renderedText = TTF_CreateText(textEngine, font, text.text.c_str(), text.text.size());
        if (renderedText == nullptr) return;

        TTF_SetTextColor(renderedText, 255, 255, 255, 255);

        const Vector2 topLeftCanvas = {
            transform.resolvedPosition.x - transform.pivot.x * transform.resolvedSize.x,
            transform.resolvedPosition.y - transform.pivot.y * transform.resolvedSize.y
        };
        const Vector2 screenPos = UICanvasToScreen(topLeftCanvas);

        TTF_DrawRendererText(renderedText, screenPos.x, screenPos.y);
        TTF_DestroyText(renderedText);
    }

    void DrawUIEntityGizmos(const UIEntityScreenQuad& quad, const ComponentUITransform& transform,
                             const bool selected, const bool hovered) {
        if (!selected && !hovered) return;

        if (selected) SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
        else SDL_SetRenderDrawColor(renderer, 255, 220, 80, 160);

        for (int i = 0; i < 4; ++i)
            DrawThickLine(renderer, quad.corners[i], quad.corners[(i + 1) % 4], selected ? 2.0f : 1.5f);

        if (!selected) return; // handles/markers are for the active selection only

        // Pivot marker
        SDL_SetRenderDrawColor(renderer, 90, 220, 255, 255);
        constexpr float pivotRadius = 4.0f;
        const SDL_FRect pivotRect = {
            quad.pivotScreen.x - pivotRadius, quad.pivotScreen.y - pivotRadius,
            pivotRadius * 2.0f, pivotRadius * 2.0f
        };
        SDL_RenderFillRect(renderer, &pivotRect);

        // Anchor markers - where anchorMin/anchorMax currently sit against the
        // full target-resolution canvas (independent of this entity's own
        // rotation - anchors are always axis-aligned against the canvas).
        SDL_SetRenderDrawColor(renderer, 255, 150, 60, 255);
        const Vector2 anchorMinScreen = UICanvasToScreen({
            transform.anchorMin.x * uiTargetResolution.x, transform.anchorMin.y * uiTargetResolution.y
        });
        const Vector2 anchorMaxScreen = UICanvasToScreen({
            transform.anchorMax.x * uiTargetResolution.x, transform.anchorMax.y * uiTargetResolution.y
        });
        for (const Vector2& anchorPoint : {anchorMinScreen, anchorMaxScreen}) {
            const SDL_FRect marker = {anchorPoint.x - 4.0f, anchorPoint.y - 4.0f, 8.0f, 8.0f};
            SDL_RenderRect(renderer, &marker);
        }

        // Rotation handle - offset outward from the rect's own top-centre
        // edge, along that edge's outward normal, so it tracks the shape as
        // it rotates.
        const Vector2 topCenter = {
            (quad.corners[0].x + quad.corners[1].x) * 0.5f,
            (quad.corners[0].y + quad.corners[1].y) * 0.5f
        };
        Vector2 outward = {topCenter.x - quad.pivotScreen.x, topCenter.y - quad.pivotScreen.y};
        const float outwardLen = std::sqrt(outward.x * outward.x + outward.y * outward.y);
        if (outwardLen > 0.0001f) {
            outward.x /= outwardLen;
            outward.y /= outwardLen;
        } else {
            outward = {0.0f, -1.0f};
        }
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
    void UIEditorDraw() {
        Level& level = LevelManager::CurrentLevel();

        // ASSUMPTION: something keeps ComponentUITransform::resolvedPosition/
        // resolvedSize current against uiTargetResolution every frame (the UI
        // layout system, presumably reachable through UISystem.hpp above).
        // Nothing in this file computes those values itself - per spec they
        // belong to that system, not the editor. If yours only resolves
        // layout on an explicit pass rather than continuously, trigger that
        // pass here first, e.g.:
        //     UISystem::ResolveLayout(level, uiTargetResolution);

        DrawUICanvasCheckerboard();
        DrawUICanvasGrid();

        // Clip entity rendering to the target-resolution boundary (not the
        // whole screen), so an element positioned outside its own canvas
        // doesn't bleed across the rest of the editor view.
        // ASSUMPTION: SDL3's SDL_SetRenderClipRect takes an integer SDL_Rect*
        // (clip rects stayed int-based in SDL3 even where most render calls
        // moved to float SDL_FRect); adjust if your SDL3 revision differs.
        const Vector2 clipTopLeft = UICanvasToScreen({0.0f, 0.0f});
        const Vector2 clipBottomRight = UICanvasToScreen(uiTargetResolution);
        const SDL_Rect clipRect = {
            static_cast<int>(clipTopLeft.x), static_cast<int>(clipTopLeft.y),
            static_cast<int>(clipBottomRight.x - clipTopLeft.x), static_cast<int>(clipBottomRight.y - clipTopLeft.y)
        };
        SDL_SetRenderClipRect(renderer, &clipRect);

        for (Entity& entity : level.entities) {
            ComponentUITransform* transform = entity.GetComponent<ComponentUITransform>();
            if (transform == nullptr) continue;

            const UIEntityScreenQuad quad = ComputeUIEntityScreenQuad(*transform);

            if (ComponentUISprite* sprite = entity.GetComponent<ComponentUISprite>()) {
                if (!sprite->texture.empty()) {
                    if (SDL_Texture* texture = GetEditorTexture(sprite->texture))
                        DrawUISpriteQuad(quad, texture, SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});
                }
            }

            if (ComponentUIText* text = entity.GetComponent<ComponentUIText>())
                DrawUITextEntity(*text, *transform);

            DrawUIEntityGizmos(quad, *transform, entity.id == selectedUIEntityID, entity.id == hoveredUIEntityID);
        }

        SDL_SetRenderClipRect(renderer, nullptr);

        // Overlays drawn last (on top of entity content) so they stay
        // visible regardless of what's rendered underneath.
        DrawUICanvasBoundary();
        DrawUICanvasCenterLines();
        DrawUICanvasSafeArea();
    }
} // namespace MapEditorInternal