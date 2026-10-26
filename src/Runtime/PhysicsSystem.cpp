//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

#include <tracy/Tracy.hpp>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Math/SIMD/SSECompat.hpp"
#include "Headers/Objects/Level.hpp"

static inline __m128 rcp_nr_ss(const __m128 x) {
    __m128 r = _mm_rcp_ss(x);
    r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(2.0f), _mm_mul_ss(x, r)));
    return r;
}

static inline __m128 rsqrt_nr_ss(const __m128 x) {
    __m128 r = _mm_rsqrt_ss(x);
    const __m128 half = _mm_set_ss(0.5f);
    const __m128 threeHalves = _mm_set_ss(1.5f);
    r = _mm_mul_ss(r, _mm_sub_ss(threeHalves, _mm_mul_ss(half, _mm_mul_ss(x, _mm_mul_ss(r, r)))));
    return r;
}

static inline __m128 dot3_ss(const __m128 a, const __m128 b) {
    const __m128 mul = _mm_mul_ps(a, b);
    const __m128 y = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(1, 1, 1, 1));
    const __m128 z = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2, 2, 2, 2));
    return _mm_add_ss(_mm_add_ss(mul, y), z);
}

static inline __m128 dot_xz_ss(const __m128 a, const __m128 b) {
    const __m128 mul = _mm_mul_ps(a, b);
    const __m128 z = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2, 2, 2, 2));
    return _mm_add_ss(mul, z);
}

static inline __m128 blend_ps(const __m128 a, const __m128 b, const __m128 mask) {
    return _mm_or_ps(_mm_and_ps(mask, b), _mm_andnot_ps(mask, a));
}

namespace {
    constexpr float MIN_WALL_HEIGHT = 0.0001f;
    constexpr float PENETRATION_SLOP = 0.001f;
    constexpr float GROUND_CONTACT_SLOP = 0.025f;
    constexpr float MAX_GROUND_SEPARATING_SPEED = 0.05f;

    // Ground snapping treats a maxSlope above this as this steep, so a body
    // allowed to stand on near-vertical floors doesn't get yanked down a
    // huge distance.
    constexpr float MAX_SNAP_SLOPE = 60.0f;

    const __m128 XZ_MASK = _mm_castsi128_ps(_mm_setr_epi32(-1, 0, -1, 0));

    // Slope strength is the slope's angle in degrees. BuildGpuSectors()
    // uploads the same rise-per-unit gradient the shader draws with.
    float SlopeGradient(const float slopeStrength) {
        return SectorSurface::GradientFromDegrees(slopeStrength);
    }

    // Axis aligned bounds of the sector in map XY. Slope offsets are measured
    // from an edge of this rectangle, so it has to match Sector.bounds in
    // Rendering_vs.glsl - that one is built from the sector's triangles.
    struct SectorBounds {
        Vector2 minimum = {0.0f, 0.0f};
        Vector2 maximum = {0.0f, 0.0f};
        bool valid = false;
    };

    SectorBounds ComputeSectorBounds(const Sector& sector) {
        SectorBounds bounds;

        __m128 minimumReg;
        __m128 maximumReg;

        if (!sector.triangles.empty()) {
            minimumReg = sector.triangles.front().a.reg;
            maximumReg = minimumReg;

            for (const Triangle& triangle : sector.triangles) {
                minimumReg = _mm_min_ps(minimumReg, triangle.a.reg);
                minimumReg = _mm_min_ps(minimumReg, triangle.b.reg);
                minimumReg = _mm_min_ps(minimumReg, triangle.c.reg);

                maximumReg = _mm_max_ps(maximumReg, triangle.a.reg);
                maximumReg = _mm_max_ps(maximumReg, triangle.b.reg);
                maximumReg = _mm_max_ps(maximumReg, triangle.c.reg);
            }
        }
        else if (!sector.vertices.empty()) {
            minimumReg = sector.vertices.front().reg;
            maximumReg = minimumReg;

            for (const Vector2& vertex : sector.vertices) {
                minimumReg = _mm_min_ps(minimumReg, vertex.reg);
                maximumReg = _mm_max_ps(maximumReg, vertex.reg);
            }
        }
        else return bounds;

        bounds.minimum.reg = minimumReg;
        bounds.maximum.reg = maximumReg;
        bounds.valid = true;

        return bounds;
    }

    // Every wall is visited from both of its sectors, so bounds get asked for
    // far more often than they change.
    class SectorBoundsCache {
    public:
        const SectorBounds& Get(const Sector* sector) {
            if (sector == nullptr) return emptyBounds;

            const auto existing = cache.find(sector);

            if (existing != cache.end()) return existing->second;

            return cache.emplace(sector, ComputeSectorBounds(*sector)).first->second;
        }

    private:
        std::unordered_map<const Sector*, SectorBounds> cache;
        SectorBounds emptyBounds;
    };

    // Mirrors GetSlopeOffset() in Rendering_vs.glsl.
    float GetSlopeOffset(
        const Vector2& point,
        const SectorBounds& bounds,
        const SlopeDirection slopeDirection,
        const float slopeStrength
    ) {
        if (!bounds.valid || slopeStrength == 0.0f) return 0.0f;

        const float gradient = SlopeGradient(slopeStrength);

        switch (slopeDirection) {
            case PLUS_X:  return (point.x - bounds.minimum.x) * gradient;
            case MINUS_X: return (bounds.maximum.x - point.x) * gradient;
            case PLUS_Z:  return (point.y - bounds.minimum.y) * gradient;
            case MINUS_Z: return (bounds.maximum.y - point.y) * gradient;
        }

        return 0.0f;
    }

    float GetSurfaceHeight(
        const SectorSurface& surface,
        const SectorBounds& bounds,
        const Vector2& point
    ) {
        return surface.height +
               GetSlopeOffset(point, bounds, surface.slopeDirection, surface.slopeStrength);
    }

    struct SectorSample {
        const Sector* sector = nullptr;
        SectorBounds bounds;
    };

    // Start and end give the geometry, middle decides the topology.
    struct WallSamplePoints {
        Vector2 start;
        Vector2 middle;
        Vector2 end;
    };

    // One floor or ceiling plane, sampled at those three points.
    struct HeightSample {
        float start = 0.0f;
        float middle = 0.0f;
        float end = 0.0f;
    };

    // Bottom and top at each end of the span, so it can follow a sloped
    // floor or ceiling instead of being a flat box. A span covers the part of
    // the wall from startT to endT (fractions from its start to its end).
    struct WallSpan {
        float bottomStart = 0.0f;
        float bottomEnd = 0.0f;
        float topStart = 0.0f;
        float topEnd = 0.0f;
        float startT = 0.0f;
        float endT = 1.0f;
    };

    bool IsSectorOpenAtHeight(
        const SectorSample& sample,
        const Vector2& point,
        const float height
    ) {
        if (sample.sector == nullptr) return false;

        for (const SectorFloor& floor : sample.sector->floors) {
            if (height > GetSurfaceHeight(floor.floor, sample.bounds, point) &&
                height < GetSurfaceHeight(floor.ceiling, sample.bounds, point)) return true;
        }

        return false;
    }

