//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

#include <tracy/Tracy.hpp>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
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

    const __m128 XZ_MASK = _mm_castsi128_ps(_mm_setr_epi32(-1, 0, -1, 0));

    // Slope strength is authored in degrees. BuildGpuSectors() converts it
    // with DegToRad before upload and the shader uses the result as a plain
    // rise-per-unit gradient, so anything that wants to agree with what is
    // on screen has to convert the same way.
    float SlopeGradient(const float slopeStrength) {
        return slopeStrength * Constants::DegToRad;
    }

    // Axis aligned bounds of the sector in map XY. Slope offsets are measured
    // from an edge of this rectangle, so it has to match GetSectorBounds() in
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

    // Bottom and top at each end of the wall, so a span can follow a sloped
    // floor or ceiling instead of being a flat box.
    struct WallSpan {
        float bottomStart = 0.0f;
        float bottomEnd = 0.0f;
        float topStart = 0.0f;
        float topEnd = 0.0f;
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

    std::vector<WallSpan> BuildWallSpans(
        const SectorSample& frontSector,
        const SectorSample& backSector,
        const WallSamplePoints& points
    ) {
        std::vector<HeightSample> heights;

        AddSectorHeights(frontSector, points, heights);
        AddSectorHeights(backSector, points, heights);

        std::ranges::sort(
            heights,
            [](const HeightSample& a, const HeightSample& b) { return a.middle < b.middle; }
        );

        // Two planes are only the same plane if they agree along the whole
        // wall - equal in the middle but diverging at the ends is a real gap.
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

        std::vector<WallSpan> spans;

        for (size_t i = 0; i + 1 < heights.size(); ++i) {
            const HeightSample& bottom = heights[i];
            const HeightSample& top = heights[i + 1];

            const SpanProbe probe = PickThickestProbe(bottom, top, points);

            if (probe.top - probe.bottom <= MIN_WALL_HEIGHT) continue;

            const float sampleHeight = (probe.bottom + probe.top) * 0.5f;

            const bool frontOpen = IsSectorOpenAtHeight(frontSector, probe.point, sampleHeight);
            const bool backOpen = IsSectorOpenAtHeight(backSector, probe.point, sampleHeight);

            if (frontOpen == backOpen) continue;

            if (!spans.empty() &&
                std::abs(spans.back().topStart - bottom.start) <= MIN_WALL_HEIGHT &&
                std::abs(spans.back().topEnd - bottom.end) <= MIN_WALL_HEIGHT) {
                spans.back().topStart = top.start;
                spans.back().topEnd = top.end;
            }
            else spans.push_back({bottom.start, bottom.end, top.start, top.end});
        }

        return spans;
    }

    int FindBestSectorFloor(
        const Sector& sector,
        const SectorBounds& bounds,
        const Vector2& point,
        const float feetHeight,
        const float headHeight
    ) {
        const float bodyHeight = headHeight - feetHeight;
        const float centreHeight = (feetHeight + headHeight) * 0.5f;

        int bestFloorIndex = -1;
        float bestOverlap = -1.0f;
        float bestCorrection = std::numeric_limits<float>::max();
        bool bestCentreInside = false;

        for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
            const SectorFloor& floor = sector.floors[floorIndex];

            const float floorHeight = GetSurfaceHeight(floor.floor, bounds, point);
            const float ceilingHeight = GetSurfaceHeight(floor.ceiling, bounds, point);

            if (ceilingHeight - floorHeight + Constants::Epsilon < bodyHeight) continue;

            const float overlap =
                std::max(0.0f, std::min(headHeight, ceilingHeight) - std::max(feetHeight, floorHeight));

            float correction = 0.0f;

            if (feetHeight < floorHeight) correction = floorHeight - feetHeight;
            else if (headHeight > ceilingHeight) correction = ceilingHeight - headHeight;

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

        return bestFloorIndex;
    }

    bool SphereIntersectsWallSpan(
        const Wall &wall,
        const WallSpan &span,
        const float radius,
        const float stepSize,
        Vector3 &sphereCentre,
        ComponentTransform &transform,
        ComponentRigidbody &rigidbody
    ) {
        // Conservative vertical extent: the span is a trapezoid now, so the
        // broadphase box has to cover the taller of the two ends.
        const float spanMinHeight = std::min(span.bottomStart, span.bottomEnd);
        const float spanMaxHeight = std::max(span.topStart, span.topEnd);

        const float minX = std::min(wall.start.x, wall.end.x) - radius;
        const float maxX = std::max(wall.start.x, wall.end.x) + radius;
        const float minY = spanMinHeight - radius;
        const float maxY = spanMaxHeight + radius;
        const float minZ = std::min(wall.start.y, wall.end.y) - radius;
        const float maxZ = std::max(wall.start.y, wall.end.y) + radius;

        const __m128 aabbMin = _mm_set_ps(0.0f, minZ, minY, minX);
        const __m128 aabbMax = _mm_set_ps(0.0f, maxZ, maxY, maxX);
        const __m128 belowMin = _mm_cmplt_ps(sphereCentre.reg, aabbMin);
        const __m128 aboveMax = _mm_cmpgt_ps(sphereCentre.reg, aabbMax);

        if ((_mm_movemask_ps(_mm_or_ps(belowMin, aboveMax)) & 0x7) != 0) return false;

        const float wallLengthSq = wall.vector.x * wall.vector.x + wall.vector.y * wall.vector.y;
        if (wallLengthSq <= Constants::Epsilon) return false;

        const float toSphereX = sphereCentre.x - wall.start.x;
        const float toSphereZ = sphereCentre.z - wall.start.y;
        const float t = std::clamp(
            (toSphereX * wall.vector.x + toSphereZ * wall.vector.y) *
                _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(wallLengthSq))),
            0.0f,
            1.0f
        );

        // The span's real extent at the closest point. Crossing slopes could
        // invert it, so keep the top pinned at or above the bottom - clamp()
        // with a reversed range is undefined.
        const float spanBottom = span.bottomStart + (span.bottomEnd - span.bottomStart) * t;
        const float spanTop =
            std::max(span.topStart + (span.topEnd - span.topStart) * t, spanBottom);

        const float closestX = wall.start.x + wall.vector.x * t;
        const float closestY = std::clamp(sphereCentre.y, spanBottom, spanTop);
        const float closestZ = wall.start.y + wall.vector.y * t;

        const __m128 closestPoint = _mm_set_ps(0.0f, closestZ, closestY, closestX);
        const __m128 delta = _mm_sub_ps(sphereCentre.reg, closestPoint);
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
            (sphereCentre.x - wall.start.x) * wall.normal.x + (sphereCentre.z - wall.start.y) * wall.normal.y;

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

        const float feetHeight = transform.position.y;
        const float stepHeight = spanTop - feetHeight;

        const bool wallStartsAtFeet = spanBottom <= feetHeight + GROUND_CONTACT_SLOP;

        const bool canStep =
                stepSize > 0.0f &&
                wallStartsAtFeet &&
                stepHeight > Constants::Epsilon &&
                stepHeight <= stepSize + Constants::Epsilon &&
                velocityIntoWall < -Constants::Epsilon;

        if (canStep) {
            const float verticalCorrection = stepHeight + PENETRATION_SLOP;

            transform.AddPosition({0.0f, verticalCorrection, 0.0f});

            sphereCentre.reg = _mm_add_ps(sphereCentre.reg,_mm_set_ps(0.0f, 0.0f, verticalCorrection,0.0f));

            if (rigidbody.velocity.y < 0.0f) rigidbody.velocity.y = 0.0f;

            rigidbody.isGrounded = true;
            rigidbody.groundNormal = {0.0f, 1.0f, 0.0f};

            return true;
        }

        const float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);

        const __m128 correction = _mm_mul_ps(collisionNormal, _mm_set1_ps(correctedPenetration));

        transform.AddPosition(Vector3(correction));
        sphereCentre.reg = _mm_add_ps(sphereCentre.reg, correction);

        if (velocityIntoWall < 0.0f)
            rigidbody.velocity.reg = _mm_sub_ps(rigidbody.velocity.reg, _mm_mul_ps(collisionNormal,_mm_set1_ps(velocityIntoWall))
            );

        return true;
    }
}

