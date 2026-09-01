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

namespace MapEditorInternal {
    void DrawThickLine(SDL_Renderer* renderer, const Vector2 start, const Vector2 end, const float thickness) {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;

        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) {
            return;
        }

        const float normalX = -dy / length;
        const float normalY = dx / length;

        const int halfThickness = static_cast<int>(thickness * 0.5f);

        // walls should the be the opposite of the theme
        if (currentTheme == THEME_DARK)
        SDL_SetRenderDrawColor(renderer, 205, 205, 205, 255);
        else if (currentTheme == THEME_LIGHT)
            SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);

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
        const Vector2 snapped = ResolveSnapPoint(mouseWorld);
        const Vector2 screenPos = WorldToScreen(snapped, cameraPos);

        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);

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
        struct PreviewColor { Uint8 r, g, b, a; };

        // Gold = "this would be accepted if you clicked/confirmed now",
        // red = "this would be rejected". Reused everywhere a shape is
        // being previewed, replacing the old freehand-only, always-red
        // fill, so every tool gives the same at-a-glance feedback.
        constexpr PreviewColor kValidLineColor = {255, 220, 80, 255};
        constexpr PreviewColor kInvalidLineColor = {230, 70, 70, 255};
        constexpr PreviewColor kAnchorColor = {80, 220, 255, 255}; // matches DrawSnapIndicator/selection cyan
        constexpr SDL_FColor kValidFillColor = {1.0f, 0.863f, 0.314f, 0.28f};
        constexpr SDL_FColor kInvalidFillColor = {0.90f, 0.27f, 0.27f, 0.28f};

        SDL_FColor ThemeTextColor() {
            if (currentTheme == THEME_LIGHT) return {0.0f, 0.0f, 0.0f, 1.0f};
            return {1.0f, 1.0f, 1.0f, 1.0f};
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

            const PreviewColor color = valid ? kValidLineColor : kInvalidLineColor;
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
            const SDL_FColor fillColor = valid ? kValidFillColor : kInvalidFillColor;
            for (const Triangle& triangle : triangles) DrawFilledTriangle(triangle, fillColor);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
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

            DrawPreviewOutline(sectorBeingCreated, false, true);

            for (std::size_t i = 0; i + 1 < sectorBeingCreated.size(); ++i)
                DrawEdgeLengthLabel(sectorBeingCreated[i], sectorBeingCreated[i + 1]);

            const PreviewColor rubberBand = kValidLineColor;
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
    // precisely placing a Dot or Entity as it is for Sector drawing.
    void DrawSectorPreview() {
        if (currentMode == MODE_SECTOR) {
            if (IsDrawingInProgress()) {
                switch (currentDrawTool) {
                    case DRAWTOOL_FREEHAND:  DrawFreehandPreview(); break;
                    case DRAWTOOL_RECTANGLE: DrawRectanglePreview(); break;
                    case DRAWTOOL_POLYGON:   DrawPolygonPreview(); break;
                    case DRAWTOOL_CIRCLE:    DrawCirclePreview(); break;
                    case DRAWTOOL_CURVE:     DrawCurvePreview(); break;
                    default: break;
                }
            }

            // this looks bad
            // DrawStatusOverlay();
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
        for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i) {
            if (Geometry::IsPointInPolygon(level.sectors[i].vertices, level.sectors[i].innerLoops, mouseWorld)) {
                hoveredSectorIndex = i;
                break;
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        constexpr SDL_FColor hoveredSectorColor = {
            1.0f,
            0.75f,
            0.0f,
            0.45f
        };

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

            // BUG FIX: hue used to be derived from `sectorIndex` (this
            // sector's position in level.sectors), which shifts for
            // every sector after the one that changed whenever an
            // *unrelated* sector is created, deleted, or reordered
            // (level.sectors.erase(...) shifts everything after it) -
            // so previews would visibly recolour themselves any time
            // the sector list changed at all. `sector.id` is stable for
            // the sector's whole lifetime (IDs are never reused - see
            // Editor::AddSector/DeleteSector), so hashing that instead
            // keeps each sector's colour fixed regardless of what
            // happens to any other sector. The golden-ratio-conjugate
            // multiply is unchanged - it's what gives evenly spread,
            // visually distinct hues across sequential IDs.
            const float hue = std::fmod(
                static_cast<float>(sector.id) * 0.618033988749895f,
                1.0f
            );

            const SDL_FColor normalSectorColor = HSVtoRGB(hue, 0.7f, 0.9f);

            const SDL_FColor sectorColor =
                    sectorIndex == hoveredSectorIndex && currentMode == MODE_SECTOR
                        ? hoveredSectorColor
                        : normalSectorColor;

            SDL_Texture *floorTexture = nullptr;

            if (textureViewMode && !sector.floors.empty()) {
                const std::string &textureFileName = sector.floors.front().floor.texture;

                if (!textureFileName.empty()) floorTexture = GetEditorTexture(textureFileName);
            }

            for (const Triangle &triangle: sector.triangles) {
                if (floorTexture != nullptr) {
                    DrawFilledTriangleTextured(
                        triangle,
                        floorTexture,
                        {1.0f, 1.0f, 1.0f, 1.0f}
                    );
                }
                else DrawFilledTriangle(triangle, sectorColor);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        if (selectedSectorID == INVALID_ID) return;

        const Sector *selectedSector = MapQueries::GetSectorByID(level, selectedSectorID);

        if (selectedSector == nullptr || selectedSector->vertices.empty()) return;

        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);

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
    // "placedCorners" concept and are now ID-stable, off-grid-capable points.
    void DrawDots() {
        if (currentTheme == THEME_DARK) SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        else if (currentTheme == THEME_LIGHT) SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

        for (const Dot& dot : dots) {
            const Vector2 screenPos = WorldToScreen(dot.position, cameraPos);

            SDL_FRect dotRect = {
                screenPos.x - 3.0f,
                screenPos.y - 3.0f,
                6.0f,
                6.0f
            };

            SDL_RenderFillRect(renderer, &dotRect);
        }

        if (selectedDotID != INVALID_ID) {
            const auto it = dotIDToIndex.find(selectedDotID);

            if (it != dotIDToIndex.end() && it->second >= 0 && it->second < static_cast<int>(dots.size())) {
                const Vector2 screenPos = WorldToScreen(dots[it->second].position, cameraPos);

                SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);

                const SDL_FRect highlightRect = {
                    screenPos.x - 6.0f,
                    screenPos.y - 6.0f,
                    12.0f,
                    12.0f
                };

                SDL_RenderRect(renderer, &highlightRect);
            }
        }
    }

    void DrawWalls() {
        const Level& level = LevelManager::CurrentLevel();

        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);

        for (const Wall& wall : level.walls) {
            const Vector2 startScreen = WorldToScreen(wall.start, cameraPos);
            const Vector2 endScreen = WorldToScreen(wall.end, cameraPos);

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

            // Texture View Mode for entities with a sprite.
            // Falls back to the existing color-block rendering whenever the
            // sprite has no texture assigned or the texture isn't available.
            const ComponentSprite* sprite = level.sprites.Get(entity.id);
            SDL_Texture* spriteTexture = nullptr;

            if (textureViewMode && sprite != nullptr && !sprite->textureFileNames.empty() && !sprite->textureFileNames[0].empty())
                spriteTexture = GetEditorTexture(sprite->textureFileNames[0]);

            if (spriteTexture != nullptr) SDL_RenderTexture(renderer, spriteTexture, nullptr, &rect);
            else {
                if (sprite != nullptr) SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
                else SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

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

        Uint8 baseR = 225, baseG = 225, baseB = 225;
        if (currentTheme == THEME_LIGHT) { baseR = 25; baseG = 25; baseB = 25; }

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

                SDL_SetRenderDrawColor(renderer, baseR, baseG, baseB, alpha);

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