    void AddSectorHeights(
        const SectorSample& sample,
        const WallSamplePoints& points,
        std::vector<HeightSample>& heights
    ) {
        if (sample.sector == nullptr) return;

        for (const SectorFloor& floor : sample.sector->floors) {
            for (const SectorSurface* surface : {&floor.floor, &floor.ceiling}) {
                heights.push_back({
                    GetSurfaceHeight(*surface, sample.bounds, points.start),
                    GetSurfaceHeight(*surface, sample.bounds, points.middle),
                    GetSurfaceHeight(*surface, sample.bounds, points.end)
                });
            }
        }
    }

    struct SpanProbe {
        Vector2 point;
        float bottom = 0.0f;
        float top = 0.0f;
    };

    // A sloped slab can pinch shut at one end and still be open at the other,
    // so test openness where it is thickest rather than always at the middle.
    SpanProbe PickThickestProbe(
        const HeightSample& bottom,
        const HeightSample& top,
        const WallSamplePoints& points
    ) {
        const SpanProbe probes[3] = {
            {points.start, bottom.start, top.start},
            {points.middle, bottom.middle, top.middle},
            {points.end, bottom.end, top.end}
        };

        SpanProbe best = probes[0];

        for (int i = 1; i < 3; ++i)
            if (probes[i].top - probes[i].bottom > best.top - best.bottom) best = probes[i];

        return best;
    }

    Vector2 PointAlong(const Vector2& start, const Vector2& end, const float t) {
        return {start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t};
    }

    // Where two of the planes cross between the wall's ends, as fractions
    // along it. Planes are straight lines along a straight wall, so between
    // two neighbouring crossings their order never changes. Sorting by the
    // middle alone misordered the part past a crossing (a sloped floor rising
    // through the neighbour's floor), and that part had no collision.
    void FindPlaneCrossings(const std::vector<HeightSample>& heights, std::vector<float>& crossings) {
        constexpr float MIN_T = 0.0001f;

        for (size_t i = 0; i < heights.size(); ++i) {
            for (size_t j = i + 1; j < heights.size(); ++j) {
                const float startDifference = heights[i].start - heights[j].start;
                const float endDifference = heights[i].end - heights[j].end;

                if (std::abs(startDifference) <= MIN_WALL_HEIGHT || std::abs(endDifference) <= MIN_WALL_HEIGHT) continue;
                if ((startDifference < 0.0f) == (endDifference < 0.0f)) continue;

                const float t = startDifference / (startDifference - endDifference);

                if (t > MIN_T && t < 1.0f - MIN_T) crossings.push_back(t);
            }
        }

        std::ranges::sort(crossings);
        crossings.erase(
            std::unique(crossings.begin(), crossings.end(), [](const float a, const float b) { return b - a <= MIN_T; }),
            crossings.end()
        );
    }

    // The wall is cut at every plane crossing and each part is split on its
    // own. With `solidOpenings` (a middle texture with collision), the parts
    // open on both sides are solid too.
    std::vector<WallSpan> BuildWallSpans(
        const SectorSample& frontSector,
        const SectorSample& backSector,
        const Vector2& wallStart,
        const Vector2& wallEnd,
        const bool solidOpenings
    ) {
        std::vector<HeightSample> heights;
        std::vector<float> cuts;

        const WallSamplePoints wallPoints{wallStart, PointAlong(wallStart, wallEnd, 0.5f), wallEnd};

        AddSectorHeights(frontSector, wallPoints, heights);
        AddSectorHeights(backSector, wallPoints, heights);

        cuts.push_back(0.0f);
        FindPlaneCrossings(heights, cuts);
        cuts.push_back(1.0f);

        std::vector<WallSpan> spans;

        for (size_t cut = 0; cut + 1 < cuts.size(); ++cut) {
            const float startT = cuts[cut];
            const float endT = cuts[cut + 1];

            const WallSamplePoints points{
                PointAlong(wallStart, wallEnd, startT),
                PointAlong(wallStart, wallEnd, (startT + endT) * 0.5f),
                PointAlong(wallStart, wallEnd, endT)
            };

            heights.clear();

            AddSectorHeights(frontSector, points, heights);
            AddSectorHeights(backSector, points, heights);

            std::ranges::sort(
                heights,
                [](const HeightSample& a, const HeightSample& b) { return a.middle < b.middle; }
            );

            // Two planes are only the same plane if they agree along the whole
            // part - equal in the middle but diverging at the ends is a real gap.
            heights.erase(
                std::unique(
                    heights.begin(),
                    heights.end(),
                    [](const HeightSample& a, const HeightSample& b) {
                        return std::abs(a.start - b.start) <= MIN_WALL_HEIGHT &&
                               std::abs(a.middle - b.middle) <= MIN_WALL_HEIGHT &&
                               std::abs(a.end - b.end) <= MIN_WALL_HEIGHT;
                    }
                ),
                heights.end()
            );

            for (size_t i = 0; i + 1 < heights.size(); ++i) {
                const HeightSample& bottom = heights[i];
                const HeightSample& top = heights[i + 1];

                const SpanProbe probe = PickThickestProbe(bottom, top, points);

                if (probe.top - probe.bottom <= MIN_WALL_HEIGHT) continue;

                const float sampleHeight = (probe.bottom + probe.top) * 0.5f;

                const bool frontOpen = IsSectorOpenAtHeight(frontSector, probe.point, sampleHeight);
                const bool backOpen = IsSectorOpenAtHeight(backSector, probe.point, sampleHeight);

                if (frontOpen == backOpen && !(frontOpen && solidOpenings)) continue;

                if (!spans.empty() &&
                    spans.back().startT == startT &&
                    std::abs(spans.back().topStart - bottom.start) <= MIN_WALL_HEIGHT &&
                    std::abs(spans.back().topEnd - bottom.end) <= MIN_WALL_HEIGHT) {
                    spans.back().topStart = top.start;
                    spans.back().topEnd = top.end;
                }
                else spans.push_back({bottom.start, bottom.end, top.start, top.end, startT, endT});
            }
        }

        return spans;
    }

    // Wall spans only change when a wall moves or the heights around it do,
    // so they are kept between runs and rebuilt per wall when needed.
    // Everything is compared exactly, so a kept span is always the one a
    // rebuild would make.
    struct WallSpanCache {
        // Per sector index: every value BuildWallSpans reads from it - each
        // floor's floor/ceiling height and slope, plus the bounds when any
        // surface slopes (only slopes read them).
        std::vector<std::vector<float>> sectorKeys;
        std::vector<uint8_t> sectorChanged;

        // Per wall index: what its spans were built from, and the spans.
        struct WallKey {
            Vector2 start;
            Vector2 end;
            int frontIndex = -1;
            int backIndex = -1;
            bool solidOpenings = false;
        };
        std::vector<WallKey> wallKeys;
        std::vector<std::vector<WallSpan>> spans;
        std::vector<uint8_t> wallBuilt;

        std::vector<float> scratchKey;
    };

    WallSpanCache wallSpanCache;

    // Per rigidbody (same index as level.rigidbodies.components): grounded
    // when the run started, which is what the last run left it at.
    std::vector<uint8_t> groundedBeforeRun;

    void BuildSectorKey(const Sector& sector, std::vector<float>& key) {
        key.clear();

        bool slopes = false;

        for (const SectorFloor& floor : sector.floors) {
            for (const SectorSurface* surface : {&floor.floor, &floor.ceiling}) {
                key.push_back(surface->height);
                key.push_back(static_cast<float>(surface->slopeDirection));
                key.push_back(surface->slopeStrength);
                slopes = slopes || surface->slopeStrength != 0.0f;
            }
        }

        if (!slopes) return;

        const SectorBounds bounds = ComputeSectorBounds(sector);
        key.push_back(bounds.valid ? 1.0f : 0.0f);
        key.push_back(bounds.minimum.x);
        key.push_back(bounds.minimum.y);
        key.push_back(bounds.maximum.x);
        key.push_back(bounds.maximum.y);
    }

