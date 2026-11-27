// The map editor canvas as an Editor2D::Frame for the OpenGL renderer: the
// same picture MapEditorDrawing.cpp draws with SDL_Renderer, function for
// function. Level things become entries + state flags (the shader colours
// them); everything else becomes screen-space overlay triangles.
//
// Canvas (Editor2DCanvas.hpp) mirrors SDL_Renderer's draw state.

#include "../EditorInternal.hpp"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/Gateways.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Editor/EventLinks.hpp"
#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "../Editor2DCanvas.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <string>

namespace MapEditorInternal {
    // Named, not anonymous: unity builds put several of these files together.
    namespace MapCanvasFrame {
        using Editor2D::Frame;
        using Editor2D::GpuOverlayVertex;
        using Editor2D::Layer;

        using Editor2DCanvas::Canvas;
        Canvas canvas;

        Vector4 Rgba01(const Vector3& color, const float alpha = 1.0f) {
            return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha};
        }

        Vector4 Rgba01(const Vector4& color) {
            return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
        }

        Vector2 Screen(const Vector2& world) {
            return WorldToScreen(world, cameraPos);
        }

        int LayerAlpha(const int alpha, const bool layerOn) {
            return layerOn ? alpha : std::min<int>(alpha, filteredAlpha / 2);
        }

        // ---- Labels -------------------------------------------------------

        void WorldLabel(const std::string& text, const Vector2& worldPos, const Vector4& rgba01) {
            if (text.empty()) return;
            canvas.frame->labels.push_back({text, Screen(worldPos), rgba01, UI_FONT_SIZE, Editor2D::LabelStyle::WorldLabel});
        }

        void LengthLabel(const Vector2& start, const Vector2& end, const float length) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.1f", length);

