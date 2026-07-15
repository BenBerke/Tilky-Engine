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
#include "Headers/Objects/Entity.hpp"

// This is an internal file for functions related to mathematical calculations about the map,
// plus (after the editor revamp) the Sector Mode chain workflow and Dot lifecycle.
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

    bool AABBCollisionWithEntity(const ComponentUITransform& transform, const Vector2& mousePosition) {
        // These functions automatically get converted to their respective platform
        // versions through the "SSECompat.hpp" header
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
        const __m128 entityPos = transform.position.reg;
        const __m128 entityScale = transform.scale.reg;
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

            if (transform != nullptr ) {
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

    Vector2 SnapToGrid(const Vector2& worldPos) {
        const float activeGridSize = GetActiveGridSize();

        return {
            std::round(worldPos.x / activeGridSize) * activeGridSize,
            std::round(worldPos.y / activeGridSize) * activeGridSize
        };
    }

    // Feature #7: snap priority is {dots, wall starts, wall ends} (nearest
    // within radius wins), falling back to grid snapping when nothing is
    // close enough. Does NOT snap to arbitrary points along a wall segment.
    Vector2 ResolveSnapPoint(const Vector2& mouseWorld) {
        constexpr float snapRadiusPixels = 12.0f;
        const float safeZoom = std::max(editorZoom, 0.0001f);
        const float snapRadiusWorld = snapRadiusPixels / safeZoom;
        const float snapRadiusSq = snapRadiusWorld * snapRadiusWorld;

        Vector2 best{};
        float bestDistSq = std::numeric_limits<float>::max();
        bool found = false;

        const auto consider = [&](const Vector2& candidate) {
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

        for (const Wall& wall : level.walls) {
            consider(wall.start);
            consider(wall.end);
        }

        if (!found) {
            return SnapToGrid(mouseWorld);
        }

        return best;
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
        bool SegmentsProperlyIntersect(const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2) {
            const auto cross = [](const Vector2& o, const Vector2& p, const Vector2& q) -> float {
                return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x);
            };

            const float d1 = cross(b1, b2, a1);
            const float d2 = cross(b1, b2, a2);
            const float d3 = cross(a1, a2, b1);
            const float d4 = cross(a1, a2, b2);

            const bool straddlesB = (d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f);
            const bool straddlesA = (d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f);

            return straddlesB && straddlesA;
        }

        // Edges that share a vertex by construction (adjacent edges of the
        // polygon) are deliberately skipped - we only care about *improper*
        // crossings between non-adjacent edges.
        bool IsChainSelfIntersecting(const std::vector<Vector2>& vertices) {
            const int n = static_cast<int>(vertices.size());

            for (int i = 0; i < n; ++i) {
                const Vector2& a1 = vertices[i];
                const Vector2& a2 = vertices[(i + 1) % n];

                for (int j = i + 1; j < n; ++j) {
                    if (j == i) continue;
                    if (j == (i + 1) % n) continue;
                    if ((j + 1) % n == i) continue;

                    const Vector2& b1 = vertices[j];
                    const Vector2& b2 = vertices[(j + 1) % n];

                    if (SegmentsProperlyIntersect(a1, a2, b1, b2)) {
                        return true;
                    }
                }
            }

            return false;
        }

        // Feature #5 "Validation before sector creation".
        bool ValidateSectorChain(const std::vector<Vector2>& vertices, std::string* outReason) {
            if (vertices.size() < 3) {
                if (outReason != nullptr) *outReason = "fewer than 3 points";
                return false;
            }

            const int n = static_cast<int>(vertices.size());

            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    if (SamePoint(vertices[i], vertices[j])) {
                        if (outReason != nullptr) *outReason = "duplicate vertex";
                        return false;
                    }
                }
            }

            for (int i = 0; i < n; ++i) {
                const Vector2& a = vertices[i];
                const Vector2& b = vertices[(i + 1) % n];

                const Vector2 edge = b - a;

                if (Vector2Math::Dot(edge, edge) <= 0.0001f) {
                    if (outReason != nullptr) *outReason = "zero-length edge";
                    return false;
                }
            }

            if (Geometry::PolygonAreaAbs(vertices) <= 0.0001f) {
                if (outReason != nullptr) *outReason = "zero-area polygon";
                return false;
            }

            if (IsChainSelfIntersecting(vertices)) {
                if (outReason != nullptr) *outReason = "self-intersecting polygon";
                return false;
            }

            return true;
        }

        // Picks a point guaranteed to be inside the polygon by using the
        // centroid of one of its own triangles (valid for any simple polygon
        // triangulation, including concave ones - unlike a naive vertex
        // average, which can land outside for concave shapes).
        Vector2 ComputePolygonInteriorPoint(const std::vector<Vector2>& vertices, const std::vector<Triangle>& triangles) {
            if (!triangles.empty()) {
                const Triangle& t = triangles.front();
                return {
                    (t.a.x + t.b.x + t.c.x) / 3.0f,
                    (t.a.y + t.b.y + t.c.y) / 3.0f
                };
            }

            Vector2 sum{0.0f, 0.0f};

            for (const Vector2& v : vertices) {
                sum.x += v.x;
                sum.y += v.y;
            }

            const float n = std::max(1.0f, static_cast<float>(vertices.size()));
            return {sum.x / n, sum.y / n};
        }

        // Feature #5 "Sector creation": reuse an existing wall if its
        // start/end match the edge in either direction, else nullptr.
        Wall* FindWallForEdge(Level& level, const Vector2& a, const Vector2& b) {
            for (Wall& wall : level.walls) {
                const bool sameDirection = SamePoint(wall.start, a) && SamePoint(wall.end, b);
                const bool oppositeDirection = SamePoint(wall.start, b) && SamePoint(wall.end, a);

                if (sameDirection || oppositeDirection) {
                    return &wall;
                }
            }

            return nullptr;
        }

        // Feature #5 "Wall front/back assignment". Never silently overwrites
        // an already-occupied frontSector/backSector with a *different*
        // sector - it logs and bails instead.
        void AssignWallSectorSide(Wall& wall, const ID sectorID, const Vector2&) {
            if (sectorID == INVALID_ID) {
                return;
            }

            // Already attached.
            if (wall.frontSector == sectorID || wall.backSector == sectorID) {
                return;
            }

            // For a newly created wall, always make the creating sector the front side.
            // This avoids winding-dependent bugs where every new wall gets only backSector set.
            if (wall.frontSector == INVALID_ID) {
                wall.frontSector = sectorID;
                return;
            }

            // Shared wall: attach the new sector to the other side.
            if (wall.backSector == INVALID_ID) {
                wall.backSector = sectorID;
                return;
            }

            spdlog::warn(
                "Wall {} already has two sectors: front={}, back={}; cannot attach sector {}",
                wall.id,
                wall.frontSector,
                wall.backSector,
                sectorID
            );
        }

        // Builds the sector itself plus every edge wall (creating new ones
        // or reusing shared ones), assigns stable IDs, assigns front/back
        // sides, and rebuilds the sector/wall ID maps exactly once at the end.
        ID CreateSectorWithWalls(const std::vector<Vector2>& vertices, const PendingSectorParams& params) {
            Level& level = LevelManager::CurrentLevel();

            Sector newSector{};
            newSector.id = level.nextSectorID++;
            newSector.vertices = vertices;
            newSector.triangles = Geometry::Triangulate(vertices);

            newSector.floorHeight = params.floorHeight;
            newSector.ceilingHeight = params.ceilHeight;

            newSector.floorColor = params.floorColor;
            newSector.ceilingColor = params.ceilColor;

            newSector.floorTextureIndex = params.floorTextureIndex;
            newSector.ceilingTextureIndex = params.ceilTextureIndex;

            newSector.lightValue = params.lightValue;

            level.sectors.push_back(newSector);

            const Vector2 interiorPoint = ComputePolygonInteriorPoint(vertices, newSector.triangles);

            const int n = static_cast<int>(vertices.size());

            for (int i = 0; i < n; ++i) {
                const Vector2& edgeStart = vertices[i];
                const Vector2& edgeEnd = vertices[(i + 1) % n];

                Wall* wall = FindWallForEdge(level, edgeStart, edgeEnd);

                if (wall == nullptr) {
                    Wall newWall(
                        edgeStart,
                        edgeEnd,
                        {255.0f, 255.0f, 255.0f, 255.0f},
                        INVALID_ID,
                        INVALID_ID,
                        params.wallTextureIndex,
                        currentFloor
                    );

                    newWall.id = level.nextWallID++;

                    level.walls.push_back(newWall);
                    wall = &level.walls.back();
                }

                AssignWallSectorSide(*wall, newSector.id, interiorPoint);
            }

            MapQueries::RebuildSectorRuntimeLinks(level);

            // ---- TEMPORARY DIAGNOSTIC — remove once root cause is found ----
            // {
            //     const auto sectorIt = level.sectorIDToIndex.find(newSector.id);
            //     const int newSectorIndex =
            //         sectorIt != level.sectorIDToIndex.end() ? sectorIt->second : -1;
            //
            //     spdlog::warn(
            //         "CREATE SECTOR DIAG: id={} index={} floor={} ceil={} vertices={} triangles={} sectorCount={} wallCount={}",
            //         newSector.id,
            //         newSectorIndex,
            //         newSector.floorHeight,
            //         newSector.ceilingHeight,
            //         newSector.vertices.size(),
            //         newSector.triangles.size(),
            //         level.sectors.size(),
            //         level.walls.size()
            //     );
            //
            //     if (newSectorIndex >= 0 && newSectorIndex < static_cast<int>(level.sectors.size())) {
            //         const Sector& createdSector = level.sectors[newSectorIndex];
            //
            //         spdlog::warn(
            //             "CREATED SECTOR AFTER REBUILD: id={} floor={} ceil={} walls={}",
            //             createdSector.id,
            //             createdSector.floorHeight,
            //             createdSector.ceilingHeight,
            //             createdSector.walls.size()
            //         );
            //
            //         for (const Wall* w : createdSector.walls) {
            //             if (w == nullptr) {
            //                 spdlog::warn("  sector wall ptr=null");
            //                 continue;
            //             }
            //
            //             spdlog::warn(
            //                 "  sector wall ptr id={} front={} back={} tex={} quadCount={} start=({}, {}) end=({}, {})",
            //                 w->id, w->frontSector, w->backSector, w->textureIndex,
            //                 w->quad3DCount, w->start.x, w->start.y, w->end.x, w->end.y
            //             );
            //         }
            //     }
            //
            //     for (const Wall& w : level.walls) {
            //         spdlog::warn(
            //             "LEVEL WALL: id={} front={} back={} tex={} quadCount={} start=({}, {}) end=({}, {})",
            //             w.id, w.frontSector, w.backSector, w.textureIndex,
            //             w.quad3DCount, w.start.x, w.start.y, w.end.x, w.end.y
            //         );
            //     }
            // }
            // ---- END TEMPORARY DIAGNOSTIC ----

            return newSector.id;
        }


        void RebuildDotIDLookup() {
            dotIDToIndex.clear();

            for (int i = 0; i < static_cast<int>(dots.size()); ++i) {
                dotIDToIndex[dots[i].id] = i;
            }
        }
    }



    void CancelSectorChain() {
        sectorBeingCreated.clear();
    }

    void FinishSectorSelection() {
        if (sectorBeingCreated.size() < 3) {
            if (!sectorBeingCreated.empty()) {
                SDL_Log("Sector cancelled: fewer than 3 points");
            }

            sectorBeingCreated.clear();
            return;
        }

        std::string rejectReason;

        if (!ValidateSectorChain(sectorBeingCreated, &rejectReason)) {
            SDL_Log("Sector cancelled: %s", rejectReason.c_str());
            sectorBeingCreated.clear();
            return;
        }

        const ID newSectorID = CreateSectorWithWalls(sectorBeingCreated, pendingSectorParams);

        SDL_Log("Sector created with %d vertices", static_cast<int>(sectorBeingCreated.size()));

        sectorBeingCreated.clear();

        selectedSectorID = newSectorID;
        editingSector = true;
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
            pendingSectorParams = PendingSectorParams{
                wallTextureIndex,
                ceilTextureIndex,
                floorTextureIndex,
                floorHeight,
                ceilHeight,
                lightValue,
                wallColor,
                ceilColor,
                floorColor
            };

            sectorBeingCreated.push_back(point);
            return;
        }

        if (sectorBeingCreated.size() >= 3 && SamePoint(point, sectorBeingCreated.front())) {
            FinishSectorSelection();
            return;
        }

        AddSectorSelectionPoint(point);
    }

    void ClearManualSectorSelection() {
        manualSectorDots.clear();
    }

    // Sector only — deliberately never touches level.walls or
    // FindWallForEdge/AssignWallSectorSide/CreateSectorWithWalls.
    void CreateManualSector() {
        if (manualSectorDots.size() < 3) return;

        std::string rejectReason;
        if (!ValidateSectorChain(manualSectorDots, &rejectReason)) {
            SDL_Log("Manual sector cancelled: %s", rejectReason.c_str());
            manualSectorDots.clear();
            return;
        }

        Level& level = LevelManager::CurrentLevel();
        const ID newSectorID = level.nextSectorID; // AddSector() will assign exactly this

        Sector newSector{};
        newSector.vertices  = manualSectorDots;
        newSector.triangles = Geometry::Triangulate(newSector.vertices);

        newSector.floorHeight         = floorHeight;
        newSector.ceilingHeight       = ceilHeight;
        newSector.floorColor          = floorColor;
        newSector.ceilingColor        = ceilColor;
        newSector.floorTextureIndex   = floorTextureIndex;
        newSector.ceilingTextureIndex = ceilTextureIndex;
        newSector.lightValue          = lightValue;

        Editor::AddSector(newSector);

        SDL_Log("Manual sector created with %d vertices", static_cast<int>(manualSectorDots.size()));

        manualSectorDots.clear();

        selectedSectorID = newSectorID;
        editingSector    = true;
    }

    void AddDot(const Vector2& position) {
        Dot dot;
        dot.id = nextDotID++;
        dot.position = position;

        dots.push_back(dot);
        dotIDToIndex[dot.id] = static_cast<int>(dots.size()) - 1;

        actions.push_back(ACTION_CREATE_DOT);
    }

    void RemoveDot(const ID dotID) {
        const auto it = dotIDToIndex.find(dotID);

        if (it == dotIDToIndex.end()) {
            return;
        }

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(dots.size())) {
            return;
        }

        dots.erase(dots.begin() + index);

        if (selectedDotID == dotID) {
            selectedDotID = INVALID_ID;
        }

        RebuildDotIDLookup();
    }

    // Feature #11 "When deleting a sector": removes the sector, clears the
    // selection if it pointed at this sector, detaches any walls that
    // referenced it (dropping walls that become orphaned on both sides), and
    // rebuilds the ID maps immediately.
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

        level.sectors.erase(level.sectors.begin() + index);

        if (selectedSectorID == sectorID) {
            selectedSectorID = INVALID_ID;
            editingSector = false;
        }

        for (int i = static_cast<int>(level.walls.size()) - 1; i >= 0; --i) {
            Wall& wall = level.walls[i];

            bool touched = false;

            if (wall.frontSector == sectorID) {
                wall.frontSector = INVALID_ID;
                touched = true;
            }

            if (wall.backSector == sectorID) {
                wall.backSector = INVALID_ID;
                touched = true;
            }

            if (touched && wall.frontSector == INVALID_ID && wall.backSector == INVALID_ID) {
                level.walls.erase(level.walls.begin() + i);
            }
        }

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

    // Dot Mode — right click on a wall selects it and opens the wall inspector.
    void HandleDotModeRightClick(const Vector2& point) {
        Level& level = LevelManager::CurrentLevel();
        const int wallIndex = GetWallAtPoint(point);
        if (wallIndex < 0 || wallIndex >= static_cast<int>(level.walls.size())) return;
        selectedWallID = level.walls[wallIndex].id;
        editingWall    = true;
    }

    // Mirrors DeleteSector's ID-safe pattern — needed by the reinstated wall inspector's delete button.
    void DeleteWall(const ID wallID) {
        Level& level = LevelManager::CurrentLevel();

        const auto it = level.wallIDToIndex.find(wallID);
        if (it == level.wallIDToIndex.end()) return;

        const int index = it->second;
        if (index < 0 || index >= static_cast<int>(level.walls.size())) return;

        level.walls.erase(level.walls.begin() + index);

        if (selectedWallID == wallID) {
            selectedWallID = INVALID_ID;
            editingWall    = false;
        }

        MapQueries::RebuildSectorRuntimeLinks(level);
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

        constexpr float clickRadiusPixels = 10.0f;
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

        if (copy.id == INVALID_ID) copy.id = level.nextSectorID++;
        else level.nextSectorID = std::max(level.nextSectorID, copy.id + 1);

        level.sectors.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    // NOTE: kept from before the revamp for backward compatibility. It does
    // NOT create/reuse walls or assign front/back sectors, so it is no
    // longer used by the Sector Mode chain workflow (see
    // MapEditorInternal::FinishSectorSelection / CreateSectorWithWalls in
    // this file instead
    void CreateSector(
     const std::vector<Vector2>& vertices,
     const float ceilHeight,
     const float floorHeight,
     const Vector3& ceilColor,
     const Vector3& floorColor,
     const int ceilTextureIndex,
     const int floorTextureIndex
 ) {
        Sector newSector{};

        newSector.vertices = vertices;
        newSector.triangles = Geometry::Triangulate(newSector.vertices);

        newSector.ceilingHeight = ceilHeight;
        newSector.floorHeight = floorHeight;

        newSector.ceilingColor = ceilColor;
        newSector.floorColor = floorColor;

        newSector.ceilingTextureIndex = ceilTextureIndex;
        newSector.floorTextureIndex = floorTextureIndex;

        AddSector(newSector);
    }

    // Legacy, probably should be removed in the future
    //todo remove
    void TriangulateSectors() {
        LevelManager::TriangulateCurrentLevelSectors();
    }
}