    // Brings wallSpanCache up to date with the level; returns how many walls
    // were rebuilt.
    size_t UpdateWallSpans(const Level& level, SectorBoundsCache& boundsCache) {
        WallSpanCache& cache = wallSpanCache;

        const size_t sectorCount = level.sectors.size();
        cache.sectorKeys.resize(sectorCount);
        cache.sectorChanged.assign(sectorCount, 0);

        for (size_t index = 0; index < sectorCount; ++index) {
            BuildSectorKey(level.sectors[index], cache.scratchKey);

            if (cache.scratchKey != cache.sectorKeys[index]) {
                cache.sectorKeys[index].assign(cache.scratchKey.begin(), cache.scratchKey.end());
                cache.sectorChanged[index] = 1;
            }
        }

        const size_t wallCount = level.walls.size();
        cache.wallKeys.resize(wallCount);
        cache.spans.resize(wallCount);
        cache.wallBuilt.resize(wallCount, 0);

        const auto indexOf = [&](const Sector* sector) {
            return sector == nullptr ? -1 : static_cast<int>(sector - level.sectors.data());
        };

        size_t rebuilt = 0;

        for (size_t wallIndex = 0; wallIndex < wallCount; ++wallIndex) {
            const Wall& wall = level.walls[wallIndex];

            const Sector* frontSectorPtr = MapQueries::GetSectorByID(level, wall.frontSector);
            const Sector* backSectorPtr = MapQueries::GetSectorByID(level, wall.backSector);

            if (frontSectorPtr == backSectorPtr) backSectorPtr = nullptr;

            const WallSpanCache::WallKey key{
                wall.start,
                wall.end,
                indexOf(frontSectorPtr),
                indexOf(backSectorPtr),
                wall.HasMiddle() && wall.middleHasCollision
            };
            WallSpanCache::WallKey& cached = cache.wallKeys[wallIndex];

            const bool sectorsChanged =
                (key.frontIndex >= 0 && cache.sectorChanged[key.frontIndex]) ||
                (key.backIndex >= 0 && cache.sectorChanged[key.backIndex]);

            if (cache.wallBuilt[wallIndex] &&
                !sectorsChanged &&
                cached.start == key.start &&
                cached.end == key.end &&
                cached.frontIndex == key.frontIndex &&
                cached.backIndex == key.backIndex &&
                cached.solidOpenings == key.solidOpenings) continue;

            const SectorSample frontSector{frontSectorPtr, boundsCache.Get(frontSectorPtr)};
            const SectorSample backSector{backSectorPtr, boundsCache.Get(backSectorPtr)};

            std::vector<WallSpan>& spans = cache.spans[wallIndex];
            spans = BuildWallSpans(frontSector, backSector, wall.start, wall.end, key.solidOpenings);

            if (spans.empty() && frontSectorPtr == nullptr && backSectorPtr == nullptr)
                spans.push_back({0.0f, 0.0f, 32.0f, 32.0f});

            cached = key;
            cache.wallBuilt[wallIndex] = 1;
            ++rebuilt;
        }

        return rebuilt;
    }

    // How far straight up from a sloped plane a round body's bottom sits
    // while touching it - zero on a flat one. Its rounded end meets the slope
    // off centre, a little above the plane's height under the centre.
    float RoundSlopeLift(const SectorSurface& surface, const float radius) {
        const float gradient = SlopeGradient(surface.slopeStrength);
        return radius * (std::sqrt(1.0f + gradient * gradient) - 1.0f);
    }

    // The lowest a round body's bottom can go over this floor.
    float RestingFloorHeight(
        const SectorSurface& floor,
        const SectorBounds& bounds,
        const Vector2& point,
        const float radius
    ) {
        return GetSurfaceHeight(floor, bounds, point) + RoundSlopeLift(floor, radius);
    }

    // The highest a round body's top can go under this ceiling.
    float RestingCeilingHeight(
        const SectorSurface& ceiling,
        const SectorBounds& bounds,
        const Vector2& point,
        const float radius
    ) {
        return GetSurfaceHeight(ceiling, bounds, point) - RoundSlopeLift(ceiling, radius);
    }

    int FindBestSectorFloor(
        const Sector& sector,
        const SectorBounds& bounds,
        const Vector2& point,
        const float bottomHeight,
        const float topHeight,
        const float radius
    ) {
        const float bodyHeight = topHeight - bottomHeight;
        const float centreHeight = (bottomHeight + topHeight) * 0.5f;

        int bestFloorIndex = -1;
        float bestOverlap = -1.0f;
        float bestCorrection = std::numeric_limits<float>::max();
        bool bestCentreInside = false;

        // Slabs the body fits in come first. When none does (a low ceiling,
        // or a sloped floor rising toward one) the body still needs a floor:
        // with none picked nothing held it up and it fell out of the map.
        for (const bool requireFit : {true, false}) {
            for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
                const SectorFloor& floor = sector.floors[floorIndex];

                const float floorHeight = RestingFloorHeight(floor.floor, bounds, point, radius);
                const float ceilingHeight = RestingCeilingHeight(floor.ceiling, bounds, point, radius);

                if (requireFit && ceilingHeight - floorHeight + Constants::Epsilon < bodyHeight) continue;

                const float overlap =
                    std::max(0.0f, std::min(topHeight, ceilingHeight) - std::max(bottomHeight, floorHeight));

                float correction = 0.0f;

                if (bottomHeight < floorHeight) correction = floorHeight - bottomHeight;
                else if (topHeight > ceilingHeight) correction = ceilingHeight - topHeight;

                const float correctionMagnitude = std::abs(correction);
                const bool centreInside = centreHeight >= floorHeight && centreHeight <= ceilingHeight;

                if ((centreInside && !bestCentreInside) ||
                    (centreInside == bestCentreInside && overlap > bestOverlap + Constants::Epsilon) ||
                    (centreInside == bestCentreInside &&
                     std::abs(overlap - bestOverlap) <= Constants::Epsilon &&
                     correctionMagnitude < bestCorrection)) {
                    bestFloorIndex = floorIndex;
                    bestOverlap = overlap;
                    bestCorrection = correctionMagnitude;
                    bestCentreInside = centreInside;
                }
            }

            if (bestFloorIndex >= 0) break;
        }

