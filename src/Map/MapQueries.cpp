//
// Created by berke on 4/30/2026.
//

#include "Headers/Map/MapQueries.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "Headers/Map/WallPieces.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Level.hpp"

namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector> &sectors, const Vector2 position, const int hintSector)
    {
        // Fast path: still in the same sector (overwhelmingly common)
        if (hintSector >= 0 && hintSector < static_cast<int>(sectors.size())) {
            if (Geometry::IsPointInPolygon(sectors[hintSector].vertices, sectors[hintSector].innerLoops, position)) return hintSector;

            // Check neighbours before doing a full scan. This is also
            // what catches the hint sector having been left through a
            // hole: the sector nested inside it is one of its neighbours
            // (the wall between them puts a sector on each side), so it
            // gets tried here without any nesting-specific logic.
            for (const Sector* nb : sectors[hintSector].neighbors) {
                if (nb == nullptr) continue;
                const int ni = static_cast<int>(nb - sectors.data());
                if (Geometry::IsPointInPolygon(sectors[ni].vertices, sectors[ni].innerLoops, position)) return ni;
            }
        }

        // Full scan fallback — only hits when entity moves far in one frame.
        // Smallest-area-wins remains a useful tie-break for malformed or
        // transiently-overlapping geometry, but well-formed nested sectors
        // no longer need it to disambiguate: the hole-aware test above
        // already excludes a parent from claiming a point that's really
        // inside one of its children.
        int bestSector = -1;
        float bestArea = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(sectors.size()); ++i) {
            if (!Geometry::IsPointInPolygon(sectors[i].vertices, sectors[i].innerLoops, position)) continue;
            const float area = Geometry::PolygonAreaAbs(sectors[i].vertices);
            if (area < bestArea) {
                bestArea = area;
                bestSector = i;
            }
        }

        return bestSector;
    }

    void AssignWallsToSectors(Level& level) {
        for (Sector& sector : level.sectors) sector.walls.clear();

        RebuildSectorIDLookup(level);

        for (Wall& wall : level.walls) {
            Sector* frontSector = GetSectorByID(level, wall.frontSector);
            Sector* backSector = GetSectorByID(level, wall.backSector);

            if (frontSector != nullptr) frontSector->walls.push_back(&wall);

            if (backSector != nullptr && backSector != frontSector) backSector->walls.push_back(&wall);
        }
    }

    void AssignNeighborsToSectors(Level& level) {
        for (Sector& sector : level.sectors) sector.neighbors.clear();

        RebuildSectorIDLookup(level);

        for (Sector& sector : level.sectors) {
            for (Wall* wall : sector.walls) {
                if (wall == nullptr) continue;

                ID neighborSectorID = INVALID_ID;

                if (wall->frontSector == sector.id) neighborSectorID = wall->backSector;
                else if (wall->backSector == sector.id) neighborSectorID = wall->frontSector;
                else continue;


                // ID is unsigned (uint32_t), so "neighborSectorID < 0" could never be
                // true - a boundary wall (no sector on the other side) was only ever
                // being filtered out downstream, by GetSectorByID() returning nullptr
                // for INVALID_ID. Comparing against the sentinel directly makes the
                // intent explicit instead of relying on that as the sole backstop.
                if (neighborSectorID == INVALID_ID || neighborSectorID == sector.id) continue;

                Sector* neighbor = GetSectorByID(level, neighborSectorID);

                if (neighbor == nullptr) continue;

                if (std::ranges::find(sector.neighbors, neighbor) == sector.neighbors.end())
                    sector.neighbors.push_back(neighbor);
            }
        }
    }

    void RebuildSectorIDLookup(Level &level) {
        level.sectorIDToIndex.clear();

        for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i)
            level.sectorIDToIndex[level.sectors[i].id] = i;
    }

    void RebuildWallIDLookup(Level &level) {
        level.wallIDToIndex.clear();

        for (int i = 0; i < static_cast<int>(level.walls.size()); ++i)
            level.wallIDToIndex[level.walls[i].id] = i;
    }

    void RebuildLevelLookups(Level &level) {
        RebuildSectorIDLookup(level);
        RebuildWallIDLookup(level);
    }

    Sector* GetSectorByID(Level &level, const ID sectorID) {
        const auto it = level.sectorIDToIndex.find(sectorID);

        if (it == level.sectorIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.sectors.size())) return nullptr;

        return &level.sectors[index];
    }

    const Sector* GetSectorByID(const Level &level, const ID sectorID) {
        const auto it = level.sectorIDToIndex.find(sectorID);

        if (it == level.sectorIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.sectors.size())) return nullptr;

        return &level.sectors[index];
    }

    Wall* GetWallByID(Level &level, const ID wallID) {
        const auto it = level.wallIDToIndex.find(wallID);

        if (it == level.wallIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.walls.size())) return nullptr;

        return &level.walls[index];
    }

    const Wall* GetWallByID(const Level &level, const ID wallID) {
        const auto it = level.wallIDToIndex.find(wallID);

        if (it == level.wallIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.walls.size())) return nullptr;

        return &level.walls[index];
    }

    void RebuildSectorRuntimeLinks(Level& level) {
        RebuildLevelLookups(level);
        AssignWallsToSectors(level);
        AssignNeighborsToSectors(level);
        level.RebuildSectorChildren();
    }

    void BuildPvs(Level& level) {
        //todo TILKYTODO
    }

    namespace {
        // Floor/ceiling gaps thinner than this are closed - same threshold
        // as MIN_WALL_HEIGHT in WallPieces.
        constexpr float MIN_OPENING_HEIGHT = 0.0001f;

        // Clip-space w below this is at or behind the camera.
        constexpr float MIN_CLIP_W = 0.0001f;

        // An eye this close to a portal (map units) sees through it at any
        // angle - edge-on, its projection would shrink to a line.
        constexpr float EYE_PORTAL_DISTANCE = 0.5f;

        // A 4-point start, one point added per clip: 4 opening clips + near.
        constexpr int MAX_POLYGON_POINTS = 12;

        // Screen pixels [minX, maxX) x [minY, maxY).
        struct PixelRect {
            int minX = 0;
            int minY = 0;
            int maxX = 0;
            int maxY = 0;

            [[nodiscard]] bool Empty() const { return minX >= maxX || minY >= maxY; }

            bool operator==(const PixelRect&) const = default;
        };

        PixelRect Intersect(const PixelRect& a, const PixelRect& b) {
            return {std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY)};
        }

        PixelRect Union(const PixelRect& a, const PixelRect& b) {
            if (a.Empty()) return b;
            if (b.Empty()) return a;
            return {std::min(a.minX, b.minX), std::min(a.minY, b.minY), std::max(a.maxX, b.maxX), std::max(a.maxY, b.maxY)};
        }

        // A point on a wall: t runs 0 at the start to 1 at the end, y is height.
        struct WallPoint {
            float t = 0.0f;
            float y = 0.0f;
        };

        struct ClipPoint {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        WallPoint Lerp(const WallPoint& a, const WallPoint& b, const float s) {
            return {a.t + (b.t - a.t) * s, a.y + (b.y - a.y) * s};
        }

        ClipPoint Lerp(const ClipPoint& a, const ClipPoint& b, const float s) {
            return {a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s, a.w + (b.w - a.w) * s};
        }

        // Sutherland-Hodgman against one half-plane: keeps points where
        // distance(point) >= 0. Returns the new point count.
        template <typename Point, typename Distance>
        int ClipPolygon(std::array<Point, MAX_POLYGON_POINTS>& polygon, const int count, Distance distance) {
            std::array<Point, MAX_POLYGON_POINTS> clipped = {};
            int clippedCount = 0;

            for (int i = 0; i < count; ++i) {
                const Point& a = polygon[i];
                const Point& b = polygon[(i + 1) % count];
                const float da = distance(a);
                const float db = distance(b);

                if (da >= 0.0f) clipped[clippedCount++] = a;
                if ((da >= 0.0f) != (db >= 0.0f)) clipped[clippedCount++] = Lerp(a, b, da / (da - db));
            }

            polygon = clipped;
            return clippedCount;
        }

        // A floor or ceiling along the wall - slopes are planes, so it is
        // linear in t.
        struct HeightLine {
            float start = 0.0f;
            float end = 0.0f;

            [[nodiscard]] float At(const float t) const { return start + (end - start) * t; }
        };

        // How far a see-through floor or ceiling leaves the opening open:
        // such a surface hides nothing behind it (a sky ceiling, a pillar's
        // invisible top), so it can't close the opening.
        constexpr float OPEN_HEIGHT = 100000.0f;

        // `openHeight` is where the line sits when the surface is see-through:
        // -OPEN_HEIGHT for a floor, +OPEN_HEIGHT for a ceiling.
        HeightLine SurfaceLine(const SectorSurface& surface, const WallPieces::SectorBounds& bounds, const Wall& wall, const float openHeight) {
            if (surface.color.w < 1.0f) return {openHeight, openHeight};

            return {
                WallPieces::GetSurfaceHeight(surface, bounds, wall.start),
                WallPieces::GetSurfaceHeight(surface, bounds, wall.end)
            };
        }

        float SegmentDistanceSq(const Vector2& point, const Vector2& a, const Vector2& b) {
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float lengthSq = abx * abx + aby * aby;

            float t = 0.0f;
            if (lengthSq > 0.0f) t = std::clamp(((point.x - a.x) * abx + (point.y - a.y) * aby) / lengthSq, 0.0f, 1.0f);

            const float dx = point.x - (a.x + abx * t);
            const float dy = point.y - (a.y + aby * t);
            return dx * dx + dy * dy;
        }

        // The eye is the point the projection sends to x = y = w = 0. Fails
        // for an orthographic projection, which has no eye point.
        bool FindEye(const Matrix4& m, float eye[3]) {
            constexpr int ROWS[3] = {0, 1, 3};
            float a[3][3];
            float c[3];

            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) a[i][j] = m.m[ROWS[i]][j];
                c[i] = -m.m[ROWS[i]][3];
            }

            const auto determinant = [](const float k[3][3]) {
                return k[0][0] * (k[1][1] * k[2][2] - k[1][2] * k[2][1]) -
                       k[0][1] * (k[1][0] * k[2][2] - k[1][2] * k[2][0]) +
                       k[0][2] * (k[1][0] * k[2][1] - k[1][1] * k[2][0]);
            };

            const float det = determinant(a);
            if (std::abs(det) < 1e-12f) return false;

            // Cramer's rule.
            for (int column = 0; column < 3; ++column) {
                float replaced[3][3];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) replaced[i][j] = j == column ? c[i] : a[i][j];
                eye[column] = determinant(replaced) / det;
            }

            return true;
        }

        struct VisibilityView {
            const Matrix4& viewProjection;
            float screenWidth;
            float screenHeight;
            bool hasEye;
            float eye[3];
        };

        // The pixels of `rect` that see through `wall` from `sector` into
        // `other`: the part where both sectors are open, projected to the
        // screen. Empty when the opening is closed or out of view.
        PixelRect PortalRect(
            const Wall& wall,
            const Sector& sector,
            const WallPieces::SectorBounds& sectorBounds,
            const Sector& other,
            const WallPieces::SectorBounds& otherBounds,
            const VisibilityView& view,
            const PixelRect& rect
        ) {
            const Matrix4& m = view.viewProjection;

            bool any = false;
            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

            for (const SectorFloor& here : sector.floors) {
                for (const SectorFloor& there : other.floors) {
                    const HeightLine floors[2] = {
                        SurfaceLine(here.floor, sectorBounds, wall, -OPEN_HEIGHT),
                        SurfaceLine(there.floor, otherBounds, wall, -OPEN_HEIGHT)
                    };
                    const HeightLine ceilings[2] = {
                        SurfaceLine(here.ceiling, sectorBounds, wall, OPEN_HEIGHT),
                        SurfaceLine(there.ceiling, otherBounds, wall, OPEN_HEIGHT)
                    };

                    const float low = std::min({floors[0].start, floors[0].end, floors[1].start, floors[1].end});
                    const float high = std::max({ceilings[0].start, ceilings[0].end, ceilings[1].start, ceilings[1].end});
                    if (high - low <= MIN_OPENING_HEIGHT) continue;

                    // Above both floors and below both ceilings. Every line is
                    // linear along the wall, so this is a convex polygon.
                    std::array<WallPoint, MAX_POLYGON_POINTS> opening = {};
                    opening[0] = {0.0f, low};
                    opening[1] = {1.0f, low};
                    opening[2] = {1.0f, high};
                    opening[3] = {0.0f, high};
                    int count = 4;

                    for (const HeightLine& line : floors)
                        count = ClipPolygon(opening, count, [&](const WallPoint& p) { return p.y - line.At(p.t); });
                    for (const HeightLine& line : ceilings)
                        count = ClipPolygon(opening, count, [&](const WallPoint& p) { return line.At(p.t) - p.y; });

                    if (count < 3) continue;

                    float area = 0.0f;
                    float openingLow = opening[0].y;
                    float openingHigh = opening[0].y;

                    for (int i = 0; i < count; ++i) {
                        const WallPoint& a = opening[i];
                        const WallPoint& b = opening[(i + 1) % count];
                        area += a.t * b.y - b.t * a.y;
                        openingLow = std::min(openingLow, a.y);
                        openingHigh = std::max(openingHigh, a.y);
                    }

                    if (std::abs(area) * 0.5f <= MIN_OPENING_HEIGHT) continue;

                    if (view.hasEye &&
                        SegmentDistanceSq({view.eye[0], view.eye[2]}, wall.start, wall.end) <= EYE_PORTAL_DISTANCE * EYE_PORTAL_DISTANCE &&
                        view.eye[1] >= openingLow - EYE_PORTAL_DISTANCE &&
                        view.eye[1] <= openingHigh + EYE_PORTAL_DISTANCE) return rect;

                    std::array<ClipPoint, MAX_POLYGON_POINTS> projected = {};
                    for (int i = 0; i < count; ++i) {
                        const float x = wall.start.x + (wall.end.x - wall.start.x) * opening[i].t;
                        const float y = opening[i].y;
                        const float z = wall.start.y + (wall.end.y - wall.start.y) * opening[i].t;

                        projected[i] = {
                            m.m[0][0] * x + m.m[0][1] * y + m.m[0][2] * z + m.m[0][3],
                            m.m[1][0] * x + m.m[1][1] * y + m.m[1][2] * z + m.m[1][3],
                            m.m[2][0] * x + m.m[2][1] * y + m.m[2][2] * z + m.m[2][3],
                            m.m[3][0] * x + m.m[3][1] * y + m.m[3][2] * z + m.m[3][3]
                        };
                    }

                    count = ClipPolygon(projected, count, [](const ClipPoint& p) { return p.w - MIN_CLIP_W; });

                    for (int i = 0; i < count; ++i) {
                        const float px = (projected[i].x / projected[i].w + 1.0f) * 0.5f * view.screenWidth;
                        const float py = (1.0f - projected[i].y / projected[i].w) * 0.5f * view.screenHeight;

                        if (!any) {
                            minX = maxX = px;
                            minY = maxY = py;
                            any = true;
                            continue;
                        }

                        minX = std::min(minX, px);
                        maxX = std::max(maxX, px);
                        minY = std::min(minY, py);
                        maxY = std::max(maxY, py);
                    }
                }
            }

            if (!any) return {};

            // Clamped before the int cast: points near the eye project far
            // off screen. One pixel of padding keeps the rounding conservative.
            const auto toPixel = [](const float value, const float limit) {
                return static_cast<int>(std::clamp(value, -2.0f, limit + 2.0f));
            };

            const PixelRect projectedRect = {
                toPixel(std::floor(minX), view.screenWidth) - 1,
                toPixel(std::floor(minY), view.screenHeight) - 1,
                toPixel(std::ceil(maxX), view.screenWidth) + 1,
                toPixel(std::ceil(maxY), view.screenHeight) + 1
            };

            return Intersect(projectedRect, rect);
        }

        // Kept between calls, so a per-frame call doesn't allocate.
        struct VisibilityScratch {
            std::vector<PixelRect> seen;
            std::vector<uint8_t> reached;
            std::vector<WallPieces::SectorBounds> bounds;
            std::vector<uint8_t> boundsReady;
            std::vector<uint8_t> queued;
            std::vector<int> queue;
        };

        thread_local VisibilityScratch visibilityScratch;
    }

    void GetVisibleSectors(
        const Level& level,
        const int cameraSectorIndex,
        const Matrix4& viewProjection,
        const float screenWidth,
        const float screenHeight,
        std::vector<const Sector*>& out
    ) {
        out.clear();

        const int sectorCount = static_cast<int>(level.sectors.size());

        if (cameraSectorIndex < 0 || cameraSectorIndex >= sectorCount || !(screenWidth >= 1.0f) || !(screenHeight >= 1.0f)) {
            for (const Sector& sector : level.sectors) out.push_back(&sector);
            return;
        }

        VisibilityScratch& scratch = visibilityScratch;
        scratch.seen.assign(sectorCount, {});
        scratch.reached.assign(sectorCount, 0);
        scratch.bounds.resize(sectorCount);
        scratch.boundsReady.assign(sectorCount, 0);
        scratch.queued.assign(sectorCount, 0);
        scratch.queue.clear();

        const auto boundsOf = [&](const int index) -> const WallPieces::SectorBounds& {
            if (!scratch.boundsReady[index]) {
                scratch.bounds[index] = WallPieces::ComputeSectorBounds(level.sectors[index]);
                scratch.boundsReady[index] = 1;
            }
            return scratch.bounds[index];
        };

        VisibilityView view{viewProjection, screenWidth, screenHeight, false, {}};
        view.hasEye = FindEye(viewProjection, view.eye);

        const PixelRect screen = {
            0,
            0,
            static_cast<int>(std::ceil(screenWidth)),
            static_cast<int>(std::ceil(screenHeight))
        };

        scratch.reached[cameraSectorIndex] = 1;
        scratch.seen[cameraSectorIndex] = screen;
        scratch.queued[cameraSectorIndex] = 1;
        scratch.queue.push_back(cameraSectorIndex);

        // Breadth first, and a sector is queued at most once at a time: a
        // view reaching it while it waits just grows its rectangle, so it is
        // flooded once with everything that sees into it. One reached again
        // after its flood goes back in the queue only if its rectangle grew -
        // then with the whole grown rectangle, so whatever the new view shows
        // is covered. Rectangles only grow in whole pixels, so this ends.
        for (size_t next = 0; next < scratch.queue.size(); ++next) {
            const int index = scratch.queue[next];
            scratch.queued[index] = 0;

            const PixelRect rect = scratch.seen[index];
            const Sector& sector = level.sectors[index];

            for (const Wall* wall : sector.walls) {
                if (wall == nullptr || !wall->IsPortal()) continue;

                const ID otherID = wall->frontSector == sector.id ? wall->backSector : wall->frontSector;
                const Sector* other = GetSectorByID(level, otherID);
                if (other == nullptr || other == &sector) continue;

                const int otherIndex = static_cast<int>(other - level.sectors.data());

                const PixelRect through = PortalRect(*wall, sector, boundsOf(index), *other, boundsOf(otherIndex), view, rect);
                if (through.Empty()) continue;

                if (scratch.reached[otherIndex]) {
                    const PixelRect grown = Union(scratch.seen[otherIndex], through);
                    if (grown == scratch.seen[otherIndex]) continue;
                    scratch.seen[otherIndex] = grown;
                }
                else {
                    scratch.reached[otherIndex] = 1;
                    scratch.seen[otherIndex] = through;
                }

                if (!scratch.queued[otherIndex]) {
                    scratch.queued[otherIndex] = 1;
                    scratch.queue.push_back(otherIndex);
                }
            }
        }

        for (int index = 0; index < sectorCount; ++index)
            if (scratch.reached[index]) out.push_back(&level.sectors[index]);
    }
}