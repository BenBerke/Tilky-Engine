#include "../EditorInternal.hpp"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp" // This includes "SSECompat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <spdlog/spdlog.h>

#include "Headers/Math/Geometry/Geometry.hpp"

namespace {
    // UTILITY
    static SDL_FColor ToFColor(const Vector3 &color,const float alpha = 1.0f) {
        return {color.r / 255.0f,color.g / 255.0f,color.b / 255.0f,alpha};
    }

    static SDL_FColor ToFColor(const Vector4 &color) {
        return {color.r / 255.0f,color.g / 255.0f,color.b / 255.0f,color.a / 255.0f};
    }
}

namespace MapEditorInternal {
    void DrawThickLine(SDL_Renderer* renderer, const Vector2 start, const Vector2 end, const float thickness) {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;

        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) return;


        const float normalX = -dy / length;
        const float normalY = dx / length;

        const int halfThickness = static_cast<int>(thickness * 0.5f);

        // walls should the be the opposite of the theme
        // if (currentTheme == THEME_DARK)
        // SDL_SetRenderDrawColor(renderer, 205, 205, 205, 255);
        // else if (currentTheme == THEME_LIGHT)
        //     SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);

        SDL_SetRenderDrawColor(renderer, normalWallColor.r, normalWallColor.g, normalWallColor.b, 255);