        return bestFloorIndex;
    }

    // An upright capsule: every point within `radius` of the vertical segment
    // between its two cap centres. A sphere is one whose cap centres meet.
    struct RoundBody {
        Vector3 base; // the collider's bottom
        float radius = 0.0f;
        float height = 0.0f;

        [[nodiscard]] float Bottom() const { return base.y; }
        [[nodiscard]] float Top() const { return base.y + height; }
        [[nodiscard]] float LowCentre() const { return base.y + radius; }
        [[nodiscard]] float HighCentre() const { return base.y + height - radius; }
    };

    RoundBody MakeRoundBody(const ComponentTransform& transform, const ComponentCollider& collider) {
        return {collider.Base(transform), collider.Radius(transform), collider.Height(transform)};
    }

    // Where two vertical ranges come closest: one height on each. Inside an
    // overlap both are the overlap's middle.
    void ClosestHeights(
        const float aLow,
        const float aHigh,
        const float bLow,
        const float bHigh,
        float& a,
        float& b
    ) {
        if (aHigh < bLow) {
            a = aHigh;
            b = bLow;
        }
        else if (bHigh < aLow) {
            a = aLow;
            b = bHigh;
        }
        else a = b = (std::max(aLow, bLow) + std::min(aHigh, bHigh)) * 0.5f;
    }

    // From the closest point of `b` to the closest point of `a`. They overlap
    // when its length is under the two radii summed.
    __m128 RoundBodyDelta(const RoundBody& a, const RoundBody& b) {
        float aHeight, bHeight;
        ClosestHeights(a.LowCentre(), a.HighCentre(), b.LowCentre(), b.HighCentre(), aHeight, bHeight);

        return _mm_set_ps(0.0f, a.base.z - b.base.z, aHeight - bHeight, a.base.x - b.base.x);
    }

    bool RoundBodyIntersectsWallSpan(
        const Wall &wall,
        const WallSpan &span,
        const float stepSize,
        RoundBody &body,
        ComponentTransform &transform,
        ComponentRigidbody &rigidbody
    ) {
        const float radius = body.radius;

        // Conservative vertical extent: the span is a trapezoid now, so the
        // broadphase box has to cover the taller of the two ends.
        const float spanMinHeight = std::min(span.bottomStart, span.bottomEnd);
        const float spanMaxHeight = std::max(span.topStart, span.topEnd);

        // Only the part of the wall the span covers.
        const Vector2 segmentStart = PointAlong(wall.start, wall.end, span.startT);
        const Vector2 segmentEnd = PointAlong(wall.start, wall.end, span.endT);
        const Vector2 segment = {segmentEnd.x - segmentStart.x, segmentEnd.y - segmentStart.y};

        const float minX = std::min(segmentStart.x, segmentEnd.x) - radius;
        const float maxX = std::max(segmentStart.x, segmentEnd.x) + radius;
        const float minZ = std::min(segmentStart.y, segmentEnd.y) - radius;
        const float maxZ = std::max(segmentStart.y, segmentEnd.y) + radius;

        const __m128 bodyXZ = _mm_set_ps(0.0f, body.base.z, 0.0f, body.base.x);
        const __m128 aabbMin = _mm_set_ps(0.0f, minZ, 0.0f, minX);
        const __m128 aabbMax = _mm_set_ps(0.0f, maxZ, 0.0f, maxX);
        const __m128 belowMin = _mm_cmplt_ps(bodyXZ, aabbMin);
        const __m128 aboveMax = _mm_cmpgt_ps(bodyXZ, aabbMax);

        if ((_mm_movemask_ps(_mm_or_ps(belowMin, aboveMax)) & 0x5) != 0) return false;
        if (body.Top() < spanMinHeight || body.Bottom() > spanMaxHeight) return false;

        const float segmentLengthSq = segment.x * segment.x + segment.y * segment.y;
        if (segmentLengthSq <= Constants::Epsilon) return false;

        const float toBodyX = body.base.x - segmentStart.x;
        const float toBodyZ = body.base.z - segmentStart.y;
        const float t = std::clamp(
            (toBodyX * segment.x + toBodyZ * segment.y) *
                _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(segmentLengthSq))),
            0.0f,
            1.0f
        );

        // The span's real extent at the closest point. Crossing slopes could
        // invert it, so keep the top pinned at or above the bottom.
        const float spanBottom = span.bottomStart + (span.bottomEnd - span.bottomStart) * t;
        const float spanTop =
            std::max(span.topStart + (span.topEnd - span.topStart) * t, spanBottom);

        float bodyY, wallY;
        ClosestHeights(body.LowCentre(), body.HighCentre(), spanBottom, spanTop, bodyY, wallY);

        const float closestX = segmentStart.x + segment.x * t;
        const float closestZ = segmentStart.y + segment.y * t;

        const __m128 delta = _mm_set_ps(0.0f, body.base.z - closestZ, bodyY - wallY, body.base.x - closestX);
        const __m128 distanceSqReg = dot3_ss(delta, delta);
        const float distanceSq = _mm_cvtss_f32(distanceSqReg);
        const float radiusSq = radius * radius;

        if (distanceSq >= radiusSq) return false;

        const __m128 safeDistanceSq = _mm_max_ss(distanceSqReg, _mm_set_ss(Constants::Epsilon));
        __m128 inverseDistance = rsqrt_nr_ss(safeDistanceSq);
        inverseDistance = TILKY_MM_SHUFFLE_PS(inverseDistance, inverseDistance, _MM_SHUFFLE(0, 0, 0, 0));

        const float distance = distanceSq * _mm_cvtss_f32(inverseDistance);
        const float penetration = radius - distance;

        if (penetration <= Constants::Epsilon) return false;

        const __m128 calculatedNormal = _mm_and_ps(_mm_mul_ps(delta, inverseDistance), XZ_MASK);

        const float planeDistance =
            (body.base.x - wall.start.x) * wall.normal.x + (body.base.z - wall.start.y) * wall.normal.y;

        const float normalDirection = planeDistance < 0.0f ? -1.0f : 1.0f;

        const __m128 fallbackNormal = _mm_set_ps(
            0.0f,
            wall.normal.y * normalDirection,
            0.0f,
            wall.normal.x * normalDirection
        );

        const __m128 horizontalLengthSqReg = dot_xz_ss(calculatedNormal, calculatedNormal);

        const __m128 hasCalculatedNormal = _mm_cmpgt_ss(horizontalLengthSqReg, _mm_set_ss(Constants::Epsilon));

        const __m128 hasCalculatedNormalBroad =
                TILKY_MM_SHUFFLE_PS(
                    hasCalculatedNormal,
                    hasCalculatedNormal,
                    _MM_SHUFFLE(0, 0, 0, 0)
                );

        __m128 collisionNormal = blend_ps(fallbackNormal, calculatedNormal, hasCalculatedNormalBroad);

        const __m128 normalLengthSqReg = dot_xz_ss(collisionNormal, collisionNormal);

        if (_mm_cvtss_f32(normalLengthSqReg) <= Constants::Epsilon)
            return false;

        __m128 inverseNormalLength = rsqrt_nr_ss(normalLengthSqReg);
        inverseNormalLength = TILKY_MM_SHUFFLE_PS(
            inverseNormalLength,
            inverseNormalLength,
            _MM_SHUFFLE(0, 0, 0, 0)
        );

        collisionNormal = _mm_mul_ps(collisionNormal, inverseNormalLength);

        const float velocityIntoWall = _mm_cvtss_f32(dot3_ss(rigidbody.velocity.reg, collisionNormal));

        const float bodyBottom = body.Bottom();
        const float stepHeight = spanTop - bodyBottom;

        const bool wallStartsAtBottom = spanBottom <= bodyBottom + GROUND_CONTACT_SLOP;

        const bool isClimbableStep =
                stepSize > 0.0f &&
                wallStartsAtBottom &&
                stepHeight > Constants::Epsilon &&
                stepHeight <= stepSize + Constants::Epsilon;

        // A step the body could climb never pushes it sideways: walking into
        // it steps up, anything else leaves it alone. Right after stepping
        // down (LevelSystem drops the body once its centre crosses the
        // edge), the body still overhangs the step it left; pushing it out
        // of that riser shoved the body - and the camera - forward by up to
        // a full radius in one frame.
        if (isClimbableStep && velocityIntoWall >= -Constants::Epsilon) return false;

        if (isClimbableStep) {
            const float verticalCorrection = stepHeight + PENETRATION_SLOP;

            transform.AddPosition({0.0f, verticalCorrection, 0.0f});

            body.base.y += verticalCorrection;

            if (rigidbody.velocity.y < 0.0f) rigidbody.velocity.y = 0.0f;

            rigidbody.isGrounded = true;
            rigidbody.groundNormal = {0.0f, 1.0f, 0.0f};

            return true;
        }

        const float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);

        const __m128 correction = _mm_mul_ps(collisionNormal, _mm_set1_ps(correctedPenetration));

        transform.AddPosition(Vector3(correction));
        body.base.reg = _mm_add_ps(body.base.reg, correction);

        if (velocityIntoWall < 0.0f)
            rigidbody.velocity.reg = _mm_sub_ps(rigidbody.velocity.reg, _mm_mul_ps(collisionNormal,_mm_set1_ps(velocityIntoWall))
            );

        return true;
    }
}

