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
#include "Headers/Math/Constants.hpp"
#include "Headers/Math/SIMD/SSECompat.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Objects/Level.hpp"

// Every SIMD use in physics goes through these, and each has a plain twin
// for NOSIMD builds, like the math headers.
namespace {
#ifndef NOSIMD
    // 1 / x: the estimate refined by one Newton-Raphson step.
    inline float FastReciprocal(const float x) {
        const __m128 value = _mm_set_ss(x);
        __m128 r = _mm_rcp_ss(value);
        r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(2.0f), _mm_mul_ss(value, r)));
        return _mm_cvtss_f32(r);
    }

    // 1 / sqrt(x): the estimate refined by one Newton-Raphson step.
    inline float FastInverseSqrt(const float x) {
        const __m128 value = _mm_set_ss(x);
        __m128 r = _mm_rsqrt_ss(value);
        const __m128 half = _mm_set_ss(0.5f);
        const __m128 threeHalves = _mm_set_ss(1.5f);
        r = _mm_mul_ss(r, _mm_sub_ss(threeHalves, _mm_mul_ss(half, _mm_mul_ss(value, _mm_mul_ss(r, r)))));
        return _mm_cvtss_f32(r);
    }

    // The dot product of the X and Z parts only.
    inline float DotXZ(const Vector3& a, const Vector3& b) {
        const __m128 mul = _mm_mul_ps(a.reg, b.reg);
        const __m128 z = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2, 2, 2, 2));
        return _mm_cvtss_f32(_mm_add_ss(mul, z));
    }

    // v with its Y zeroed.
    inline Vector3 FlattenXZ(const Vector3& v) {
        const __m128 mask = _mm_castsi128_ps(_mm_setr_epi32(-1, 0, -1, 0));
        return {_mm_and_ps(v.reg, mask)};
    }

    // Whether the point is outside the box in X or Z; Y is ignored.
    inline bool OutsideXZ(const Vector3& point, const Vector3& minimum, const Vector3& maximum) {
        const __m128 outside = _mm_or_ps(_mm_cmplt_ps(point.reg, minimum.reg), _mm_cmpgt_ps(point.reg, maximum.reg));
        return (_mm_movemask_ps(outside) & 0x5) != 0;
    }

    inline Vector2 MinVector2(const Vector2& a, const Vector2& b) { return {_mm_min_ps(a.reg, b.reg)}; }
    inline Vector2 MaxVector2(const Vector2& a, const Vector2& b) { return {_mm_max_ps(a.reg, b.reg)}; }
    inline Vector3 MinVector3(const Vector3& a, const Vector3& b) { return {_mm_min_ps(a.reg, b.reg)}; }
