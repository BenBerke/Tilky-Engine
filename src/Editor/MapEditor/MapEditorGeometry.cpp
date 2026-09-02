#include "../EditorInternal.hpp"

#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp" // This includes "SSECompat.hpp"
#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <spdlog/spdlog.h>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/MapTopology.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

#include <cstdio>

// This is an internal file for functions related to mathematical calculations about the map,
// plus (after the editor revamp) the Sector Mode chain workflow, the internal
// Dot lifecycle, and Geometry Mode's wall picking/selection/editing.
namespace MapEditorInternal {
    float GetActiveGridSize() {
        constexpr float minPixelSpacing = 24.0f;

        float activeGridSize = GRID_SIZE;

        const float safeZoom = std::max(editorZoom, 0.0001f);

        while (activeGridSize * safeZoom < minPixelSpacing) {
            activeGridSize *= 2.0f;
        }

        return activeGridSize;
    }

    bool SamePoint(const Vector2& a, const Vector2& b) {
        return a == b;
    }

    bool WithinRadius(const Vector2& a, const Vector2& b, const float radius) {
        return Vector2Math::DistanceSquared(a, b) < radius * radius;
    }

    // Doing this in SIMD is probably overkill but whatever
    bool AABBCollisionWithEntity(const ComponentUITransform& transform, const Vector2& mousePosition) {
        const __m128 pos = transform.resolvedPosition.reg;
        const __m128 size = transform.resolvedSize.reg;
        const __m128 mouse = mousePosition.reg;

        const __m128 bottomRight = _mm_add_ps(pos, size);

        const __m128 gte = _mm_cmpge_ps(mouse, pos);
        const __m128 lte = _mm_cmple_ps(mouse, bottomRight);

        const __m128 result = _mm_and_ps(gte, lte);

        return (_mm_movemask_ps(result) & 3) == 3;
    }

    bool AABBCollisionWithEntity(const ComponentTransform& transform, const Vector2& mousePosition) {
        const Vector2 planarPosition = {transform.position.x,transform.position.z};

        const Vector2 planarScale = {transform.scale.x, transform.scale.z};

        const __m128 entityPos = planarPosition.reg;
        const __m128 entityScale = planarScale.reg;
        const __m128 mousePos = mousePosition.reg;

        const __m128 halfOnes = _mm_set1_ps(0.5f);
        const __m128 entityHalfSize = _mm_mul_ps(entityScale, halfOnes);
        const __m128 mouseHalfSize = _mm_set1_ps(0.05f);

        const __m128 entityMin = _mm_sub_ps(entityPos, entityHalfSize);
        const __m128 entityMax = _mm_add_ps(entityPos, entityHalfSize);

        const __m128 mouseMin = _mm_sub_ps(mousePos, mouseHalfSize);
        const __m128 mouseMax = _mm_add_ps(mousePos, mouseHalfSize);

        const __m128 c1 = _mm_cmple_ps(entityMin, mouseMax);
        const __m128 c2 = _mm_cmpge_ps(entityMax, mouseMin);

        const __m128 result = _mm_and_ps(c1, c2);

        return (_mm_movemask_ps(result) & 3) == 3;
    }

    Entity* EntityAt(const Vector2& mouseClick) {
        Level& level = LevelManager::CurrentLevel();

        for (Entity& entity : level.entities) {
            const ComponentTransform* transform = level.transforms.Get(entity.id);
            const ComponentUITransform* uiTransform = level.ui_transforms.Get(entity.id);

            if (transform != nullptr) {
                if (AABBCollisionWithEntity(*transform, mouseClick)) return &entity;
            }
            else if (uiTransform != nullptr) if (AABBCollisionWithEntity(*uiTransform, mouseClick)) return &entity;
        }

        return nullptr;
    }

    bool HasLineBetween(const Vector2& a, const Vector2& b) {
        Level& level = LevelManager::CurrentLevel();

        for (const Wall& wall : level.walls) {
            const bool sameDirection = SamePoint(wall.start, a) && SamePoint(wall.end, b);
            const bool oppositeDirection = SamePoint(wall.start, b) && SamePoint(wall.end, a);

            if (sameDirection || oppositeDirection) return true;
        }

        return false;
    }

    std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate() {
        std::vector<Vector2> result = sectorBeingCreated;

        if (result.size() >= 2 && SamePoint(result.front(), result.back())) result.pop_back();

        return result;
    }

    bool IsSectorClosed(const std::vector<Vector2>& vertices) {
        if (vertices.size() < 3) return false;

        if (sectorBeingCreated.size() >= 4 && SamePoint(sectorBeingCreated.front(), sectorBeingCreated.back()))
            return true;

        for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
            const Vector2& a = vertices[i];
            const Vector2& b = vertices[(i + 1) % vertices.size()];

            if (!HasLineBetween(a, b)) return false;
        }

