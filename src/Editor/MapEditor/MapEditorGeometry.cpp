#include "../EditorInternal.hpp"

#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp" // This includes "SSECompat.hpp"
#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/MapTopology.hpp"
#include "Headers/Map/WallPieces.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/TagRegistry.hpp"
#include "Headers/Objects/Entity.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

#include <cstdio>

namespace fs = std::filesystem;

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
            // Filtered out in the Entity Mode panel: drawn dimmed, never picked.
            if (!EntityPassesFilter(entity)) continue;

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
            (worldPos.y - cameraPos.y) * editorZoom + screenHeight * 0.5f
        };
    }

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos) {
        return {
            (screenPos.x - screenWidth * 0.5f) / editorZoom + cameraPos.x,
            (screenPos.y - screenHeight * 0.5f) / editorZoom + cameraPos.y
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
        // Ctrl is the universal "just this once, ignore the grid" override -
        // centralised here (rather than in each tool's Resolve* function)
        // so every drawing/dragging path that ends up here gets it for
        // free, staying consistent with vertex snapping and grid snapping
        // never getting a separate override of their own.
        const bool snapOverrideHeld = GetKeyBind(KEYBIND_DISABLE_SNAP);
        const bool gridSnapActive = gridSnapEnabled && !snapOverrideHeld;

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
            if (MapTopology::ClosestPointOnAnyWall(snapCandidateWalls, mouseWorld, snapRadiusWorld, &onWall)) {
                if (!gridSnapActive) return onWall;

                // Still on the grid while over a wall: snap, then slide the
                // grid point back onto the wall it came from - the same
                // approach Geometry Mode's Alt-click split uses. Exact on
                // axis-aligned walls; on a diagonal one it lands on the wall
                // as near the grid point as the wall allows.
                // The wall `onWall` sits on is the one nearest to it.
                const Wall* hitWall = nullptr;
                float hitDistSq = std::numeric_limits<float>::max();

                for (const Wall& wall : snapCandidateWalls) {
                    const Vector2 onThisWall = MapTopology::ClosestPointOnSegment(onWall, wall.start, wall.end);
                    const float distSq = Vector2Math::DistanceSquared(onWall, onThisWall);

                    if (distSq < hitDistSq) {
                        hitDistSq = distSq;
                        hitWall = &wall;
                    }
                }

                if (hitWall == nullptr) return onWall;

                return MapTopology::ClosestPointOnSegment(SnapToGrid(onWall), hitWall->start, hitWall->end);
            }
        }

        if (gridSnapActive) return SnapToGrid(mouseWorld);

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
            params.wallTopTexture = wallTopTexture;
            params.wallBottomTexture = wallBottomTexture;
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

        void HandleStaircaseClick(const Vector2& rawMouseWorld) {
            if (!staircaseHasFirstCorner) {
                staircaseFirstCorner = ResolveSnapPoint(rawMouseWorld);
                staircaseHasFirstCorner = true;
                return;
            }

            const Vector2 opposite = ResolveStaircaseCorner(rawMouseWorld);
            const StaircasePlan plan = BuildStaircasePlan(staircaseFirstCorner, opposite);

            if (!plan.valid) {
                lastGeometryError = plan.error;
                return; // stay in progress, same convention as the other shape tools
            }

            if (CommitStaircasePlan(plan)) staircaseHasFirstCorner = false;
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
        return GetKeyBind(KEYBIND_CONSTRAIN_ANGLE);
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

    bool SnapToPendingChainPoint(const Vector2& mouseWorld, Vector2* outPoint) {
        if (outPoint == nullptr) return false;
        if (!vertexSnapEnabled) return false; // same toggle every other vertex snap obeys
        if (manualSectorMode) return false;   // manual mode only ever picks *existing* corners
        if (sectorBeingCreated.size() < 3) return false;

        constexpr float snapRadiusPixels = 12.0f; // matches ResolveSnapPointExcluding / FindExistingCorner
        const float safeZoom = std::max(editorZoom, 0.0001f);
        const float snapRadiusWorld = snapRadiusPixels / safeZoom;

        const Vector2& start = sectorBeingCreated.front();
        if (Vector2Math::DistanceSquared(mouseWorld, start) > snapRadiusWorld * snapRadiusWorld) return false;

        *outPoint = start;
        return true;
    }

    Vector2 ResolveFreehandPoint(const Vector2& mouseWorld) {
        // A vertex of the chain in progress outranks everything else: it
        // beats grid/wall snapping because it's the more specific target,
        // and it beats the angle constraint because constraining on top of
        // it would immediately rotate the point back off the vertex it just
        // snapped to - making a loop impossible to close while Shift is held.
        Vector2 pendingPoint{};
        if (SnapToPendingChainPoint(mouseWorld, &pendingPoint)) return pendingPoint;

        Vector2 point = ResolveSnapPoint(mouseWorld);

        if (!manualSectorMode && !sectorBeingCreated.empty() && IsConstrainModifierHeld())
            point = ConstrainToAngleStep(sectorBeingCreated.back(), point, Constants::QuarterPi);

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

    bool StaircaseModeUsesStepLength(const StaircaseCalculationMode mode) {
        return mode == STAIRCASE_CALC_STEP_HEIGHT || mode == STAIRCASE_CALC_TARGET_HEIGHT;
    }

    // Shift here means "snap the staircase's length to a whole number of
    // steps", not the Rectangle tool's "force a square" - so this can't
    // reuse ResolveRectangleCorner despite the identical click shape.
    // Only the coordinate along the progression axis is touched; the
    // perpendicular dimension of the rectangle is left exactly where the
    // cursor put it.
    Vector2 ResolveStaircaseCorner(const Vector2& mouseWorld) {
        Vector2 corner = ResolveSnapPoint(mouseWorld);
        if (!staircaseHasFirstCorner) return corner;

        if (IsConstrainModifierHeld() && StaircaseModeUsesStepLength(staircaseCalcMode) && staircaseStepLength > 0.0f) {
            const bool horizontal = staircaseDirection == STAIRCASE_DIR_HORIZONTAL;

            const float axisStart = horizontal ? staircaseFirstCorner.x : staircaseFirstCorner.y;
            const float axisValue = horizontal ? corner.x : corner.y;

            const float delta = axisValue - axisStart;
            const float sign = delta < 0.0f ? -1.0f : 1.0f;
            const float steps = std::max(1.0f, std::round(std::fabs(delta) / staircaseStepLength));
            const float constrained = axisStart + sign * steps * staircaseStepLength;

            if (horizontal) corner.x = constrained;
            else corner.y = constrained;
        }

        return corner;
    }

    Vector2 ResolvePolygonHandle(const Vector2& mouseWorld) {
        Vector2 handle = ResolveSnapPoint(mouseWorld);
        if (!polygonHasCenter) return handle;

        if (IsConstrainModifierHeld()) {
            constexpr float rotationStep = Constants::Pi; // 15 degrees
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

        if (IsConstrainModifierHeld()) end = ConstrainToAngleStep(curveStart, end, Constants::QuarterPi);

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
            const float angle = startAngle + (Constants::TwoPi * static_cast<float>(i)) / static_cast<float>(sideCount);
            points.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
        }

        return points;
    }

    std::vector<Vector2> BuildEllipse(const Vector2& center, const float radiusX, const float radiusY, int segments) {
        segments = std::max(segments, 3);

        std::vector<Vector2> points;
        points.reserve(segments);

        for (int i = 0; i < segments; ++i) {
            const float angle = (Constants::TwoPi * static_cast<float>(i)) / static_cast<float>(segments);
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

    StaircasePlan BuildStaircasePlan(const Vector2& firstCorner, const Vector2& secondCorner) {
        StaircasePlan plan;

        if (!std::isfinite(firstCorner.x) || !std::isfinite(firstCorner.y) ||
            !std::isfinite(secondCorner.x) || !std::isfinite(secondCorner.y)) {
            plan.error = Localisation::Get("editor.staircase.error.rectangle_invalid");
            return plan;
        }

        const float width = std::fabs(secondCorner.x - firstCorner.x);
        const float height = std::fabs(secondCorner.y - firstCorner.y);

        if (!std::isfinite(width) || !std::isfinite(height) ||
            width < MIN_DRAW_SHAPE_DIMENSION || height < MIN_DRAW_SHAPE_DIMENSION) {
            plan.error = Localisation::Get("editor.draw.error.rectangle_zero_size");
            return plan;
        }

        const bool horizontal = staircaseDirection == STAIRCASE_DIR_HORIZONTAL;
        const float relevantLength = horizontal ? width : height;

        const float startingFloorHeight = floorHeight;
        const float startingCeilHeight = ceilHeight;

        // Guards a ceil(length / stepLength) step count against landing
        // one too high purely from float error on an exact multiple.
        constexpr float kLengthEpsilon = 0.001f;

        int rawStepCount = 0;
        float lengthPerFullStep = 0.0f;
        bool mergeTinyTailStep = false;

        switch (staircaseCalcMode) {
            case STAIRCASE_CALC_STEP_HEIGHT:
            case STAIRCASE_CALC_TARGET_HEIGHT: {
                if (!std::isfinite(staircaseStepLength) || staircaseStepLength <= 0.0f) {
                    plan.error = Localisation::Get("editor.staircase.error.step_length_invalid");
                    return plan;
                }

                if (staircaseCalcMode == STAIRCASE_CALC_STEP_HEIGHT) {
                    if (!std::isfinite(staircaseStepHeight)) {
                        plan.error = Localisation::Get("editor.staircase.error.step_height_invalid");
                        return plan;
                    }
                } else if (!std::isfinite(staircaseTargetFloorHeight)) {
                    plan.error = Localisation::Get("editor.staircase.error.target_height_invalid");
                    return plan;
                }

                rawStepCount = std::max(1, static_cast<int>(std::ceil(relevantLength / staircaseStepLength - kLengthEpsilon)));
                lengthPerFullStep = staircaseStepLength;
                mergeTinyTailStep = true;
                break;
            }

            case STAIRCASE_CALC_STEP_COUNT: {
                if (staircaseStepCount < 1) {
                    plan.error = Localisation::Get("editor.staircase.error.step_count_invalid");
                    return plan;
                }
                if (!std::isfinite(staircaseStepHeight)) {
                    plan.error = Localisation::Get("editor.staircase.error.step_height_invalid");
                    return plan;
                }

                rawStepCount = staircaseStepCount;
                lengthPerFullStep = relevantLength / static_cast<float>(rawStepCount);
                mergeTinyTailStep = false;
                break;
            }

            default:
                plan.error = Localisation::Get("bug.unknown");
                return plan;
        }

        if (!std::isfinite(lengthPerFullStep) || lengthPerFullStep <= 0.0f) {
            plan.error = Localisation::Get("editor.staircase.error.calculation_invalid");
            return plan;
        }

        if (rawStepCount > MAX_STAIRCASE_STEPS) {
            char buffer[192];
            std::snprintf(
                buffer, sizeof(buffer),
                Localisation::Get("editor.staircase.error.too_many_steps").c_str(),
                rawStepCount, MAX_STAIRCASE_STEPS
            );
            plan.error = buffer;
            return plan;
        }

        // ---- Division boundaries along the progression axis ---------------
        // Each boundary is computed from the original rectangle corner and
        // the step index (never by accumulating the previous boundary), so
        // float error can't compound across steps; the final boundary is
        // always the rectangle's exact endpoint.
        const float axisStart = horizontal ? firstCorner.x : firstCorner.y;
        const float axisEnd = horizontal ? secondCorner.x : secondCorner.y;
        const float sign = axisEnd >= axisStart ? 1.0f : -1.0f;

        std::vector<float> boundaries;
        boundaries.reserve(rawStepCount + 1);

        for (int k = 0; k < rawStepCount; ++k) {
            const float clampedLength = std::min(static_cast<float>(k) * lengthPerFullStep, relevantLength);
            boundaries.push_back(axisStart + sign * clampedLength);
        }
        boundaries.push_back(axisEnd);

        // Step-length modes only: a near-exact-multiple rectangle can leave
        // an essentially-zero final sliver from float error alone - fold
        // it into the previous step rather than emit a degenerate sector.
        // Step Count mode never merges: it must produce exactly the
        // requested number of sectors.
        constexpr float kMinStepSpan = 0.05f;

        if (mergeTinyTailStep && boundaries.size() >= 3) {
            const std::size_t last = boundaries.size() - 1;
            if (std::fabs(boundaries[last] - boundaries[last - 1]) < kMinStepSpan)
                boundaries.erase(boundaries.begin() + static_cast<long>(last) - 1);
        }

        const int stepCount = static_cast<int>(boundaries.size()) - 1;

        if (stepCount < 1) {
            plan.error = Localisation::Get("editor.draw.error.rectangle_zero_size");
            return plan;
        }

        // ---- Effective height/length, now that the step count is settled --
        float effectiveStepHeight;

        if (staircaseCalcMode == STAIRCASE_CALC_TARGET_HEIGHT) {
            effectiveStepHeight = stepCount > 1
                ? (staircaseTargetFloorHeight - startingFloorHeight) / static_cast<float>(stepCount - 1)
                : 0.0f;

            if (stepCount == 1) plan.targetSingleSectorNote = true;
        } else {
            effectiveStepHeight = staircaseStepHeight;
        }

        const float effectiveStepLength = (staircaseCalcMode == STAIRCASE_CALC_STEP_COUNT)
            ? relevantLength / static_cast<float>(stepCount)
            : staircaseStepLength;

        if (!std::isfinite(effectiveStepHeight) || !std::isfinite(effectiveStepLength)) {
            plan.error = Localisation::Get("editor.staircase.error.calculation_invalid");
            return plan;
        }

        // ---- Build the steps ------------------------------------------------
        // staircaseReverseRise is a plain persistent toggle now (flipped by
        // a single LALT/RALT press in HandleEditorInput, MapEditorInput.cpp)
        // rather than a held modifier - this file only reads it.
        const bool reversed = staircaseReverseRise;

        plan.steps.reserve(stepCount);

        for (int slot = 0; slot < stepCount; ++slot) {
            const float b0 = boundaries[slot];
            const float b1 = boundaries[slot + 1];

            const int heightIndex = reversed ? (stepCount - 1 - slot) : slot;

            const float stepFloor = startingFloorHeight + static_cast<float>(heightIndex) * effectiveStepHeight;
            const float stepCeil = staircaseRaiseCeiling
                ? startingCeilHeight + static_cast<float>(heightIndex) * effectiveStepHeight
                : startingCeilHeight;

            if (!std::isfinite(b0) || !std::isfinite(b1) || !std::isfinite(stepFloor) || !std::isfinite(stepCeil)) {
                plan.error = Localisation::Get("editor.staircase.error.calculation_invalid");
                plan.steps.clear();
                return plan;
            }

            if (!staircaseRaiseCeiling && stepCeil <= stepFloor) plan.headroomWarning = true;

            StaircaseStepSpan step;
            step.index = heightIndex;
            step.floorHeight = stepFloor;
            step.ceilHeight = stepCeil;

            if (horizontal) {
                step.corners[0] = {b0, firstCorner.y};
                step.corners[1] = {b1, firstCorner.y};
                step.corners[2] = {b1, secondCorner.y};
                step.corners[3] = {b0, secondCorner.y};
            } else {
                step.corners[0] = {firstCorner.x, b0};
                step.corners[1] = {secondCorner.x, b0};
                step.corners[2] = {secondCorner.x, b1};
                step.corners[3] = {firstCorner.x, b1};
            }

            plan.steps.push_back(step);
        }

        plan.valid = true;
        plan.stepCount = stepCount;
        plan.effectiveStepHeight = effectiveStepHeight;
        plan.effectiveStepLength = effectiveStepLength;
        plan.startingFloorHeight = startingFloorHeight;
        plan.finalFloorHeight = startingFloorHeight + static_cast<float>(stepCount - 1) * effectiveStepHeight;

        return plan;
    }

    bool IsDrawingInProgress() {
        if (!sectorBeingCreated.empty() || manualSectorMode) return true;
        if (rectangleHasFirstCorner) return true;
        if (polygonHasCenter) return true;
        if (circleHasCenter) return true;
        if (curveStage != CURVE_STAGE_START) return true;
        if (staircaseHasFirstCorner) return true;

        return false;
    }

    void CancelActiveDrawing() {
        CancelSectorChain();
        ClearManualSectorSelection();

        rectangleHasFirstCorner = false;
        polygonHasCenter = false;
        circleHasCenter = false;
        curveStage = CURVE_STAGE_START;
        staircaseHasFirstCorner = false;
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

            case DRAWTOOL_STAIRCASE:
                HandleStaircaseClick(rawMouseWorld);
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

            case DRAWTOOL_STAIRCASE:
                staircaseHasFirstCorner = false;
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
            case DRAWTOOL_STAIRCASE: return Localisation::Get("editor.draw.tool.staircase");
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
                const float sideLength = 2.0f * radius * std::sin(Constants::Pi / static_cast<float>(sides));

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

            case DRAWTOOL_STAIRCASE: {
                if (!staircaseHasFirstCorner) return Localisation::Get("editor.draw.prompt.first_corner");

                const Vector2 opposite = ResolveStaircaseCorner(mouseWorld);
                const StaircasePlan plan = BuildStaircasePlan(staircaseFirstCorner, opposite);

                if (!plan.valid) return plan.error;

                std::snprintf(buffer, sizeof(buffer), "%d %s   %s: %.1f -> %.1f",
                              plan.stepCount,
                              Localisation::Get("editor.staircase.measure.steps").c_str(),
                              Localisation::Get("editor.staircase.measure.floor_range").c_str(),
                              plan.startingFloorHeight, plan.finalFloorHeight);
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
        ClearRedo();
    }

    bool ApplyDrawnGeometry(const std::vector<Vector2>& drawnPoints, const PendingSectorParams& params) {
        Level& level = LevelManager::CurrentLevel();

        MapTopology::NewSectorParams topologyParams;
        topologyParams.wallTopTexture = params.wallTopTexture;
        topologyParams.wallBottomTexture = params.wallBottomTexture;
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
        ClearRedo();

        if (!applyResult.affectedSectorIDs.empty()) {
            // Through the shared helper rather than assigning
            // selectedSectorID on its own: a primary that isn't in
            // selectedSectors is exactly the inconsistent shape the
            // hierarchy highlight and the inspector's multi-edit fan-out
            // both read, so a freshly drawn sector has to land in the
            // plural selection too.
            SelectSector(applyResult.affectedSectorIDs.front());
            editingSector = true;
        }

        lastGeometryError.clear();

        return true;
    }

    namespace {
        // Same base values every other new sector gets (BuildPendingSectorParams
        // reads the current wallTop/BottomTexture/floorTexture/ceilTexture/colors/
        // light globals), with just this one step's floor/ceiling heights
        // substituted in - preserves every applicable pending sector and
        // wall setting per step, as the Rectangle tool would for a single
        // sector.
        MapTopology::NewSectorParams BuildStaircaseStepTopologyParams(const float stepFloorHeight, const float stepCeilHeight) {
            const PendingSectorParams base = BuildPendingSectorParams();

            MapTopology::NewSectorParams params;
            params.wallTopTexture = base.wallTopTexture;
            params.wallBottomTexture = base.wallBottomTexture;
            params.floors = base.floors;

            if (params.floors.empty()) params.floors.resize(1);

            params.floors.front().floor.height = stepFloorHeight;
            params.floors.front().ceiling.height = stepCeilHeight;

            params.lightValue = base.lightValue;
            params.wallColor = base.wallColor;
            return params;
        }
    }

    bool CommitStaircasePlan(const StaircasePlan& plan) {
        if (!plan.valid || plan.steps.empty()) {
            lastGeometryError = plan.error.empty()
                ? Localisation::Get("editor.draw.error.rectangle_zero_size")
                : plan.error;
            return false;
        }

        Level& level = LevelManager::CurrentLevel();

        // One snapshot for the WHOLE staircase, taken before any step is
        // applied - every step below goes straight through
        // MapTopology::ApplyDrawnGeometry (bypassing the single-sector
        // ApplyDrawnGeometry above, which would push its own undo entry
        // per step) so the entire multi-sector staircase becomes exactly
        // one ACTION_APPLY_GEOMETRY entry.
        const GeometrySnapshot snapshot = CaptureGeometrySnapshot(level);

        std::vector<ID> affectedSectorIDs;

        for (const StaircaseStepSpan& step : plan.steps) {
            const MapTopology::NewSectorParams stepParams =
                BuildStaircaseStepTopologyParams(step.floorHeight, step.ceilHeight);

            const std::vector<Vector2> loop = {
                step.corners[0], step.corners[1], step.corners[2], step.corners[3], step.corners[0]
            };

            const MapTopology::ApplyResult result = MapTopology::ApplyDrawnGeometry(level, loop, stepParams);

            if (!result.success) {
                // Roll back every step already applied this call - a
                // partially-built staircase is worse than none, and the
                // caller expects "left exactly as it was" on rejection,
                // same contract the single-sector ApplyDrawnGeometry has.
                level.walls = snapshot.walls;
                level.sectors = snapshot.sectors;
                level.nextWallID = snapshot.nextWallID;
                level.nextSectorID = snapshot.nextSectorID;
                MapQueries::RebuildSectorRuntimeLinks(level);

                spdlog::warn("Staircase geometry rejected on step {}: {}", step.index, result.message);
                lastGeometryError = result.message;
                return false;
            }

            for (const ID sectorID : result.affectedSectorIDs)
                if (std::find(affectedSectorIDs.begin(), affectedSectorIDs.end(), sectorID) == affectedSectorIDs.end())
                    affectedSectorIDs.push_back(sectorID);
        }

        geometrySnapshots.push_back(snapshot);
        actions.push_back(ACTION_APPLY_GEOMETRY);
        ClearRedo();

        if (!affectedSectorIDs.empty()) {
            // Every newly created step sector, not just the primary - see
            // the header comment on CommitStaircasePlan.
            selectedSectors = affectedSectorIDs;
            selectedSectorID = affectedSectorIDs.front();
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

        // GeometrySnapshot predates the plural sector selection and never
        // captured it, so rather than leaving behind a selection that can
        // name sectors this undo just removed, collapse to the primary the
        // snapshot did capture. That primary is guaranteed to exist: it
        // came out of the same snapshot as level.sectors above.
        selectedSectors.clear();
        if (selectedSectorID != INVALID_ID) selectedSectors.push_back(selectedSectorID);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    void AddDot(const Vector2& position) {
        Dot dot;
        dot.id = nextDotID++;
        dot.position = position;

        dots.push_back(dot);
        dotIDToIndex[dot.id] = static_cast<int>(dots.size()) - 1;

        actions.push_back(ACTION_CREATE_CORNER);
        ClearRedo();
    }

    void DeleteDot(const ID dotID) {
        const auto it = dotIDToIndex.find(dotID);

        if (it == dotIDToIndex.end()) return;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(dots.size())) return;

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

        // Drop the deleted sector out of the multi-selection, and hand the
        // primary to whatever is left rather than closing the inspector on
        // a multi-select - the same rule DeleteWall follows below. The
        // editingSector guard keeps this from *opening* an inspector that
        // wasn't open: unlike walls, selecting a sector on the canvas
        // deliberately doesn't imply editing it.
        const auto selectedIt = std::find(selectedSectors.begin(), selectedSectors.end(), sectorID);
        if (selectedIt != selectedSectors.end()) selectedSectors.erase(selectedIt);

        if (selectedSectorID == sectorID) {
            selectedSectorID = selectedSectors.empty() ? INVALID_ID : selectedSectors.front();
            editingSector = editingSector && selectedSectorID != INVALID_ID;
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

        // editingEntity intentionally left untouched - SelectEntity only
        // moves the selection, it never opens the inspector.
        SelectEntity(entity->id);
    }

    // Entity Mode — right click selects AND opens the inspector. Ctrl+right
    // click adds it to (or removes it from) the selection instead, so the
    // inspector edits every selected entity together.
    void HandleEntityModeRightClick(const Vector2& point) {
        Entity* entity = EntityAt(point);
        if (entity == nullptr) return;

        if (GetKeyBind(KEYBIND_MULTI_SELECT)) {
            ToggleEntitySelection(entity->id);
            editingEntity = !selectedEntities.empty();
            return;
        }

        // Routed through the shared helper so selectedEntities follows the
        // primary. Assigning selectedEntity on its own used to leave the
        // plural selection pointing at whatever was picked before, which
        // the inspector's multi-edit fan-out would then write to.
        SelectEntity(entity->id);
        editingEntity = true;
    }

    // Sector Mode — right click selects a sector and opens its inspector.
    // Ctrl+right click adds it to (or removes it from) the selection instead,
    // so the inspector edits every selected sector together.
    void HandleSectorModeRightClick(const Vector2& point) {
        Level& level = LevelManager::CurrentLevel();
        const int index = MapQueries::FindSectorContainingPoint(level.sectors, point, -1);
        if (index < 0) return;

        if (GetKeyBind(KEYBIND_MULTI_SELECT)) {
            ToggleSectorSelection(level.sectors[index].id);

            // Deselecting the last one already closed the inspector.
            if (selectedSectorID != INVALID_ID) editingSector = true;
            return;
        }

        SelectSector(level.sectors[index].id);
        editingSector = true;
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

            // Filtered out in the Geometry Mode panel: drawn dimmed, never picked.
            if (currentMode == MODE_GEOMETRY && !WallPassesFilter(wall)) continue;

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

    void RebuildGeometryAfterWallDrag(const std::vector<Vector2>& movedPoints,
                                      const std::vector<ID>& draggedWallIDs) {
        Level& level = LevelManager::CurrentLevel();

        // A moved point now sitting on a corner of a wall the drag didn't
        // touch joins two pieces of geometry without any wall overlapping,
        // so PlanarizeWalls alone wouldn't notice it.
        bool joinedCorner = false;

        for (const Wall& wall : level.walls) {
            if (std::ranges::find(draggedWallIDs, wall.id) != draggedWallIDs.end()) continue;

            for (const Vector2& moved : movedPoints) {
                if (MapTopology::PointsEquivalent(moved, wall.start) || MapTopology::PointsEquivalent(moved, wall.end)) {
                    joinedCorner = true;
                    break;
                }
            }

            if (joinedCorner) break;
        }

        std::vector<Wall> walls = level.walls;
        ID nextWallID = level.nextWallID;

        const bool planarized = MapTopology::PlanarizeWalls(walls, nextWallID, draggedWallIDs);

        // Nothing landed on anything: keep the sectors exactly as the drag
        // left them, with everything a re-derive would drop (tags, ...).
        if (!joinedCorner && !planarized) {
            RebuildGeometryAfterMove();
            return;
        }

        level.walls = std::move(walls);
        level.nextWallID = nextWallID;

        RebuildGeometryAfterEdit();
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

    // =========================================================================
    //  Geometry Mode panel actions
    // =========================================================================

    namespace {
        // Map units one texture repeat covers at scale 1 (tileSize in
        // Rendering.vs.glsl). A wall's U is s * scale / tile + offset / tile.
        constexpr float WALL_TEXTURE_TILE = 32.0f;

        // Neither half of a split may be shorter than this.
        constexpr float MIN_SPLIT_LENGTH = 0.5f;

        constexpr std::array<WallSurfaceSlot, 2> WALL_SLOTS = {WallSurfaceSlot::Top, WallSurfaceSlot::Bottom};

        bool NearlySame(const Vector2& a, const Vector2& b) {
            return MapTopology::PointsEquivalent(a, b);
        }

        Wall* FindWall(Level& level, const ID wallID) {
            const auto it = level.wallIDToIndex.find(wallID);
            if (it == level.wallIDToIndex.end() || it->second < 0 || it->second >= static_cast<int>(level.walls.size())) return nullptr;

            return &level.walls[it->second];
        }

        bool IsWallSelected(const ID wallID) {
            return std::find(selectedWalls.begin(), selectedWalls.end(), wallID) != selectedWalls.end();
        }

        // An offset only matters modulo one repeat; keeps the numbers small.
        float WrapTextureOffset(const float offset) {
            const float wrapped = std::fmod(offset, WALL_TEXTURE_TILE);
            return wrapped < 0.0f ? wrapped + WALL_TEXTURE_TILE : wrapped;
        }

        // +1 when U grows from start to end, -1 when Flip X reverses it.
        float USign(const WallSurface& surface) {
            return surface.flipTextureX ? -1.0f : 1.0f;
        }

        // Calls edit(loop) on every sector outline, outer rings and holes,
        // and re-triangulates each sector whose outline changed.
        template<typename F>
        void EditSectorLoops(Level& level, F&& edit) {
            for (Sector& sector : level.sectors) {
                bool changed = edit(sector.vertices);

                for (std::vector<Vector2>& hole : sector.innerLoops) changed = edit(hole) || changed;

                if (changed) RetriangulateSector(sector);
            }
        }

        // Puts `point` between a and b wherever they are neighbours in `loop`.
        bool InsertIntoEdge(std::vector<Vector2>& loop, const Vector2& a, const Vector2& b, const Vector2& point) {
            const size_t count = loop.size();
            if (count < 2) return false;

            for (size_t i = 0; i < count; ++i) {
                const Vector2& p = loop[i];
                const Vector2& q = loop[(i + 1) % count];

                if ((NearlySame(p, a) && NearlySame(q, b)) || (NearlySame(p, b) && NearlySame(q, a))) {
                    loop.insert(loop.begin() + static_cast<std::ptrdiff_t>(i + 1), point);
                    return true;
                }
            }

            return false;
        }

        // Removes `point` from `loop`, keeping a closing duplicate if it had one.
        bool RemoveFromLoop(std::vector<Vector2>& loop, const Vector2& point) {
            const bool closed = loop.size() >= 2 && NearlySame(loop.front(), loop.back());
            const size_t before = loop.size();

            std::erase_if(loop, [&point](const Vector2& p) { return NearlySame(p, point); });

            if (closed && loop.size() >= 2 && !NearlySame(loop.front(), loop.back())) loop.push_back(loop.front());

            return loop.size() != before;
        }

        // The split itself, without undo. Returns the new half's ID, or
        // INVALID_ID if the point is too close to an end.
        ID SplitWallNoUndo(Level& level, const ID wallID, const Vector2& point) {
            Wall* wall = FindWall(level, wallID);
            if (wall == nullptr) return INVALID_ID;

            const Vector2 splitPoint = MapTopology::ClosestPointOnSegment(point, wall->start, wall->end);
            const float startDistance = std::sqrt(Vector2Math::DistanceSquared(wall->start, splitPoint));
            const float endDistance = std::sqrt(Vector2Math::DistanceSquared(splitPoint, wall->end));

            if (startDistance < MIN_SPLIT_LENGTH || endDistance < MIN_SPLIT_LENGTH) return INVALID_ID;

            const Vector2 oldStart = wall->start;
            const Vector2 oldEnd = wall->end;

            // The second half starts its U at the split point, so its offset
            // moves on by the length of the first half.
            Wall second = *wall;
            second.id = level.nextWallID++;
            second.start = splitPoint;

            for (const WallSurfaceSlot slot : WALL_SLOTS) {
                WallSurface& surface = second.Surface(slot);
                surface.textureOffset.x = WrapTextureOffset(surface.textureOffset.x + USign(surface) * startDistance * surface.textureScale.x);
            }

            second.RefreshDerived();

            wall->end = splitPoint;
            wall->RefreshDerived();

            const bool wasSelected = IsWallSelected(wallID);

            // `wall` is not used after this, push_back may move it.
            level.walls.push_back(second);
            if (wasSelected) selectedWalls.push_back(second.id);

            EditSectorLoops(level, [&](std::vector<Vector2>& loop) {
                return InsertIntoEdge(loop, oldStart, oldEnd, splitPoint);
            });

            RebuildGeometryAfterMove();

            return second.id;
        }

        // How many walls start or end at `point`.
        int WallsAtPoint(const Level& level, const Vector2& point) {
            int count = 0;

            for (const Wall& wall : level.walls)
                if (NearlySame(wall.start, point) || NearlySame(wall.end, point)) ++count;

            return count;
        }

        // Joins `second` onto `first` if they qualify (see MergeSelectedWalls).
        bool TryMergeWalls(Level& level, const ID firstID, const ID secondID) {
            Wall* first = FindWall(level, firstID);
            Wall* second = FindWall(level, secondID);
            if (first == nullptr || second == nullptr || first == second) return false;

            const bool firstEndShared = NearlySame(first->end, second->start) || NearlySame(first->end, second->end);
            const bool firstStartShared = NearlySame(first->start, second->start) || NearlySame(first->start, second->end);
            if (firstEndShared == firstStartShared) return false; // no shared corner, or both (a two-wall loop)

            const Vector2 shared = firstEndShared ? first->end : first->start;
            const Vector2 firstFar = firstEndShared ? first->start : first->end;
            const bool secondStartShared = NearlySame(second->start, shared);
            const Vector2 secondFar = secondStartShared ? second->end : second->start;

            if (WallsAtPoint(level, shared) != 2) return false;

            // Straight on through the shared corner.
            const Vector2 inDirection = Vector2Math::Normalized(shared - firstFar);
            const Vector2 outDirection = Vector2Math::Normalized(secondFar - shared);
            const float cross = inDirection.x * outDirection.y - inDirection.y * outDirection.x;
            if (std::abs(cross) > 0.001f || Vector2Math::Dot(inDirection, outDirection) <= 0.0f) return false;

            // Same sector on each side, accounting for which way each runs.
            const bool sameDirection = firstEndShared == secondStartShared;
            const bool sameSides = sameDirection
                ? first->frontSector == second->frontSector && first->backSector == second->backSector
                : first->frontSector == second->backSector && first->backSector == second->frontSector;
            if (!sameSides) return false;

            const float secondLength = std::sqrt(Vector2Math::DistanceSquared(shared, secondFar));

            if (firstEndShared) first->end = secondFar;
            else {
                // The start moves back, and U starts there, so the offset
                // moves back by the same length to keep the texture still.
                first->start = secondFar;

                for (const WallSurfaceSlot slot : WALL_SLOTS) {
                    WallSurface& surface = first->Surface(slot);
                    surface.textureOffset.x = WrapTextureOffset(surface.textureOffset.x - USign(surface) * secondLength * surface.textureScale.x);
                }
            }

            first->RefreshDerived();

            std::erase(selectedWalls, secondID);
            if (selectedWallID == secondID) selectedWallID = firstID;
            if (hoveredWallID == secondID) hoveredWallID = INVALID_ID;

            std::erase_if(level.walls, [secondID](const Wall& wall) { return wall.id == secondID; });

            EditSectorLoops(level, [&shared](std::vector<Vector2>& loop) { return RemoveFromLoop(loop, shared); });

            // The erase moved walls; the next lookup needs fresh indices.
            MapQueries::RebuildSectorRuntimeLinks(level);

            return true;
        }

        // A wall in the chain walk: d = +1 when walked start to end.
        struct ChainStep {
            ID wallID = INVALID_ID;
            float direction = 1.0f;
        };

        // Selected walls connected to the primary, walked forwards from its
        // end and backwards from its start. A fork takes the first match.
        void WalkSelectedChain(Level& level, const Wall& primary, std::vector<ChainStep>& forward, std::vector<ChainStep>& backward) {
            std::vector<ID> visited = {primary.id};

            const auto walk = [&](Vector2 point, const bool forwards, std::vector<ChainStep>& steps) {
                while (true) {
                    bool found = false;

                    for (const ID wallID : selectedWalls) {
                        if (std::find(visited.begin(), visited.end(), wallID) != visited.end()) continue;

                        const Wall* wall = FindWall(level, wallID);
                        if (wall == nullptr) continue;

                        // Forwards a wall is entered at `point`; backwards it is left there.
                        const bool startHere = NearlySame(wall->start, point);
                        const bool endHere = NearlySame(wall->end, point);
                        if (!startHere && !endHere) continue;

                        const bool runsStartToEnd = forwards ? startHere : endHere;

                        steps.push_back({wallID, runsStartToEnd ? 1.0f : -1.0f});
                        visited.push_back(wallID);
                        point = startHere ? wall->end : wall->start;
                        found = true;
                        break;
                    }

                    if (!found) return;
                }
            };

            walk(primary.end, true, forward);
            walk(primary.start, false, backward);
        }

        // Sets the wall surface so its U reads `entryU` where the walk
        // enters it and grows by `chainSign` per map unit along the walk.
        // Returns the U where the walk leaves it.
        float ContinueChain(WallSurface& surface, const float length, const float direction, const float chainSign, const float entryU) {
            const float sign = direction * chainSign;
            surface.flipTextureX = sign < 0.0f;

            const float entryS = direction > 0.0f ? 0.0f : length;
            surface.textureOffset.x = WrapTextureOffset(WALL_TEXTURE_TILE * entryU - sign * entryS * surface.textureScale.x);

            return entryU + chainSign * length * surface.textureScale.x / WALL_TEXTURE_TILE;
        }

        // Moves the given entities, recording one undo entry if any moved.
        template<typename F>
        int MoveSelectedEntities(F&& newPosition) {
            Level& level = LevelManager::CurrentLevel();

            EntityTransformUndo undo;
            int moved = 0;

            for (const ID entityID : selectedEntities) {
                ComponentTransform* transform = level.transforms.Get(entityID);
                if (transform == nullptr) continue;

                const Vector3 before = transform->position;
                const Vector3 after = newPosition(entityID, before);
                if (after.x == before.x && after.y == before.y && after.z == before.z) continue;

                undo.positions.emplace_back(entityID, before);
                transform->SetPosition(after);
                transform->isDirty = true;
                ++moved;
            }

            if (moved > 0) {
                entityTransformUndos.push_back(std::move(undo));
                actions.push_back(ACTION_ENTITY_TRANSFORMS);
                ClearRedo();
                MarkUnsavedChanges();
            }

            return moved;
        }

        // The selected entities that have a world Transform.
        std::vector<ComponentTransform*> SelectedTransforms(Level& level) {
            std::vector<ComponentTransform*> transforms;

            for (const ID entityID : selectedEntities)
                if (ComponentTransform* transform = level.transforms.Get(entityID)) transforms.push_back(transform);

            return transforms;
        }

        // A prefab file name from an entity name: no path separators or
        // characters Windows refuses.
        std::string PrefabFileStem(const std::string& entityName) {
            std::string stem;

            for (const char c : entityName) stem += std::string_view("\\/:*?\"<>|").find(c) == std::string_view::npos ? c : '_';

            while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();

            return stem.empty() ? "Entity" : stem;
        }
    }

    bool SplitWallAt(const ID wallID, const Vector2& point) {
        Level& level = LevelManager::CurrentLevel();

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();
        if (SplitWallNoUndo(level, wallID, point) == INVALID_ID) return false;

        PushGeometryUndoSnapshot(std::move(snapshot));
        MarkUnsavedChanges();
        return true;
    }

    int SplitSelectedWallsAtMidpoint() {
        Level& level = LevelManager::CurrentLevel();

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();
        int split = 0;

        // Copied: each split adds its new half to the selection.
        for (const ID wallID : std::vector<ID>(selectedWalls)) {
            const Wall* wall = FindWall(level, wallID);
            if (wall == nullptr) continue;

            const Vector2 midpoint = {(wall->start.x + wall->end.x) * 0.5f, (wall->start.y + wall->end.y) * 0.5f};
            if (SplitWallNoUndo(level, wallID, midpoint) != INVALID_ID) ++split;
        }

        if (split > 0) {
            PushGeometryUndoSnapshot(std::move(snapshot));
            MarkUnsavedChanges();
        }

        return split;
    }

    int MergeSelectedWalls() {
        Level& level = LevelManager::CurrentLevel();

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();
        int merged = 0;

        // Every join can make another possible (a row of three becomes one),
        // so keep going until a pass finds nothing.
        for (bool joined = true; joined;) {
            joined = false;

            for (size_t i = 0; i < selectedWalls.size() && !joined; ++i)
                for (size_t j = 0; j < selectedWalls.size() && !joined; ++j)
                    if (i != j && TryMergeWalls(level, selectedWalls[i], selectedWalls[j])) {
                        joined = true;
                        ++merged;
                    }
        }

        if (merged > 0) {
            RebuildGeometryAfterMove();
            PushGeometryUndoSnapshot(std::move(snapshot));
            MarkUnsavedChanges();
        }

        return merged;
    }

    int ApplyToSelectedWalls(const WallBatchValues& values) {
        if (!values.applyTopTexture && !values.applyBottomTexture && !values.applyColor) return 0;

        Level& level = LevelManager::CurrentLevel();

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();
        int changed = 0;

        for (const ID wallID : selectedWalls) {
            Wall* wall = FindWall(level, wallID);
            if (wall == nullptr) continue;

            if (values.applyTopTexture) wall->top.texture = values.topTexture;
            if (values.applyBottomTexture) wall->bottom.texture = values.bottomTexture;
            if (values.applyColor) wall->color = values.color;
            ++changed;
        }

        if (changed > 0) {
            PushGeometryUndoSnapshot(std::move(snapshot));
            MarkUnsavedChanges();
        }

        return changed;
    }

    int AlignSelectedWallTextures() {
        if (selectedWalls.empty()) return 0;

        Level& level = LevelManager::CurrentLevel();

        const ID primaryID = IsWallSelected(selectedWallID) ? selectedWallID : selectedWalls.front();
        Wall* primary = FindWall(level, primaryID);
        if (primary == nullptr) return 0;

        std::vector<ChainStep> forward;
        std::vector<ChainStep> backward;
        WalkSelectedChain(level, *primary, forward, backward);

        if (forward.empty() && backward.empty()) return 0;

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();

        for (const WallSurfaceSlot slot : WALL_SLOTS) {
            const WallSurface& first = primary->Surface(slot);

            // The walk keeps the primary's U direction the whole way round.
            const float chainSign = USign(first);
            const float startU = first.textureOffset.x / WALL_TEXTURE_TILE;
            const float endU = startU + chainSign * primary->length * first.textureScale.x / WALL_TEXTURE_TILE;

            float u = endU;
            for (const ChainStep& step : forward) {
                Wall* wall = FindWall(level, step.wallID);
                WallSurface& surface = wall->Surface(slot);

                u = ContinueChain(surface, wall->length, step.direction, chainSign, u);
                surface.textureOffset.y = first.textureOffset.y;
            }

            // Backwards each wall is left at the known U, so its entry U
            // is that minus its own length.
            u = startU;
            for (const ChainStep& step : backward) {
                Wall* wall = FindWall(level, step.wallID);
                WallSurface& surface = wall->Surface(slot);

                const float entryU = u - chainSign * wall->length * surface.textureScale.x / WALL_TEXTURE_TILE;
                ContinueChain(surface, wall->length, step.direction, chainSign, entryU);
                surface.textureOffset.y = first.textureOffset.y;
                u = entryU;
            }
        }

        PushGeometryUndoSnapshot(std::move(snapshot));
        MarkUnsavedChanges();

        return static_cast<int>(forward.size() + backward.size()) + 1;
    }

    int ResetSelectedWallOffsets() {
        Level& level = LevelManager::CurrentLevel();

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();
        int changed = 0;

        for (const ID wallID : selectedWalls) {
            Wall* wall = FindWall(level, wallID);
            if (wall == nullptr) continue;

            wall->top.textureOffset = {0.0f, 0.0f};
            wall->bottom.textureOffset = {0.0f, 0.0f};
            ++changed;
        }

        if (changed > 0) {
            PushGeometryUndoSnapshot(std::move(snapshot));
            MarkUnsavedChanges();
        }

        return changed;
    }

    int FitSelectedWallTexturesToHeight() {
        Level& level = LevelManager::CurrentLevel();

        GeometrySnapshot snapshot = CaptureGeometryUndoSnapshot();
        int changed = 0;

        WallPieces::SectorBoundsCache boundsCache;
        std::vector<WallPieces::WallPiece> pieces;

        for (const ID wallID : selectedWalls) {
            Wall* wall = FindWall(level, wallID);
            if (wall == nullptr) continue;

            pieces.clear();
            WallPieces::Build(level, *wall, boundsCache, pieces);

            bool wallChanged = false;

            for (const WallSurfaceSlot slot : WALL_SLOTS) {
                float tallest = 0.0f;

                for (const WallPieces::WallPiece& piece : pieces) {
                    if (piece.slot != slot) continue;

                    tallest = std::max({tallest, piece.topStart - piece.bottomStart, piece.topEnd - piece.bottomEnd});
                }

                if (tallest < 0.01f) continue;

                WallSurface& surface = wall->Surface(slot);
                surface.textureScale.y = WALL_TEXTURE_TILE / tallest;
                surface.textureOffset.y = 0.0f;
                wallChanged = true;
            }

            if (wallChanged) ++changed;
        }

        if (changed > 0) {
            PushGeometryUndoSnapshot(std::move(snapshot));
            MarkUnsavedChanges();
        }

        return changed;
    }

    // =========================================================================
    //  Canvas filters
    // =========================================================================

    bool showSolidWalls = true;
    bool showPortalWalls = true;

    std::vector<int> hiddenEntityComponentTypes;
    std::vector<std::string> hiddenEntityTags;

    bool WallPassesFilter(const Wall& wall) {
        return wall.IsPortal() ? showPortalWalls : showSolidWalls;
    }

    bool EntityPassesFilter(const Entity& entity) {
        for (const int type : hiddenEntityComponentTypes)
            if (entity.componentsMask.test(static_cast<size_t>(type))) return false;

        for (const std::string& tag : hiddenEntityTags)
            if (std::find(entity.tags.begin(), entity.tags.end(), tag) != entity.tags.end()) return false;

        return true;
    }

    // =========================================================================
    //  Entity Mode placement and selection actions
    // =========================================================================

    std::string entityPaletteSelection;
    bool placeOnFloor = true;
    bool placeRandomYaw = false;
    std::string placeDefaultTag;

    std::vector<EntityTransformUndo> entityTransformUndos;

    float FloorHeightAt(const Vector2& point) {
        const Level& level = LevelManager::CurrentLevel();

        const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, point);

        return sectorIndex >= 0 && !level.sectors[sectorIndex].floors.empty()
            ? level.sectors[sectorIndex].floors.front().floor.height
            : 0.0f;
    }

    ID PlacePaletteEntityAt(const Vector2& point) {
        Level& level = LevelManager::CurrentLevel();

        ID id = INVALID_ID;

        if (entityPaletteSelection.empty()) id = level.CreateEntity(false);
        else {
            const Level* prefab = Prefab::Get(entityPaletteSelection);
            if (prefab == nullptr || prefab->ui_transforms.Has(Prefab::RootOf(*prefab).id)) return INVALID_ID;

            id = Prefab::Instantiate(level, entityPaletteSelection);
        }

        if (id == INVALID_ID) return INVALID_ID;

        if (ComponentTransform* transform = level.transforms.Get(id)) {
            transform->SetPosition({point.x, placeOnFloor ? FloorHeightAt(point) : 0.0f, point.y});

            if (placeRandomYaw) {
                static std::mt19937 random(std::random_device{}());
                std::uniform_real_distribution<float> angle(0.0f, 2.0f * Constants::Pi);

                transform->rotation = Quaternion::FromAxisAngle(0.0f, 1.0f, 0.0f, angle(random)) * transform->rotation;
            }

            transform->isDirty = true;
        }

        if (Entity* entity = level.GetEntity(id); entity != nullptr && !placeDefaultTag.empty()) {
            const auto tagId = TagRegistry::Find(placeDefaultTag);

            if (tagId && std::find(entity->tags.begin(), entity->tags.end(), placeDefaultTag) == entity->tags.end()) {
                entity->tags.push_back(placeDefaultTag);
                entity->tagIds.push_back(*tagId);
            }
        }

        SelectEntity(id);
        MarkUnsavedChanges();

        return id;
    }

    int AlignSelectedEntities(const EntityAlignment alignment) {
        Level& level = LevelManager::CurrentLevel();

        const std::vector<ComponentTransform*> transforms = SelectedTransforms(level);
        if (transforms.size() < 2) return 0;

        const bool alongX = alignment == ALIGN_MIN_X || alignment == ALIGN_CENTER_X || alignment == ALIGN_MAX_X;

        float low = std::numeric_limits<float>::max();
        float high = std::numeric_limits<float>::lowest();

        for (const ComponentTransform* transform : transforms) {
            const float value = alongX ? transform->position.x : transform->position.z;
            low = std::min(low, value);
            high = std::max(high, value);
        }

        float target = (low + high) * 0.5f;
        if (alignment == ALIGN_MIN_X || alignment == ALIGN_MIN_Z) target = low;
        if (alignment == ALIGN_MAX_X || alignment == ALIGN_MAX_Z) target = high;

        return MoveSelectedEntities([alongX, target](ID, Vector3 position) {
            (alongX ? position.x : position.z) = target;
            return position;
        });
    }

    int DistributeSelectedEntities(const bool alongX) {
        Level& level = LevelManager::CurrentLevel();

        std::vector<std::pair<float, ID>> order;

        for (const ID entityID : selectedEntities)
            if (const ComponentTransform* transform = level.transforms.Get(entityID))
                order.emplace_back(alongX ? transform->position.x : transform->position.z, entityID);

        if (order.size() < 3) return 0;

        std::sort(order.begin(), order.end());

        const float first = order.front().first;
        const float step = (order.back().first - first) / static_cast<float>(order.size() - 1);

        return MoveSelectedEntities([&order, alongX, first, step](const ID entityID, Vector3 position) {
            const auto it = std::find_if(order.begin(), order.end(), [entityID](const auto& entry) { return entry.second == entityID; });
            (alongX ? position.x : position.z) = first + step * static_cast<float>(it - order.begin());
            return position;
        });
    }

    int SnapSelectedEntitiesToGrid() {
        return MoveSelectedEntities([](ID, const Vector3 position) {
            const Vector2 snapped = SnapToGrid({position.x, position.z});
            return Vector3{snapped.x, position.y, snapped.y};
        });
    }

    int DropSelectedEntitiesToFloor() {
        return MoveSelectedEntities([](ID, const Vector3 position) {
            return Vector3{position.x, FloorHeightAt({position.x, position.z}), position.z};
        });
    }

    void UndoEntityTransforms() {
        if (entityTransformUndos.empty()) return;

        Level& level = LevelManager::CurrentLevel();

        for (const auto& [entityID, position] : entityTransformUndos.back().positions) {
            ComponentTransform* transform = level.transforms.Get(entityID);
            if (transform == nullptr) continue;

            transform->SetPosition(position);
            transform->isDirty = true;
        }

        entityTransformUndos.pop_back();
        MarkUnsavedChanges();
    }

    namespace {
        // The current positions of the entities `undo` names.
        EntityTransformUndo CurrentPositions(const Level& level, const EntityTransformUndo& undo) {
            EntityTransformUndo current;

            for (const auto& [entityID, position] : undo.positions)
                if (const ComponentTransform* transform = level.transforms.Get(entityID))
                    current.positions.emplace_back(entityID, transform->position);

            return current;
        }
    }

    void UndoLastAction() {
        if (actions.empty()) return;

        Level& level = LevelManager::CurrentLevel();

        switch (actions.back()) {
            case ACTION_CREATE_CORNER:
                if (!dots.empty()) DeleteDot(dots.back().id);
                ClearRedo();
                break;
            case ACTION_CREATE_WALL:
                if (!level.walls.empty()) DeleteWall(level.walls.back().id);
                ClearRedo();
                break;
            case ACTION_CREATE_SECTOR:
                if (!level.sectors.empty()) DeleteSector(level.sectors.back().id);
                ClearRedo();
                break;
            case ACTION_CREATE_OBJECT:
                if (!level.entities.empty()) {
                    const Entity entity = level.entities.back();
                    level.DestroyEntity(entity.id);
                }
                ClearRedo();
                break;
            case ACTION_APPLY_GEOMETRY:
                // One ApplyDrawnGeometry call can create/split many
                // walls and sectors at once, so undo restores the
                // whole-operation snapshot rather than guessing at
                // "the last wall" / "the last sector" the way the
                // simpler action kinds above do.
                if (!geometrySnapshots.empty()) {
                    RedoEntry redo;
                    redo.action = ACTION_APPLY_GEOMETRY;
                    redo.geometry = CaptureGeometryUndoSnapshot();

                    RestoreGeometrySnapshot(geometrySnapshots.back());
                    geometrySnapshots.pop_back();

                    redoEntries.push_back(std::move(redo));
                }
                break;
            case ACTION_ENTITY_TRANSFORMS:
                if (!entityTransformUndos.empty()) {
                    RedoEntry redo;
                    redo.action = ACTION_ENTITY_TRANSFORMS;
                    redo.transforms = CurrentPositions(level, entityTransformUndos.back());

                    UndoEntityTransforms();
                    redoEntries.push_back(std::move(redo));
                }
                break;
            default: break;
        }

        actions.pop_back();
    }

    void RedoLastAction() {
        if (redoEntries.empty()) return;

        RedoEntry redo = std::move(redoEntries.back());
        redoEntries.pop_back();

        Level& level = LevelManager::CurrentLevel();

        if (redo.action == ACTION_APPLY_GEOMETRY) {
            geometrySnapshots.push_back(CaptureGeometryUndoSnapshot());
            RestoreGeometrySnapshot(redo.geometry);
        }
        else if (redo.action == ACTION_ENTITY_TRANSFORMS) {
            entityTransformUndos.push_back(CurrentPositions(level, redo.transforms));

            for (const auto& [entityID, position] : redo.transforms.positions) {
                ComponentTransform* transform = level.transforms.Get(entityID);
                if (transform == nullptr) continue;

                transform->SetPosition(position);
                transform->isDirty = true;
            }

            MarkUnsavedChanges();
        }
        else return;

        // Not through the usual push: that would clear the rest of the redo.
        actions.push_back(redo.action);
    }

    void SelectAllInMode() {
        Level& level = LevelManager::CurrentLevel();

        if (currentMode == MODE_GEOMETRY) {
            ClearWallSelection();

            for (const Wall& wall : level.walls) {
                if (!WallPassesFilter(wall)) continue;

                selectedWalls.push_back(wall.id);
                selectedWallID = wall.id;
            }
        }
        else if (currentMode == MODE_ENTITY) {
            ClearEntitySelection();

            for (const Entity& entity : level.entities) {
                if (!EntityPassesFilter(entity) || level.transforms.Get(entity.id) == nullptr) continue;

                if (selectedEntities.empty()) SelectEntity(entity.id);
                else selectedEntities.push_back(entity.id);
            }
        }
        else if (currentMode == MODE_SECTOR) {
            ClearSectorSelection();

            for (const Sector& sector : level.sectors) {
                if (selectedSectors.empty()) SelectSector(sector.id);
                else selectedSectors.push_back(sector.id);
            }
        }
    }

    int CreatePrefabsFromSelectedEntities(std::string* errorMessage) {
        Level& level = LevelManager::CurrentLevel();

        const fs::path folder = ProjectManager::GetAssetsPath() / "Prefabs";

        std::error_code ec;
        fs::create_directories(folder, ec);
        if (ec) {
            if (errorMessage) *errorMessage = "could not create " + folder.string() + ": " + ec.message();
            return 0;
        }

        int created = 0;

        // Copied: linking an entity doesn't change the selection, but be safe.
        for (const ID entityID : std::vector<ID>(selectedEntities)) {
            const Entity* entity = level.GetEntity(entityID);
            if (entity == nullptr || level.ui_transforms.Has(entityID)) continue;

            // Never overwrite: "Goblin.prefab", then "Goblin (2).prefab", ...
            const std::string stem = PrefabFileStem(entity->name);
            std::string fileName = stem + std::string(Prefab::kExtension);

            for (int copy = 2; fs::exists(folder / fileName, ec); ++copy)
                fileName = stem + " (" + std::to_string(copy) + ")" + std::string(Prefab::kExtension);

            if (Prefab::CreateFromEntity(level, entityID, "Prefabs/" + fileName, errorMessage)) ++created;
        }

        if (created > 0) MarkUnsavedChanges();

        return created;
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
                {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
                {40.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}}
            });
        }

        if (copy.id == INVALID_ID) copy.id = level.nextSectorID++;
        else level.nextSectorID = std::max(level.nextSectorID, copy.id + 1);

        level.sectors.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }
}