namespace PhysicsSystem {
    const ComponentCollider* FindBaseCollider(const Level& level, const ID ownerID) {
        const ComponentTransform* transform = level.transforms.Get(ownerID);
        if (transform == nullptr) return nullptr;

        const ComponentCollider* base = nullptr;
        float baseBottom = std::numeric_limits<float>::max();

        for (const ComponentInstanceID instanceID : level.colliders.InstancesOf(ownerID)) {
            const ComponentCollider* collider = level.colliders.GetInstance(instanceID);

            if (!collider->isActive || collider->isTrigger || !collider->IsRound()) continue;

            const float bottom = collider->Base(*transform).y;

            if (bottom < baseBottom) {
                base = collider;
                baseBottom = bottom;
            }
        }

        return base;
    }

    void Run(Level& level, Contacts& contacts) {
        ZoneScopedN("PhysicsSystem::Run");

        groundedBeforeRun.resize(level.rigidbodies.components.size());

        for (size_t index = 0; index < level.rigidbodies.components.size(); ++index) {
            ComponentRigidbody& rigidbody = level.rigidbodies.components[index];

            groundedBeforeRun[index] = rigidbody.isGrounded ? 1 : 0;

            rigidbody.isGrounded = false;
            rigidbody.groundNormal = {};
            rigidbody.slideNormal = {};
        }

        ColliderStorage& colliders = level.colliders;

        SectorBoundsCache boundsCache;

        {
            ZoneScopedN("Build wall collision spans");
            UpdateWallSpans(level, boundsCache);
        }

        const std::vector<std::vector<WallSpan>>& wallSpans = wallSpanCache.spans;

        std::vector<ID> allEntities;
        std::vector<const Wall*> allWalls;
        allEntities.reserve(64);
        allWalls.reserve(64);

        // Spheres and capsules. Each collider of an entity is pushed out on
        // its own and moves the whole entity (a compound shape).
        for (ComponentCollider& selfCollider : colliders.ActiveRound()) {
            ZoneScopedN("Round collider iteration");

            if (selfCollider.isTrigger) continue;

            ComponentTransform* selfTransform = level.transforms.Get(selfCollider.ownerID);
            if (selfTransform == nullptr) [[unlikely]] continue;

            ComponentRigidbody* selfRigidbody = level.rigidbodies.Get(selfCollider.ownerID);
            if (selfRigidbody == nullptr || selfRigidbody->isStatic) continue;

            const int sectorIndex = selfTransform->sectorIndex;
            if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) continue;

            Sector& sector = level.sectors[sectorIndex];

            {
                ZoneScopedN("Candidate gather");

                allEntities.clear();
                allWalls.clear();

                allEntities.insert(
                    allEntities.end(),
                    sector.entitiesInside.begin(),
                    sector.entitiesInside.end()
                );

                allWalls.insert(
                    allWalls.end(),
                    sector.walls.begin(),
                    sector.walls.end()
                );

                for (const Sector* neighbour : sector.neighbors) {
                    if (neighbour == nullptr) [[unlikely]] continue;

                    allEntities.insert(
                        allEntities.end(),
                        neighbour->entitiesInside.begin(),
                        neighbour->entitiesInside.end()
                    );

                    allWalls.insert(
                        allWalls.end(),
                        neighbour->walls.begin(),
                        neighbour->walls.end()
                    );
                }

                std::ranges::sort(allEntities);
                allEntities.erase(std::unique(allEntities.begin(), allEntities.end()), allEntities.end());

                std::ranges::sort(allWalls, std::less<const Wall*>{});
                allWalls.erase(std::unique(allWalls.begin(), allWalls.end()), allWalls.end());
            }

            RoundBody selfBody = MakeRoundBody(*selfTransform, selfCollider);

            {
                ZoneScopedN("Entity narrowphase");

                for (const ID otherID : allEntities) {
                    if (otherID == selfCollider.ownerID) continue;

                    ComponentTransform* otherTransform = level.transforms.Get(otherID);
                    if (otherTransform == nullptr) [[unlikely]] continue;

                    const ComponentRigidbody* otherRigidbody = level.rigidbodies.Get(otherID);
                    const bool otherIsStatic = otherRigidbody == nullptr || otherRigidbody->isStatic;

                    if (!otherIsStatic && selfCollider.ownerID > otherID) continue;

                    for (const ComponentInstanceID otherInstance : level.colliders.InstancesOf(otherID)) {
                        const ComponentCollider* otherCollider = level.colliders.GetInstance(otherInstance);

                        if (!otherCollider->isActive ||
                            otherCollider->isTrigger ||
                            !otherCollider->IsRound()) continue;

                        const RoundBody otherBody = MakeRoundBody(*otherTransform, *otherCollider);
                        const float radiusSum = selfBody.radius + otherBody.radius;

                        const __m128 delta = RoundBodyDelta(selfBody, otherBody);
                        const __m128 distanceSqReg = dot3_ss(delta, delta);
                        const float distanceSq = _mm_cvtss_f32(distanceSqReg);

                        if (distanceSq >= radiusSum * radiusSum) continue;

                        const __m128 safeDistanceSq = _mm_max_ss(distanceSqReg, _mm_set_ss(Constants::Epsilon));

                        __m128 inverseDistance = rsqrt_nr_ss(safeDistanceSq);
                        inverseDistance = TILKY_MM_SHUFFLE_PS(inverseDistance, inverseDistance, _MM_SHUFFLE(0, 0, 0, 0));

                        const float distance = distanceSq * _mm_cvtss_f32(inverseDistance);
                        const float penetration = radiusSum - distance;

                        if (penetration <= Constants::Epsilon) continue;

                        contacts.collisions.push_back({
                            std::min(selfCollider.ownerID, otherID),
                            std::max(selfCollider.ownerID, otherID)
                        });

                        const __m128 calculatedDirection = _mm_mul_ps(delta, inverseDistance);
                        const __m128 fallbackDirection = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
                        const __m128 hasDirection = _mm_cmpgt_ss(distanceSqReg, _mm_set_ss(Constants::Epsilon));
                        const __m128 hasDirectionBroad = TILKY_MM_SHUFFLE_PS(hasDirection, hasDirection, _MM_SHUFFLE(0, 0, 0, 0));
                        const __m128 pushDirection = blend_ps(fallbackDirection, calculatedDirection, hasDirectionBroad);

                        const float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);
                        const __m128 correction = _mm_mul_ps(pushDirection, _mm_set1_ps(correctedPenetration));

                        const float selfPush = otherIsStatic ? 1.0f : 0.5f;

                        selfTransform->AddPosition(Vector3(_mm_mul_ps(correction, _mm_set1_ps(selfPush))));

                        if (!otherIsStatic)
                            otherTransform->AddPosition(Vector3(_mm_mul_ps(correction, _mm_set1_ps(-0.5f))));


                        selfBody = MakeRoundBody(*selfTransform, selfCollider);
                    }
                }
            }