        for (int i = -halfThickness; i <= halfThickness; ++i) {
            const float offsetX = normalX * static_cast<float>(i);
            const float offsetY = normalY * static_cast<float>(i);

            SDL_RenderLine(renderer, start.x + offsetX, start.y + offsetY, end.x + offsetX, end.y + offsetY);
        }
    }

    void DrawFilledTriangle(const Triangle& triangle, const SDL_FColor color) {
        const Vector2 a = WorldToScreen(triangle.a, cameraPos);
        const Vector2 b = WorldToScreen(triangle.b, cameraPos);
        const Vector2 c = WorldToScreen(triangle.c, cameraPos);

        SDL_Vertex vertices[3];

        vertices[0].position = {a.x, a.y};
        vertices[0].color = color;
        vertices[0].tex_coord = {0.0f, 0.0f};

        vertices[1].position = {b.x, b.y};
        vertices[1].color = color;
        vertices[1].tex_coord = {0.0f, 0.0f};

        vertices[2].position = {c.x, c.y};
        vertices[2].color = color;
        vertices[2].tex_coord = {0.0f, 0.0f};

        SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0);
    }

    void DrawFilledTriangleTextured(const Triangle& triangle, SDL_Texture* texture, const SDL_FColor tint) {
        if (texture == nullptr) return;

        constexpr float uvScale = 1.0f / 64.0f;

        const Vector2 a = WorldToScreen(triangle.a, cameraPos);
        const Vector2 b = WorldToScreen(triangle.b, cameraPos);
        const Vector2 c = WorldToScreen(triangle.c, cameraPos);

        SDL_Vertex vertices[3];

        vertices[0].position = {a.x, a.y};
        vertices[0].color = tint;
        vertices[0].tex_coord = {triangle.a.x * uvScale, triangle.a.y * uvScale};

        vertices[1].position = {b.x, b.y};
        vertices[1].color = tint;
        vertices[1].tex_coord = {triangle.b.x * uvScale, triangle.b.y * uvScale};

        vertices[2].position = {c.x, c.y};
        vertices[2].color = tint;
        vertices[2].tex_coord = {triangle.c.x * uvScale, triangle.c.y * uvScale};

        SDL_RenderGeometry(renderer, texture, vertices, 3, nullptr, 0);
    }

    void DrawSnapIndicator() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        const bool freehandDrawing = currentMode == MODE_SECTOR && currentDrawTool == DRAWTOOL_FREEHAND && !manualSectorMode;

        const Vector2 snapped = freehandDrawing ? ResolveFreehandPoint(mouseWorld) : ResolveSnapPoint(mouseWorld);
        const Vector2 screenPos = WorldToScreen(snapped, cameraPos);

        SDL_SetRenderDrawColor(renderer, 80, snapIndicatorColor.r, snapIndicatorColor.g, snapIndicatorColor.b);

        constexpr float radius = 5.0f;

        const SDL_FRect ring = {
            screenPos.x - radius,
            screenPos.y - radius,
            radius * 2.0f,
            radius * 2.0f
        };

        SDL_RenderRect(renderer, &ring);
    }

    // =========================================================================
    //  Sector Mode — drawing-tool previews
    // =========================================================================
    //
    // One function per tool renders that tool's live in-progress shape;
    // DrawSectorPreview() (bottom of this block) just picks which one
    // applies. They share a handful of outline/fill/label/anchor
    // primitives so every tool looks and behaves consistently, and they
    // reuse GetActiveDrawToolMeasurementText() (MapEditorGeometry.cpp)
    // for on-canvas labels so the floating label next to the shape and
    // the status-overlay text always agree.
    namespace {
        // Gold = "this would be accepted if you clicked/confirmed now",
        // red = "this would be rejected". Reused everywhere a shape is
        // being previewed, replacing the old freehand-only, always-red
        // fill, so every tool gives the same at-a-glance feedback.

        SDL_FColor ThemeTextColor() {
            const SDL_FColor r = {themeTextColor.r / 255.0f, themeTextColor.g / 255.0f, themeTextColor.b / 255.0f};
            return r;
        }

        // Renders `text` in world space, anchored just above `worldPos`
        // on screen, at whatever point size `font` is already configured
        // with. Deliberately never calls TTF_SetFontSize - font/textEngine
        // are shared globals, and mutating font size here could leak into
        // any other text drawn this frame.
        void DrawWorldLabel(const std::string& text, const Vector2& worldPos, const SDL_FColor color) {
            if (font == nullptr || textEngine == nullptr || text.empty()) return;

            TTF_Text* renderedText = TTF_CreateText(textEngine, font, text.c_str(), 0);
            if (renderedText == nullptr) return;

            TTF_SetTextColor(
                renderedText,
                static_cast<Uint8>(color.r * 255.0f),
                static_cast<Uint8>(color.g * 255.0f),
                static_cast<Uint8>(color.b * 255.0f),
                static_cast<Uint8>(color.a * 255.0f)
            );

            int textWidth = 0;
            int textHeight = 0;
            TTF_GetTextSize(renderedText, &textWidth, &textHeight);

            const Vector2 screenPos = WorldToScreen(worldPos, cameraPos);

            // Clamp on-screen so a label near the edge of the view never
            // renders half off the window.
            const float maxX = std::max(2.0f, screenWidth - static_cast<float>(textWidth) - 2.0f);
            const float maxY = std::max(2.0f, screenHeight - static_cast<float>(textHeight) - 2.0f);

            const float labelX = std::clamp(screenPos.x - static_cast<float>(textWidth) * 0.5f, 2.0f, maxX);
            const float labelY = std::clamp(screenPos.y - static_cast<float>(textHeight) - 8.0f, 2.0f, maxY);

            TTF_DrawRendererText(renderedText, labelX, labelY);
            TTF_DestroyText(renderedText);
        }

        void DrawEdgeLengthLabel(const Vector2& worldA, const Vector2& worldB) {
            const float length = std::sqrt(Vector2Math::DistanceSquared(worldA, worldB));

            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.1f", length);

            const Vector2 midpoint = {(worldA.x + worldB.x) * 0.5f, (worldA.y + worldB.y) * 0.5f};
            DrawWorldLabel(buffer, midpoint, ThemeTextColor());
        }

        void DrawPreviewOutline(const std::vector<Vector2>& points, const bool closeLoop, const bool valid) {
            if (points.size() < 2) return;

            const Vector3 color = valid ? kValidLineColor : kInvalidLineColor;
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

            const std::size_t segmentCount = closeLoop ? points.size() : points.size() - 1;

            for (std::size_t i = 0; i < segmentCount; ++i) {
                const Vector2 a = WorldToScreen(points[i], cameraPos);
                const Vector2 b = WorldToScreen(points[(i + 1) % points.size()], cameraPos);
                DrawThickLine(renderer, a, b, 3.0f);
            }
        }

        // `closedLoopPoints` is an open (no repeated closing point) ring,
        // matching what GetSectorVerticesWithoutClosingDuplicate()/the
        // Build*() shape generators already produce.
        void DrawPreviewFill(const std::vector<Vector2>& closedLoopPoints, const bool valid) {
            if (closedLoopPoints.size() < 3) return;

            const std::vector<Triangle> triangles = Geometry::Triangulate(closedLoopPoints);

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

            const Vector4 fillColor = valid ? kValidFillColor : kInvalidFillColor;
            const SDL_FColor fc = ToFColor(fillColor);

            for (const Triangle& triangle : triangles) DrawFilledTriangle(triangle, fc);

            for (const Triangle& triangle : triangles) DrawFilledTriangle(triangle, fc);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        void DrawSnapTargetRing(const Vector2& worldPos) {
            const Vector2 screenPos = WorldToScreen(worldPos, cameraPos);

            SDL_SetRenderDrawColor(renderer, kValidLineColor.r, kValidLineColor.g, kValidLineColor.b, kValidLineColor.a);

            constexpr float half = 8.0f;
            const SDL_FRect ring = {screenPos.x - half, screenPos.y - half, half * 2.0f, half * 2.0f};
            SDL_RenderRect(renderer, &ring);
        }

        void DrawAnchorPoint(const Vector2& worldPos) {
            const Vector2 screenPos = WorldToScreen(worldPos, cameraPos);
            SDL_SetRenderDrawColor(renderer, kAnchorColor.r, kAnchorColor.g, kAnchorColor.b, kAnchorColor.a);

            constexpr float half = 4.0f;
            const SDL_FRect rect = {screenPos.x - half, screenPos.y - half, half * 2.0f, half * 2.0f};
            SDL_RenderFillRect(renderer, &rect);
        }

        // Freehand keeps its own logic (including the manual-corners
        // sub-mode) rather than going through a Resolve+Build pair like
        // the other four tools, since it commits one point at a time via
        // TrySectorChainClick/CreateManualSector rather than a fixed
        // shape formula.
        void DrawFreehandPreview() {
            if (manualSectorMode) {
                DrawPreviewOutline(manualSectorDots, false, true);

                for (std::size_t i = 0; i + 1 < manualSectorDots.size(); ++i)
                    DrawEdgeLengthLabel(manualSectorDots[i], manualSectorDots[i + 1]);

                for (const Vector2& dot : manualSectorDots) DrawAnchorPoint(dot);

                return;
            }

            if (sectorBeingCreated.empty()) return;

            const Vector2 mouseScreen = InputManager::GetMousePosition();
            const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
            const Vector2 previewPoint = ResolveFreehandPoint(mouseWorld);

            Vector2 snapTarget{};
            const bool snappedToChain = SnapToPendingChainPoint(mouseWorld, &snapTarget);

            const bool wouldCloseLoop = snappedToChain &&
                                        sectorBeingCreated.size() >= 3 &&
                                        SamePoint(snapTarget, sectorBeingCreated.front());

            DrawPreviewOutline(sectorBeingCreated, wouldCloseLoop, true);

            for (std::size_t i = 0; i + 1 < sectorBeingCreated.size(); ++i)
                DrawEdgeLengthLabel(sectorBeingCreated[i], sectorBeingCreated[i + 1]);

            const Vector3 rubberBand = kValidLineColor;
            SDL_SetRenderDrawColor(renderer, rubberBand.r, rubberBand.g, rubberBand.b, 160);

            const Vector2 lastScreen = WorldToScreen(sectorBeingCreated.back(), cameraPos);
            const Vector2 previewScreen = WorldToScreen(previewPoint, cameraPos);
            DrawThickLine(renderer, lastScreen, previewScreen, 2.0f);
            DrawWorldLabel(GetActiveDrawToolMeasurementText(), previewPoint, ThemeTextColor());

            const std::vector<Vector2> committedLoop = GetSectorVerticesWithoutClosingDuplicate();
            const std::vector<Vector2> dedupedLoop = DedupeConsecutivePoints(committedLoop);
            const bool wouldCloseCleanly = dedupedLoop.size() >= 3 && !ClosedLoopSelfIntersects(dedupedLoop);

            DrawPreviewFill(committedLoop, wouldCloseCleanly);

            for (const Vector2& point : sectorBeingCreated) DrawAnchorPoint(point);

            if (wouldCloseLoop) DrawSnapTargetRing(snapTarget);
        }

        void DrawRectanglePreview() {
            if (!rectangleHasFirstCorner) return;

            const Vector2 mouseScreen = InputManager::GetMousePosition();
            const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
            const Vector2 opposite = ResolveRectangleCorner(mouseWorld);

            const float width = std::fabs(opposite.x - rectangleFirstCorner.x);
            const float height = std::fabs(opposite.y - rectangleFirstCorner.y);
            const bool valid = width >= MIN_DRAW_SHAPE_DIMENSION && height >= MIN_DRAW_SHAPE_DIMENSION;

            const std::vector<Vector2> corners = {
                rectangleFirstCorner,
                {opposite.x, rectangleFirstCorner.y},
                opposite,
                {rectangleFirstCorner.x, opposite.y}
            };

            DrawPreviewOutline(corners, true, valid);
            DrawPreviewFill(corners, valid);
            DrawAnchorPoint(rectangleFirstCorner);
            DrawWorldLabel(GetActiveDrawToolMeasurementText(), opposite, ThemeTextColor());
        }

        void DrawPolygonPreview() {
            if (!polygonHasCenter) return;

            const Vector2 mouseScreen = InputManager::GetMousePosition();
            const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
            const Vector2 handle = ResolvePolygonHandle(mouseWorld);

            const float radius = std::sqrt(Vector2Math::DistanceSquared(polygonCenter, handle));
            const bool valid = radius >= MIN_DRAW_SHAPE_DIMENSION;

            const std::vector<Vector2> corners = BuildRegularPolygon(polygonCenter, handle, polygonSideCount);

            DrawPreviewOutline(corners, true, valid);
            DrawPreviewFill(corners, valid);
            DrawAnchorPoint(polygonCenter);
            DrawWorldLabel(GetActiveDrawToolMeasurementText(), handle, ThemeTextColor());
        }

        void DrawCirclePreview() {
            if (!circleHasCenter) return;

            const Vector2 mouseScreen = InputManager::GetMousePosition();
            const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
            const Vector2 handle = ResolveCircleHandle(mouseWorld);

            const float radiusX = std::fabs(handle.x - circleCenter.x);
            const float radiusY = std::fabs(handle.y - circleCenter.y);
            const bool valid = radiusX >= MIN_DRAW_SHAPE_DIMENSION && radiusY >= MIN_DRAW_SHAPE_DIMENSION;

            const std::vector<Vector2> points = BuildEllipse(circleCenter, radiusX, radiusY, circleSegments);

            DrawPreviewOutline(points, true, valid);
            DrawPreviewFill(points, valid);
            DrawAnchorPoint(circleCenter);
            DrawWorldLabel(GetActiveDrawToolMeasurementText(), handle, ThemeTextColor());
        }

        void DrawCurvePreview() {
            if (curveStage == CURVE_STAGE_START) return;

            const Vector2 mouseScreen = InputManager::GetMousePosition();
            const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

            if (curveStage == CURVE_STAGE_END) {
                const Vector2 end = ResolveCurveEnd(mouseWorld);
                const bool valid = Vector2Math::DistanceSquared(curveStart, end) >=
                                    MIN_DRAW_SHAPE_DIMENSION * MIN_DRAW_SHAPE_DIMENSION;

                DrawPreviewOutline({curveStart, end}, false, valid);
                DrawAnchorPoint(curveStart);
                DrawWorldLabel(GetActiveDrawToolMeasurementText(), end, ThemeTextColor());
                return;
            }

            // CURVE_STAGE_CONTROL - the curve itself plus thin guide lines
            // out to the control handle, all in the same "valid" gold
            // (a control point can't make the curve invalid on its own).
            const Vector2 control = ResolveSnapPoint(mouseWorld);
            const std::vector<Vector2> curvePoints = BuildQuadraticCurve(curveStart, control, curveEnd, curveSubdivisions);

            DrawPreviewOutline(curvePoints, false, true);
            DrawPreviewOutline({curveStart, control}, false, true);
            DrawPreviewOutline({curveEnd, control}, false, true);
            DrawAnchorPoint(curveStart);
            DrawAnchorPoint(curveEnd);
            DrawAnchorPoint(control);
            DrawWorldLabel(GetActiveDrawToolMeasurementText(), control, ThemeTextColor());
        }

        // Simple two-stroke arrowhead from `fromWorld` to `toWorld`, used
        // to show the staircase's rising direction. Deliberately its own
        // tiny primitive rather than a reuse of DrawThickLine (which
        // forces the theme wall colour) or DrawColoredThickLine (defined
        // further down this file, after this point uses it).
        void DrawArrow(const Vector2& fromWorld, const Vector2& toWorld, const Vector3& color) {
            const Vector2 from = WorldToScreen(fromWorld, cameraPos);
            const Vector2 to = WorldToScreen(toWorld, cameraPos);

            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.5f) return;

            SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(color.r), static_cast<Uint8>(color.g), static_cast<Uint8>(color.b), 255);
            SDL_RenderLine(renderer, from.x, from.y, to.x, to.y);

            const float dirX = dx / length;
            const float dirY = dy / length;

            constexpr float headLength = 14.0f;
            constexpr float headAngle = 0.5f; // radians

            const auto rotate = [](const float x, const float y, const float angle) {
                return Vector2{x * std::cos(angle) - y * std::sin(angle), x * std::sin(angle) + y * std::cos(angle)};
            };

            const Vector2 left = rotate(-dirX, -dirY, headAngle);
            const Vector2 right = rotate(-dirX, -dirY, -headAngle);

            SDL_RenderLine(renderer, to.x, to.y, to.x + left.x * headLength, to.y + left.y * headLength);
            SDL_RenderLine(renderer, to.x, to.y, to.x + right.x * headLength, to.y + right.y * headLength);
        }

        Vector2 StepCentroid(const StaircaseStepSpan& step) {
            return {
                (step.corners[0].x + step.corners[1].x + step.corners[2].x + step.corners[3].x) * 0.25f,
                (step.corners[0].y + step.corners[1].y + step.corners[2].y + step.corners[3].y) * 0.25f
            };
        }

        void DrawStaircasePreview() {
            if (!staircaseHasFirstCorner) return;

            const Vector2 mouseScreen = InputManager::GetMousePosition();
            const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
            const Vector2 opposite = ResolveStaircaseCorner(mouseWorld);

            // The exact same plan a click would commit right now - the
            // preview can never disagree with what confirming produces.
            const StaircasePlan plan = BuildStaircasePlan(staircaseFirstCorner, opposite);

            const std::vector<Vector2> outerCorners = {
                staircaseFirstCorner,
                {opposite.x, staircaseFirstCorner.y},
                opposite,
                {staircaseFirstCorner.x, opposite.y}
            };

            DrawPreviewOutline(outerCorners, true, plan.valid);
            DrawAnchorPoint(staircaseFirstCorner);

            if (!plan.valid) {
                DrawWorldLabel(plan.error, opposite, {0.95f, 0.35f, 0.35f, 1.0f});
                return;
            }

            for (const StaircaseStepSpan& step : plan.steps) {
                const std::vector<Vector2> quad = {step.corners[0], step.corners[1], step.corners[2], step.corners[3]};

                DrawPreviewOutline(quad, true, true);
                DrawPreviewFill(quad, true);

                char label[64];
                std::snprintf(label, sizeof(label), "#%d  h:%.1f", step.index, step.floorHeight);
                DrawWorldLabel(label, StepCentroid(step), ThemeTextColor());
            }

            // Rising-direction arrow: bottom step's centre -> top step's.
            if (plan.steps.size() >= 2) {
                const StaircaseStepSpan* bottom = &plan.steps.front();
                const StaircaseStepSpan* top = &plan.steps.front();

                for (const StaircaseStepSpan& step : plan.steps) {
                    if (step.index == 0) bottom = &step;
                    if (step.index == plan.stepCount - 1) top = &step;
                }

                DrawArrow(StepCentroid(*bottom), StepCentroid(*top), kAnchorColor);
            }

            DrawWorldLabel(GetActiveDrawToolMeasurementText(), opposite, ThemeTextColor());

            if (plan.headroomWarning)
                DrawWorldLabel(Localisation::Get("editor.staircase.warning.headroom"), staircaseFirstCorner, {1.0f, 0.65f, 0.15f, 1.0f});

            if (plan.targetSingleSectorNote)
                DrawWorldLabel(Localisation::Get("editor.staircase.notice.target_single_step"), staircaseFirstCorner, {1.0f, 0.8f, 0.2f, 1.0f});
        }

        // Small always-on-top HUD: active tool, grid size/snap state, and
        // that tool's live measurement text - so none of the above is
        // only discoverable by already knowing it's there.
        void DrawStatusOverlay() {
            if (font == nullptr || textEngine == nullptr) return;

            // .c_str() on named locals, not on Get()/GetActiveDrawToolName()
            // temporaries - the temporaries would be destroyed before
            // snprintf ran.
            const std::string toolName = GetActiveDrawToolName();
            const std::string gridWord = Localisation::Get("editor.draw.status.grid");
            const std::string gridSnapOff = gridSnapEnabled
                                                ? std::string()
                                                : "  " + Localisation::Get("editor.draw.status.grid_snap_off");
            const std::string pointSnapOff = vertexSnapEnabled
                                                 ? std::string()
                                                 : "  " + Localisation::Get("editor.draw.status.point_snap_off");

            char line[192];
            std::snprintf(
                line, sizeof(line), "%s   %s %.0f%s%s",
                toolName.c_str(),
                gridWord.c_str(),
                GRID_SIZE,
                gridSnapOff.c_str(),
                pointSnapOff.c_str()
            );

            const std::string measurement = GetActiveDrawToolMeasurementText();

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
            const SDL_FRect background = {8.0f, 8.0f, 360.0f, measurement.empty() ? 24.0f : 44.0f};
            SDL_RenderFillRect(renderer, &background);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            const auto drawScreenLine = [](const char* text, const float x, const float y) {
                TTF_Text* renderedText = TTF_CreateText(textEngine, font, text, 0);
                if (renderedText == nullptr) return;

                TTF_SetTextColor(renderedText, 255, 255, 255, 255);
                TTF_DrawRendererText(renderedText, x, y);
                TTF_DestroyText(renderedText);
            };

            drawScreenLine(line, 14.0f, 12.0f);
            if (!measurement.empty()) drawScreenLine(measurement.c_str(), 14.0f, 30.0f);
        }
    }

    // Dispatches to whichever tool is active. DRAWTOOL_FREEHAND (the
    // original behaviour) and the four shape tools all end up going
    // through the same commit path (ApplyDrawnGeometry, by way of
    // CommitClosedShape/CommitOpenShape in MapEditorGeometry.cpp) once
    // confirmed, so this function is purely about what's shown while a
    // shape is still in progress. The snap indicator keeps rendering in
    // every mode exactly as before, since it's just as useful for
    // precisely placing an Entity or dragging a wall endpoint as it is
    // for Sector drawing.
    void DrawSectorPreview() {
        if (currentMode == MODE_SECTOR) {
            if (IsDrawingInProgress()) {
                switch (currentDrawTool) {
                    case DRAWTOOL_FREEHAND:  DrawFreehandPreview(); break;
                    case DRAWTOOL_RECTANGLE: DrawRectanglePreview(); break;
                    case DRAWTOOL_POLYGON:   DrawPolygonPreview(); break;
                    case DRAWTOOL_CIRCLE:    DrawCirclePreview(); break;
                    case DRAWTOOL_CURVE:     DrawCurvePreview(); break;
                    case DRAWTOOL_STAIRCASE: DrawStaircasePreview(); break;
                    default: break;
                }
            }

            // this looks bad
            //DrawStatusOverlay();
        }

        DrawSnapIndicator();
    }

    void DrawExistingSectors() {
        const Level &level = LevelManager::CurrentLevel();

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        int hoveredSectorIndex = -1;

        // Hole-aware on purpose: a sector with something nested inside it
        // doesn't occupy that inner space, so hovering there has to fall
        // through to the sector that does rather than highlighting the
        // parent (the plain polygon test can't tell the two apart).
        for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i)
            if (Geometry::IsPointInPolygon(level.sectors[i].vertices, level.sectors[i].innerLoops, mouseWorld)) {
                hoveredSectorIndex = i;
                break;
            }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const auto HSVtoRGB = [](const float h, const float s, const float v) -> SDL_FColor {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;

            const int region = static_cast<int>(std::floor(h * 6.0f));
            const float fraction = h * 6.0f - static_cast<float>(region);
            const float p = v * (1.0f - s);
            const float q = v * (1.0f - fraction * s);
            const float t = v * (1.0f - (1.0f - fraction) * s);

            switch (region % 6) {
                case 0: r = v;
                    g = t;
                    b = p;
                    break;
                case 1: r = q;
                    g = v;
                    b = p;
                    break;
                case 2: r = p;
                    g = v;
                    b = t;
                    break;
                case 3: r = p;
                    g = q;
                    b = v;
                    break;
                case 4: r = t;
                    g = p;
                    b = v;
                    break;
                case 5: r = v;
                    g = p;
                    b = q;
                    break;
                default: break;
            }

            return {r, g, b, 0.55f};
        };

        const int totalSectors = static_cast<int>(level.sectors.size());

        for (int sectorIndex = 0; sectorIndex < totalSectors; ++sectorIndex) {
            const Sector &sector = level.sectors[sectorIndex];

            const float hue = std::fmod(static_cast<float>(sector.id) * 0.618033988749895f, 1.0f);

            const SDL_FColor normalSectorColor = HSVtoRGB(hue, 0.7f, 0.9f);

            SDL_FColor sectorColor = normalSectorColor;

            const bool selected = !selectedSectors.empty() &&
                std::find(selectedSectors.begin(),selectedSectors.end(),sector.id) != selectedSectors.end() &&
                editingSector &&
                currentMode == MODE_SECTOR;

            const bool hovered = sectorIndex == hoveredSectorIndex && currentMode == MODE_SECTOR;

            if (selected) sectorColor = ToFColor(highlightedSectorColor, 0.75f);
            else if (hovered) sectorColor = ToFColor(hoveredSectorColor, 0.65f);

            SDL_Texture *floorTexture = nullptr;

            if (textureViewMode && !sector.floors.empty()) {
                const std::string &textureFileName = sector.floors.front().floor.texture;

                if (!textureFileName.empty()) floorTexture = GetEditorTexture(textureFileName);
            }

            for (const Triangle& triangle : sector.triangles) {
                if (floorTexture != nullptr)
                    DrawFilledTriangleTextured(triangle,floorTexture,{1.0f, 1.0f, 1.0f, 1.0f});
                else DrawFilledTriangle(triangle, sectorColor);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        if (selectedSectorID == INVALID_ID) return;

        const Sector *selectedSector = MapQueries::GetSectorByID(level, selectedSectorID);

        if (selectedSector == nullptr || selectedSector->vertices.empty()) return;

        SDL_SetRenderDrawColor(renderer, kAnchorColor.r, kAnchorColor.g, kAnchorColor.b, 255);

        const auto outlineLoop = [](const std::vector<Vector2>& loop) {
            const int vertexCount = static_cast<int>(loop.size());

            for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                const Vector2 start = WorldToScreen(loop[vertexIndex], cameraPos);
                const Vector2 end = WorldToScreen(loop[(vertexIndex + 1) % vertexCount], cameraPos);

                DrawThickLine(renderer, start, end, 3.0f);
            }
        };

        outlineLoop(selectedSector->vertices);

        // A selected sector's inner boundaries are part of its outline
        // too - without these, selecting a sector with something nested
        // inside it highlights only its outer edge and gives no visual
        // indication of where its floor actually stops.
        for (const std::vector<Vector2>& innerLoop : selectedSector->innerLoops) outlineLoop(innerLoop);
    }
    // =========================================================================
    //  Geometry / Wall Edit Mode — canvas overlay
    // =========================================================================
    //
    // Replaced DrawDots(), which drew dots as an independent, selectable
    // object type. Dots are internal snap anchors now (see the Dot struct
    // in EditorInternal.hpp) and are not drawn at all; what this draws
    // instead is Geometry Mode's hover highlight, selection highlight and
    // endpoint handles.
    namespace {
        // DrawThickLine() deliberately forces the theme's wall colour, so
        // it can't be used for highlights - this is the same thickness
        // logic with the caller's draw colour left alone.
        void DrawColoredThickLine(const Vector2& startScreen, const Vector2& endScreen, const float thickness) {
            const float dx = endScreen.x - startScreen.x;
            const float dy = endScreen.y - startScreen.y;

            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= Constants::Epsilon) return;

            const float normalX = -dy / length;
            const float normalY = dx / length;

            const int halfThickness = static_cast<int>(thickness * 0.5f);

            for (int i = -halfThickness; i <= halfThickness; ++i) {
                const float offsetX = normalX * static_cast<float>(i);
                const float offsetY = normalY * static_cast<float>(i);

                SDL_RenderLine(
                    renderer,
                    startScreen.x + offsetX, startScreen.y + offsetY,
                    endScreen.x + offsetX, endScreen.y + offsetY
                );
            }
        }

        // Fixed *pixel* size, like the picking tolerance it mirrors: a
        // handle has to stay grabbable at every zoom level.
        void DrawEndpointHandle(const Vector2& worldPos, const bool hovered) {
            const Vector2 screenPos = WorldToScreen(worldPos, cameraPos);

            const float halfSize = hovered ? 6.0f : 4.5f;

            const SDL_FRect handle = {
                screenPos.x - halfSize,
                screenPos.y - halfSize,
                halfSize * 2.0f,
                halfSize * 2.0f
            };

            if (hovered) SDL_SetRenderDrawColor(renderer, highlightedHandleColor.r, highlightedHandleColor.g, highlightedHandleColor.b, 255);
            else SDL_SetRenderDrawColor(renderer, normalHandleColor.r, normalHandleColor.g, normalHandleColor.b, 255);

            SDL_RenderFillRect(renderer, &handle);

            SDL_SetRenderDrawColor(renderer, handleOutlineColor.r, handleOutlineColor.g, handleOutlineColor.b, 255);
            SDL_RenderRect(renderer, &handle);
        }
    }

    void DrawGeometryEditOverlay() {
        if (currentMode != MODE_GEOMETRY) return;

        const Level& level = LevelManager::CurrentLevel();

        const auto findWall = [&](const ID wallID) -> const Wall* {
            const auto it = level.wallIDToIndex.find(wallID);
            if (it == level.wallIDToIndex.end()) return nullptr;
            if (it->second < 0 || it->second >= static_cast<int>(level.walls.size())) return nullptr;

            return &level.walls[it->second];
        };

        const auto isSelected = [](const ID wallID) {
            return std::find(selectedWalls.begin(), selectedWalls.end(), wallID) != selectedWalls.end();
        };

        // Hover highlight, amber - skipped for a wall that is already
        // selected, which has its own, stronger highlight below.
        if (hoveredWallID != INVALID_ID && !isSelected(hoveredWallID)) {
            if (const Wall* wall = findWall(hoveredWallID)) {
                SDL_SetRenderDrawColor(renderer, 255, 190, 60, 255);

                DrawColoredThickLine(
                    WorldToScreen(wall->start, cameraPos),
                    WorldToScreen(wall->end, cameraPos),
                    7.0f
                );
            }
        }

        // Selection highlight, cyan - matching the selected-sector outline
        // and the snap indicator.
        for (const ID wallID : selectedWalls) {
            const Wall* wall = findWall(wallID);
            if (wall == nullptr) continue;

            SDL_SetRenderDrawColor(renderer, kAnchorColor.r, kAnchorColor.g, kAnchorColor.b, 255);

            DrawColoredThickLine(
                WorldToScreen(wall->start, cameraPos),
                WorldToScreen(wall->end, cameraPos),
                7.0f
            );
        }

        // Handles last, so they sit on top of every selected wall's line.
        // Asking PickSelectedWallEndpointAt() which handle is hovered
        // (rather than re-deriving it here) is what keeps the highlighted
        // handle and the grabbable handle the same one.
        ID hoveredHandleWallID = INVALID_ID;
        bool hoveredHandleIsStart = false;

        const bool handleHovered = PickSelectedWallEndpointAt(
            ScreenToWorld(InputManager::GetMousePosition(), cameraPos),
            &hoveredHandleWallID,
            &hoveredHandleIsStart
        );

        for (const ID wallID : selectedWalls) {
            const Wall* wall = findWall(wallID);
            if (wall == nullptr) continue;

            const bool hoveredHandleIsOnThisWall = handleHovered && hoveredHandleWallID == wallID;

            DrawEndpointHandle(wall->start, hoveredHandleIsOnThisWall && hoveredHandleIsStart);
            DrawEndpointHandle(wall->end, hoveredHandleIsOnThisWall && !hoveredHandleIsStart);
        }
    }

    void DrawWalls() {
        const Level& level = LevelManager::CurrentLevel();

        for (const Wall& wall : level.walls) {
            const Vector2 startScreen = WorldToScreen(wall.start, cameraPos);
            const Vector2 endScreen = WorldToScreen(wall.end, cameraPos);

            //todo TILKYTODO make this a user editable sector
            if (std::find(selectedWalls.begin(), selectedWalls.end(), wall.id) != selectedWalls.end() &&
                editingWall && currentMode == MODE_GEOMETRY)
                SDL_SetRenderDrawColor(renderer, highlightedWallColor.r, highlightedWallColor.g, highlightedWallColor.b, 255);
            else SDL_SetRenderDrawColor(renderer, normalWallColor.r, normalWallColor.g, normalWallColor.b, 255);

            DrawThickLine(renderer, startScreen, endScreen, 5.0f);
        }
    }

    void DrawEntities() {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& entity : level.entities) {
            const ComponentTransform* transform = level.transforms.Get(entity.id);

            if (transform == nullptr) continue;

            const Vector2 screenPos = WorldToScreen({transform->position.x, transform->position.z}, cameraPos);

            const float screenEntitySize = ENTITY_SIZE * editorZoom;

            SDL_FRect rect = {
                screenPos.x - screenEntitySize * 0.5f,
                screenPos.y - screenEntitySize * 0.5f,
                screenEntitySize,
                screenEntitySize
            };

            Vector3 entityColor = normalEntityColor;

            // Texture View Mode for entities with a sprite.
            // Falls back to the existing color-block rendering whenever the
            // sprite has no texture assigned or the texture isn't available.
            const ComponentSprite* sprite = level.sprites.Get(entity.id);
            SDL_Texture* spriteTexture = nullptr;

            if (textureViewMode && sprite != nullptr && !sprite->textureFileNames.empty() && !sprite->textureFileNames[0].empty())
                spriteTexture = GetEditorTexture(sprite->textureFileNames[0]);

            if (spriteTexture != nullptr) SDL_RenderTexture(renderer, spriteTexture, nullptr, &rect);
            else {
                if (sprite != nullptr) entityColor = spriteEntityColor;
                if (entity.id == selectedEntity.id && editingEntity && currentMode == MODE_ENTITY) entityColor = highlightedEntityColor;

                SDL_SetRenderDrawColor(renderer, entityColor.x, entityColor.y, entityColor.z, 255);
                SDL_RenderFillRect(renderer, &rect);
            }

            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderRect(renderer, &rect);
        }
    }

    namespace {
        // Every Nth true grid line (counted in fixed GRID_SIZE units,
        // never in the zoom-adaptive render stride) is drawn as a
        // bigger, brighter "major" dot so the grid reads at a glance
        // instead of as a uniform field of dots.
        constexpr int MAJOR_GRID_LINE_INTERVAL = 8;

        bool IsOnMajorGridLine(const float worldCoord) {
            const float stepsFromOrigin = worldCoord / GRID_SIZE;
            const float rounded = std::round(stepsFromOrigin);

            // Guards against float drift landing just off the nearest
            // integer grid step before checking it's a multiple of the
            // major interval.
            if (std::fabs(stepsFromOrigin - rounded) > 0.01f) return false;

            const long long stepIndex = static_cast<long long>(std::llround(rounded));
            return (stepIndex % MAJOR_GRID_LINE_INTERVAL) == 0;
        }
    }

    void DrawGridDots() {
        constexpr float minorDotSize = 3.0f;
        constexpr float majorDotSize = 5.0f;

        const float activeGridSize = GetActiveGridSize();

        // Once zoom has forced the render stride coarser than the true
        // grid unit, every dot actually being drawn already skipped
        // some real grid lines to stay legible - dim minor dots a touch
        // in that state as a visual hint that this isn't the full,
        // fine-grained grid (the major dots stay full brightness so the
        // overall layout still reads clearly). This never changes what
        // SnapToGrid uses, only how dense/bright this render pass looks.
        const bool renderingCoarserThanTrueGrid = activeGridSize > GRID_SIZE + 0.001f;
        const Uint8 minorAlpha = renderingCoarserThanTrueGrid ? 150 : 255;

        const float visibleHalfWidthWorld = (screenWidth * 0.5f) / editorZoom;
        const float visibleHalfHeightWorld = (screenHeight * 0.5f) / editorZoom;

        const float leftWorld = cameraPos.x - visibleHalfWidthWorld;
        const float rightWorld = cameraPos.x + visibleHalfWidthWorld;
        const float bottomWorld = cameraPos.y - visibleHalfHeightWorld;
        const float topWorld = cameraPos.y + visibleHalfHeightWorld;

        const float startX = std::floor(leftWorld / activeGridSize) * activeGridSize;
        const float startY = std::floor(bottomWorld / activeGridSize) * activeGridSize;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        for (float worldX = startX; worldX <= rightWorld; worldX += activeGridSize) {
            const bool majorX = IsOnMajorGridLine(worldX);

            for (float worldY = startY; worldY <= topWorld; worldY += activeGridSize) {
                const bool major = majorX && IsOnMajorGridLine(worldY);
                const float dotSize = major ? majorDotSize : minorDotSize;
                const Uint8 alpha = major ? 255 : minorAlpha;

                SDL_SetRenderDrawColor(renderer, gridColor.r, gridColor.g, gridColor.b, alpha);

                const Vector2 screenPos = WorldToScreen({worldX, worldY}, cameraPos);

                SDL_FRect dotRect = {
                    screenPos.x - dotSize * 0.5f,
                    screenPos.y - dotSize * 0.5f,
                    dotSize,
                    dotSize
                };

                SDL_RenderFillRect(renderer, &dotRect);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}