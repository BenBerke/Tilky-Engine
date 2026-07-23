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

    Vector2 SnapToGrid(const Vector2& worldPos) {
        const float activeGridSize = GetActiveGridSize();

        return {
            std::round(worldPos.x / activeGridSize) * activeGridSize,
            std::round(worldPos.y / activeGridSize) * activeGridSize
        };
    }

    // Snap priority is {dots, wall starts, wall ends} (nearest within
    // radius wins); if nothing there is close enough, falls back to the
    // closest point along any wall's *interior* within radius; only
    // falls back to grid snapping if nothing at all is close by.
    // Snapping onto a wall's interior is what lets a drawn chain start
    // or end mid-wall - committing the chain is what actually splits the
    // wall there (see ApplyDrawnGeometry / MapTopology::ApplyDrawnGeometry).
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

        if (found) return best;

        Vector2 onWall{};
        if (MapTopology::ClosestPointOnAnyWall(level.walls, mouseWorld, snapRadiusWorld, &onWall)) return onWall;

        return SnapToGrid(mouseWorld);
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
            snapshot.selectedDotID = selectedDotID;
            snapshot.editingSector = editingSector;
            snapshot.editingWall = editingWall;
            return snapshot;
        }
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
        selectedDotID = snapshot.selectedDotID;
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

        if (copy.floors.empty()) {
            copy.floors.push_back({
                {0.0f, {255.0f, 255.0f, 255.0f}, {}},
                {40.0f, {255.0f, 255.0f, 255.0f}, {}}
            });
        }

        if (copy.id == INVALID_ID) copy.id = level.nextSectorID++;
        else level.nextSectorID = std::max(level.nextSectorID, copy.id + 1);

        level.sectors.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    // Legacy single-room helper. Sector Mode uses
    // MapEditorInternal::ApplyDrawnGeometry so walls and sector sides are
    // created through MapTopology.
    void CreateSector(
        const std::vector<Vector2>& vertices,
        const float ceilHeight,
        const float floorHeight,
        const Vector3& ceilColor,
        const Vector3& floorColor,
        const std::string& ceilTexture,
        const std::string& floorTexture
    ) {
        if (ceilHeight <= floorHeight) return;

        Sector newSector;
        newSector.vertices = vertices;
        newSector.triangles = Geometry::Triangulate(vertices);
        newSector.floors = {
            {
                {floorHeight, floorColor, floorTexture},
                {ceilHeight, ceilColor, ceilTexture}
            }
        };

        AddSector(newSector);
    }

    // Legacy, probably should be removed in the future
    //todo remove
    void TriangulateSectors() {
        LevelManager::TriangulateCurrentLevelSectors();
    }
}