            const Vector2 a = Screen(start);
            const Vector2 b = Screen(end);
            const Vector4 textColor = {225.0f / 255.0f, 225.0f / 255.0f, 225.0f / 255.0f, 1.0f};
            canvas.frame->labels.push_back({buffer, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, textColor, LABEL_FONT_SIZE,
                                            Editor2D::LabelStyle::LengthPill});
        }

        Vector4 ThemeText() {
            return Rgba01(themeTextColor);
        }

        // ---- Shared primitives (MapEditorDrawing.cpp) --------------------

        void FilledTriangle(const Triangle& triangle, const Vector4& rgba01) {
            canvas.GeometryTriangle(Screen(triangle.a), Screen(triangle.b), Screen(triangle.c), rgba01);
        }

        // DrawWallLine: one quad exactly `thickness` pixels wide.
        void WallLine(const Vector2& startScreen, const Vector2& endScreen, const float thickness, const Vector3& color, const int alpha) {
            const float dx = endScreen.x - startScreen.x;
            const float dy = endScreen.y - startScreen.y;
            if (std::sqrt(dx * dx + dy * dy) <= Constants::Epsilon) return;

            canvas.Band(startScreen, endScreen, thickness, 0.0f, canvas.Effective(Rgba01(color, static_cast<float>(static_cast<Uint8>(alpha)) / 255.0f)));
        }

        void EndpointHandle(const Vector2& worldPos, const bool hovered) {
            const Vector2 screenPos = Screen(worldPos);
            const float halfSize = hovered ? 6.0f : 4.5f;
            const SDL_FRect handle = {screenPos.x - halfSize, screenPos.y - halfSize, halfSize * 2.0f, halfSize * 2.0f};

            if (hovered) canvas.SetColor(highlightedHandleColor.r, highlightedHandleColor.g, highlightedHandleColor.b, 255);
            else canvas.SetColor(normalHandleColor.r, normalHandleColor.g, normalHandleColor.b, 255);
            canvas.FillRect(handle);

            canvas.SetColor(handleOutlineColor.r, handleOutlineColor.g, handleOutlineColor.b, 255);
            canvas.Rect(handle);
        }

        // ---- Sector Mode previews -----------------------------------------

        void PreviewOutline(const std::vector<Vector2>& points, const bool closeLoop, const bool valid) {
            if (points.size() < 2) return;

            const Vector3 color = valid ? kValidLineColor : kInvalidLineColor;
            canvas.SetColor(color.r, color.g, color.b, 255);

            const std::size_t segmentCount = closeLoop ? points.size() : points.size() - 1;
            for (std::size_t i = 0; i < segmentCount; ++i)
                canvas.ThickLine(Screen(points[i]), Screen(points[(i + 1) % points.size()]), 3.0f);
        }

        void PreviewFill(const std::vector<Vector2>& closedLoopPoints, const bool valid) {
            if (closedLoopPoints.size() < 3) return;

            const std::vector<Triangle> triangles = Geometry::Triangulate(closedLoopPoints);

            canvas.SetBlend(true);
            const Vector4 fill = Rgba01(valid ? kValidFillColor : kInvalidFillColor);

            // Twice, as the SDL version does.
            for (const Triangle& triangle : triangles) FilledTriangle(triangle, fill);
            for (const Triangle& triangle : triangles) FilledTriangle(triangle, fill);
            canvas.SetBlend(false);
        }

        void EdgeLengthLabel(const Vector2& worldA, const Vector2& worldB) {
            const float length = std::sqrt(Vector2Math::DistanceSquared(worldA, worldB));
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.1f", length);
            WorldLabel(buffer, {(worldA.x + worldB.x) * 0.5f, (worldA.y + worldB.y) * 0.5f}, ThemeText());
        }

        void SnapTargetRing(const Vector2& worldPos) {
            const Vector2 screenPos = Screen(worldPos);
            canvas.SetColor(kValidLineColor.r, kValidLineColor.g, kValidLineColor.b, kValidLineColor.a);
            constexpr float half = 8.0f;
            canvas.Rect({screenPos.x - half, screenPos.y - half, half * 2.0f, half * 2.0f});
        }

        void AnchorPoint(const Vector2& worldPos) {
            const Vector2 screenPos = Screen(worldPos);
            canvas.SetColor(kAnchorColor.r, kAnchorColor.g, kAnchorColor.b, kAnchorColor.a);
            constexpr float half = 4.0f;
            canvas.FillRect({screenPos.x - half, screenPos.y - half, half * 2.0f, half * 2.0f});
        }

        void FreehandPreview() {
            if (manualSectorMode) {
                PreviewOutline(manualSectorDots, false, true);
                for (std::size_t i = 0; i + 1 < manualSectorDots.size(); ++i) EdgeLengthLabel(manualSectorDots[i], manualSectorDots[i + 1]);
                for (const Vector2& dot : manualSectorDots) AnchorPoint(dot);
                return;
            }

            if (sectorBeingCreated.empty()) return;

            const Vector2 mouseWorld = ScreenToWorld(MapMousePosition(), cameraPos);
            const Vector2 previewPoint = ResolveFreehandPoint(mouseWorld);

            Vector2 snapTarget{};
            const bool snappedToChain = SnapToPendingChainPoint(mouseWorld, &snapTarget);
            const bool wouldCloseLoop = snappedToChain && sectorBeingCreated.size() >= 3 && SamePoint(snapTarget, sectorBeingCreated.front());

            PreviewOutline(sectorBeingCreated, wouldCloseLoop, true);

            for (std::size_t i = 0; i + 1 < sectorBeingCreated.size(); ++i) EdgeLengthLabel(sectorBeingCreated[i], sectorBeingCreated[i + 1]);

            canvas.SetColor(kValidLineColor.r, kValidLineColor.g, kValidLineColor.b, 160);
            canvas.ThickLine(Screen(sectorBeingCreated.back()), Screen(previewPoint), 2.0f);
            WorldLabel(GetActiveDrawToolMeasurementText(), previewPoint, ThemeText());

            const std::vector<Vector2> committedLoop = GetSectorVerticesWithoutClosingDuplicate();
            const std::vector<Vector2> dedupedLoop = DedupeConsecutivePoints(committedLoop);
            const bool wouldCloseCleanly = dedupedLoop.size() >= 3 && !ClosedLoopSelfIntersects(dedupedLoop);

            PreviewFill(committedLoop, wouldCloseCleanly);

            for (const Vector2& point : sectorBeingCreated) AnchorPoint(point);

            if (wouldCloseLoop) SnapTargetRing(snapTarget);
        }

        void RectanglePreview() {
            if (!rectangleHasFirstCorner) return;

            const Vector2 opposite = ResolveRectangleCorner(ScreenToWorld(MapMousePosition(), cameraPos));
            const float width = std::fabs(opposite.x - rectangleFirstCorner.x);
            const float height = std::fabs(opposite.y - rectangleFirstCorner.y);
            const bool valid = width >= MIN_DRAW_SHAPE_DIMENSION && height >= MIN_DRAW_SHAPE_DIMENSION;

            const std::vector<Vector2> corners = {
                rectangleFirstCorner, {opposite.x, rectangleFirstCorner.y}, opposite, {rectangleFirstCorner.x, opposite.y}
            };

            PreviewOutline(corners, true, valid);
            PreviewFill(corners, valid);
            AnchorPoint(rectangleFirstCorner);
            WorldLabel(GetActiveDrawToolMeasurementText(), opposite, ThemeText());
        }

        void PolygonPreview() {
            if (!polygonHasCenter) return;

            const Vector2 handle = ResolvePolygonHandle(ScreenToWorld(MapMousePosition(), cameraPos));
            const float radius = std::sqrt(Vector2Math::DistanceSquared(polygonCenter, handle));
            const bool valid = radius >= MIN_DRAW_SHAPE_DIMENSION;
            const std::vector<Vector2> corners = BuildRegularPolygon(polygonCenter, handle, polygonSideCount);

            PreviewOutline(corners, true, valid);
            PreviewFill(corners, valid);
            AnchorPoint(polygonCenter);
            WorldLabel(GetActiveDrawToolMeasurementText(), handle, ThemeText());
        }

        void CirclePreview() {
            if (!circleHasCenter) return;

            const Vector2 handle = ResolveCircleHandle(ScreenToWorld(MapMousePosition(), cameraPos));
            const float radiusX = std::fabs(handle.x - circleCenter.x);
            const float radiusY = std::fabs(handle.y - circleCenter.y);
            const bool valid = radiusX >= MIN_DRAW_SHAPE_DIMENSION && radiusY >= MIN_DRAW_SHAPE_DIMENSION;
            const std::vector<Vector2> points = BuildEllipse(circleCenter, radiusX, radiusY, circleSegments);

            PreviewOutline(points, true, valid);
            PreviewFill(points, valid);
            AnchorPoint(circleCenter);
            WorldLabel(GetActiveDrawToolMeasurementText(), handle, ThemeText());
        }

        void CurvePreview() {
            if (curveStage == CURVE_STAGE_START) return;

            const Vector2 mouseWorld = ScreenToWorld(MapMousePosition(), cameraPos);

            if (curveStage == CURVE_STAGE_END) {
                const Vector2 end = ResolveCurveEnd(mouseWorld);
                const bool valid = Vector2Math::DistanceSquared(curveStart, end) >= MIN_DRAW_SHAPE_DIMENSION * MIN_DRAW_SHAPE_DIMENSION;

                PreviewOutline({curveStart, end}, false, valid);
                AnchorPoint(curveStart);
                WorldLabel(GetActiveDrawToolMeasurementText(), end, ThemeText());
                return;
            }

            const Vector2 control = ResolveSnapPoint(mouseWorld);
            const std::vector<Vector2> curvePoints = BuildQuadraticCurve(curveStart, control, curveEnd, curveSubdivisions);

            PreviewOutline(curvePoints, false, true);
            PreviewOutline({curveStart, control}, false, true);
            PreviewOutline({curveEnd, control}, false, true);
            AnchorPoint(curveStart);
            AnchorPoint(curveEnd);
            AnchorPoint(control);
            WorldLabel(GetActiveDrawToolMeasurementText(), control, ThemeText());
        }

        void Arrow(const Vector2& fromWorld, const Vector2& toWorld, const Vector3& color) {
            const Vector2 from = Screen(fromWorld);
            const Vector2 to = Screen(toWorld);
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.5f) return;

            canvas.SetColor(color.r, color.g, color.b, 255);
            canvas.Line(from, to);

            const float dirX = dx / length;
            const float dirY = dy / length;
            constexpr float headLength = 14.0f;
            constexpr float headAngle = 0.5f;

            const auto rotate = [](const float x, const float y, const float angle) {
                return Vector2{x * std::cos(angle) - y * std::sin(angle), x * std::sin(angle) + y * std::cos(angle)};
            };

            const Vector2 left = rotate(-dirX, -dirY, headAngle);
            const Vector2 right = rotate(-dirX, -dirY, -headAngle);
            canvas.Line(to, {to.x + left.x * headLength, to.y + left.y * headLength});
            canvas.Line(to, {to.x + right.x * headLength, to.y + right.y * headLength});
        }

        Vector2 StepCentroid(const StaircaseStepSpan& step) {
            return {
                (step.corners[0].x + step.corners[1].x + step.corners[2].x + step.corners[3].x) * 0.25f,
                (step.corners[0].y + step.corners[1].y + step.corners[2].y + step.corners[3].y) * 0.25f
            };
        }

        void StaircasePreview() {
            if (!staircaseHasFirstCorner) return;

            const Vector2 opposite = ResolveStaircaseCorner(ScreenToWorld(MapMousePosition(), cameraPos));
            const StaircasePlan plan = BuildStaircasePlan(staircaseFirstCorner, opposite);

            const std::vector<Vector2> outerCorners = {
                staircaseFirstCorner, {opposite.x, staircaseFirstCorner.y}, opposite, {staircaseFirstCorner.x, opposite.y}
            };

            PreviewOutline(outerCorners, true, plan.valid);
            AnchorPoint(staircaseFirstCorner);

            if (!plan.valid) {
                WorldLabel(plan.error, opposite, {0.95f, 0.35f, 0.35f, 1.0f});
                return;
            }

            for (const StaircaseStepSpan& step : plan.steps) {
                const std::vector<Vector2> quad = {step.corners[0], step.corners[1], step.corners[2], step.corners[3]};
                PreviewOutline(quad, true, true);
                PreviewFill(quad, true);

                char label[64];
                std::snprintf(label, sizeof(label), "#%d  h:%.1f", step.index, step.floorHeight);
                WorldLabel(label, StepCentroid(step), ThemeText());
            }

            if (plan.steps.size() >= 2) {
                const StaircaseStepSpan* bottom = &plan.steps.front();
                const StaircaseStepSpan* top = &plan.steps.front();
                for (const StaircaseStepSpan& step : plan.steps) {
                    if (step.index == 0) bottom = &step;
                    if (step.index == plan.stepCount - 1) top = &step;
                }
                Arrow(StepCentroid(*bottom), StepCentroid(*top), kAnchorColor);
            }

            WorldLabel(GetActiveDrawToolMeasurementText(), opposite, ThemeText());

            if (plan.headroomWarning)
                WorldLabel(Localisation::Get("editor.staircase.warning.headroom"), staircaseFirstCorner, {1.0f, 0.65f, 0.15f, 1.0f});
            if (plan.targetSingleSectorNote)
                WorldLabel(Localisation::Get("editor.staircase.notice.target_single_step"), staircaseFirstCorner, {1.0f, 0.8f, 0.2f, 1.0f});
        }

        void SnapIndicator() {
            // Hidden while the middle button pans the map.
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) return;

            const Vector2 mouseWorld = ScreenToWorld(MapMousePosition(), cameraPos);
            const bool freehandDrawing = currentMode == MODE_SECTOR && currentDrawTool == DRAWTOOL_FREEHAND && !manualSectorMode;

            const Vector2 snapped = freehandDrawing ? ResolveFreehandPoint(mouseWorld)
                                  : currentMode == MODE_GEOMETRY && linedefMode ? ResolveLinedefSnapPoint(mouseWorld, {})
                                  : ResolveSnapPoint(mouseWorld);
            const Vector2 screenPos = Screen(snapped);

            // The same (shifted) channels the SDL version passes.
            canvas.SetColor(80, snapIndicatorColor.r, snapIndicatorColor.g, snapIndicatorColor.b);

            constexpr float radius = 5.0f;
            canvas.Rect({screenPos.x - radius, screenPos.y - radius, radius * 2.0f, radius * 2.0f});
        }

        void SectorPreview() {
            if (currentMode == MODE_SECTOR && IsDrawingInProgress()) {
                switch (currentDrawTool) {
                    case DRAWTOOL_FREEHAND:  FreehandPreview(); break;
                    case DRAWTOOL_RECTANGLE: RectanglePreview(); break;
                    case DRAWTOOL_POLYGON:   PolygonPreview(); break;
                    case DRAWTOOL_CIRCLE:    CirclePreview(); break;
                    case DRAWTOOL_CURVE:     CurvePreview(); break;
                    case DRAWTOOL_STAIRCASE: StaircasePreview(); break;
                    default: break;
                }
            }

            SnapIndicator();
        }

        // ---- View modes ---------------------------------------------------

        float PlayerHeight(const Level& level) {
            for (const ComponentPlayerController& controller : level.playerControllers.components) {
                const ComponentCollider* collider = level.colliders.Get(controller.ownerID);
                if (collider != nullptr) return collider->Height(*level.transforms.Get(controller.ownerID));
            }
            return 0.0f;
        }

        std::optional<float> ViewModeValue(const Sector& sector) {
            if (sector.floors.empty()) return std::nullopt;
            const SectorFloor& floor = sector.floors.front();
            switch (textureViewMode) {
                case TextureViewMode::FloorHeight: return floor.floor.height;
                case TextureViewMode::CeilingHeight: return floor.ceiling.height;
                case TextureViewMode::RoomHeight: return floor.ceiling.height - floor.floor.height;
                default: return std::nullopt;
            }
        }

        // ---- Level things -------------------------------------------------

        void BuildSectors(Frame& frame) {
            const Level& level = LevelManager::CurrentLevel();

            // The view range over every sector, as ComputeViewRange.
            bool any = false;
            for (const Sector& sector : level.sectors) {
                const std::optional<float> value = ViewModeValue(sector);
                if (!value.has_value()) continue;
                frame.style.viewRangeLow = any ? std::min(frame.style.viewRangeLow, *value) : *value;
                frame.style.viewRangeHigh = any ? std::max(frame.style.viewRangeHigh, *value) : *value;
                any = true;
            }

            frame.style.viewMode = static_cast<int>(textureViewMode);
            frame.style.playerHeight = textureViewMode == TextureViewMode::RoomHeight ? PlayerHeight(level) : 0.0f;
            frame.style.sectorLayerOn = canvasLayerSectors;
            frame.style.highlightedSectorColor = Rgba01(highlightedSectorColor);
            frame.style.hoveredSectorColor = Rgba01(hoveredSectorColor);

            frame.sectors.reserve(level.sectors.size());
            frame.sectorFlags.reserve(level.sectors.size());

            for (std::size_t index = 0; index < level.sectors.size(); ++index) {
                const Sector& sector = level.sectors[index];
                const bool hasFloor = !sector.floors.empty();
                const float hue = std::fmod(static_cast<float>(sector.id) * 0.618033988749895f, 1.0f);

                frame.sectors.push_back({
                    {sector.light.x, sector.light.y, sector.light.z, hue},
                    {hasFloor ? sector.floors.front().floor.height : 0.0f, hasFloor ? sector.floors.front().ceiling.height : 0.0f,
                     hasFloor ? 1.0f : 0.0f, 0.0f}
                });

                std::uint32_t flags = 0;
                if (editingSector && std::ranges::find(selectedSectors, sector.id) != selectedSectors.end()) flags |= Editor2D::SECTOR_SELECTED;
                if (sector.id == hoveredSectorID && currentMode == MODE_SECTOR) flags |= Editor2D::SECTOR_HOVER_FILL;
                frame.sectorFlags.push_back(flags);

                std::string texture;
                if (IsTextureViewShowingTextures(textureViewMode) && hasFloor) {
                    const SectorFloor& shown = sector.floors.front();
                    texture = textureViewMode == TextureViewMode::Ceiling ? shown.ceiling.texture.First() : shown.floor.texture.First();
                    // Still loading or missing: drawn untextured, as before.
                    if (!texture.empty() && GetEditorTexture(texture) == nullptr) texture.clear();
                }

                const auto first = static_cast<std::uint32_t>(frame.sectorTriangles.size());
                for (const Triangle& triangle : sector.triangles)
                    frame.sectorTriangles.push_back({{triangle.a.x, triangle.a.y, triangle.b.x, triangle.b.y},
                                                     {triangle.c.x, triangle.c.y, static_cast<float>(index), 0.0f}});
                Frame::ExtendRun(frame.sectorRuns, texture, first, static_cast<std::uint32_t>(frame.sectorTriangles.size()) - first);
            }

            // The selected sector's outline (DrawExistingSectors ends with blending off).
            canvas.layer = Layer::AfterSectors;
            canvas.SetBlend(false);

            if (selectedSectorID == INVALID_ID) return;
            const Sector* selectedSector = MapQueries::GetSectorByID(level, selectedSectorID);
            if (selectedSector == nullptr || selectedSector->vertices.empty()) return;

            canvas.SetColor(kAnchorColor.r, kAnchorColor.g, kAnchorColor.b, 255);
            const auto outlineLoop = [](const std::vector<Vector2>& loop) {
                for (std::size_t i = 0; i < loop.size(); ++i) canvas.ThickLine(Screen(loop[i]), Screen(loop[(i + 1) % loop.size()]), 3.0f);
            };
            outlineLoop(selectedSector->vertices);
            for (const std::vector<Vector2>& innerLoop : selectedSector->innerLoops) outlineLoop(innerLoop);
        }

        void BuildWalls(Frame& frame) {
            const Level& level = LevelManager::CurrentLevel();

            frame.style.normalWallColor = Rgba01(normalWallColor);
            frame.style.portalWallColor = Rgba01(portalWallColor);
            frame.style.highlightedWallColor = Rgba01(highlightedWallColor);
            frame.style.hoverColor = {1.0f, 190.0f / 255.0f, 60.0f / 255.0f, 1.0f};
            frame.style.wallAlpha = static_cast<float>(static_cast<Uint8>(LayerAlpha(255, canvasLayerWalls))) / 255.0f;
            frame.style.wallFaintAlpha = static_cast<float>(static_cast<Uint8>(LayerAlpha(filteredAlpha, canvasLayerWalls))) / 255.0f;

            const bool wallOverlay = currentMode == MODE_GEOMETRY && !linedefMode;

            frame.walls.reserve(level.walls.size());
            frame.wallFlags.reserve(level.walls.size());

            for (const Wall& wall : level.walls) {
                const bool portal = !(wall.frontSector == -1 || wall.backSector == -1);
                frame.walls.push_back({{wall.start.x, wall.start.y, wall.end.x, wall.end.y}, {portal ? 1.0f : 0.0f, 0, 0, 0}});

                const bool selected = std::ranges::find(selectedWalls, wall.id) != selectedWalls.end();
                const bool hovered = wall.id == hoveredWallID;

                std::uint32_t flags = 0;
                if ((currentMode == MODE_GEOMETRY && (linedefMode || !WallPassesFilter(wall))) || currentMode != MODE_GEOMETRY)
                    flags |= Editor2D::LINE_FAINT;
                if (selected && editingWall) flags |= Editor2D::LINE_HIDDEN;

                // DrawGeometryEditOverlay's highlights.
                if (selected && (editingWall || wallOverlay)) flags |= Editor2D::LINE_SELECTED;
                else if (hovered && (wallOverlay ? !selected : !(editingWall && selected))) flags |= Editor2D::LINE_HOVERED;

                frame.wallFlags.push_back(flags);
            }
        }

        void BuildLinedefs(Frame& frame) {
            const Level& level = LevelManager::CurrentLevel();

            const int alpha = LayerAlpha(currentMode == MODE_GEOMETRY && linedefMode ? 255 : filteredAlpha, canvasLayerLinedefs);
            frame.style.linedefColor = Rgba01(linedefColor);
            frame.style.linedefEndpointColor = Rgba01(linedefEndpointColor);
            frame.style.linedefAlpha = static_cast<float>(static_cast<Uint8>(alpha)) / 255.0f;
            frame.style.linedefThickness = linedefThickness;
            frame.style.linedefEndpointSize = static_cast<float>(linedefEndpointSize);

            const bool linedefOverlay = currentMode == MODE_GEOMETRY && linedefMode;

            for (const Linedef& linedef : level.linedefs) {
                frame.linedefs.push_back({{linedef.start.x, linedef.start.y, linedef.end.x, linedef.end.y}, {}});

                const bool selected = std::ranges::find(selectedLinedefs, linedef.id) != selectedLinedefs.end();
                const bool hovered = linedef.id == hoveredLinedefID;

                std::uint32_t flags = 0;
                if (selected && (editingLinedef || linedefOverlay)) flags |= Editor2D::LINE_SELECTED;
                else if (hovered && (linedefOverlay ? !selected : !(editingLinedef && selected))) flags |= Editor2D::LINE_HOVERED;
                frame.linedefFlags.push_back(flags);
            }

            // The one being drawn, from its start to where a click would end it.
            if (!(drawingLinedef && currentMode == MODE_GEOMETRY && linedefMode)) return;

            canvas.layer = Layer::AfterLinedefs;
            canvas.SetBlend(true);

            const Vector2 start = linedefDrawStart;
            const Vector2 end = ResolveLinedefSnapPoint(ScreenToWorld(MapMousePosition(), cameraPos), {});
            const Vector2 startScreen = Screen(start);
            const Vector2 endScreen = Screen(end);

            WallLine(startScreen, endScreen, linedefThickness, linedefColor, alpha);

            const Vector2 line = end - start;
            const float length = std::sqrt(Vector2Math::Dot(line, line));
            if (length > Constants::Epsilon) {
                constexpr float TICK_PIXELS = 6.0f;
                const Vector2 front = {-line.y / length, line.x / length};
                const Vector2 middle = (start + end) * 0.5f;
                const Vector2 tickEnd = middle + front * (TICK_PIXELS / std::max(editorZoom, 0.0001f));
                WallLine(Screen(middle), Screen(tickEnd), linedefThickness, linedefColor, alpha);
            }

            canvas.SetColor(linedefEndpointColor.r, linedefEndpointColor.g, linedefEndpointColor.b, static_cast<float>(alpha));
            const float squareSize = static_cast<float>(linedefEndpointSize);
            for (const Vector2& point : {startScreen, endScreen})
                canvas.FillRect({std::round(point.x - squareSize * 0.5f), std::round(point.y - squareSize * 0.5f), squareSize, squareSize});
        }

        // DrawGeometryEditOverlay, less the wall/linedef highlights (flags).
        void BuildGeometryEditOverlay(Frame& frame) {
            const Level& level = LevelManager::CurrentLevel();
            canvas.layer = Layer::AfterHighlights;

            // A hovered sector is outlined outside Sector Mode, unless the
            // inspector already highlights it.
            if (const Sector* sector = currentMode != MODE_SECTOR ? level.GetSector(hoveredSectorID) : nullptr;
                sector != nullptr && !(editingSector && std::ranges::find(selectedSectors, sector->id) != selectedSectors.end())) {
                canvas.SetColor(hoveredSectorColor.x, hoveredSectorColor.y, hoveredSectorColor.z, 255);

                const auto outlineLoop = [](const std::vector<Vector2>& loop) {
                    for (std::size_t i = 0; i < loop.size(); ++i) canvas.ThickLine(Screen(loop[i]), Screen(loop[(i + 1) % loop.size()]), 3.0f);
                };
                outlineLoop(sector->vertices);
                for (const std::vector<Vector2>& innerLoop : sector->innerLoops) outlineLoop(innerLoop);
            }

            if (currentMode != MODE_GEOMETRY) return;

            if (linedefMode) {
                for (const ID linedefID : selectedLinedefs)
                    if (const Linedef* linedef = level.GetLinedef(linedefID)) LengthLabel(linedef->start, linedef->end, linedef->length);

                ID hoveredHandleLinedefID = INVALID_ID;
                bool hoveredHandleIsStart = false;
                const bool handleHovered = !drawingLinedef && PickSelectedLinedefEndpointAt(
                    ScreenToWorld(MapMousePosition(), cameraPos), &hoveredHandleLinedefID, &hoveredHandleIsStart);

                for (const ID linedefID : selectedLinedefs) {
                    const Linedef* linedef = level.GetLinedef(linedefID);
                    if (linedef == nullptr) continue;
                    const bool onThis = handleHovered && hoveredHandleLinedefID == linedefID;
                    EndpointHandle(linedef->start, onThis && hoveredHandleIsStart);
                    EndpointHandle(linedef->end, onThis && !hoveredHandleIsStart);
                }

                Vector2 unselectedEnd{};
                if (!drawingLinedef && !handleHovered && !draggingLinedefGeometry &&
                    PickUnselectedLinedefEndpointAt(ScreenToWorld(MapMousePosition(), cameraPos), &unselectedEnd))
                    EndpointHandle(unselectedEnd, true);

                if (drawingLinedef) {
                    const Vector2 end = ResolveLinedefSnapPoint(ScreenToWorld(MapMousePosition(), cameraPos), {});
                    EndpointHandle(linedefDrawStart, false);
                    LengthLabel(linedefDrawStart, end, std::sqrt(Vector2Math::DistanceSquared(linedefDrawStart, end)));
                }
                return;
            }

            const auto findWall = [&](const ID wallID) -> const Wall* {
                const auto it = level.wallIDToIndex.find(wallID);
                if (it == level.wallIDToIndex.end()) return nullptr;
                if (it->second < 0 || it->second >= static_cast<int>(level.walls.size())) return nullptr;
                return &level.walls[it->second];
            };

            for (const Wall& wall : level.walls)
                if (std::ranges::find(selectedWalls, wall.id) != selectedWalls.end()) LengthLabel(wall.start, wall.end, wall.length);

            ID hoveredHandleWallID = INVALID_ID;
            bool hoveredHandleIsStart = false;
            const bool handleHovered = PickSelectedWallEndpointAt(ScreenToWorld(MapMousePosition(), cameraPos),
                                                                  &hoveredHandleWallID, &hoveredHandleIsStart);

            for (const ID wallID : selectedWalls) {
                const Wall* wall = findWall(wallID);
                if (wall == nullptr) continue;
                const bool onThis = handleHovered && hoveredHandleWallID == wallID;
                EndpointHandle(wall->start, onThis && hoveredHandleIsStart);
                EndpointHandle(wall->end, onThis && !hoveredHandleIsStart);
            }
        }

        void AmberWall(const Wall& wall) {
            canvas.SetColor(255, 190, 60, 255);
            canvas.ThickLine(Screen(wall.start), Screen(wall.end), 7.0f);
        }

        void BuildPickHighlights() {
            const Level& level = LevelManager::CurrentLevel();
            canvas.layer = Layer::AfterHighlights;

            // Snap to Wall, then a teleporter's sector, then a gateway's wall.
            if (wallSnapHoveredWallID != INVALID_ID)
                for (const Wall& wall : level.walls)
                    if (wall.id == wallSnapHoveredWallID) {
                        AmberWall(wall);
                        break;
                    }

            if (teleporterPickHoveredSectorID != INVALID_ID)
                if (const Sector* sector = level.GetSector(teleporterPickHoveredSectorID))
                    for (const Wall* wall : sector->walls)
                        if (wall != nullptr) AmberWall(*wall);
        }

        void BuildGatewayLinks() {
            const Level& level = LevelManager::CurrentLevel();
            canvas.layer = Layer::AfterHighlights;
            if (!Gateways::AnyLinked(level)) return;

            for (const Wall& wall : level.walls) {
                if (wall.gatewayTarget == INVALID_ID) continue;

                const Wall* target = MapQueries::GetWallByID(level, wall.gatewayTarget);
                if (target == nullptr) continue;

                std::array<Gateways::Opening, 2> openings;
                const int openingCount = Gateways::Compute(level, wall, openings);
                const bool works = openingCount > 0;
                const Gateways::Opening& opening = openings[0];

                const Vector2 middle = (wall.start + wall.end) * 0.5f;
                const Vector2 targetMiddle = (target->start + target->end) * 0.5f;

                if (works) canvas.SetColor(190, 90, 255, 255);
                else canvas.SetColor(150, 150, 150, 255);

                const Vector2 start = works ? opening.StartPoint() : wall.start;
                const Vector2 end = works ? opening.EndPoint() : wall.end;
                canvas.ThickLine(Screen(start), Screen(end), 5.0f);

                for (int index = 0; index < openingCount; ++index) {
                    constexpr float TICK_PIXELS = 9.0f;
                    const Vector2 inward = openings[static_cast<size_t>(index)].inward;
                    const Vector2 tickEnd = middle + inward * (TICK_PIXELS / std::max(editorZoom, 0.0001f));
                    canvas.ThickLine(Screen(middle), Screen(tickEnd), 3.0f);
                }

                const Vector2 from = Screen(middle);
                const Vector2 to = Screen(targetMiddle);
                canvas.Line(from, to);

                const float dx = to.x - from.x;
                const float dy = to.y - from.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 24.0f) {
                    const float ux = dx / length;
                    const float uy = dy / length;
                    const Vector2 tip = {to.x - ux * 10.0f, to.y - uy * 10.0f};
                    constexpr float HEAD = 8.0f;
                    canvas.Line(tip, {tip.x - ux * HEAD - uy * HEAD * 0.6f, tip.y - uy * HEAD + ux * HEAD * 0.6f});
                    canvas.Line(tip, {tip.x - ux * HEAD + uy * HEAD * 0.6f, tip.y - uy * HEAD - ux * HEAD * 0.6f});
                }
            }
        }

        void BuildGatewayPickHighlight() {
            if (gatewayPickHoveredWallID == INVALID_ID) return;
            const Wall* wall = MapQueries::GetWallByID(LevelManager::CurrentLevel(), gatewayPickHoveredWallID);
            if (wall == nullptr) return;
            canvas.layer = Layer::AfterHighlights;
            AmberWall(*wall);
        }

        void BuildEntities(Frame& frame) {
            Level& level = LevelManager::CurrentLevel();

            frame.style.normalEntityColor = Rgba01(normalEntityColor);
            frame.style.spriteEntityColor = Rgba01(spriteEntityColor);
            frame.style.disabledEntityColor = Rgba01(disabledEntityColor);
            frame.style.highlightedEntityColor = Rgba01(highlightedEntityColor);
            frame.style.entityAlpha = static_cast<float>(static_cast<Uint8>(LayerAlpha(255, canvasLayerEntities))) / 255.0f;
            frame.style.entityFaintAlpha = static_cast<float>(static_cast<Uint8>(LayerAlpha(filteredAlpha, canvasLayerEntities))) / 255.0f;

            frame.style.showColliders = showColliderOutlines;
            frame.style.colliderColor = Rgba01(colliderOutlineColor);
            frame.style.triggerColor = Rgba01(triggerOutlineColor);
            frame.style.colliderThickness = colliderOutlineThickness;

            for (const Entity& entity : level.entities) {
                const ComponentTransform* transform = level.transforms.Get(entity.id);
                if (transform == nullptr) continue;

                const Vector2 half = EntityHalfSize(level, entity.id);
                const ComponentSprite* sprite = level.sprites.Get(entity.id);

                std::string texture;
                if (textureViewMode != TextureViewMode::Off && sprite != nullptr && !sprite->textureFileNames.empty() &&
                    GetEditorTexture(sprite->textureFileNames[0]) != nullptr)
                    texture = sprite->textureFileNames[0];

                const bool faint = (currentMode == MODE_ENTITY && !EntityPassesFilter(entity)) || currentMode != MODE_ENTITY;
                const bool inspected = editingEntity && entity.id == selectedEntity.id;

                std::uint32_t flags = 0;
                if (faint) flags |= Editor2D::ENTITY_FAINT;
                if (inspected) flags |= Editor2D::ENTITY_SELECTED;
                if (entity.id == hoveredEntityID && !inspected) flags |= Editor2D::ENTITY_HOVERED;

                const auto index = static_cast<std::uint32_t>(frame.entities.size());
                frame.entities.push_back({{transform->position.x, transform->position.z, half.x, half.y},
                                          {sprite != nullptr ? 1.0f : 0.0f, entity.enabled ? 1.0f : 0.0f, 0.0f, 0.0f}});
                frame.entityFlags.push_back(flags);
                Frame::ExtendRun(frame.entityRuns, texture, index, 1);

                if (!level.colliders.Has(entity.id)) continue;

                for (const ComponentCollider* collider : level.colliders.GetAll(entity.id)) {
                    if (collider == nullptr) continue;

                    const Vector3 base = collider->Base(*transform);
                    Editor2D::GpuCollider gpu{};

                    if (collider->IsRound()) {
                        const float radius = collider->Radius(*transform);
                        if (radius * editorZoom < 0.5f) continue;
                        gpu.shape = {base.x, base.z, radius, radius};
                    }
                    else {
                        const Vector3 size = collider->WorldSize(*transform);
                        if ((size.x * 0.5f + size.z * 0.5f) * editorZoom < 0.5f) continue;
                        gpu.shape = {base.x, base.z, size.x * 0.5f, size.z * 0.5f};
                    }

                    gpu.data = {collider->IsRound() ? 1.0f : 0.0f, collider->isTrigger ? 1.0f : 0.0f, collider->isActive ? 1.0f : 0.0f, 0.0f};
                    frame.colliders.push_back(gpu);
                    frame.colliderFlags.push_back(faint ? Editor2D::COLLIDER_FAINT : 0u);
                }
            }
        }

        // ---- Top layer ----------------------------------------------------

        void BuildEventLinks() {
            const Level& level = LevelManager::CurrentLevel();
            canvas.layer = Layer::Top;

            std::vector<EventLinks::Link> links;
            const auto add = [&](const EventOwnerKind kind, const ID id) {
                const std::vector<EventLinks::Link> found = EventLinks::Collect(level, kind, id);
                links.insert(links.end(), found.begin(), found.end());
            };

            if (editingWall && selectedWallID != INVALID_ID) add(EventOwnerKind::LINE, selectedWallID);
            if (editingLinedef && selectedLinedefID != INVALID_ID) add(EventOwnerKind::LINEDEF, selectedLinedefID);
            if (editingSector && selectedSectorID != INVALID_ID) add(EventOwnerKind::SECTOR, selectedSectorID);
            if (editingEntity && selectedEntity.id != INVALID_ID) add(EventOwnerKind::ENTITY, selectedEntity.id);

            const auto instigatorLabel = [&](const EventOwnerKind kind, const ID id) {
                Vector3 anchor;
                if (EventLinks::CallsInstigator(level, kind, id) && EventLinks::AnchorPoint(level, kind, id, anchor))
                    WorldLabel(Localisation::Get("events.links.instigator"), {anchor.x, anchor.z}, Rgba01(eventLinkColor));
            };

            if (editingWall && selectedWallID != INVALID_ID) instigatorLabel(EventOwnerKind::LINE, selectedWallID);
            if (editingLinedef && selectedLinedefID != INVALID_ID) instigatorLabel(EventOwnerKind::LINEDEF, selectedLinedefID);
            if (editingSector && selectedSectorID != INVALID_ID) instigatorLabel(EventOwnerKind::SECTOR, selectedSectorID);
            if (editingEntity && selectedEntity.id != INVALID_ID) instigatorLabel(EventOwnerKind::ENTITY, selectedEntity.id);

            if (links.empty()) return;

            canvas.SetBlend(true);
            const Vector4 color = Rgba01(eventLinkColor);

            for (const EventLinks::Link& link : links) {
                const Vector2 from = Screen({link.from.x, link.from.z});
                const Vector2 to = Screen({link.to.x, link.to.z});

                const float dx = to.x - from.x;
                const float dy = to.y - from.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length <= Constants::Epsilon) continue;

                const Vector2 direction = {dx / length, dy / length};
                const float headLength = std::min(length, 8.0f + eventLinkThickness * 3.0f);
                const float headHalfWidth = 4.0f + eventLinkThickness * 1.5f;
                const Vector2 headBase = {to.x - direction.x * headLength, to.y - direction.y * headLength};

                WallLine(from, headBase, eventLinkThickness, eventLinkColor, 255);

                const Vector2 side = {-direction.y * headHalfWidth, direction.x * headHalfWidth};
                canvas.GeometryTriangle(to, {headBase.x + side.x, headBase.y + side.y}, {headBase.x - side.x, headBase.y - side.y}, color);
            }
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

            canvas.SetColor(color.r, color.g, color.b, 255);
            canvas.ThickLine(start, headBase, SHAFT_THICKNESS);

            canvas.GeometryTriangle(tip, {headBase.x - dirY * HEAD_HALF_WIDTH, headBase.y + dirX * HEAD_HALF_WIDTH},
                                    {headBase.x + dirY * HEAD_HALF_WIDTH, headBase.y - dirX * HEAD_HALF_WIDTH}, Rgba01(color));
        }

        void BuildEntityMoveGizmo() {
            EntityGizmoLayout layout;
            if (!GetEntityGizmoLayout(&layout)) return;
            canvas.layer = Layer::Top;

            EntityGizmoPart highlighted = GetActiveEntityGizmoPart();
            if (highlighted == GIZMO_NONE && mapMouseFree) highlighted = PickEntityGizmoPart(MapMousePosition());

            const auto colorFor = [highlighted](const EntityGizmoPart part, const Vector3& color) -> const Vector3& {
                return highlighted == part ? highlightedHandleColor : color;
            };

            GizmoArrow(layout.xStart, layout.xTip, colorFor(GIZMO_X, gizmoXColor));
            GizmoArrow(layout.zStart, layout.zTip, colorFor(GIZMO_Z, gizmoZColor));

            const Vector3& squareColor = colorFor(GIZMO_FREE, gizmoYColor);
            canvas.SetColor(squareColor.r, squareColor.g, squareColor.b, 255);
            canvas.FillRect(layout.square);
            canvas.SetColor(handleOutlineColor.r, handleOutlineColor.g, handleOutlineColor.b, 255);
            canvas.Rect(layout.square);
        }

        void BuildSelectionBox() {
            Vector2 startWorld{};
            if (!GetSelectionBoxStart(&startWorld)) return;
            canvas.layer = Layer::Top;

            const Vector2 start = Screen(startWorld);
            const Vector2 end = MapMousePosition();
            const SDL_FRect box = {std::min(start.x, end.x), std::min(start.y, end.y), std::fabs(end.x - start.x), std::fabs(end.y - start.y)};

            canvas.SetBlend(true);
            canvas.SetColor(90, 150, 255, 40);
            canvas.FillRect(box);
            canvas.SetColor(90, 150, 255, 200);
            canvas.Rect(box);
            canvas.SetBlend(false);
        }

        void BuildPaintSelectBrush() {
            if (!IsPaintSelectActive(!mapMouseFree)) return;
            canvas.layer = Layer::Top;

            constexpr int SEGMENTS = 32;
            const Vector2 center = MapMousePosition();

            canvas.SetBlend(true);
            canvas.SetColor(highlightedHandleColor.r, highlightedHandleColor.g, highlightedHandleColor.b, 220);

            for (int i = 0; i < SEGMENTS; ++i) {
                const float a0 = 2.0f * Constants::Pi * i / SEGMENTS;
                const float a1 = 2.0f * Constants::Pi * (i + 1) / SEGMENTS;
                canvas.Line({center.x + std::cos(a0) * paintSelectRadius, center.y + std::sin(a0) * paintSelectRadius},
                            {center.x + std::cos(a1) * paintSelectRadius, center.y + std::sin(a1) * paintSelectRadius});
            }

            canvas.SetBlend(false);
        }

        // ---- Background and grid ------------------------------------------

        void BuildBackgroundImage(Frame& frame) {
            const EditorBackgroundImage& image = LevelManager::CurrentLevel().backgroundImage;
            if (!image.visible || image.texture.empty() || image.opacity <= 0.0f) return;
            if (GetEditorTexture(image.texture) == nullptr) return;

            frame.background.texture = image.texture;
            frame.background.tileScale = image.scale * editorZoom;
            frame.background.origin = Screen({0.0f, 0.0f});
            frame.background.opacity = image.opacity;
        }

        constexpr int MAJOR_GRID_LINE_INTERVAL = 8;

        bool IsOnMajorGridLine(const float worldCoord) {
            const float stepsFromOrigin = worldCoord / GRID_SIZE;
            const float rounded = std::round(stepsFromOrigin);
            if (std::fabs(stepsFromOrigin - rounded) > 0.01f) return false;
            return std::llround(rounded) % MAJOR_GRID_LINE_INTERVAL == 0;
        }

        void BuildGrid() {
            canvas.layer = Layer::AfterSectors;

            constexpr float minorDotSize = 3.0f;
            constexpr float majorDotSize = 5.0f;

            const float activeGridSize = GetActiveGridSize();

            const float visibleHalfWidthWorld = (screenWidth * 0.5f) / editorZoom;
            const float visibleHalfHeightWorld = (screenHeight * 0.5f) / editorZoom;

            float leftGrid = std::numeric_limits<float>::max();
            float rightGrid = std::numeric_limits<float>::lowest();
            float bottomGrid = std::numeric_limits<float>::max();
            float topGrid = std::numeric_limits<float>::lowest();

            for (const float cornerX : {-visibleHalfWidthWorld, visibleHalfWidthWorld}) {
                for (const float cornerY : {-visibleHalfHeightWorld, visibleHalfHeightWorld}) {
                    const Vector2 corner = WorldToGrid({cameraPos.x + cornerX, cameraPos.y + cornerY});
                    leftGrid = std::min(leftGrid, corner.x);
                    rightGrid = std::max(rightGrid, corner.x);
                    bottomGrid = std::min(bottomGrid, corner.y);
                    topGrid = std::max(topGrid, corner.y);
                }
            }

            // Each line is index * size rather than a running sum, so float
            // error doesn't build up across the screen at small grid sizes.
            const double gridStep = activeGridSize;
            const auto firstIndexX = static_cast<int64_t>(std::floor(leftGrid / gridStep));
            const auto lastIndexX = static_cast<int64_t>(std::floor(rightGrid / gridStep));
            const auto firstIndexY = static_cast<int64_t>(std::floor(bottomGrid / gridStep));
            const auto lastIndexY = static_cast<int64_t>(std::floor(topGrid / gridStep));
            const auto lineAt = [gridStep](const int64_t index) { return static_cast<float>(static_cast<double>(index) * gridStep); };

            canvas.SetBlend(true);

            if (gridStyle == GridStyle::Lines) {
                constexpr float MINOR_LINE_ALPHA_SCALE = 0.5f;

                const auto drawLine = [](const Vector2& startGrid, const Vector2& endGrid, const bool major) {
                    const float alpha = major ? gridColor.a : gridColor.a * MINOR_LINE_ALPHA_SCALE;
                    canvas.SetColor(gridColor.r * 255.0f, gridColor.g * 255.0f, gridColor.b * 255.0f, alpha * 255.0f);
                    canvas.Line(Screen(GridToWorld(startGrid)), Screen(GridToWorld(endGrid)));
                };

                for (int64_t ix = firstIndexX; ix <= lastIndexX; ++ix) {
                    const float gridX = lineAt(ix);
                    drawLine({gridX, bottomGrid}, {gridX, topGrid}, IsOnMajorGridLine(gridX));
                }
                for (int64_t iy = firstIndexY; iy <= lastIndexY; ++iy) {
                    const float gridY = lineAt(iy);
                    drawLine({leftGrid, gridY}, {rightGrid, gridY}, IsOnMajorGridLine(gridY));
                }
            }
            else {
                canvas.SetColor(gridColor.r * 255.0f, gridColor.g * 255.0f, gridColor.b * 255.0f, gridColor.a * 255.0f);

                for (int64_t ix = firstIndexX; ix <= lastIndexX; ++ix) {
                    const float gridX = lineAt(ix);
                    const bool majorX = IsOnMajorGridLine(gridX);

                    for (int64_t iy = firstIndexY; iy <= lastIndexY; ++iy) {
                        const float gridY = lineAt(iy);
                        const bool major = majorX && IsOnMajorGridLine(gridY);
                        const float dotSize = major ? majorDotSize : minorDotSize;
                        const Vector2 screenPos = Screen(GridToWorld({gridX, gridY}));
                        canvas.FillRect({screenPos.x - dotSize * 0.5f, screenPos.y - dotSize * 0.5f, dotSize, dotSize});
                    }
                }
            }

            constexpr float ORIGIN_CROSS_HALF_SIZE = 8.0f;
            const Vector2 origin = Screen({0.0f, 0.0f});
            canvas.SetColor(themeTextColor.r, themeTextColor.g, themeTextColor.b, 200);
            canvas.Line({origin.x - ORIGIN_CROSS_HALF_SIZE, origin.y}, {origin.x + ORIGIN_CROSS_HALF_SIZE, origin.y});
            canvas.Line({origin.x, origin.y - ORIGIN_CROSS_HALF_SIZE}, {origin.x, origin.y + ORIGIN_CROSS_HALF_SIZE});

            canvas.SetBlend(false);
        }
    }

    void BuildMapEditorFrame(Editor2D::Frame& frame) {
        using namespace MapCanvasFrame;

        frame.Clear();
        frame.view = {cameraPos, editorZoom, screenWidth, screenHeight};
        frame.style = {};
        frame.style.clearColor = Rgba01(backgroundColor);

        canvas = {};
        canvas.frame = &frame;

        // Same order as Editor::Update's STATE_MAP branch.
        BuildBackgroundImage(frame);
        BuildSectors(frame);
        BuildGrid();

        canvas.SetBlend(true); // DrawWalls
        BuildWalls(frame);
        BuildLinedefs(frame);
        BuildGeometryEditOverlay(frame);
        BuildPickHighlights();
        BuildGatewayLinks();
        BuildGatewayPickHighlight();

        BuildEntities(frame);
        canvas.SetBlend(true); // DrawEntities and DrawColliderOutlines leave it on
        BuildEventLinks();
        BuildEntityMoveGizmo();
        BuildSelectionBox();

        canvas.layer = Layer::Top;
        if (currentMode == MODE_SECTOR || currentMode == MODE_GEOMETRY) SectorPreview();

        BuildPaintSelectBrush();
    }
}