        return true;
    }

    // Pushes `point` onto the in-progress chain, rejecting it if it
    // duplicates any vertex already in the chain. Closing-on-the-first-point
    // is intercepted earlier by TrySectorChainClick, so by the time this is
    // called we know `point` isn't a valid closing click.
    void AddSectorSelectionPoint(const Vector2& point) {
        if (sectorBeingCreated.size() >= 3 && SamePoint(point, sectorBeingCreated.front())) {
            sectorBeingCreated.push_back(point);
            return;
        }

        for (const Vector2& existingPoint : sectorBeingCreated)
            if (SamePoint(existingPoint, point))
                return;

        sectorBeingCreated.push_back(point);
    }

    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos) {
        return {
            (worldPos.x - cameraPos.x) * editorZoom + screenWidth * 0.5f,
            screenHeight * 0.5f - (worldPos.y - cameraPos.y) * editorZoom
        };
    }

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos) {
        return {
            (screenPos.x - screenWidth * 0.5f) / editorZoom + cameraPos.x,
            (screenHeight * 0.5f - screenPos.y) / editorZoom + cameraPos.y
        };
    }

    // BUG FIX: this used to snap against GetActiveGridSize(), which
    // doubles as the camera zooms out (to keep DrawGridDots() from
    // rendering an unreadably dense mass of dots). That's exactly right
    // for *rendering*, but using the same value here meant the actual
    // snapping interval silently changed with zoom - panning/zooming
    // out would make everything snap to a coarser grid than what was
    // used a moment ago at a closer zoom. Snapping must always use the
    // fixed, user-set GRID_SIZE, independent of editorZoom.
    Vector2 SnapToGrid(const Vector2& worldPos) {
        const float gridSize = std::max(GRID_SIZE, MIN_GRID_SIZE);

        return {
            std::round(worldPos.x / gridSize) * gridSize,
            std::round(worldPos.y / gridSize) * gridSize
        };
    }

    // Snap priority is {dots, wall starts, wall ends} (nearest within
    // radius wins); if nothing there is close enough, falls back to the
    // closest point along any wall's *interior* within radius; only
    // falls back to grid snapping if nothing at all is close by.
    // Snapping onto a wall's interior is what lets a drawn chain start
    // or end mid-wall - committing the chain is what actually splits the
    // wall there (see ApplyDrawnGeometry / MapTopology::ApplyDrawnGeometry).
    //
    // The exclusion lists exist for Geometry Mode's drags: a dragged
    // endpoint is itself a snap candidate, and the wall it belongs to is
    // itself a wall-interior candidate, so without them a drag snaps
    // straight back onto where it started and nothing ever moves.
    Vector2 ResolveSnapPointExcluding(const Vector2& mouseWorld,
                                      const std::vector<Vector2>& ignoredPoints,
                                      const std::vector<ID>& ignoredWallIDs) {
        if (vertexSnapEnabled) {
            constexpr float snapRadiusPixels = 12.0f;
            const float safeZoom = std::max(editorZoom, 0.0001f);
            const float snapRadiusWorld = snapRadiusPixels / safeZoom;
            const float snapRadiusSq = snapRadiusWorld * snapRadiusWorld;

            Vector2 best{};
            float bestDistSq = std::numeric_limits<float>::max();
            bool found = false;

            const auto isIgnoredPoint = [&](const Vector2& candidate) {
                for (const Vector2& ignored : ignoredPoints)
                    if (SamePoint(ignored, candidate)) return true;

                return false;
            };

            const auto consider = [&](const Vector2& candidate) {
                if (isIgnoredPoint(candidate)) return;

                const float distSq = Vector2Math::DistanceSquared(mouseWorld, candidate);

                if (distSq <= snapRadiusSq && distSq < bestDistSq) {
                    bestDistSq = distSq;
                    best = candidate;
                    found = true;
                }
            };

            for (const Dot& dot : dots) {
                consider(dot.position);
            }

            const Level& level = LevelManager::CurrentLevel();

            // An ignored wall contributes neither its endpoints nor its
            // interior. Ignoring only the *original* positions isn't
            // enough: mid-drag a moving wall's stored coordinates are
            // already wherever the cursor last put them, so leaving the
            // wall in would let the drag snap onto itself and freeze one
            // frame in.
            const bool filteringWalls = !ignoredWallIDs.empty();

            std::vector<Wall> candidateWalls;

            if (filteringWalls) {
                candidateWalls.reserve(level.walls.size());

                for (const Wall& wall : level.walls) {
                    const bool ignored =
                        std::find(ignoredWallIDs.begin(), ignoredWallIDs.end(), wall.id) != ignoredWallIDs.end();

                    if (!ignored) candidateWalls.push_back(wall);
                }
            }

            const std::vector<Wall>& snapCandidateWalls = filteringWalls ? candidateWalls : level.walls;

            for (const Wall& wall : snapCandidateWalls) {
                consider(wall.start);
                consider(wall.end);
            }

            if (found) return best;

            Vector2 onWall{};
            if (MapTopology::ClosestPointOnAnyWall(snapCandidateWalls, mouseWorld, snapRadiusWorld, &onWall)) return onWall;
        }

        if (gridSnapEnabled) return SnapToGrid(mouseWorld);

        return mouseWorld;
    }

    Vector2 ResolveSnapPoint(const Vector2& mouseWorld) {
        return ResolveSnapPointExcluding(mouseWorld, {}, {});
    }

    bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point) {
        bool inside = false;

        for (int i = 0, j = static_cast<int>(polygon.size()) - 1;
             i < static_cast<int>(polygon.size());
             j = i++) {

            const Vector2& a = polygon[i];
            const Vector2& b = polygon[j];

            const bool crossesY = (a.y > point.y) != (b.y > point.y);

            if (crossesY) {
                const float intersectX =
                    (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;

                if (point.x < intersectX) {
                    inside = !inside;
                }
            }
        }

        return inside;
    }

    namespace {
        // True if `point` (already resolved/snapped) coincides with an
        // existing Dot, an existing wall's endpoint, or an existing
        // wall's interior - i.e. it's geometry that was already part of
        // the level before this click, as opposed to a fresh point in
        // open space. Endpoint/dot matching is exact equality
        // deliberately: `point` only ever reaches here by way of
        // ResolveSnapPoint, which snaps to the exact stored coordinate of
        // whatever it matched, so comparing against that same stored
        // value is safe - this isn't the "two independently computed
        // floats" case map topology has to guard against.
        bool IsPointOnExistingGeometry(const Vector2& point) {
            for (const Dot& dot : dots) {
                if (SamePoint(dot.position, point)) return true;
            }

            const Level& level = LevelManager::CurrentLevel();

            for (const Wall& wall : level.walls) {
                if (SamePoint(wall.start, point) || SamePoint(wall.end, point)) return true;
                if (MapTopology::PointOnSegmentInterior(point, wall.start, wall.end)) return true;
            }

            return false;
        }

        void RebuildDotIDLookup() {
            dotIDToIndex.clear();

            for (int i = 0; i < static_cast<int>(dots.size()); ++i) {
                dotIDToIndex[dots[i].id] = i;
            }
        }

        // A point that's genuinely inside `sector`, holes included -
        // the centroid of its first triangle, since the triangles are
        // already the hole-aware ones. A plain vertex average would sit
        // inside the hole for a sector with something nested in it,
        // which is exactly the case this is needed for.
        Vector2 SectorInteriorPoint(const Sector& sector) {
            if (!sector.triangles.empty()) {
                const Triangle& triangle = sector.triangles.front();

                return {
                    (triangle.a.x + triangle.b.x + triangle.c.x) / 3.0f,
                    (triangle.a.y + triangle.b.y + triangle.c.y) / 3.0f
                };
            }

            Vector2 sum{0.0f, 0.0f};
            for (const Vector2& vertex : sector.vertices) {
                sum.x += vertex.x;
                sum.y += vertex.y;
            }

            const float count = std::max(1.0f, static_cast<float>(sector.vertices.size()));
            return {sum.x / count, sum.y / count};
        }

        // Finds the sector that currently lists `inner` as one of its
        // holes, if any, reporting which loop that is through
        // `outLoopIndex`. Once `inner` is deleted that loop is provably
        // stale, and DeleteSector has to drop it before re-tracing -
        // otherwise reconciliation still believes the enclosing sector
        // has a hole there and refuses to match it to its own face.
        ID FindEnclosingSectorID(const Level& level, const Sector& inner, int* outLoopIndex) {
            const Vector2 interior = SectorInteriorPoint(inner);

            for (const Sector& candidate : level.sectors) {
                if (candidate.id == inner.id) continue;

                for (int loopIndex = 0; loopIndex < static_cast<int>(candidate.innerLoops.size()); ++loopIndex) {
                    if (!Geometry::IsPointInPolygon(candidate.innerLoops[loopIndex], interior)) continue;

                    *outLoopIndex = loopIndex;
                    return candidate.id;
                }
            }

            return INVALID_ID;
        }

        PendingSectorParams BuildPendingSectorParams() {
            PendingSectorParams params;
            params.wallTexture = wallTexture;
            params.floors = {
                {
                    {floorHeight, floorColor, floorTexture},
                    {ceilHeight, ceilColor, ceilTexture}
                }
            };
            params.lightValue = lightValue;
            params.wallColor = wallColor;
            return params;
        }
    }



    void CancelSectorChain() {
        sectorBeingCreated.clear();
    }

    // Closes the in-progress chain back onto its own starting point and
    // commits it. A closed, non-self-intersecting chain of >= 3 points
    // always bounds at least itself, so unlike TrySectorChainClick's
    // other commit path there's no need to check whether it would
    // enclose anything first.
    void FinishSectorSelection() {
        if (sectorBeingCreated.size() < 3) {
            sectorBeingCreated.clear();
            return;
        }

        std::vector<Vector2> closedChain = sectorBeingCreated;
        closedChain.push_back(sectorBeingCreated.front());

        ApplyDrawnGeometry(closedChain, pendingSectorParams);

        sectorBeingCreated.clear();
    }

    // Manual Mode: only accept a click that lands on a real existing
    // corner (a Dot, or a wall's start/end) — no grid fallback.
    bool FindExistingCorner(const Vector2& mouseWorld, Vector2* outPoint) {
        constexpr float pickRadiusPixels = 12.0f;
        const float safeZoom = std::max(editorZoom, 0.0001f);
        const float pickRadiusWorld = pickRadiusPixels / safeZoom;
        const float pickRadiusSq = pickRadiusWorld * pickRadiusWorld;

        float bestDistSq = std::numeric_limits<float>::max();
        bool found = false;

        const auto consider = [&](const Vector2& candidate) {
            const float distSq = Vector2Math::DistanceSquared(mouseWorld, candidate);
            if (distSq <= pickRadiusSq && distSq < bestDistSq) {
                bestDistSq = distSq;
                *outPoint = candidate;
                found = true;
            }
        };

        for (const Dot& dot : dots) consider(dot.position);

        const Level& level = LevelManager::CurrentLevel();
        for (const Wall& wall : level.walls) {
            consider(wall.start);
            consider(wall.end);
        }

        return found;
    }

    void TryManualCornerClick(const Vector2& point) {
        Vector2 corner{};
        if (!FindExistingCorner(point, &corner)) {
            return; // not a click on a real dot/wall endpoint — ignore
        }

        for (const Vector2& existing : manualSectorDots)
            if (SamePoint(existing, corner)) return; // already picked

        manualSectorDots.push_back(corner);
    }

    void TrySectorChainClick(const Vector2& point) {
        if (manualSectorMode) {
            TryManualCornerClick(point);
            return;
        }

        if (sectorBeingCreated.empty()) {
            // Snapshot the Editor menu's current sector params now, so
            // fiddling with them mid-chain can't retroactively change the
            // sector that's about to be created.
            pendingSectorParams = BuildPendingSectorParams();

            sectorBeingCreated.push_back(point);
            return;
        }

        // Closing back on the chain's own start always commits.
        if (sectorBeingCreated.size() >= 3 && SamePoint(point, sectorBeingCreated.front())) {
            FinishSectorSelection();
            return;
        }

        // Landing on existing geometry only commits if the chain would
        // actually enclose something new yet; otherwise it's just a
        // pass-through point (e.g. touching one existing corner on the
        // way to somewhere else), so keep building the chain.
        if (IsPointOnExistingGeometry(point)) {
            std::vector<Vector2> tentative = sectorBeingCreated;
            tentative.push_back(point);

            if (MapTopology::WouldEncloseNewFace(LevelManager::CurrentLevel(), tentative)) {
                ApplyDrawnGeometry(tentative, pendingSectorParams);
                sectorBeingCreated.clear();
                return;
            }
        }

        AddSectorSelectionPoint(point);
    }

    // =========================================================================
    //  Sector Mode — drawing tools (Rectangle / Polygon / Circle / Curve)
    // =========================================================================
    //
    // All four tools funnel their finished shape through the same
    // CommitDrawnShape() -> ApplyDrawnGeometry() path the freehand chain
    // above already uses, so they get the same wall/sector-splitting,
    // undo snapshotting, and rejection reporting for free.

    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        bool NearlyEqualPoints(const Vector2& a, const Vector2& b) {
            constexpr float epsilon = 0.01f;
            return Vector2Math::DistanceSquared(a, b) < epsilon * epsilon;
        }

        // Standard orientation-based *proper* segment intersection test -
        // segments that only touch at an endpoint (as every pair of
        // adjacent polygon edges does) are deliberately not flagged.
        float SignedArea2(const Vector2& o, const Vector2& a, const Vector2& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }

        bool ProperSegmentsIntersect(const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& p4) {
            const float d1 = SignedArea2(p3, p4, p1);
            const float d2 = SignedArea2(p3, p4, p2);
            const float d3 = SignedArea2(p1, p2, p3);
            const float d4 = SignedArea2(p1, p2, p4);

            const bool straddles1 = (d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f);
            const bool straddles2 = (d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f);

            return straddles1 && straddles2;
        }

        // Validates and normalises `loop` in place: strips duplicate
        // consecutive points, requires >= 3 unique corners, rejects a
        // self-intersecting boundary, then re-closes the ring (repeats
        // the first point) the way ApplyDrawnGeometry expects. Leaves
        // `loop` unspecified-but-valid-to-discard on rejection.
        bool ValidateClosedLoop(std::vector<Vector2>& loop, std::string* outError) {
            if (loop.size() >= 2 && SamePoint(loop.front(), loop.back())) loop.pop_back();

            loop = DedupeConsecutivePoints(loop);

            if (loop.size() < 3) {
                *outError = Localisation::Get("editor.draw.error.min_corners");
                return false;
            }

            if (ClosedLoopSelfIntersects(loop)) {
                *outError = Localisation::Get("editor.draw.error.self_intersect");
                return false;
            }

            loop.push_back(loop.front());
            return true;
        }

        bool ValidateOpenPolyline(std::vector<Vector2>& points, std::string* outError) {
            points = DedupeConsecutivePoints(points);

            if (points.size() < 2) {
                *outError = Localisation::Get("editor.draw.error.min_points");
                return false;
            }

            return true;
        }

        // Shared commit path for every drawing tool below (freehand's
        // FinishSectorSelection/manual sector's CreateManualSector go
        // through ApplyDrawnGeometry directly instead, since they have
        // their own validation history already). Reports rejections
        // through lastGeometryError exactly like ApplyDrawnGeometry does
        // on its own, so the toast in DrawEditorUI picks up either kind.
        bool CommitClosedShape(std::vector<Vector2> loop) {
            std::string error;

            if (!ValidateClosedLoop(loop, &error)) {
                lastGeometryError = error;
                return false;
            }

            return ApplyDrawnGeometry(loop, BuildPendingSectorParams());
        }

        bool CommitOpenShape(std::vector<Vector2> points) {
            std::string error;

            if (!ValidateOpenPolyline(points, &error)) {
                lastGeometryError = error;
                return false;
            }

            return ApplyDrawnGeometry(points, BuildPendingSectorParams());
        }

        // Freehand's confirm (Enter) path: commits the in-progress chain
        // exactly as it was drawn, without adding a closing edge back to the
        // start the way FinishSectorSelection does. MapTopology only raises a
        // face where the drawn points actually enclose one, so an open chain
        // committed here just lays its walls down - splitting whatever they
        // land on or cross, same as any other drawn geometry - and stops
        // there. A chain the user already closed by hand still becomes a
        // sector, because its own points enclose the face on their own.
        //
        // Unlike CommitOpenShape this uses the chain's snapshotted
        // pendingSectorParams rather than re-reading the Editor menu, so a
        // chain commits with the properties that were showing when it was
        // started - the same rule FinishSectorSelection follows.
        bool CommitSectorChainAsDrawn() {
            std::vector<Vector2> chain = sectorBeingCreated;
            std::string error;

            if (!ValidateOpenPolyline(chain, &error)) {
                lastGeometryError = error;
                return false;
            }

            return ApplyDrawnGeometry(chain, pendingSectorParams);
        }

        void HandleRectangleClick(const Vector2& rawMouseWorld) {
            if (!rectangleHasFirstCorner) {
                rectangleFirstCorner = ResolveSnapPoint(rawMouseWorld);
                rectangleHasFirstCorner = true;
                return;
            }

            const Vector2 opposite = ResolveRectangleCorner(rawMouseWorld);

            const float width = std::fabs(opposite.x - rectangleFirstCorner.x);
            const float height = std::fabs(opposite.y - rectangleFirstCorner.y);

            if (width < MIN_DRAW_SHAPE_DIMENSION || height < MIN_DRAW_SHAPE_DIMENSION) {
                lastGeometryError = Localisation::Get("editor.draw.error.rectangle_zero_size");
                return; // stay in progress so the user can just move and click again
            }

            const Vector2 a = rectangleFirstCorner;
            const Vector2 b = {opposite.x, rectangleFirstCorner.y};
            const Vector2 c = opposite;
            const Vector2 d = {rectangleFirstCorner.x, opposite.y};

            if (CommitClosedShape({a, b, c, d})) rectangleHasFirstCorner = false;
        }

        void HandlePolygonClick(const Vector2& rawMouseWorld) {
            if (!polygonHasCenter) {
                polygonCenter = ResolveSnapPoint(rawMouseWorld);
                polygonHasCenter = true;
                return;
            }

            const Vector2 handle = ResolvePolygonHandle(rawMouseWorld);
            const float radius = std::sqrt(Vector2Math::DistanceSquared(polygonCenter, handle));

            if (radius < MIN_DRAW_SHAPE_DIMENSION) {
                lastGeometryError = Localisation::Get("editor.draw.error.polygon_radius");
                return;
            }

            if (CommitClosedShape(BuildRegularPolygon(polygonCenter, handle, polygonSideCount)))
                polygonHasCenter = false;
        }

        void HandleCircleClick(const Vector2& rawMouseWorld) {
            if (!circleHasCenter) {
                circleCenter = ResolveSnapPoint(rawMouseWorld);
                circleHasCenter = true;
                return;
            }

            const Vector2 handle = ResolveCircleHandle(rawMouseWorld);
            const float radiusX = std::fabs(handle.x - circleCenter.x);
            const float radiusY = std::fabs(handle.y - circleCenter.y);

            if (radiusX < MIN_DRAW_SHAPE_DIMENSION || radiusY < MIN_DRAW_SHAPE_DIMENSION) {
                lastGeometryError = Localisation::Get("editor.draw.error.circle_radius");
                return;
            }

            if (CommitClosedShape(BuildEllipse(circleCenter, radiusX, radiusY, circleSegments)))
                circleHasCenter = false;
        }

        void HandleCurveClick(const Vector2& rawMouseWorld) {
            switch (curveStage) {
                case CURVE_STAGE_START:
                    curveStart = ResolveSnapPoint(rawMouseWorld);
                    curveStage = CURVE_STAGE_END;
                    break;

                case CURVE_STAGE_END: {
                    const Vector2 end = ResolveCurveEnd(rawMouseWorld);

                    if (Vector2Math::DistanceSquared(curveStart, end) < MIN_DRAW_SHAPE_DIMENSION * MIN_DRAW_SHAPE_DIMENSION) {
                        lastGeometryError = Localisation::Get("editor.draw.error.curve_same_point");
                        return;
                    }

                    curveEnd = end;
                    curveStage = CURVE_STAGE_CONTROL;
                    break;
                }

                case CURVE_STAGE_CONTROL: {
                    const Vector2 control = ResolveSnapPoint(rawMouseWorld);
                    const std::vector<Vector2> curvePoints = BuildQuadraticCurve(curveStart, control, curveEnd, curveSubdivisions);

                    if (CommitOpenShape(curvePoints)) curveStage = CURVE_STAGE_START;
                    break;
                }

                default: break;
            }
        }
    }

    bool IsConstrainModifierHeld() {
        return InputManager::GetKey(SDL_SCANCODE_LSHIFT) || InputManager::GetKey(SDL_SCANCODE_RSHIFT);
    }

    Vector2 ConstrainToAngleStep(const Vector2& reference, const Vector2& target, const float stepRadians) {
        const float dx = target.x - reference.x;
        const float dy = target.y - reference.y;

        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.0001f) return target;

        float angle = std::atan2(dy, dx);
        angle = std::round(angle / stepRadians) * stepRadians;

        return {reference.x + std::cos(angle) * length, reference.y + std::sin(angle) * length};
    }

    std::vector<Vector2> DedupeConsecutivePoints(const std::vector<Vector2>& points) {
        std::vector<Vector2> result;
        result.reserve(points.size());

        for (const Vector2& point : points) {
            if (result.empty() || !NearlyEqualPoints(result.back(), point)) result.push_back(point);
        }

        return result;
    }

    bool ClosedLoopSelfIntersects(const std::vector<Vector2>& uniqueRingPoints) {
        const int n = static_cast<int>(uniqueRingPoints.size());
        if (n < 4) return false;

        for (int i = 0; i < n; ++i) {
            const Vector2& a1 = uniqueRingPoints[i];
            const Vector2& a2 = uniqueRingPoints[(i + 1) % n];

            for (int j = i + 1; j < n; ++j) {
                const bool adjacent = (j == (i + 1) % n) || ((j + 1) % n == i);
                if (adjacent) continue;

                const Vector2& b1 = uniqueRingPoints[j];
                const Vector2& b2 = uniqueRingPoints[(j + 1) % n];

                if (ProperSegmentsIntersect(a1, a2, b1, b2)) return true;
            }
        }

        return false;
    }

    Vector2 ResolveFreehandPoint(const Vector2& mouseWorld) {
        Vector2 point = ResolveSnapPoint(mouseWorld);

        if (!manualSectorMode && !sectorBeingCreated.empty() && IsConstrainModifierHeld())
            point = ConstrainToAngleStep(sectorBeingCreated.back(), point, kPi / 4.0f);

        return point;
    }

    Vector2 ResolveRectangleCorner(const Vector2& mouseWorld) {
        Vector2 corner = ResolveSnapPoint(mouseWorld);
        if (!rectangleHasFirstCorner) return corner;

        if (IsConstrainModifierHeld()) {
            const float width = corner.x - rectangleFirstCorner.x;
            const float height = corner.y - rectangleFirstCorner.y;
            const float side = std::max(std::fabs(width), std::fabs(height));

            const float signedWidth = width < 0.0f ? -side : side;
            const float signedHeight = height < 0.0f ? -side : side;

            corner = {rectangleFirstCorner.x + signedWidth, rectangleFirstCorner.y + signedHeight};
        }

        return corner;
    }

    Vector2 ResolvePolygonHandle(const Vector2& mouseWorld) {
        Vector2 handle = ResolveSnapPoint(mouseWorld);
        if (!polygonHasCenter) return handle;

        if (IsConstrainModifierHeld()) {
            constexpr float rotationStep = kPi / 12.0f; // 15 degrees
            handle = ConstrainToAngleStep(polygonCenter, handle, rotationStep);
        }

        return handle;
    }

    Vector2 ResolveCircleHandle(const Vector2& mouseWorld) {
        Vector2 handle = ResolveSnapPoint(mouseWorld);
        if (!circleHasCenter) return handle;

        if (IsConstrainModifierHeld()) {
            const float radiusX = std::fabs(handle.x - circleCenter.x);
            const float radiusY = std::fabs(handle.y - circleCenter.y);
            const float radius = std::max(radiusX, radiusY);

            const float signX = (handle.x < circleCenter.x) ? -1.0f : 1.0f;
            const float signY = (handle.y < circleCenter.y) ? -1.0f : 1.0f;

            handle = {circleCenter.x + signX * radius, circleCenter.y + signY * radius};
        }

        return handle;
    }

    Vector2 ResolveCurveEnd(const Vector2& mouseWorld) {
        Vector2 end = ResolveSnapPoint(mouseWorld);

        if (IsConstrainModifierHeld()) end = ConstrainToAngleStep(curveStart, end, kPi / 4.0f);

        return end;
    }

    std::vector<Vector2> BuildRegularPolygon(const Vector2& center, const Vector2& handle, int sideCount) {
        sideCount = std::max(sideCount, 3);

        const float dx = handle.x - center.x;
        const float dy = handle.y - center.y;
        const float radius = std::sqrt(dx * dx + dy * dy);
        const float startAngle = std::atan2(dy, dx);

        std::vector<Vector2> points;
        points.reserve(sideCount);

        for (int i = 0; i < sideCount; ++i) {
            const float angle = startAngle + (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(sideCount);
            points.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
        }

        return points;
    }

    std::vector<Vector2> BuildEllipse(const Vector2& center, const float radiusX, const float radiusY, int segments) {
        segments = std::max(segments, 3);

        std::vector<Vector2> points;
        points.reserve(segments);

        for (int i = 0; i < segments; ++i) {
            const float angle = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
            points.push_back({center.x + radiusX * std::cos(angle), center.y + radiusY * std::sin(angle)});
        }

        return points;
    }

    std::vector<Vector2> BuildQuadraticCurve(const Vector2& start, const Vector2& control, const Vector2& end, int subdivisions) {
        subdivisions = std::max(subdivisions, 1);

        std::vector<Vector2> points;
        points.reserve(subdivisions + 1);

        for (int i = 0; i <= subdivisions; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(subdivisions);
            const float oneMinusT = 1.0f - t;

            const float x = oneMinusT * oneMinusT * start.x + 2.0f * oneMinusT * t * control.x + t * t * end.x;
            const float y = oneMinusT * oneMinusT * start.y + 2.0f * oneMinusT * t * control.y + t * t * end.y;

            points.push_back({x, y});
        }

        return points;
    }

    bool IsDrawingInProgress() {
        if (!sectorBeingCreated.empty() || manualSectorMode) return true;
        if (rectangleHasFirstCorner) return true;
        if (polygonHasCenter) return true;
        if (circleHasCenter) return true;
        if (curveStage != CURVE_STAGE_START) return true;

        return false;
    }

    void CancelActiveDrawing() {
        CancelSectorChain();
        ClearManualSectorSelection();

        rectangleHasFirstCorner = false;
        polygonHasCenter = false;
        circleHasCenter = false;
        curveStage = CURVE_STAGE_START;
    }

    void SetActiveDrawTool(const DrawTool tool) {
        if (tool == currentDrawTool) return;

        CancelActiveDrawing();
        currentDrawTool = tool;
    }

    void HandleSectorDrawClick(const Vector2& rawMouseWorld) {
        switch (currentDrawTool) {
            case DRAWTOOL_FREEHAND:
                TrySectorChainClick(ResolveFreehandPoint(rawMouseWorld));
                break;

            case DRAWTOOL_RECTANGLE:
                HandleRectangleClick(rawMouseWorld);
                break;

            case DRAWTOOL_POLYGON:
                HandlePolygonClick(rawMouseWorld);
                break;

            case DRAWTOOL_CIRCLE:
                HandleCircleClick(rawMouseWorld);
                break;

            case DRAWTOOL_CURVE:
                HandleCurveClick(rawMouseWorld);
                break;

            default: break;
        }
    }

    void UndoLastDrawPoint() {
        switch (currentDrawTool) {
            case DRAWTOOL_FREEHAND:
                if (manualSectorMode) {
                    if (!manualSectorDots.empty()) manualSectorDots.pop_back();
                }
                else if (!sectorBeingCreated.empty()) {
                    sectorBeingCreated.pop_back();
                }
                break;

            case DRAWTOOL_RECTANGLE:
                rectangleHasFirstCorner = false;
                break;

            case DRAWTOOL_POLYGON:
                polygonHasCenter = false;
                break;

            case DRAWTOOL_CIRCLE:
                circleHasCenter = false;
                break;

            case DRAWTOOL_CURVE:
                if (curveStage == CURVE_STAGE_CONTROL) curveStage = CURVE_STAGE_END;
                else if (curveStage == CURVE_STAGE_END) curveStage = CURVE_STAGE_START;
                break;

            default: break;
        }
    }

    void ConfirmActiveDrawing() {
        if (currentMode != MODE_SECTOR) return;

        if (currentDrawTool == DRAWTOOL_FREEHAND) {
            if (manualSectorMode) {
                if (manualSectorDots.size() >= 3) CreateManualSector();
                return;
            }

            // Enter commits whatever is currently on screen instead of only
            // ever force-closing a >= 3 point chain into a sector: two points
            // lay down a single wall, an open three-point chain lays down two,
            // and so on. Closing a loop into a sector is still done by
            // clicking back onto the chain's own start point (handled in
            // TrySectorChainClick), which is the only place that adds the
            // closing edge for you.
            //
            // Guarded on empty rather than left to CommitSectorChainAsDrawn's
            // own validation so that Enter with nothing drawn stays the silent
            // no-op it has always been, instead of raising a "need at least 2
            // points" toast at someone who hasn't started drawing yet.
            if (sectorBeingCreated.empty()) return;

            // Rejected geometry deliberately leaves the chain in place - the
            // user can back a point off with Backspace and retry, matching how
            // the Rectangle/Polygon/Circle/Curve tools hold their in-progress
            // state when a commit is refused.
            if (CommitSectorChainAsDrawn()) sectorBeingCreated.clear();

            return;
        }

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        HandleSectorDrawClick(mouseWorld);
    }

    // Returns by value rather than as a const char*: Localisation::Get
    // hands back a std::string by value, so a .c_str() out of one would
    // dangle the moment this function returned.
    std::string GetActiveDrawToolName() {
        switch (currentDrawTool) {
            case DRAWTOOL_FREEHAND:
                return Localisation::Get(manualSectorMode ? "editor.draw.tool.freehand_manual" : "editor.draw.tool.freehand");
            case DRAWTOOL_RECTANGLE: return Localisation::Get("editor.draw.tool.rectangle");
            case DRAWTOOL_POLYGON:   return Localisation::Get("editor.draw.tool.polygon");
            case DRAWTOOL_CIRCLE:    return Localisation::Get("editor.draw.tool.circle");
            case DRAWTOOL_CURVE:     return Localisation::Get("editor.draw.tool.curve");
            default:                 return Localisation::Get("bug.unknown");
        }
    }

    // Live, tool-specific measurement/prompt line, shared by the on-canvas
    // label next to the shape and the status overlay so the two can never
    // disagree.
    //
    // Localised words are injected into compile-time literal format
    // strings via %s rather than pulling whole format strings out of the
    // translation data - a translation that dropped or reordered a %.1f
    // would otherwise be undefined behaviour at runtime. The two manual-
    // corner keys are the exception: they already exist as "%d corner(s)
    // selected" strings and are consumed the same way elsewhere in the
    // editor (see the sector-chain reminder in DrawMode).
    std::string GetActiveDrawToolMeasurementText() {
        if (currentMode != MODE_SECTOR) return "";

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        char buffer[160];

        switch (currentDrawTool) {
            case DRAWTOOL_FREEHAND: {
                if (manualSectorMode) {
                    const int cornerCount = static_cast<int>(manualSectorDots.size());

                    const std::string cornerText = (cornerCount == 1)
                        ? Localisation::Get("editor.manual_sector.corner_selected")
                        : Localisation::Get("editor.manual_sector.corners_selected");

                    std::snprintf(buffer, sizeof(buffer), cornerText.c_str(), cornerCount);
                    return buffer;
                }

                if (sectorBeingCreated.empty()) return "";

                const Vector2 next = ResolveFreehandPoint(mouseWorld);
                const float edgeLength = std::sqrt(Vector2Math::DistanceSquared(sectorBeingCreated.back(), next));

                std::snprintf(buffer, sizeof(buffer), "%d %s   %s: %.1f",
                              static_cast<int>(sectorBeingCreated.size()),
                              Localisation::Get("editor.draw.measure.points").c_str(),
                              Localisation::Get("editor.draw.measure.next_edge").c_str(),
                              edgeLength);

                return buffer;
            }

            case DRAWTOOL_RECTANGLE: {
                if (!rectangleHasFirstCorner) return Localisation::Get("editor.draw.prompt.first_corner");

                const Vector2 opposite = ResolveRectangleCorner(mouseWorld);
                const float width = std::fabs(opposite.x - rectangleFirstCorner.x);
                const float height = std::fabs(opposite.y - rectangleFirstCorner.y);

                std::snprintf(buffer, sizeof(buffer), "%.1f x %.1f   %s: %.1f",
                              width, height,
                              Localisation::Get("editor.draw.measure.area").c_str(),
                              width * height);
                return buffer;
            }

            case DRAWTOOL_POLYGON: {
                if (!polygonHasCenter) return Localisation::Get("editor.draw.prompt.centre");

                const Vector2 handle = ResolvePolygonHandle(mouseWorld);
                const float radius = std::sqrt(Vector2Math::DistanceSquared(polygonCenter, handle));
                const int sides = std::max(polygonSideCount, 3);
                const float sideLength = 2.0f * radius * std::sin(kPi / static_cast<float>(sides));

                std::snprintf(buffer, sizeof(buffer), "%s %.1f   %s %.1f   %d %s",
                              Localisation::Get("editor.draw.measure.radius").c_str(), radius,
                              Localisation::Get("editor.draw.measure.side").c_str(), sideLength,
                              sides,
                              Localisation::Get("editor.draw.measure.sides").c_str());
                return buffer;
            }

            case DRAWTOOL_CIRCLE: {
                if (!circleHasCenter) return Localisation::Get("editor.draw.prompt.centre");

                const Vector2 handle = ResolveCircleHandle(mouseWorld);
                const float radiusX = std::fabs(handle.x - circleCenter.x);
                const float radiusY = std::fabs(handle.y - circleCenter.y);

                if (std::fabs(radiusX - radiusY) < 0.01f)
                    std::snprintf(buffer, sizeof(buffer), "%s %.1f   %d %s",
                                  Localisation::Get("editor.draw.measure.radius").c_str(), radiusX,
                                  circleSegments,
                                  Localisation::Get("editor.draw.measure.segments").c_str());
                else
                    std::snprintf(buffer, sizeof(buffer), "%.1f x %.1f   %d %s",
                                  radiusX * 2.0f, radiusY * 2.0f,
                                  circleSegments,
                                  Localisation::Get("editor.draw.measure.segments").c_str());

                return buffer;
            }

            case DRAWTOOL_CURVE: {
                if (curveStage == CURVE_STAGE_START) return Localisation::Get("editor.draw.prompt.curve_start");

                if (curveStage == CURVE_STAGE_END) {
                    const Vector2 end = ResolveCurveEnd(mouseWorld);
                    const float chord = std::sqrt(Vector2Math::DistanceSquared(curveStart, end));

                    std::snprintf(buffer, sizeof(buffer), "%s %.1f   %s",
                                  Localisation::Get("editor.draw.measure.chord").c_str(), chord,
                                  Localisation::Get("editor.draw.prompt.curve_end").c_str());
                    return buffer;
                }

                // CURVE_STAGE_CONTROL
                const Vector2 control = ResolveSnapPoint(mouseWorld);
                const std::vector<Vector2> curvePoints = BuildQuadraticCurve(curveStart, control, curveEnd, curveSubdivisions);

                float totalLength = 0.0f;
                for (std::size_t i = 0; i + 1 < curvePoints.size(); ++i)
                    totalLength += std::sqrt(Vector2Math::DistanceSquared(curvePoints[i], curvePoints[i + 1]));

                std::snprintf(buffer, sizeof(buffer), "%s %.1f   %d %s",
                              Localisation::Get("editor.draw.measure.length").c_str(), totalLength,
                              curveSubdivisions,
                              Localisation::Get("editor.draw.measure.segments").c_str());
                return buffer;
            }

            default: return "";
        }
    }

    void ClearManualSectorSelection() {
        manualSectorDots.clear();
    }

    // Manual mode only restricts *which points* are pickable (existing
    // corners, via FindExistingCorner) - sector creation itself now goes
    // through the exact same topology system as the freehand chain, so
    // a manual sector gets real bordering walls (reusing whichever of
    // them already exist) instead of floor/ceiling geometry with nothing
    // around it.
    void CreateManualSector() {
        if (manualSectorDots.size() < 3) {
            manualSectorDots.clear();
            return;
        }

        std::vector<Vector2> closedChain = manualSectorDots;
        closedChain.push_back(manualSectorDots.front());

        ApplyDrawnGeometry(closedChain, BuildPendingSectorParams());

        manualSectorDots.clear();
    }

    namespace {
        GeometrySnapshot CaptureGeometrySnapshot(const Level& level) {
            GeometrySnapshot snapshot;
            snapshot.walls = level.walls;
            snapshot.sectors = level.sectors;
            snapshot.nextWallID = level.nextWallID;
            snapshot.nextSectorID = level.nextSectorID;
            snapshot.selectedSectorID = selectedSectorID;
            snapshot.selectedWallID = selectedWallID;
            snapshot.selectedWalls = selectedWalls;
            snapshot.editingSector = editingSector;
            snapshot.editingWall = editingWall;
            return snapshot;
        }
    }

    GeometrySnapshot CaptureGeometryUndoSnapshot() {
        return CaptureGeometrySnapshot(LevelManager::CurrentLevel());
    }

    void PushGeometryUndoSnapshot(GeometrySnapshot snapshot) {
        geometrySnapshots.push_back(std::move(snapshot));
        actions.push_back(ACTION_APPLY_GEOMETRY);
    }

    bool ApplyDrawnGeometry(const std::vector<Vector2>& drawnPoints, const PendingSectorParams& params) {
        Level& level = LevelManager::CurrentLevel();

        MapTopology::NewSectorParams topologyParams;
        topologyParams.wallTexture = params.wallTexture;
        topologyParams.floors = params.floors;
        topologyParams.lightValue = params.lightValue;
        topologyParams.wallColor = params.wallColor;

        // Taken unconditionally, before we know whether the edit will be
        // accepted - cheap to discard if MapTopology::ApplyDrawnGeometry
        // rejects it (which leaves `level` itself completely untouched).
        GeometrySnapshot snapshot = CaptureGeometrySnapshot(level);

        const MapTopology::ApplyResult applyResult =
            MapTopology::ApplyDrawnGeometry(level, drawnPoints, topologyParams);

        if (!applyResult.success) {
            spdlog::warn("Sector geometry rejected: {}", applyResult.message);
            lastGeometryError = applyResult.message;
            return false;
        }

        geometrySnapshots.push_back(std::move(snapshot));
        actions.push_back(ACTION_APPLY_GEOMETRY);

        if (!applyResult.affectedSectorIDs.empty()) {
            selectedSectorID = applyResult.affectedSectorIDs.front();
            editingSector = true;
        }

        lastGeometryError.clear();

        return true;
    }

    void RestoreGeometrySnapshot(const GeometrySnapshot& snapshot) {
        Level& level = LevelManager::CurrentLevel();

        level.walls = snapshot.walls;
        level.sectors = snapshot.sectors;
        level.nextWallID = snapshot.nextWallID;
        level.nextSectorID = snapshot.nextSectorID;

        selectedSectorID = snapshot.selectedSectorID;
        selectedWallID = snapshot.selectedWallID;
        selectedWalls = snapshot.selectedWalls;
        editingSector = snapshot.editingSector;
        editingWall = snapshot.editingWall;

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    void AddDot(const Vector2& position) {
        Dot dot;
        dot.id = nextDotID++;
        dot.position = position;

        dots.push_back(dot);
        dotIDToIndex[dot.id] = static_cast<int>(dots.size()) - 1;

        actions.push_back(ACTION_CREATE_CORNER);
    }

    void DeleteDot(const ID dotID) {
        const auto it = dotIDToIndex.find(dotID);

        if (it == dotIDToIndex.end()) {
            return;
        }

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(dots.size())) {
            return;
        }

        dots.erase(dots.begin() + index);

        RebuildDotIDLookup();
    }

    // Deleting a sector returns its space to whatever surrounded it: for
    // an ordinary sector that's the level exterior (its walls were only
    // holding back open space, so they go with it), and for a sector
    // nested inside another it's the enclosing sector, which reclaims
    // the space and inherits anything that was nested deeper still.
    void DeleteSector(const ID sectorID) {
        Level& level = LevelManager::CurrentLevel();

        const auto it = level.sectorIDToIndex.find(sectorID);

        if (it == level.sectorIDToIndex.end()) {
            return;
        }

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.sectors.size())) {
            return;
        }

        int enclosingLoopIndex = -1;
        const ID enclosingSectorID = FindEnclosingSectorID(level, level.sectors[index], &enclosingLoopIndex);

        level.sectors.erase(level.sectors.begin() + index);

        if (selectedSectorID == sectorID) {
            selectedSectorID = INVALID_ID;
            editingSector = false;
        }

        if (enclosingSectorID != INVALID_ID) {
            for (Sector& sector : level.sectors) {
                if (sector.id != enclosingSectorID) continue;

                if (enclosingLoopIndex < static_cast<int>(sector.innerLoops.size()))
                    sector.innerLoops.erase(sector.innerLoops.begin() + enclosingLoopIndex);

                break;
            }
        }

        for (int i = static_cast<int>(level.walls.size()) - 1; i >= 0; --i) {
            Wall& wall = level.walls[i];

            const bool referencedInFront = wall.frontSector == sectorID;
            const bool referencedInBack = wall.backSector == sectorID;

            if (!referencedInFront && !referencedInBack) continue;

            // A wall goes only when it stops separating two different
            // things. Facing open space on its far side is the existing
            // condition; facing the enclosing sector is the same
            // situation one nesting level in, since that sector is about
            // to absorb this space. A wall whose far side is some *other*
            // sector still borders that one and has to stay.
            const ID otherSide = referencedInFront ? wall.backSector : wall.frontSector;

            if (otherSide == INVALID_ID || otherSide == enclosingSectorID) {
                level.walls.erase(level.walls.begin() + i);
                continue;
            }

            if (referencedInFront) wall.frontSector = INVALID_ID;
            if (referencedInBack) wall.backSector = INVALID_ID;
        }

        // Nested deletion changes which sectors enclose which, and that
        // can't be patched by hand - deleting one room of a two-room
        // island, for instance, leaves the enclosing sector needing a
        // hole around the *surviving* room, which no amount of editing
        // the old loop produces. Re-derive the sector layer from the
        // walls instead; sectors still enclosed the same way keep their
        // IDs and properties.
        if (enclosingSectorID != INVALID_ID && MapTopology::RebuildSectorsFromWalls(level)) return;

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    // Entity Mode — left click selects/positions only, never opens the inspector.
    void HandleEntityModeLeftClick(const Vector2& point) {
        Entity* entity = EntityAt(point);
        if (entity == nullptr) return;
        selectedEntity = *entity; // editingEntity intentionally left untouched
    }

    // Entity Mode — right click selects AND opens the inspector.
    void HandleEntityModeRightClick(const Vector2& point) {
        Entity* entity = EntityAt(point);
        if (entity == nullptr) return;
        selectedEntity = *entity;
        editingEntity  = true;
    }

    // Sector Mode — right click selects a sector and opens its inspector.
    void HandleSectorModeRightClick(const Vector2& point) {
        Level& level = LevelManager::CurrentLevel();
        const int index = MapQueries::FindSectorContainingPoint(level.sectors, point, -1);
        if (index < 0) return;
        selectedSectorID = level.sectors[index].id;
        editingSector    = true;
    }

    void DeleteWall(const ID wallID) {
        Level& level = LevelManager::CurrentLevel();

        const auto it = level.wallIDToIndex.find(wallID);
        if (it == level.wallIDToIndex.end()) return;

        const int index = it->second;
        if (index < 0 || index >= static_cast<int>(level.walls.size())) return;

        level.walls.erase(level.walls.begin() + index);

        const auto selectedIt = std::find(selectedWalls.begin(), selectedWalls.end(), wallID);
        if (selectedIt != selectedWalls.end()) selectedWalls.erase(selectedIt);

        // The inspector follows the primary wall, so hand it to whatever
        // is left of the selection rather than closing on a multi-select.
        if (selectedWallID == wallID) {
            selectedWallID = selectedWalls.empty() ? INVALID_ID : selectedWalls.front();
            editingWall    = selectedWallID != INVALID_ID;
        }

        // Was a bare RebuildSectorRuntimeLinks(). Removing a wall can open
        // a sector's boundary, and no amount of relinking fixes that - the
        // sector layer has to be re-derived from the surviving walls (with
        // the relink as the fallback when it can't be).
        RebuildGeometryAfterEdit();
    }

    float DistancePointToSegmentSq(const Vector2& point, const Vector2& a, const Vector2& b) {
        const Vector2 ab = b - a;
        const Vector2 ap = point - a;

        const float abLengthSq = Vector2Math::Dot(ab, ab);

        if (abLengthSq <= 0.00001f) {
            const Vector2 diff = point - a;
            return Vector2Math::Dot(diff, diff);
        }

        float t = Vector2Math::Dot(ap, ab) / abLengthSq;
        t = std::clamp(t, 0.0f, 1.0f);

        const Vector2 closestPoint = {
            a.x + ab.x * t,
            a.y + ab.y * t
        };

        const Vector2 diff = point - closestPoint;
        return Vector2Math::Dot(diff, diff);
    }

    int GetWallAtPoint(const Vector2& worldPoint) {
        Level& level = LevelManager::CurrentLevel();

        // Shared with Geometry Mode's handles (WALL_ENDPOINT_PICK_RADIUS_PIXELS)
        // so body and endpoint picking use one consistent, zoom-independent
        // tolerance instead of two numbers that can drift apart.
        constexpr float clickRadiusPixels = WALL_PICK_RADIUS_PIXELS;
        const float safeZoom = std::max(editorZoom, 0.0001f);

        const float clickRadiusWorld = clickRadiusPixels / safeZoom;
        const float clickRadiusSq = clickRadiusWorld * clickRadiusWorld;

        int closestWallIndex = -1;
        float closestDistanceSq = clickRadiusSq;

        for (int i = 0; i < static_cast<int>(level.walls.size()); ++i) {
            const Wall& wall = level.walls[i];

            const float distanceSq = DistancePointToSegmentSq(
                worldPoint,
                wall.start,
                wall.end
            );

            if (distanceSq <= closestDistanceSq) {
                closestDistanceSq = distanceSq;
                closestWallIndex = i;
            }
        }

        return closestWallIndex;
    }

    // =========================================================================
    //  Geometry / Wall Edit Mode
    // =========================================================================
    //
    // Picking, selection and wall editing for MODE_GEOMETRY.
    //
    // Moving geometry edits the sectors in place rather than re-deriving
    // them. A Sector carries far more than its outline - floors, wall/
    // floor/ceiling textures, light, colours, its ID and its entity list -
    // and none of that can be recovered from the wall graph, so rebuilding
    // the sector layer after every drag would quietly reset it. Since a
    // move doesn't change *which* faces exist, only where their corners
    // are, the correct sector is the one already there with its boundary
    // points moved and its triangles regenerated.
    namespace {
        // Regenerates `sector.triangles` from its current boundary. Kept
        // defensive about degenerate input: a drag that lands one corner
        // exactly on top of its neighbour collapses an edge to nothing,
        // and feeding that to the triangulator produces garbage rather
        // than an error.
        void RetriangulateSector(Sector& sector) {
            std::vector<Vector2> outerRing = DedupeConsecutivePoints(sector.vertices);

            // DedupeConsecutivePoints only looks at adjacent entries in a
            // linear list, so a ring that closes back onto its own first
            // point needs that trailing duplicate stripped separately.
            if (outerRing.size() >= 2 && SamePoint(outerRing.front(), outerRing.back())) outerRing.pop_back();

            if (outerRing.size() < 3) {
                sector.triangles.clear();
                return;
            }

            std::vector<std::vector<Vector2>> holeRings;
            holeRings.reserve(sector.innerLoops.size());

            for (const std::vector<Vector2>& innerLoop : sector.innerLoops) {
                std::vector<Vector2> holeRing = DedupeConsecutivePoints(innerLoop);

                if (holeRing.size() >= 2 && SamePoint(holeRing.front(), holeRing.back())) holeRing.pop_back();
                if (holeRing.size() >= 3) holeRings.push_back(std::move(holeRing));
            }

            if (holeRings.empty()) {
                sector.triangles = Geometry::Triangulate(outerRing);
                return;
            }

            // Hole-aware overload, mirroring the (outer, innerLoops, ...)
            // pair Geometry::IsPointInPolygon already takes. If the
            // Geometry header spells this differently, this is the only
            // line that needs adjusting.
            sector.triangles = Geometry::Triangulate(outerRing, holeRings);
        }
    }

    ID PickWallAt(const Vector2& mouseWorld) {
        const Level& level = LevelManager::CurrentLevel();

        const int index = GetWallAtPoint(mouseWorld);
        if (index < 0 || index >= static_cast<int>(level.walls.size())) return INVALID_ID;

        return level.walls[index].id;
    }

    bool PickSelectedWallEndpointAt(const Vector2& mouseWorld, ID* outWallID, bool* outIsStart) {
        const Level& level = LevelManager::CurrentLevel();

        const float safeZoom = std::max(editorZoom, 0.0001f);
        const float pickRadiusWorld = WALL_ENDPOINT_PICK_RADIUS_PIXELS / safeZoom;

        float bestDistSq = pickRadiusWorld * pickRadiusWorld;
        bool found = false;

        for (const ID wallID : selectedWalls) {
            const auto it = level.wallIDToIndex.find(wallID);
            if (it == level.wallIDToIndex.end()) continue;

            const int index = it->second;
            if (index < 0 || index >= static_cast<int>(level.walls.size())) continue;

            const Wall& wall = level.walls[index];

            const float startDistSq = Vector2Math::DistanceSquared(mouseWorld, wall.start);

            if (startDistSq <= bestDistSq) {
                bestDistSq = startDistSq;
                if (outWallID != nullptr) *outWallID = wall.id;
                if (outIsStart != nullptr) *outIsStart = true;
                found = true;
            }

            const float endDistSq = Vector2Math::DistanceSquared(mouseWorld, wall.end);

            if (endDistSq <= bestDistSq) {
                bestDistSq = endDistSq;
                if (outWallID != nullptr) *outWallID = wall.id;
                if (outIsStart != nullptr) *outIsStart = false;
                found = true;
            }
        }

        return found;
    }

    void ClearWallSelection() {
        selectedWalls.clear();
        selectedWallID = INVALID_ID;
        editingWall = false;
    }

    void SelectWall(const ID wallID) {
        if (wallID == INVALID_ID) {
            ClearWallSelection();
            return;
        }

        selectedWalls.clear();
        selectedWalls.push_back(wallID);

        // Selecting a wall always opens the inspector - that is the whole
        // point of the mode, and it is the same panel the hierarchy opens.
        selectedWallID = wallID;
        editingWall = true;
    }

    void ToggleWallSelection(const ID wallID) {
        if (wallID == INVALID_ID) return;

        const auto it = std::find(selectedWalls.begin(), selectedWalls.end(), wallID);

        if (it != selectedWalls.end()) {
            selectedWalls.erase(it);

            if (selectedWallID == wallID) {
                selectedWallID = selectedWalls.empty() ? INVALID_ID : selectedWalls.back();
                editingWall = selectedWallID != INVALID_ID;
            }

            return;
        }

        selectedWalls.push_back(wallID);

        selectedWallID = wallID;
        editingWall = true;
    }

    void ExtendWallSelectionTo(const ID wallID) {
        if (wallID == INVALID_ID) return;

        if (selectedWallID == INVALID_ID) {
            SelectWall(wallID);
            return;
        }

        const Level& level = LevelManager::CurrentLevel();

        const ID firstID = std::min(wallID, selectedWallID);
        const ID lastID = std::max(wallID, selectedWallID);

        selectedWalls.clear();

        for (const Wall& wall : level.walls)
            if (wall.id >= firstID && wall.id <= lastID) selectedWalls.push_back(wall.id);

        selectedWallID = wallID;
        editingWall = true;
    }

    void MoveSharedEndpoint(const Vector2& from, const Vector2& to) {
        if (SamePoint(from, to)) return;

        Level& level = LevelManager::CurrentLevel();

        // Every wall touching that corner moves with it - that is what
        // stops a shared vertex from being torn into two coincident-then-
        // divergent points and silently breaking the sector loops built
        // on top of it.
        for (Wall& wall : level.walls) {
            if (SamePoint(wall.start, from)) wall.start = to;
            if (SamePoint(wall.end, from)) wall.end = to;
        }

        // ...and so does every sector corner sitting on it. The sector
        // keeps its identity and all its properties; only the point moves.
        for (Sector& sector : level.sectors) {
            bool touched = false;

            for (Vector2& vertex : sector.vertices) {
                if (!SamePoint(vertex, from)) continue;

                vertex = to;
                touched = true;
            }

            for (std::vector<Vector2>& innerLoop : sector.innerLoops) {
                for (Vector2& vertex : innerLoop) {
                    if (!SamePoint(vertex, from)) continue;

                    vertex = to;
                    touched = true;
                }
            }

            if (touched) RetriangulateSector(sector);
        }
    }

    void ApplyGeometryDragOffset(const std::vector<Wall>& originalWalls,
                                 const std::vector<Sector>& originalSectors,
                                 const std::vector<Vector2>& movingPoints,
                                 const Vector2& delta) {
        Level& level = LevelManager::CurrentLevel();

        const auto follows = [&](const Vector2& point) {
            for (const Vector2& movingPoint : movingPoints)
                if (SamePoint(movingPoint, point)) return true;

            return false;
        };

        const auto offset = [&](const Vector2& point) -> Vector2 {
            return {point.x + delta.x, point.y + delta.y};
        };

        std::unordered_map<ID, const Wall*> originalWallByID;
        originalWallByID.reserve(originalWalls.size());

        for (const Wall& wall : originalWalls) originalWallByID[wall.id] = &wall;

        // Recomputed from the drag-start capture rather than nudged, so a
        // point that is shared by several walls lands on exactly the same
        // coordinate in all of them however many frames the drag lasts -
        // SamePoint()'s exact comparison depends on that.
        for (Wall& wall : level.walls) {
            const auto it = originalWallByID.find(wall.id);
            if (it == originalWallByID.end()) continue;

            const Wall& original = *it->second;

            wall.start = follows(original.start) ? offset(original.start) : original.start;
            wall.end = follows(original.end) ? offset(original.end) : original.end;
        }

        std::unordered_map<ID, const Sector*> originalSectorByID;
        originalSectorByID.reserve(originalSectors.size());

        for (const Sector& sector : originalSectors) originalSectorByID[sector.id] = &sector;

        // The sector objects themselves are never replaced - only the
        // boundary points that the drag moves, plus the triangles derived
        // from them. Everything else a Sector carries (floors, textures,
        // light, colours, entitiesInside) is left exactly as it was, which
        // is the whole reason moves don't go through MapTopology.
        for (Sector& sector : level.sectors) {
            const auto it = originalSectorByID.find(sector.id);
            if (it == originalSectorByID.end()) continue;

            const Sector& original = *it->second;

            bool touched = false;

            const auto rewriteLoop = [&](std::vector<Vector2>& target, const std::vector<Vector2>& source) {
                target = source;

                for (Vector2& point : target) {
                    if (!follows(point)) continue;

                    point = offset(point);
                    touched = true;
                }
            };

            rewriteLoop(sector.vertices, original.vertices);

            sector.innerLoops.resize(original.innerLoops.size());

            for (std::size_t loopIndex = 0; loopIndex < original.innerLoops.size(); ++loopIndex)
                rewriteLoop(sector.innerLoops[loopIndex], original.innerLoops[loopIndex]);

            // Only the sectors the drag actually reaches pay for
            // triangulation, and only while they're being dragged.
            if (touched) RetriangulateSector(sector);
        }
    }

    void RebuildGeometryAfterMove() {
        // Deliberately NOT MapTopology::RebuildSectorsFromWalls: the
        // sector layer is already correct (its points moved with the walls
        // and its triangles were regenerated as they moved), and
        // re-deriving it would replace every Sector with a freshly built
        // one, losing textures, floor/ceiling heights, light and colours.
        // All that's left is to refresh the ID lookups and the
        // sector/wall/entity links that point into them.
        MapQueries::RebuildSectorRuntimeLinks(LevelManager::CurrentLevel());
    }

    void RebuildGeometryAfterEdit() {
        Level& level = LevelManager::CurrentLevel();

        // For edits that changed which faces exist, where re-deriving is
        // the only correct answer - see RebuildGeometryAfterMove() for the
        // move case, which must NOT come through here.
        //
        // RebuildSectorsFromWalls re-derives sector loops, holes and
        // triangulation from the wall graph and relinks the runtime data
        // itself on success (the same contract DeleteSector relies on for
        // its nested case); if it can't reconcile the level, the plain
        // relink at least leaves the ID lookups valid.
        if (MapTopology::RebuildSectorsFromWalls(level)) return;

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    void DeleteSelectedWalls() {
        if (selectedWalls.empty()) return;

        Level& level = LevelManager::CurrentLevel();

        const std::vector<ID> wallsToDelete = selectedWalls;

        // One undo entry and one topology rebuild for the whole
        // selection, rather than one of each per wall.
        PushGeometryUndoSnapshot(CaptureGeometryUndoSnapshot());

        ClearWallSelection();

        for (int i = static_cast<int>(level.walls.size()) - 1; i >= 0; --i) {
            const ID wallID = level.walls[i].id;

            if (std::find(wallsToDelete.begin(), wallsToDelete.end(), wallID) == wallsToDelete.end()) continue;

            level.walls.erase(level.walls.begin() + i);
        }

        RebuildGeometryAfterEdit();
    }
}

namespace Editor {
    void AddWall(const Wall& wall) {
        Level& level = LevelManager::CurrentLevel();

        Wall copy = wall;

        if (copy.id == INVALID_ID) copy.id = level.nextWallID++;
        else level.nextWallID = std::max(level.nextWallID, copy.id + 1);

        level.walls.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    void AddSector(const Sector& sector) {
        Level& level = LevelManager::CurrentLevel();

        Sector copy = sector;

        if (copy.floors.empty()) {
            copy.floors.push_back({
                {0.0f, std::numeric_limits<uint32_t>::max(), {}},
                {40.0f, std::numeric_limits<uint32_t>::max(), {}}
            });
        }

        if (copy.id == INVALID_ID) copy.id = level.nextSectorID++;
        else level.nextSectorID = std::max(level.nextSectorID, copy.id + 1);

        level.sectors.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }
}