            const auto resolveWalls = [&] {
                for (const Wall* wall : allWalls) {
                    if (wall == nullptr || level.walls.empty()) continue;

                    const std::ptrdiff_t wallIndex = wall - level.walls.data();

                    if (wallIndex < 0 || wallIndex >= static_cast<std::ptrdiff_t>(wallSpans.size())) continue;

                    for (const WallSpan &span: wallSpans[wallIndex])
                        if (RoundBodyIntersectsWallSpan(
                                *wall,
                                span,
                                selfCollider.stepSize,
                                selfBody,
                                *selfTransform,
                                *selfRigidbody
                            ))
                            contacts.walls.push_back({selfCollider.ownerID, wall->id});
                }
            };

            {
                ZoneScopedN("Wall narrowphase");
                resolveWalls();
            }

            {
                ZoneScopedN("Sector room clamp");

                // relativeHeight belongs to the entity's base collider only.
                const bool isBase = PhysicsSystem::FindBaseCollider(level, selfCollider.ownerID) == &selfCollider;

                const Vector2 basePoint = {
                    selfBody.base.x,
                    selfBody.base.z
                };

                // sectorIndex is only updated after physics, so on the frame
                // the body crosses into a neighbour it is stale. Clamp to the
                // sector the collider is really over: skipping the clamp there
                // left the body with no floor for that frame.
                const int baseSectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, basePoint, sectorIndex);
                if (baseSectorIndex < 0) continue;

                const Sector& baseSector = level.sectors[baseSectorIndex];

                const SectorBounds& sectorBounds = boundsCache.Get(&baseSector);

                const auto getSlopeAxis = [](const SlopeDirection direction) -> Vector2 {
                    switch (direction) {
                        case PLUS_X: return {1.0f, 0.0f};
                        case MINUS_X: return {-1.0f, 0.0f};
                        case PLUS_Z: return {0.0f, 1.0f};
                        case MINUS_Z: return {0.0f, -1.0f};
                    }
                    return {1.0f, 0.0f};
                };

                const auto getFloorNormal = [&](const SectorSurface &surface) -> Vector3 {
                    const Vector2 slopeAxis = getSlopeAxis(surface.slopeDirection);
                    const float gradient = SlopeGradient(surface.slopeStrength);

                    return Vector3Math::Normalized({
                        -slopeAxis.x * gradient,
                        1.0f,
                        -slopeAxis.y * gradient
                    });
                };

                const auto getCeilingNormal = [&](const SectorSurface &surface) -> Vector3 {
                    const Vector2 slopeAxis = getSlopeAxis(surface.slopeDirection);
                    const float gradient = SlopeGradient(surface.slopeStrength);

                    return Vector3Math::Normalized({
                        slopeAxis.x * gradient,
                        -1.0f,
                        slopeAxis.y * gradient
                    });
                };

                const float radius = selfBody.radius;
                const float bottomHeight = selfBody.Bottom();
                const float topHeight = selfBody.Top();

                const int floorIndex = FindBestSectorFloor(
                    baseSector,
                    sectorBounds,
                    basePoint,
                    bottomHeight,
                    topHeight,
                    radius
                );

                if (floorIndex < 0) {
                    if (isBase) selfTransform->relativeHeight = bottomHeight;
                    continue;
                }

                const SectorFloor &floor = baseSector.floors[floorIndex];

                const float ceilingHeight = RestingCeilingHeight(floor.ceiling, sectorBounds, basePoint, radius);
                const Vector3 ceilingBottomNormal = getCeilingNormal(floor.ceiling);

                const float horizontalSpeed = std::sqrt(
                    selfRigidbody->velocity.x * selfRigidbody->velocity.x +
                    selfRigidbody->velocity.z * selfRigidbody->velocity.z
                );

                // The floor the body rests on: its own slab's, unless a sloped
                // floor next door rises under its rounded edge.
                const SectorSurface* ground = &floor.floor;
                const SectorBounds* groundBounds = &sectorBounds;
                float floorHeight = RestingFloorHeight(floor.floor, sectorBounds, basePoint, radius);

                // A round body touches a sloped plane uphill of its centre, by
                // radius * sin(angle). At the foot of a ramp in the next sector
                // that point is across the edge before the centre is, and only
                // testing the floor under the centre let the body sink into the
                // ramp, then threw it up or back once the centre crossed. A
                // ramp whose foot is a ledge too high to climb is left to the
                // wall between the sectors.
                for (const Sector* neighbour : baseSector.neighbors) {
                    if (neighbour == nullptr || neighbour == &baseSector) continue;

                    const SectorBounds& neighbourBounds = boundsCache.Get(neighbour);

                    const int neighbourFloorIndex = FindBestSectorFloor(
                        *neighbour,
                        neighbourBounds,
                        basePoint,
                        bottomHeight,
                        topHeight,
                        radius
                    );
                    if (neighbourFloorIndex < 0) continue;

                    const SectorSurface& candidate = neighbour->floors[neighbourFloorIndex].floor;
                    if (candidate.slopeStrength == 0.0f) continue;

                    const float gradient = SlopeGradient(candidate.slopeStrength);
                    const Vector2 axis = getSlopeAxis(candidate.slopeDirection);
                    const float reach = radius * gradient / std::sqrt(1.0f + gradient * gradient);
                    const Vector2 contact = {basePoint.x + axis.x * reach, basePoint.y + axis.y * reach};

                    const int neighbourIndex = static_cast<int>(neighbour - level.sectors.data());
                    if (MapQueries::FindSectorContainingPoint(level.sectors, contact, neighbourIndex) != neighbourIndex) continue;

                    const float candidateHeight = RestingFloorHeight(candidate, neighbourBounds, basePoint, radius);
                    const float reachable = std::max(selfCollider.stepSize, horizontalSpeed * GameTime::deltaTime * std::abs(gradient));

                    if (candidateHeight > floorHeight && candidateHeight <= bottomHeight + reachable + GROUND_CONTACT_SLOP) {
                        ground = &candidate;
                        groundBounds = &neighbourBounds;
                        floorHeight = candidateHeight;
                    }
                }

                // Gradients are rise per unit, so the slope's angle is their
                // arctangent. A negative strength slopes the other way.
                const auto isWalkable = [&](const float gradient) {
                    return std::atan(std::abs(gradient)) * Constants::RadToDeg <= selfCollider.maxSlope + Constants::Epsilon;
                };

                Vector3 floorNormal = getFloorNormal(*ground);
                float floorGradient = SlopeGradient(ground->slopeStrength);
                bool floorIsWalkable = isWalkable(floorGradient);