#else
    inline float FastReciprocal(const float x) { return 1.0f / x; }

    inline float FastInverseSqrt(const float x) { return 1.0f / std::sqrt(x); }

    inline float DotXZ(const Vector3& a, const Vector3& b) { return a.x * b.x + a.z * b.z; }

    inline Vector3 FlattenXZ(const Vector3& v) { return {v.x, 0.0f, v.z}; }

    inline bool OutsideXZ(const Vector3& point, const Vector3& minimum, const Vector3& maximum) {
        return point.x < minimum.x || point.x > maximum.x || point.z < minimum.z || point.z > maximum.z;
    }

    inline Vector2 MinVector2(const Vector2& a, const Vector2& b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
    inline Vector2 MaxVector2(const Vector2& a, const Vector2& b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }

    inline Vector3 MinVector3(const Vector3& a, const Vector3& b) {
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    }
#endif
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

    // A push out of one wall can put the body into another (a corner), so
    // walls are gone through again until nothing moves it, this many times
    // at most.
    constexpr int MAX_WALL_PASSES = 8;

    // A sub-step moves a body at most this much of its half width (or half
    // height, going up or down). Its centre then can't get past a wall's
    // line before the wall pushes it back: a body that did was pushed out
    // on the far side and went through.
    constexpr float MAX_SUBSTEP_TRAVEL = 0.5f;

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

        if (!sector.triangles.empty()) {
            bounds.minimum = sector.triangles.front().a;
            bounds.maximum = bounds.minimum;

            for (const Triangle& triangle : sector.triangles) {
                for (const Vector2* corner : {&triangle.a, &triangle.b, &triangle.c}) {
                    bounds.minimum = MinVector2(bounds.minimum, *corner);
                    bounds.maximum = MaxVector2(bounds.maximum, *corner);
                }
            }
        }
        else if (!sector.vertices.empty()) {
            bounds.minimum = sector.vertices.front();
            bounds.maximum = bounds.minimum;

            for (const Vector2& vertex : sector.vertices) {
                bounds.minimum = MinVector2(bounds.minimum, vertex);
                bounds.maximum = MaxVector2(bounds.maximum, vertex);
            }
        }
        else return bounds;

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

    // A solid shape as physics sees it, standing on `base` (the collider's
    // bottom). Round: an upright capsule - every point within `radius` of
    // the vertical segment between its two cap centres (a sphere's meet).
    // Box: an AABB centred on the base in X and Z, `height` tall.
    struct Body {
        Vector3 base;
        float height = 0.0f;
        float radius = 0.0f; // round only
        float halfX = 0.0f;  // box only: half its size along X and Z
        float halfZ = 0.0f;
        bool isBox = false;

        [[nodiscard]] float Bottom() const { return base.y; }
        [[nodiscard]] float Top() const { return base.y + height; }
        [[nodiscard]] float LowCentre() const { return base.y + radius; }
        [[nodiscard]] float HighCentre() const { return base.y + height - radius; }

        [[nodiscard]] Vector3 Min() const { return {base.x - halfX, base.y, base.z - halfZ}; }
        [[nodiscard]] Vector3 Max() const { return {base.x + halfX, base.y + height, base.z + halfZ}; }

        // The shortest way from its centre to its side, seen from above.
        [[nodiscard]] float HalfWidth() const { return isBox ? std::min(halfX, halfZ) : radius; }
    };

    Body MakeBody(const ComponentTransform& transform, const ComponentCollider& collider) {
        Body body;
        body.base = collider.Base(transform);
        body.height = collider.Height(transform);

        if (collider.type == COLLIDERTYPE_BOX) {
            const Vector3 size = collider.WorldSize(transform);
            body.halfX = size.x * 0.5f;
            body.halfZ = size.z * 0.5f;
            body.isBox = true;
        }
        else body.radius = collider.Radius(transform);

        return body;
    }

    // Half a box's footprint along the slope's axis.
    float HalfAlongSlope(const Body& body, const SlopeDirection direction) {
        return direction == PLUS_X || direction == MINUS_X ? body.halfX : body.halfZ;
    }

    // How far straight up from a sloped plane the body's bottom sits while
    // touching it - zero on a flat one. A round body's end meets the slope
    // off centre, a little above the plane's height under the centre. A
    // box's flat bottom rests on the highest point of the plane under it,
    // at its uphill edge.
    float SlopeLift(const SectorSurface& surface, const Body& body) {
        const float gradient = SlopeGradient(surface.slopeStrength);

        if (body.isBox) return HalfAlongSlope(body, surface.slopeDirection) * std::abs(gradient);

        return body.radius * (std::sqrt(1.0f + gradient * gradient) - 1.0f);
    }

    // How far from its centre along the slope's axis the body touches the
    // slope - signed like the gradient, so it points uphill.
    float SlopeContactReach(const SectorSurface& surface, const Body& body) {
        const float gradient = SlopeGradient(surface.slopeStrength);

        if (body.isBox) {
            const float half = HalfAlongSlope(body, surface.slopeDirection);
            return gradient > 0.0f ? half : gradient < 0.0f ? -half : 0.0f;
        }

        return body.radius * gradient / std::sqrt(1.0f + gradient * gradient);
    }

    // The most SlopeLift can be on a slope of `angle` radians, whichever way
    // it runs.
    float MaxSlopeLift(const Body& body, const float angle) {
        if (body.isBox) return std::max(body.halfX, body.halfZ) * std::tan(angle);

        return body.radius * (1.0f / std::cos(angle) - 1.0f);
    }

    // The lowest the body's bottom can go over this floor.
    float RestingFloorHeight(
        const SectorSurface& floor,
        const SectorBounds& bounds,
        const Vector2& point,
        const Body& body
    ) {
        return GetSurfaceHeight(floor, bounds, point) + SlopeLift(floor, body);
    }

    // The highest the body's top can go under this ceiling.
    float RestingCeilingHeight(
        const SectorSurface& ceiling,
        const SectorBounds& bounds,
        const Vector2& point,
        const Body& body
    ) {
        return GetSurfaceHeight(ceiling, bounds, point) - SlopeLift(ceiling, body);
    }

    int FindBestSectorFloor(
        const Sector& sector,
        const SectorBounds& bounds,
        const Vector2& point,
        const float bottomHeight,
        const float topHeight,
        const Body& body
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

                const float floorHeight = RestingFloorHeight(floor.floor, bounds, point, body);
                const float ceilingHeight = RestingCeilingHeight(floor.ceiling, bounds, point, body);

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

    // Which way, and how far, `a` has to move to stop overlapping `b`. False
    // when they don't overlap. The direction is a unit vector.
    bool FindRoundRoundOverlap(const Body& a, const Body& b, Vector3& direction, float& depth) {
        float aHeight, bHeight;
        ClosestHeights(a.LowCentre(), a.HighCentre(), b.LowCentre(), b.HighCentre(), aHeight, bHeight);

        // From the closest point of `b` to the closest point of `a`.
        const Vector3 delta = {a.base.x - b.base.x, aHeight - bHeight, a.base.z - b.base.z};
        const float radiusSum = a.radius + b.radius;
        const float distanceSq = Vector3Math::LengthSquared(delta);

        if (distanceSq >= radiusSum * radiusSum) return false;

        const float inverseDistance = FastInverseSqrt(std::max(distanceSq, Constants::Epsilon));
        depth = radiusSum - distanceSq * inverseDistance;

        if (depth <= Constants::Epsilon) return false;

        direction = distanceSq > Constants::Epsilon ? delta * inverseDistance : Vector3{1.0f, 0.0f, 0.0f};

        return true;
    }

    // The smallest of `count` pushes, as a direction and depth.
    void PickShallowestPush(const Vector3* directions, const float* depths, const int count, Vector3& direction, float& depth) {
        int best = 0;

        for (int i = 1; i < count; ++i)
            if (depths[i] < depths[best]) best = i;

        direction = directions[best];
        depth = depths[best];
    }

    bool FindRoundBoxOverlap(const Body& round, const Body& box, Vector3& direction, float& depth) {
        const Vector3 boxMin = box.Min();
        const Vector3 boxMax = box.Max();

        float roundHeight, boxHeight;
        ClosestHeights(round.LowCentre(), round.HighCentre(), boxMin.y, boxMax.y, roundHeight, boxHeight);

        // From the box's closest point to the round body's axis.
        const Vector3 delta = {
            round.base.x - std::clamp(round.base.x, boxMin.x, boxMax.x),
            roundHeight - boxHeight,
            round.base.z - std::clamp(round.base.z, boxMin.z, boxMax.z)
        };
        const float distanceSq = Vector3Math::LengthSquared(delta);
        const float radius = round.radius;

        if (distanceSq >= radius * radius) return false;

        if (distanceSq > Constants::Epsilon) {
            const float inverseDistance = FastInverseSqrt(distanceSq);
            depth = radius - distanceSq * inverseDistance;

            if (depth <= Constants::Epsilon) return false;

            direction = delta * inverseDistance;

            return true;
        }

        // Its axis is inside the box: out through the nearest face, far
        // enough for the whole round shape to clear it.
        const Vector3 directions[6] = {
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}
        };
        const float depths[6] = {
            boxMax.x - (round.base.x - radius), round.base.x + radius - boxMin.x,
            boxMax.y - round.Bottom(), round.Top() - boxMin.y,
            boxMax.z - (round.base.z - radius), round.base.z + radius - boxMin.z
        };

        PickShallowestPush(directions, depths, 6, direction, depth);

        return depth > Constants::Epsilon;
    }

    bool FindBoxBoxOverlap(const Body& a, const Body& b, Vector3& direction, float& depth) {
        // How deep they overlap along each axis.
        const Vector3 overlap = MinVector3(a.Max() - b.Min(), b.Max() - a.Min());

        if (overlap.x <= Constants::Epsilon || overlap.y <= Constants::Epsilon || overlap.z <= Constants::Epsilon) return false;

        // Away from b's centre, along the axis they overlap least on.
        const Vector3 directions[3] = {
            {a.base.x >= b.base.x ? 1.0f : -1.0f, 0.0f, 0.0f},
            {0.0f, a.Bottom() + a.Top() >= b.Bottom() + b.Top() ? 1.0f : -1.0f, 0.0f},
            {0.0f, 0.0f, a.base.z >= b.base.z ? 1.0f : -1.0f}
        };
        const float depths[3] = {overlap.x, overlap.y, overlap.z};

        PickShallowestPush(directions, depths, 3, direction, depth);

        return true;
    }

    bool FindBodyOverlap(const Body& a, const Body& b, Vector3& direction, float& depth) {
        if (!a.isBox && !b.isBox) return FindRoundRoundOverlap(a, b, direction, depth);
        if (a.isBox && b.isBox) return FindBoxBoxOverlap(a, b, direction, depth);
        if (b.isBox) return FindRoundBoxOverlap(a, b, direction, depth);

        if (!FindRoundBoxOverlap(b, a, direction, depth)) return false;

        direction = -direction;

        return true;
    }

    // A body overlapping a wall span: the way out (horizontal, unit length),
    // how far, and how tall the span is where the body touches it.
    struct WallHit {
        Vector3 normal;
        float penetration = 0.0f;
        float spanBottom = 0.0f;
        float spanTop = 0.0f;
    };

    bool FindRoundWallHit(const Wall& wall, const WallSpan& span, const Body& body, WallHit& hit) {
        const float radius = body.radius;

        // Conservative vertical extent: the span is a trapezoid now, so the
        // broadphase box has to cover the taller of the two ends.
        const float spanMinHeight = std::min(span.bottomStart, span.bottomEnd);
        const float spanMaxHeight = std::max(span.topStart, span.topEnd);

        // Only the part of the wall the span covers.
        const Vector2 segmentStart = PointAlong(wall.start, wall.end, span.startT);
        const Vector2 segmentEnd = PointAlong(wall.start, wall.end, span.endT);
        const Vector2 segment = {segmentEnd.x - segmentStart.x, segmentEnd.y - segmentStart.y};

        const Vector3 broadMin = {std::min(segmentStart.x, segmentEnd.x) - radius, 0.0f, std::min(segmentStart.y, segmentEnd.y) - radius};
        const Vector3 broadMax = {std::max(segmentStart.x, segmentEnd.x) + radius, 0.0f, std::max(segmentStart.y, segmentEnd.y) + radius};

        if (OutsideXZ(body.base, broadMin, broadMax)) return false;
        if (body.Top() < spanMinHeight || body.Bottom() > spanMaxHeight) return false;

        const float segmentLengthSq = segment.x * segment.x + segment.y * segment.y;
        if (segmentLengthSq <= Constants::Epsilon) return false;

        const float toBodyX = body.base.x - segmentStart.x;
        const float toBodyZ = body.base.z - segmentStart.y;
        const float t = std::clamp(
            (toBodyX * segment.x + toBodyZ * segment.y) * FastReciprocal(segmentLengthSq),
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

        const Vector3 delta = {
            body.base.x - (segmentStart.x + segment.x * t),
            bodyY - wallY,
            body.base.z - (segmentStart.y + segment.y * t)
        };
        const float distanceSq = Vector3Math::LengthSquared(delta);

        if (distanceSq >= radius * radius) return false;

        const float inverseDistance = FastInverseSqrt(std::max(distanceSq, Constants::Epsilon));
        const float penetration = radius - distanceSq * inverseDistance;

        if (penetration <= Constants::Epsilon) return false;

        Vector3 normal = FlattenXZ(delta * inverseDistance);

        // Right on the wall's line: out on the side the centre is on.
        if (DotXZ(normal, normal) <= Constants::Epsilon) {
            const float planeDistance =
                (body.base.x - wall.start.x) * wall.normal.x + (body.base.z - wall.start.y) * wall.normal.y;
            const float side = planeDistance < 0.0f ? -1.0f : 1.0f;

            normal = {wall.normal.x * side, 0.0f, wall.normal.y * side};
        }

        const float normalLengthSq = DotXZ(normal, normal);
        if (normalLengthSq <= Constants::Epsilon) return false;

        hit.normal = normal * FastInverseSqrt(normalLengthSq);
        hit.penetration = penetration;
        hit.spanBottom = spanBottom;
        hit.spanTop = spanTop;

        return true;
    }

    // Seen from above: the box's footprint against the wall's segment,
    // separated along X, Z or the wall's normal, whichever is shallowest.
    bool FindBoxWallHit(const Wall& wall, const WallSpan& span, const Body& body, WallHit& hit) {
        const Vector2 segmentStart = PointAlong(wall.start, wall.end, span.startT);
        const Vector2 segmentEnd = PointAlong(wall.start, wall.end, span.endT);
        const Vector2 segment = {segmentEnd.x - segmentStart.x, segmentEnd.y - segmentStart.y};

        if (segment.x * segment.x + segment.y * segment.y <= Constants::Epsilon) return false;

        const Vector3 boxMin = body.Min();
        const Vector3 boxMax = body.Max();

        // The part of the segment over the footprint, as fractions along it
        // (Liang-Barsky). None means they don't touch.
        float enterT = 0.0f;
        float exitT = 1.0f;

        const auto clip = [&](const float p, const float q) {
            if (std::abs(p) <= Constants::Epsilon) return q >= 0.0f;

            const float r = q / p;

            if (p < 0.0f) {
                if (r > exitT) return false;
                enterT = std::max(enterT, r);
            }
            else {
                if (r < enterT) return false;
                exitT = std::min(exitT, r);
            }

            return true;
        };

        if (!clip(-segment.x, segmentStart.x - boxMin.x) ||
            !clip(segment.x, boxMax.x - segmentStart.x) ||
            !clip(-segment.y, segmentStart.y - boxMin.z) ||
            !clip(segment.y, boxMax.z - segmentStart.y)) return false;

        // The span's extent over that part: lowest bottom, highest top.
        const auto bottomAt = [&](const float t) { return span.bottomStart + (span.bottomEnd - span.bottomStart) * t; };
        const auto topAt = [&](const float t) {
            return std::max(span.topStart + (span.topEnd - span.topStart) * t, bottomAt(t));
        };

        const float spanBottom = std::min(bottomAt(enterT), bottomAt(exitT));
        const float spanTop = std::max(topAt(enterT), topAt(exitT));

        if (body.Top() <= spanBottom || body.Bottom() >= spanTop) return false;

        const float segmentMinX = std::min(segmentStart.x, segmentEnd.x);
        const float segmentMaxX = std::max(segmentStart.x, segmentEnd.x);
        const float segmentMinZ = std::min(segmentStart.y, segmentEnd.y);
        const float segmentMaxZ = std::max(segmentStart.y, segmentEnd.y);

        const float planeDistance =
            (body.base.x - segmentStart.x) * wall.normal.x + (body.base.z - segmentStart.y) * wall.normal.y;
        const float side = planeDistance < 0.0f ? -1.0f : 1.0f;
        const float extentAlongNormal = body.halfX * std::abs(wall.normal.x) + body.halfZ * std::abs(wall.normal.y);

        const Vector3 directions[5] = {
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
            {wall.normal.x * side, 0.0f, wall.normal.y * side}
        };
        const float depths[5] = {
            segmentMaxX - boxMin.x, boxMax.x - segmentMinX,
            segmentMaxZ - boxMin.z, boxMax.z - segmentMinZ,
            extentAlongNormal - std::abs(planeDistance)
        };

        Vector3 normal;
        float penetration;
        PickShallowestPush(directions, depths, 5, normal, penetration);

        if (penetration <= Constants::Epsilon) return false;

        hit.normal = normal;
        hit.penetration = penetration;
        hit.spanBottom = spanBottom;
        hit.spanTop = spanTop;

        return true;
    }

    // The body's centre went through the span's line during this step,
    // `from` being where it started: it moved further than its own size.
    // Pushed out the normal way it would leave on the far side, so it goes
    // back to the side it came from.
    bool FindCrossedWallHit(const Wall& wall, const WallSpan& span, const Body& body, const Vector3& from, WallHit& hit) {
        const Vector2 segmentStart = PointAlong(wall.start, wall.end, span.startT);
        const Vector2 segmentEnd = PointAlong(wall.start, wall.end, span.endT);
        const Vector2 segment = {segmentEnd.x - segmentStart.x, segmentEnd.y - segmentStart.y};

        const float segmentLengthSq = segment.x * segment.x + segment.y * segment.y;
        if (segmentLengthSq <= Constants::Epsilon) return false;

        const float startDistance =
            (from.x - segmentStart.x) * wall.normal.x + (from.z - segmentStart.y) * wall.normal.y;
        const float endDistance =
            (body.base.x - segmentStart.x) * wall.normal.x + (body.base.z - segmentStart.y) * wall.normal.y;

        if ((startDistance >= 0.0f) == (endDistance >= 0.0f)) return false;

        // Where the move crossed the line, and whether the wall is there.
        const float moveT = startDistance / (startDistance - endDistance);
        const Vector3 crossing = from + (body.base - from) * moveT;

        const float t = ((crossing.x - segmentStart.x) * segment.x + (crossing.z - segmentStart.y) * segment.y) *
                        FastReciprocal(segmentLengthSq);
        if (t < 0.0f || t > 1.0f) return false;

        const float spanBottom = span.bottomStart + (span.bottomEnd - span.bottomStart) * t;
        const float spanTop = std::max(span.topStart + (span.topEnd - span.topStart) * t, spanBottom);

        // Over or under the span as it crossed, it got past legitimately.
        if (crossing.y + body.height <= spanBottom || crossing.y >= spanTop) return false;

        const float side = startDistance >= 0.0f ? 1.0f : -1.0f;
        const float extentAlongNormal = body.isBox
            ? body.halfX * std::abs(wall.normal.x) + body.halfZ * std::abs(wall.normal.y)
            : body.radius;

        hit.normal = {wall.normal.x * side, 0.0f, wall.normal.y * side};
        hit.penetration = std::abs(endDistance) + extentAlongNormal;
        hit.spanBottom = spanBottom;
        hit.spanTop = spanTop;

        return true;
    }

    enum class WallResult {
        NONE,     // not touching
        TOUCHING, // resting against it, nothing to push
        PUSHED    // moved out of it or up onto it
    };

    // `from` is where the body's bottom was before this step's move.
    WallResult ResolveWallSpan(
        const Wall &wall,
        const WallSpan &span,
        const float stepSize,
        const Vector3 &from,
        Body &body,
        ComponentTransform &transform,
        ComponentRigidbody &rigidbody
    ) {
        WallHit hit;

        if (!FindCrossedWallHit(wall, span, body, from, hit) &&
            !(body.isBox ? FindBoxWallHit(wall, span, body, hit) : FindRoundWallHit(wall, span, body, hit)))
            return WallResult::NONE;

        const float velocityIntoWall = Vector3Math::Dot(rigidbody.velocity, hit.normal);

        const float bodyBottom = body.Bottom();
        const float stepHeight = hit.spanTop - bodyBottom;

        const bool wallStartsAtBottom = hit.spanBottom <= bodyBottom + GROUND_CONTACT_SLOP;

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
        if (isClimbableStep && velocityIntoWall >= -Constants::Epsilon) return WallResult::NONE;

        if (isClimbableStep) {
            const float verticalCorrection = stepHeight + PENETRATION_SLOP;

            transform.AddPosition({0.0f, verticalCorrection, 0.0f});

            body.base.y += verticalCorrection;

            if (rigidbody.velocity.y < 0.0f) rigidbody.velocity.y = 0.0f;

            rigidbody.isGrounded = true;
            rigidbody.groundNormal = {0.0f, 1.0f, 0.0f};

            return WallResult::PUSHED;
        }

        const float correctedPenetration = std::max(0.0f, hit.penetration - PENETRATION_SLOP);

        if (velocityIntoWall < 0.0f)
            rigidbody.velocity = rigidbody.velocity - hit.normal * velocityIntoWall;

        if (correctedPenetration <= Constants::Epsilon) return WallResult::TOUCHING;

        const Vector3 correction = hit.normal * correctedPenetration;

        transform.AddPosition(correction);
        body.base = body.base + correction;

        return WallResult::PUSHED;
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

            if (!collider->InPhysics() || collider->isTrigger) continue;

            const float bottom = collider->Base(*transform).y;

            if (bottom < baseBottom) {
                base = collider;
                baseBottom = bottom;
            }
        }

        return base;
    }

    bool ColliderOverlapsSphere(const ComponentTransform& transform, const ComponentCollider& collider,
                                const Vector3& center, const float radius) {
        const Body body = MakeBody(transform, collider);

        if (body.isBox) {
            const Vector3 boxMin = body.Min();
            const Vector3 boxMax = body.Max();
            const Vector3 closest = {
                std::clamp(center.x, boxMin.x, boxMax.x),
                std::clamp(center.y, boxMin.y, boxMax.y),
                std::clamp(center.z, boxMin.z, boxMax.z)
            };
            return Vector3Math::LengthSquared(center - closest) <= radius * radius;
        }

        // The nearest point on the capsule's axis, then its radius on top.
        const float low = std::min(body.LowCentre(), body.HighCentre());
        const float high = std::max(body.LowCentre(), body.HighCentre());
        const Vector3 axisPoint = {body.base.x, std::clamp(center.y, low, high), body.base.z};
        const float reach = radius + body.radius;

        return Vector3Math::LengthSquared(center - axisPoint) <= reach * reach;
    }

    int SubstepCount(const Level& level, const float deltaTime, const int maxSubsteps) {
        if (maxSubsteps <= 1 || deltaTime <= 0.0f) return 1;

        float steps = 1.0f;

        for (const ComponentRigidbody& rigidbody : level.rigidbodies.components) {
            // Only an entity's first rigidbody simulates; the others are inert.
            if (rigidbody.isStatic || !rigidbody.ownerEnabled || level.rigidbodies.Get(rigidbody.ownerID) != &rigidbody) continue;

            const ComponentTransform* transform = level.transforms.Get(rigidbody.ownerID);
            if (transform == nullptr) [[unlikely]] continue;

            const float horizontalTravel =
                std::sqrt(rigidbody.velocity.x * rigidbody.velocity.x + rigidbody.velocity.z * rigidbody.velocity.z) * deltaTime;
            const float verticalTravel = std::abs(rigidbody.velocity.y) * deltaTime;

            if (horizontalTravel <= Constants::Epsilon && verticalTravel <= Constants::Epsilon) continue;

            // Its smallest solid collider decides.
            for (const ComponentInstanceID instanceID : level.colliders.InstancesOf(rigidbody.ownerID)) {
                const ComponentCollider* collider = level.colliders.GetInstance(instanceID);

                if (!collider->InPhysics() || collider->isTrigger) continue;

                const Body body = MakeBody(*transform, *collider);
                const float halfWidth = body.HalfWidth() * MAX_SUBSTEP_TRAVEL;
                const float halfHeight = body.height * 0.5f * MAX_SUBSTEP_TRAVEL;

                if (halfWidth > Constants::Epsilon) steps = std::max(steps, horizontalTravel / halfWidth);
                if (halfHeight > Constants::Epsilon) steps = std::max(steps, verticalTravel / halfHeight);
            }
        }

        return std::clamp(static_cast<int>(std::ceil(steps)), 1, maxSubsteps);
    }

    void Run(Level& level, Contacts& contacts, const float deltaTime) {
        ZoneScopedN("PhysicsSystem::Run");

        groundedBeforeRun.resize(level.rigidbodies.components.size());

        for (size_t index = 0; index < level.rigidbodies.components.size(); ++index) {
            ComponentRigidbody& rigidbody = level.rigidbodies.components[index];

            groundedBeforeRun[index] = rigidbody.isGrounded ? 1 : 0;

            // A disabled entity's body keeps its ground state for when it's back.
            if (!rigidbody.ownerEnabled) continue;

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

        std::vector<ID> nearbyEntities;
        std::vector<const Wall*> nearbyWalls;
        nearbyEntities.reserve(64);
        nearbyWalls.reserve(64);

        // Entities (and walls) in the sector the entity was in and the one
        // the body is over now, and their neighbours. sectorIndex is only
        // updated after physics, so once the body crossed an edge this frame
        // (or an earlier sub-step) it is stale, and the walls of the sector
        // it moved into went untested - and a fast body can land in a sector
        // that isn't even a neighbour of the old one. False when the body's
        // entity isn't in a sector.
        const auto gatherNearby = [&](const ComponentTransform& transform, const Body& body, const bool withWalls) {
            ZoneScopedN("Candidate gather");

            const int sectorIndex = transform.sectorIndex;
            if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) return false;

            const int bodySectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, {body.base.x, body.base.z}, sectorIndex);

            nearbyEntities.clear();
            nearbyWalls.clear();

            const auto addSector = [&](const Sector& sector) {
                nearbyEntities.insert(nearbyEntities.end(), sector.entitiesInside.begin(), sector.entitiesInside.end());
                if (withWalls) nearbyWalls.insert(nearbyWalls.end(), sector.walls.begin(), sector.walls.end());
            };

            const auto addWithNeighbours = [&](const Sector& sector) {
                addSector(sector);

                for (const Sector* neighbour : sector.neighbors)
                    if (neighbour != nullptr) [[likely]] addSector(*neighbour);
            };

            addWithNeighbours(level.sectors[sectorIndex]);
            if (bodySectorIndex >= 0 && bodySectorIndex != sectorIndex) addWithNeighbours(level.sectors[bodySectorIndex]);

            std::ranges::sort(nearbyEntities);
            nearbyEntities.erase(std::unique(nearbyEntities.begin(), nearbyEntities.end()), nearbyEntities.end());

            std::ranges::sort(nearbyWalls, std::less<const Wall*>{});
            nearbyWalls.erase(std::unique(nearbyWalls.begin(), nearbyWalls.end()), nearbyWalls.end());

            return true;
        };

        // A solid collider on an entity with a non-static Rigidbody: the
        // ones physics moves.
        const auto getMovingBody = [&](const ComponentCollider& collider, ComponentTransform*& transform, ComponentRigidbody*& rigidbody) {
            if (collider.isTrigger) return false;

            transform = level.transforms.Get(collider.ownerID);
            if (transform == nullptr) [[unlikely]] return false;

            rigidbody = level.rigidbodies.Get(collider.ownerID);
            return rigidbody != nullptr && !rigidbody->isStatic;
        };

        // Bodies push each other first, then walls, floors and ceilings get
        // the last word. Done per collider, a body shoved by one processed
        // later was left inside the wall it was shoved into.
        {
            ZoneScopedN("Entity narrowphase");

            for (ComponentCollider& selfCollider : colliders.ActiveColliders()) {
                ComponentTransform* selfTransform;
                ComponentRigidbody* selfRigidbody;
                if (!getMovingBody(selfCollider, selfTransform, selfRigidbody)) continue;

                Body selfBody = MakeBody(*selfTransform, selfCollider);
                if (!gatherNearby(*selfTransform, selfBody, false)) continue;

                for (const ID otherID : nearbyEntities) {
                    if (otherID == selfCollider.ownerID) continue;

                    ComponentTransform* otherTransform = level.transforms.Get(otherID);
                    if (otherTransform == nullptr) [[unlikely]] continue;

                    const ComponentRigidbody* otherRigidbody = level.rigidbodies.Get(otherID);
                    const bool otherIsStatic = otherRigidbody == nullptr || otherRigidbody->isStatic;

                    if (!otherIsStatic && selfCollider.ownerID > otherID) continue;

                    for (const ComponentInstanceID otherInstance : level.colliders.InstancesOf(otherID)) {
                        const ComponentCollider* otherCollider = level.colliders.GetInstance(otherInstance);

                        if (!otherCollider->InPhysics() || otherCollider->isTrigger) continue;

                        Vector3 pushDirection;
                        float penetration;

                        if (!FindBodyOverlap(selfBody, MakeBody(*otherTransform, *otherCollider), pushDirection, penetration)) continue;

                        contacts.collisions.push_back({
                            std::min(selfCollider.ownerID, otherID),
                            std::max(selfCollider.ownerID, otherID)
                        });

                        const Vector3 correction = pushDirection * std::max(0.0f, penetration - PENETRATION_SLOP);

                        selfTransform->AddPosition(correction * (otherIsStatic ? 1.0f : 0.5f));

                        if (!otherIsStatic) otherTransform->AddPosition(correction * -0.5f);

                        selfBody = MakeBody(*selfTransform, selfCollider);
                    }
                }
            }
        }

        for (ComponentCollider& selfCollider : colliders.ActiveColliders()) {
            ZoneScopedN("Collider world iteration");

            ComponentTransform* selfTransform;
            ComponentRigidbody* selfRigidbody;
            if (!getMovingBody(selfCollider, selfTransform, selfRigidbody)) continue;

            const int sectorIndex = selfTransform->sectorIndex;

            Body selfBody = MakeBody(*selfTransform, selfCollider);
            if (!gatherNearby(*selfTransform, selfBody, true)) continue;

            // Where it was before this step's move: LevelSystem moved it by
            // velocity * deltaTime right before this run. Moves that don't
            // come from velocity (a script setting the position) aren't
            // swept, so teleporting past a wall still works.
            const Vector3 sweptFrom = selfBody.base - selfRigidbody->velocity * deltaTime;

            const auto resolveWalls = [&] {
                bool pushed = true;

                for (int pass = 0; pass < MAX_WALL_PASSES && pushed; ++pass) {
                    pushed = false;

                    for (const Wall* wall : nearbyWalls) {
                        if (wall == nullptr || level.walls.empty()) continue;

                        const std::ptrdiff_t wallIndex = wall - level.walls.data();

                        if (wallIndex < 0 || wallIndex >= static_cast<std::ptrdiff_t>(wallSpans.size())) continue;

                        for (const WallSpan &span : wallSpans[wallIndex]) {
                            const WallResult result =
                                ResolveWallSpan(*wall, span, selfCollider.stepSize, sweptFrom, selfBody, *selfTransform, *selfRigidbody);

                            if (result == WallResult::NONE) continue;

                            // Later passes only report walls that moved it,
                            // not the ones it still rests against.
                            if (pass == 0 || result == WallResult::PUSHED)
                                contacts.walls.push_back({selfCollider.ownerID, wall->id});

                            pushed = pushed || result == WallResult::PUSHED;
                        }
                    }
                }

                // Still being pushed: wedged in a corner narrower than
                // itself, where each wall shoves it into the other. Left
                // where the last push sent it, it squeezed out through one
                // of them. Back where it started this step, it was clear.
                if (pushed) {
                    const Vector3 back = FlattenXZ(sweptFrom - selfBody.base);

                    selfTransform->AddPosition(back);
                    selfBody.base = selfBody.base + back;
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

                const float bottomHeight = selfBody.Bottom();
                const float topHeight = selfBody.Top();

                const int floorIndex = FindBestSectorFloor(
                    baseSector,
                    sectorBounds,
                    basePoint,
                    bottomHeight,
                    topHeight,
                    selfBody
                );

                if (floorIndex < 0) {
                    if (isBase) selfTransform->relativeHeight = bottomHeight;
                    continue;
                }

                const SectorFloor &floor = baseSector.floors[floorIndex];

                const float ceilingHeight = RestingCeilingHeight(floor.ceiling, sectorBounds, basePoint, selfBody);
                const Vector3 ceilingBottomNormal = getCeilingNormal(floor.ceiling);

                const float horizontalSpeed = std::sqrt(
                    selfRigidbody->velocity.x * selfRigidbody->velocity.x +
                    selfRigidbody->velocity.z * selfRigidbody->velocity.z
                );

                // The floor the body rests on: its own slab's, unless a sloped
                // floor next door rises under its edge.
                const SectorSurface* ground = &floor.floor;
                const SectorBounds* groundBounds = &sectorBounds;
                float groundDrop = std::max(0.0f, -floor.floorDelta);
                float floorHeight = RestingFloorHeight(floor.floor, sectorBounds, basePoint, selfBody);

                // The body touches a sloped plane uphill of its centre (a
                // round one by radius * sin(angle), a box at its edge). At the
                // foot of a ramp in the next sector that point is across the
                // edge before the centre is, and only testing the floor under
                // the centre let the body sink into the ramp, then threw it up
                // or back once the centre crossed. A ramp whose foot is a
                // ledge too high to climb is left to the wall between the
                // sectors.
                for (const Sector* neighbour : baseSector.neighbors) {
                    if (neighbour == nullptr || neighbour == &baseSector) continue;

                    const SectorBounds& neighbourBounds = boundsCache.Get(neighbour);

                    const int neighbourFloorIndex = FindBestSectorFloor(
                        *neighbour,
                        neighbourBounds,
                        basePoint,
                        bottomHeight,
                        topHeight,
                        selfBody
                    );
                    if (neighbourFloorIndex < 0) continue;

                    const SectorSurface& candidate = neighbour->floors[neighbourFloorIndex].floor;
                    if (candidate.slopeStrength == 0.0f) continue;

                    const float gradient = SlopeGradient(candidate.slopeStrength);
                    const Vector2 axis = getSlopeAxis(candidate.slopeDirection);
                    const float reach = SlopeContactReach(candidate, selfBody);
                    const Vector2 contact = {basePoint.x + axis.x * reach, basePoint.y + axis.y * reach};

                    const int neighbourIndex = static_cast<int>(neighbour - level.sectors.data());
                    if (MapQueries::FindSectorContainingPoint(level.sectors, contact, neighbourIndex) != neighbourIndex) continue;

                    const float candidateHeight = RestingFloorHeight(candidate, neighbourBounds, basePoint, selfBody);
                    const float reachable = std::max(selfCollider.stepSize, horizontalSpeed * deltaTime * std::abs(gradient));

                    if (candidateHeight > floorHeight && candidateHeight <= bottomHeight + reachable + GROUND_CONTACT_SLOP) {
                        ground = &candidate;
                        groundBounds = &neighbourBounds;
                        groundDrop = std::max(0.0f, -neighbour->floors[neighbourFloorIndex].floorDelta);
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
                            std::clamp(uphillSpeed * deltaTime * steepness, 0.0f, correction);
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
                        selfBody = MakeBody(*selfTransform, selfCollider);
                        resolveWalls();

                        const Vector2 pushedPoint = {selfBody.base.x, selfBody.base.z};
                        const float slopeHeight = RestingFloorHeight(*ground, *groundBounds, pushedPoint, selfBody);
                        const float ownHeight = RestingFloorHeight(floor.floor, sectorBounds, pushedPoint, selfBody);

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
                        const float pushedFloorHeight = RestingFloorHeight(*ground, *groundBounds, pushedPoint, selfBody);

                        // A body resting on a sloped floor follows it down, so
                        // it stays grounded instead of hovering after the push.
                        const float bottomDrop = correctedBottomHeight - floorHeight <= GROUND_CONTACT_SLOP
                            ? std::min(0.0f, pushedFloorHeight - floorHeight)
                            : 0.0f;

                        selfTransform->AddPosition({pushDirection.x * pushDistance, bottomDrop, pushDirection.z * pushDistance});

                        floorHeight = pushedFloorHeight;

                        const float velocityIntoPinch = Vector3Math::Dot(selfRigidbody->velocity, pushDirection);

                        if (velocityIntoPinch < 0.0f)
                            selfRigidbody->velocity = selfRigidbody->velocity - pushDirection * velocityIntoPinch;

                        // The push is sideways, so it can go into a wall.
                        selfBody = MakeBody(*selfTransform, selfCollider);
                        resolveWalls();

                        correctedBottomHeight = selfBody.Bottom();
                        correctedTopHeight = selfBody.Top();
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
                // how much higher the body rests on such a slope than on a
                // flat floor. Going faster than that, or off a ledge, it
                // still falls. A floor lowering under it (a lift going down)
                // carries it down by however far the floor dropped this frame.
                const size_t rigidbodyIndex = static_cast<size_t>(selfRigidbody - level.rigidbodies.components.data());
                const bool wasGrounded = rigidbodyIndex < groundedBeforeRun.size() && groundedBeforeRun[rigidbodyIndex] != 0;

                if (wasGrounded &&
                    floorIsWalkable &&
                    groundDistance > GROUND_CONTACT_SLOP &&
                    groundSeparatingSpeed <= MAX_GROUND_SEPARATING_SPEED) {
                    const float maxAngle = std::min(selfCollider.maxSlope, MAX_SNAP_SLOPE) * Constants::DegToRad;
                    const float maxGradient = std::tan(maxAngle);

                    const float snapDistance =
                        horizontalSpeed * deltaTime * maxGradient +
                        MaxSlopeLift(selfBody, maxAngle) +
                        groundDrop +
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
                else if (!floorIsWalkable && groundDistance <= GROUND_CONTACT_SLOP) selfRigidbody->slideNormal = floorNormal;

                if (isBase) selfTransform->relativeHeight = groundDistance;
            }
        }

        {
            ZoneScopedN("Trigger overlaps");

            // After every push-out above, so overlaps are tested at the
            // frame's final positions. Triggers don't need a rigidbody -
            // a static trigger volume is the common case.
            for (const ComponentCollider& trigger : colliders.ActiveColliders()) {
                if (!trigger.isTrigger) continue;

                const ComponentTransform* triggerTransform = level.transforms.Get(trigger.ownerID);
                if (triggerTransform == nullptr) [[unlikely]] continue;

                const Body triggerBody = MakeBody(*triggerTransform, trigger);
                if (!gatherNearby(*triggerTransform, triggerBody, false)) continue;

                for (const ID otherID : nearbyEntities) {
                    if (otherID == trigger.ownerID) continue;

                    const ComponentTransform* otherTransform = level.transforms.Get(otherID);
                    if (otherTransform == nullptr) [[unlikely]] continue;

                    // Any of the other entity's colliders counts; the pair is
                    // reported per entity, and the caller dedupes (two
                    // overlapping triggers, or several colliders, find each
                    // other more than once).
                    for (const ComponentInstanceID otherInstance : level.colliders.InstancesOf(otherID)) {
                        const ComponentCollider* otherCollider = level.colliders.GetInstance(otherInstance);

                        if (!otherCollider->InPhysics()) continue;

                        Vector3 direction;
                        float depth;

                        if (!FindBodyOverlap(triggerBody, MakeBody(*otherTransform, *otherCollider), direction, depth)) continue;

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