namespace PhysicsSystem {
    void Run(Level& level) {
        ZoneScopedN("PhysicsSystem::Run");

        for (ComponentRigidbody& rigidbody : level.rigidbodies.components) {
            rigidbody.isGrounded = false;
            rigidbody.groundNormal = {};
        }

        ColliderStorage& colliders = level.colliders;

        SectorBoundsCache boundsCache;

        std::vector<std::vector<WallSpan>> wallSpans(level.walls.size());

        {
            ZoneScopedN("Build wall collision spans");

            for (size_t wallIndex = 0; wallIndex < level.walls.size(); ++wallIndex) {
                const Wall& wall = level.walls[wallIndex];

                const Sector* frontSectorPtr = MapQueries::GetSectorByID(level, wall.frontSector);
                const Sector* backSectorPtr = MapQueries::GetSectorByID(level, wall.backSector);

                if (frontSectorPtr == backSectorPtr) backSectorPtr = nullptr;

                const SectorSample frontSector{frontSectorPtr, boundsCache.Get(frontSectorPtr)};
                const SectorSample backSector{backSectorPtr, boundsCache.Get(backSectorPtr)};

                const WallSamplePoints points{
                    wall.start,
                    Vector2{
                        (wall.start.x + wall.end.x) * 0.5f,
                        (wall.start.y + wall.end.y) * 0.5f
                    },
                    wall.end
                };

                wallSpans[wallIndex] = BuildWallSpans(frontSector, backSector, points);

                if (wallSpans[wallIndex].empty() &&
                    frontSectorPtr == nullptr &&
                    backSectorPtr == nullptr) {
                    wallSpans[wallIndex].push_back({0.0f, 0.0f, 32.0f, 32.0f});
                }
            }
        }

        std::vector<ID> allEntities;
        std::vector<const Wall*> allWalls;
        allEntities.reserve(64);
        allWalls.reserve(64);

        for (ComponentCollider& selfCollider : colliders.ActiveSpheres()) {
            ZoneScopedN("Sphere iteration");

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

            const float selfRadius = std::max(0.0f, selfCollider.scale.x);

            Vector3 selfPosition = {
                selfTransform->position.x,
                selfTransform->position.y + selfRadius,
                selfTransform->position.z
            };

            {
                ZoneScopedN("Entity narrowphase");

                for (const ID otherID : allEntities) {
                    if (otherID == selfCollider.ownerID) continue;

                    ComponentCollider* otherCollider = level.colliders.Get(otherID);

                    if (otherCollider == nullptr ||
                        !otherCollider->isActive ||
                        otherCollider->isTrigger ||
                        otherCollider->type != COLLIDERTYPE_SPHERE) continue;


                    ComponentTransform* otherTransform = level.transforms.Get(otherID);
                    if (otherTransform == nullptr) [[unlikely]] continue;

                    const ComponentRigidbody* otherRigidbody = level.rigidbodies.Get(otherID);
                    const bool otherIsStatic = otherRigidbody == nullptr || otherRigidbody->isStatic;

                    if (!otherIsStatic && selfCollider.ownerID > otherID) continue;

                    const float otherRadius = std::max(0.0f, otherCollider->scale.x);
                    const float radiusSum = selfRadius + otherRadius;

                    const Vector3 otherPosition = {
                        otherTransform->position.x,
                        otherTransform->position.y + otherRadius,
                        otherTransform->position.z
                    };

                    const __m128 delta = _mm_sub_ps(selfPosition.reg, otherPosition.reg);
                    const __m128 distanceSqReg = dot3_ss(delta, delta);
                    const float distanceSq = _mm_cvtss_f32(distanceSqReg);

                    if (distanceSq >= radiusSum * radiusSum) continue;

                    const __m128 safeDistanceSq = _mm_max_ss(distanceSqReg, _mm_set_ss(Constants::Epsilon));

                    __m128 inverseDistance = rsqrt_nr_ss(safeDistanceSq);
                    inverseDistance = TILKY_MM_SHUFFLE_PS(inverseDistance, inverseDistance, _MM_SHUFFLE(0, 0, 0, 0));

                    const float distance = distanceSq * _mm_cvtss_f32(inverseDistance);
                    const float penetration = radiusSum - distance;

                    if (penetration <= Constants::Epsilon) continue;

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


                    selfPosition = {
                        selfTransform->position.x,
                        selfTransform->position.y + selfRadius,
                        selfTransform->position.z
                    };
                }
            }

            {
                ZoneScopedN("Wall narrowphase");

                for (const Wall* wall : allWalls) {
                    if (wall == nullptr || level.walls.empty()) continue;

                    const std::ptrdiff_t wallIndex = wall - level.walls.data();

                    if (wallIndex < 0 || wallIndex >= static_cast<std::ptrdiff_t>(wallSpans.size())) continue;

                    for (const WallSpan &span: wallSpans[wallIndex])
                        SphereIntersectsWallSpan(
                            *wall,
                            span,
                            selfRadius,
                            selfCollider.stepSize,
                            selfPosition,
                            *selfTransform,
                            *selfRigidbody
                        );
                }
            }

            {
                ZoneScopedN("Sector room clamp");

                const Vector2 feetPoint = {
                    selfTransform->position.x,
                    selfTransform->position.z
                };

                if (sector.vertices.empty() || !Geometry::IsPointInPolygon(sector.vertices, feetPoint)) continue;

                const SectorBounds& sectorBounds = boundsCache.Get(&sector);

                const auto getSlopeAxis = [](const SlopeDirection direction) -> Vector2 {
                    switch (direction) {
                        case PLUS_X: return {1.0f, 0.0f};
                        case MINUS_X: return {-1.0f, 0.0f};
                        case PLUS_Z: return {0.0f, 1.0f};
                        case MINUS_Z: return {0.0f, -1.0f};
                    }
                    return {1.0f, 0.0f};
                };

                const auto getSurfaceHeight = [&](const SectorSurface &surface) -> float {
                    return GetSurfaceHeight(surface, sectorBounds, feetPoint);
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

                const float bodyHeight = std::max(std::abs(selfTransform->scale.y), selfRadius * 2.0f);

                const float feetHeight = selfTransform->position.y;
                const float headHeight = feetHeight + bodyHeight;

                const int floorIndex = FindBestSectorFloor(
                    sector,
                    sectorBounds,
                    feetPoint,
                    feetHeight,
                    headHeight
                );

                if (floorIndex < 0) {
                    selfTransform->relativeHeight = selfTransform->position.y;
                    continue;
                }

                const SectorFloor &floor = sector.floors[floorIndex];

                const float floorHeight = getSurfaceHeight(floor.floor);
                const float ceilingHeight = getSurfaceHeight(floor.ceiling);

                const Vector3 floorNormal = getFloorNormal(floor.floor);
                const Vector3 ceilingBottomNormal = getCeilingNormal(floor.ceiling);

                float correctedFeetHeight = selfTransform->position.y;
                float correctedHeadHeight = correctedFeetHeight + bodyHeight;

                if (correctedFeetHeight < floorHeight) {
                    const float correction = floorHeight - correctedFeetHeight;

                    selfTransform->AddPosition({0.0f, correction, 0.0f});

                    correctedFeetHeight += correction;
                    correctedHeadHeight += correction;

                    const float velocityIntoFloor = Vector3Math::Dot(selfRigidbody->velocity, floorNormal);

                    if (velocityIntoFloor < 0.0f)
                        selfRigidbody->velocity = selfRigidbody->velocity - floorNormal * velocityIntoFloor;
                }

                if (correctedHeadHeight > ceilingHeight) {
                    const float correction = ceilingHeight - correctedHeadHeight;

                    selfTransform->AddPosition({0.0f, correction, 0.0f});

                    correctedFeetHeight += correction;
                    correctedHeadHeight += correction;

                    const float velocityIntoCeiling = Vector3Math::Dot(selfRigidbody->velocity, ceilingBottomNormal);

                    if (velocityIntoCeiling < 0.0f)
                        selfRigidbody->velocity = selfRigidbody->velocity - ceilingBottomNormal * velocityIntoCeiling;
                }

                const float groundDistance = correctedFeetHeight - floorHeight;

                const float groundSeparatingSpeed = Vector3Math::Dot(selfRigidbody->velocity, floorNormal);

                if (groundDistance <= GROUND_CONTACT_SLOP && groundSeparatingSpeed <= MAX_GROUND_SEPARATING_SPEED) {
                    selfRigidbody->isGrounded = true;
                    selfRigidbody->groundNormal = floorNormal;

                    if (groundSeparatingSpeed < 0.0f)
                        selfRigidbody->velocity = selfRigidbody->velocity - floorNormal * groundSeparatingSpeed;
                }

                selfTransform->relativeHeight = correctedFeetHeight - floorHeight;
            }
        }

        for (ComponentCollider& selfCollider : colliders.ActiveBoxes()) {
            ZoneScopedN("AABB");
            // TODO TILKY_TODO AABB Collision
            /* use boxCollider.scale.x/y/z for the AABB size
               selfTransform.position for the position
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
    }
}