                // Map direction the floor rises toward, and how fast the body
                // is moving that way.
                const Vector2 floorSlopeAxis = getSlopeAxis(ground->slopeDirection);
                const Vector2 uphill = floorGradient < 0.0f ? -floorSlopeAxis : floorSlopeAxis;
                const float uphillSpeed =
                    selfRigidbody->velocity.x * uphill.x + selfRigidbody->velocity.z * uphill.y;

                float correctedBottomHeight = bottomHeight;
                float correctedTopHeight = topHeight;

                if (correctedBottomHeight < floorHeight) {
                    const float correction = floorHeight - correctedBottomHeight;

                    if (floorIsWalkable) {
                        // Straight up, so a body standing on a slope doesn't slide.
                        selfTransform->AddPosition({0.0f, correction, 0.0f});

                        correctedBottomHeight += correction;
                        correctedTopHeight += correction;
                    }
                    else {
                        // A slope too steep to stand on is a wall to walk
                        // into: the part of the overlap this step's uphill
                        // movement made is undone straight back downhill, so
                        // walking never gains height on it. Pushing all of it
                        // out along the slope's normal kept most of the
                        // movement, and holding forward walked right up.
                        const float steepness = std::abs(floorGradient);
                        const float wallCorrection =
                            std::clamp(uphillSpeed * GameTime::deltaTime * steepness, 0.0f, correction);
                        const float wallDistance = wallCorrection / steepness;

                        // The rest (falling onto it) goes out along the normal:
                        // (-gradient, 1) * rest / (1 + gradient^2) clears
                        // exactly `rest` of height above the plane.
                        const float scale = (correction - wallCorrection) / (1.0f + floorGradient * floorGradient);
                        const Vector3 push = {
                            -uphill.x * wallDistance - floorSlopeAxis.x * floorGradient * scale,
                            scale,
                            -uphill.y * wallDistance - floorSlopeAxis.y * floorGradient * scale
                        };

                        selfTransform->AddPosition(push);

                        // The push can put the body back into a wall, and a
                        // wall can stop it (a body wedged between a wall and
                        // the slope) - pushed only sideways it then sank into
                        // the slope a little every step until it fell out of
                        // the map. So walls go again, then the body is lifted
                        // onto whatever holds it where it ended up: the slope,
                        // or its own floor when it was pushed back off a ramp
                        // next door.
                        selfBody = MakeRoundBody(*selfTransform, selfCollider);
                        resolveWalls();

                        const Vector2 pushedPoint = {selfBody.base.x, selfBody.base.z};
                        const float slopeHeight = RestingFloorHeight(*ground, *groundBounds, pushedPoint, radius);
                        const float ownHeight = RestingFloorHeight(floor.floor, sectorBounds, pushedPoint, radius);

                        // At the crease both hold it; the own floor counts, so
                        // a body pressing into a ramp's foot stays on the ground.
                        if (ground != &floor.floor && ownHeight >= slopeHeight - GROUND_CONTACT_SLOP) {
                            ground = &floor.floor;
                            groundBounds = &sectorBounds;
                            floorNormal = getFloorNormal(*ground);
                            floorGradient = SlopeGradient(ground->slopeStrength);
                            floorIsWalkable = isWalkable(floorGradient);
                        }

                        floorHeight = std::max(slopeHeight, ownHeight);

                        const float lift = std::max(0.0f, floorHeight - selfBody.Bottom());
                        selfTransform->AddPosition({0.0f, lift, 0.0f});
                        selfBody.base.y += lift;


                        correctedBottomHeight = selfBody.Bottom();
                        correctedTopHeight = selfBody.Top();
                        floorHeight = std::min(floorHeight, correctedBottomHeight);
                    }

                    // A walkable floor only stops the fall: taking the velocity
                    // into its tilted surface instead left a downhill part, and
                    // a body landing on a gentle slope slid all the way down.
                    // A steep one stops uphill movement like a wall and keeps
                    // the downhill part, which is what slides it.
                    if (floorIsWalkable) {
                        if (selfRigidbody->velocity.y < 0.0f) selfRigidbody->velocity.y = 0.0f;
                    }
                    else {
                        if (uphillSpeed > 0.0f) {
                            selfRigidbody->velocity.x -= uphill.x * uphillSpeed;
                            selfRigidbody->velocity.z -= uphill.y * uphillSpeed;
                        }

                        const float velocityIntoFloor = Vector3Math::Dot(selfRigidbody->velocity, floorNormal);

                        if (velocityIntoFloor < 0.0f)
                            selfRigidbody->velocity = selfRigidbody->velocity - floorNormal * velocityIntoFloor;
                    }
                }

                if (correctedTopHeight > ceilingHeight) {
                    // The floor wins: in a slab shorter than the body the top
                    // stays in the ceiling instead of the bottom being pushed
                    // back through the floor.
                    const float correction =
                        std::min(0.0f, std::max(ceilingHeight - correctedTopHeight, floorHeight - correctedBottomHeight));

                    selfTransform->AddPosition({0.0f, correction, 0.0f});

                    correctedBottomHeight += correction;
                    correctedTopHeight += correction;

                    const float velocityIntoCeiling = Vector3Math::Dot(selfRigidbody->velocity, ceilingBottomNormal);

                    if (velocityIntoCeiling < 0.0f)
                        selfRigidbody->velocity = selfRigidbody->velocity - ceilingBottomNormal * velocityIntoCeiling;
                }

                // Still in the ceiling: the slab is too short here, because the
                // ceiling slopes down or the floor slopes up toward it. Pushing
                // down stopped at the floor, so push back sideways to where the
                // slab is tall enough - otherwise the body walked right through.
                const float ceilingPenetration = correctedTopHeight - ceilingHeight;

                if (ceilingPenetration > PENETRATION_SLOP) {
                    const Vector2 ceilingAxis = getSlopeAxis(floor.ceiling.slopeDirection);
                    const Vector2 floorAxis = getSlopeAxis(ground->slopeDirection);
                    const float ceilingGradient = SlopeGradient(floor.ceiling.slopeStrength);

                    // How much taller the slab gets per unit moved along X and Z.
                    const Vector2 opening = {
                        ceilingAxis.x * ceilingGradient - floorAxis.x * floorGradient,
                        ceilingAxis.y * ceilingGradient - floorAxis.y * floorGradient
                    };
                    const float openingRate = std::sqrt(opening.x * opening.x + opening.y * opening.y);

                    if (openingRate > Constants::Epsilon) {
                        const Vector3 pushDirection = {opening.x / openingRate, 0.0f, opening.y / openingRate};
                        const float pushDistance = (ceilingPenetration - PENETRATION_SLOP) / openingRate;

                        const Vector2 pushedPoint = {
                            basePoint.x + pushDirection.x * pushDistance,
                            basePoint.y + pushDirection.z * pushDistance
                        };
                        const float pushedFloorHeight = RestingFloorHeight(*ground, *groundBounds, pushedPoint, radius);

                        // A body resting on a sloped floor follows it down, so
                        // it stays grounded instead of hovering after the push.
                        const float bottomDrop = correctedBottomHeight - floorHeight <= GROUND_CONTACT_SLOP
                            ? std::min(0.0f, pushedFloorHeight - floorHeight)
                            : 0.0f;

                        selfTransform->AddPosition({pushDirection.x * pushDistance, bottomDrop, pushDirection.z * pushDistance});

                        correctedBottomHeight += bottomDrop;
                        correctedTopHeight += bottomDrop;
                        floorHeight = pushedFloorHeight;

                        const float velocityIntoPinch = Vector3Math::Dot(selfRigidbody->velocity, pushDirection);

                        if (velocityIntoPinch < 0.0f)
                            selfRigidbody->velocity = selfRigidbody->velocity - pushDirection * velocityIntoPinch;
                    }
                }

                float groundDistance = correctedBottomHeight - floorHeight;

                // Straight up, matching how walkable floors push.
                const float groundSeparatingSpeed = selfRigidbody->velocity.y;

                // Walking down a slope moves the body sideways off a floor
                // that drops away under it: it left the ground every frame,
                // fell back and bounced all the way down. A body that was
                // grounded and isn't moving up follows the floor down by as
                // much as a walkable floor can drop in this step - its
                // horizontal move times the steepest walkable gradient, plus
                // how much higher a round body rests on such a slope than on
                // a flat floor. Going faster than that, or off a ledge, it
                // still falls.
                const size_t rigidbodyIndex = static_cast<size_t>(selfRigidbody - level.rigidbodies.components.data());
                const bool wasGrounded = rigidbodyIndex < groundedBeforeRun.size() && groundedBeforeRun[rigidbodyIndex] != 0;

                if (wasGrounded &&
                    floorIsWalkable &&
                    groundDistance > GROUND_CONTACT_SLOP &&
                    groundSeparatingSpeed <= MAX_GROUND_SEPARATING_SPEED) {
                    const float maxAngle = std::min(selfCollider.maxSlope, MAX_SNAP_SLOPE) * Constants::DegToRad;
                    const float maxGradient = std::tan(maxAngle);

                    const float snapDistance =
                        horizontalSpeed * GameTime::deltaTime * maxGradient +
                        radius * (1.0f / std::cos(maxAngle) - 1.0f) +
                        GROUND_CONTACT_SLOP;

                    if (groundDistance <= snapDistance) {
                        selfTransform->AddPosition({0.0f, -groundDistance, 0.0f});

                        correctedBottomHeight -= groundDistance;
                        groundDistance = 0.0f;
                    }
                }

                // A slope steeper than maxSlope isn't ground: gravity keeps
                // pulling and the body slides down it.
                if (floorIsWalkable &&
                    groundDistance <= GROUND_CONTACT_SLOP &&
                    groundSeparatingSpeed <= MAX_GROUND_SEPARATING_SPEED) {
                    selfRigidbody->isGrounded = true;
                    selfRigidbody->groundNormal = floorNormal;

                    if (groundSeparatingSpeed < 0.0f) selfRigidbody->velocity.y = 0.0f;
                }
                else if (!floorIsWalkable && groundDistance <= GROUND_CONTACT_SLOP) {
                    selfRigidbody->slideNormal = floorNormal;
                }

                if (isBase) selfTransform->relativeHeight = groundDistance;
            }
        }

        for (ComponentCollider& selfCollider : colliders.ActiveBoxes()) {
            ZoneScopedN("AABB");
            // TODO TILKY_TODO AABB Collision
            /* use boxCollider.WorldSize(transform) for the AABB size
               boxCollider.Base(transform) for the position
                Make sure to follow the SIMD style as sphere
                You can look at the sphere collision to see how things are done

                Required features:
                AABB-AABB
                AABB-Sphere
                AABB-Wall
                AABB-Ceiling/Floor (with slopes)
            */

            if (selfCollider.isTrigger) continue;

            ComponentTransform* selfTransform = level.transforms.Get(selfCollider.ownerID);
            if (selfTransform == nullptr) [[unlikely]] continue;

            ComponentRigidbody* selfRigidbody = level.rigidbodies.Get(selfCollider.ownerID);
            if (selfRigidbody == nullptr || selfRigidbody->isStatic) continue;

            const int sectorIndex = selfTransform->sectorIndex;
            if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) continue;

            Sector& sector = level.sectors[sectorIndex];

            {
                ZoneScopedN("Candidate gather");

                allEntities.clear();
                allWalls.clear();

                allEntities.insert(
                    allEntities.end(),
                    sector.entitiesInside.begin(),
                    sector.entitiesInside.end()
                );

                allWalls.insert(
                    allWalls.end(),
                    sector.walls.begin(),
                    sector.walls.end()
                );

                for (const Sector* neighbour : sector.neighbors) {
                    if (neighbour == nullptr) [[unlikely]] continue;

                    allEntities.insert(
                        allEntities.end(),
                        neighbour->entitiesInside.begin(),
                        neighbour->entitiesInside.end()
                    );

                    allWalls.insert(
                        allWalls.end(),
                        neighbour->walls.begin(),
                        neighbour->walls.end()
                    );
                }

                std::ranges::sort(allEntities);
                allEntities.erase(std::unique(allEntities.begin(), allEntities.end()), allEntities.end());

                std::ranges::sort(allWalls, std::less<const Wall*>{});
                allWalls.erase(std::unique(allWalls.begin(), allWalls.end()), allWalls.end());
            }
        }

        {
            ZoneScopedN("Trigger overlaps");

            // After every push-out above, so overlaps are tested at the
            // frame's final positions. Triggers don't need a rigidbody -
            // a static trigger volume is the common case.
            for (const ComponentCollider& trigger : colliders.ActiveRound()) {
                if (!trigger.isTrigger) continue;

                const ComponentTransform* triggerTransform = level.transforms.Get(trigger.ownerID);
                if (triggerTransform == nullptr) [[unlikely]] continue;

                const int sectorIndex = triggerTransform->sectorIndex;
                if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) continue;

                const Sector& sector = level.sectors[sectorIndex];

                allEntities.clear();
                allEntities.insert(allEntities.end(), sector.entitiesInside.begin(), sector.entitiesInside.end());

                for (const Sector* neighbour : sector.neighbors) {
                    if (neighbour == nullptr) [[unlikely]] continue;
                    allEntities.insert(allEntities.end(), neighbour->entitiesInside.begin(), neighbour->entitiesInside.end());
                }

                std::ranges::sort(allEntities);
                allEntities.erase(std::unique(allEntities.begin(), allEntities.end()), allEntities.end());

                const RoundBody triggerBody = MakeRoundBody(*triggerTransform, trigger);

                for (const ID otherID : allEntities) {
                    if (otherID == trigger.ownerID) continue;

                    const ComponentTransform* otherTransform = level.transforms.Get(otherID);
                    if (otherTransform == nullptr) [[unlikely]] continue;

                    // Any of the other entity's colliders counts; the pair is
                    // reported per entity, and the caller dedupes (two
                    // overlapping triggers, or several colliders, find each
                    // other more than once).
                    for (const ComponentInstanceID otherInstance : level.colliders.InstancesOf(otherID)) {
                        const ComponentCollider* otherCollider = level.colliders.GetInstance(otherInstance);

                        if (!otherCollider->isActive ||
                            !otherCollider->IsRound()) continue;

                        const RoundBody otherBody = MakeRoundBody(*otherTransform, *otherCollider);
                        const float radiusSum = triggerBody.radius + otherBody.radius;

                        const __m128 delta = RoundBodyDelta(triggerBody, otherBody);
                        const float distanceSq = _mm_cvtss_f32(dot3_ss(delta, delta));

                        if (distanceSq >= radiusSum * radiusSum) continue;

                        contacts.triggers.push_back({
                            std::min(trigger.ownerID, otherID),
                            std::max(trigger.ownerID, otherID)
                        });
                        break;
                    }
                }
            }
        }
    }
}