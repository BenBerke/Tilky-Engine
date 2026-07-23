//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
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

    struct WallSpan {
        float bottom;
        float top;
    };

    bool IsSectorOpenAtHeight(const Sector& sector, const float height) {
        for (const SectorFloor& floor : sector.floors) {
            if (height > floor.floor.height && height < floor.ceiling.height) return true;
        }

        return false;
    }

    void AddSectorHeights(const Sector& sector, std::vector<float>& heights) {
        for (const SectorFloor& floor : sector.floors) {
            heights.push_back(floor.floor.height);
            heights.push_back(floor.ceiling.height);
        }
    }

    std::vector<WallSpan> BuildWallSpans(const Sector* frontSector, const Sector* backSector) {
        std::vector<float> heights;

        if (frontSector != nullptr) AddSectorHeights(*frontSector, heights);
        if (backSector != nullptr) AddSectorHeights(*backSector, heights);

        std::ranges::sort(heights);

        heights.erase(
            std::unique(
                heights.begin(),
                heights.end(),
                [](const float a, const float b) {
                    return std::abs(a - b) <= MIN_WALL_HEIGHT;
                }
            ),
            heights.end()
        );

        std::vector<WallSpan> spans;

        for (size_t i = 0; i + 1 < heights.size(); ++i) {
            const float bottom = heights[i];
            const float top = heights[i + 1];

            if (top - bottom <= MIN_WALL_HEIGHT) continue;

            const float sampleHeight = (bottom + top) * 0.5f;
            const bool frontOpen = frontSector != nullptr && IsSectorOpenAtHeight(*frontSector, sampleHeight);
            const bool backOpen = backSector != nullptr && IsSectorOpenAtHeight(*backSector, sampleHeight);

            if (frontOpen == backOpen) continue;

            if (!spans.empty() && std::abs(spans.back().top - bottom) <= MIN_WALL_HEIGHT) spans.back().top = top;
            else spans.push_back({bottom, top});
        }

        return spans;
    }

    int FindBestSectorFloor(
        const Sector& sector,
        const float feetHeight,
        const float headHeight
    ) {
        const float bodyHeight = headHeight - feetHeight;
        const float centreHeight = (feetHeight + headHeight) * 0.5f;

        int bestFloorIndex = -1;
        float bestOverlap = -1.0f;
        float bestCorrection = std::numeric_limits<float>::max();

        for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
            const SectorFloor& floor = sector.floors[floorIndex];
            const float floorHeight = floor.floor.height;
            const float ceilingHeight = floor.ceiling.height;

            if (ceilingHeight - floorHeight + Constants::Epsilon < bodyHeight) continue;

            const float overlap =
                std::max(0.0f, std::min(headHeight, ceilingHeight) - std::max(feetHeight, floorHeight));

            float correction = 0.0f;

            if (feetHeight < floorHeight) correction = floorHeight - feetHeight;
            else if (headHeight > ceilingHeight) correction = ceilingHeight - headHeight;

            const float correctionMagnitude = std::abs(correction);
            const bool centreInside = centreHeight >= floorHeight && centreHeight <= ceilingHeight;
            const bool bestCentreInside =
                bestFloorIndex >= 0 &&
                centreHeight >= sector.floors[bestFloorIndex].floor.height &&
                centreHeight <= sector.floors[bestFloorIndex].ceiling.height;

            if ((centreInside && !bestCentreInside) ||
                (centreInside == bestCentreInside && overlap > bestOverlap + Constants::Epsilon) ||
                (centreInside == bestCentreInside &&
                 std::abs(overlap - bestOverlap) <= Constants::Epsilon &&
                 correctionMagnitude < bestCorrection)) {
                bestFloorIndex = floorIndex;
                bestOverlap = overlap;
                bestCorrection = correctionMagnitude;
            }
        }

        return bestFloorIndex;
    }

    bool SphereIntersectsWallSpan(
        const Wall& wall,
        const WallSpan& span,
        const float radius,
        Vector3& sphereCentre,
        ComponentTransform& transform,
        ComponentRigidbody& rigidbody
    ) {
        const float minX = std::min(wall.start.x, wall.end.x) - radius;
        const float maxX = std::max(wall.start.x, wall.end.x) + radius;
        const float minY = span.bottom - radius;
        const float maxY = span.top + radius;
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

        const float closestX = wall.start.x + wall.vector.x * t;
        const float closestY = std::clamp(sphereCentre.y, span.bottom, span.top);
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
            (sphereCentre.x - wall.start.x) * wall.normal.x +
            (sphereCentre.z - wall.start.y) * wall.normal.y;

        const float normalDirection = planeDistance < 0.0f ? -1.0f : 1.0f;
        const __m128 fallbackNormal = _mm_set_ps(
            0.0f,
            wall.normal.y * normalDirection,
            0.0f,
            wall.normal.x * normalDirection
        );

        const __m128 horizontalLengthSqReg = dot_xz_ss(calculatedNormal, calculatedNormal);
        const __m128 hasCalculatedNormal =
            _mm_cmpgt_ss(horizontalLengthSqReg, _mm_set_ss(Constants::Epsilon));
        const __m128 hasCalculatedNormalBroad =
            TILKY_MM_SHUFFLE_PS(hasCalculatedNormal, hasCalculatedNormal, _MM_SHUFFLE(0, 0, 0, 0));

        __m128 collisionNormal = blend_ps(fallbackNormal, calculatedNormal, hasCalculatedNormalBroad);
        const __m128 normalLengthSqReg = dot_xz_ss(collisionNormal, collisionNormal);

        if (_mm_cvtss_f32(normalLengthSqReg) <= Constants::Epsilon) return false;

        __m128 inverseNormalLength = rsqrt_nr_ss(normalLengthSqReg);
        inverseNormalLength =
            TILKY_MM_SHUFFLE_PS(inverseNormalLength, inverseNormalLength, _MM_SHUFFLE(0, 0, 0, 0));
        collisionNormal = _mm_mul_ps(collisionNormal, inverseNormalLength);

        const float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);
        const __m128 correction = _mm_mul_ps(collisionNormal, _mm_set1_ps(correctedPenetration));

        transform.AddPosition(Vector3(correction));
        sphereCentre.reg = _mm_add_ps(sphereCentre.reg, correction);

        const float velocityIntoWall =
            _mm_cvtss_f32(dot3_ss(rigidbody.velocity.reg, collisionNormal));

        if (velocityIntoWall < 0.0f) {
            rigidbody.velocity.reg = _mm_sub_ps(
                rigidbody.velocity.reg,
                _mm_mul_ps(collisionNormal, _mm_set1_ps(velocityIntoWall))
            );
        }

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

        std::vector<std::vector<WallSpan>> wallSpans(level.walls.size());

        {
            ZoneScopedN("Build wall collision spans");

            for (size_t wallIndex = 0; wallIndex < level.walls.size(); ++wallIndex) {
                const Wall& wall = level.walls[wallIndex];

                const Sector* frontSector = MapQueries::GetSectorByID(level, wall.frontSector);
                const Sector* backSector = MapQueries::GetSectorByID(level, wall.backSector);

                if (frontSector == backSector) backSector = nullptr;

                wallSpans[wallIndex] = BuildWallSpans(frontSector, backSector);

                if (wallSpans[wallIndex].empty() &&
                    frontSector == nullptr &&
                    backSector == nullptr) {
                    wallSpans[wallIndex].push_back({0.0f, 32.0f});
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
                        otherCollider->type != COLLIDERTYPE_SPHERE) {
                        continue;
                    }

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

                    const __m128 safeDistanceSq =
                        _mm_max_ss(distanceSqReg, _mm_set_ss(Constants::Epsilon));

                    __m128 inverseDistance = rsqrt_nr_ss(safeDistanceSq);
                    inverseDistance =
                        TILKY_MM_SHUFFLE_PS(inverseDistance, inverseDistance, _MM_SHUFFLE(0, 0, 0, 0));

                    const float distance = distanceSq * _mm_cvtss_f32(inverseDistance);
                    const float penetration = radiusSum - distance;

                    if (penetration <= Constants::Epsilon) continue;

                    const __m128 calculatedDirection = _mm_mul_ps(delta, inverseDistance);
                    const __m128 fallbackDirection = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
                    const __m128 hasDirection =
                        _mm_cmpgt_ss(distanceSqReg, _mm_set_ss(Constants::Epsilon));
                    const __m128 hasDirectionBroad =
                        TILKY_MM_SHUFFLE_PS(hasDirection, hasDirection, _MM_SHUFFLE(0, 0, 0, 0));
                    const __m128 pushDirection =
                        blend_ps(fallbackDirection, calculatedDirection, hasDirectionBroad);

                    const float correctedPenetration =
                        std::max(0.0f, penetration - PENETRATION_SLOP);

                    const __m128 correction =
                        _mm_mul_ps(pushDirection, _mm_set1_ps(correctedPenetration));

                    const float selfPush = otherIsStatic ? 1.0f : 0.5f;

                    selfTransform->AddPosition(
                        Vector3(_mm_mul_ps(correction, _mm_set1_ps(selfPush)))
                    );

                    if (!otherIsStatic) {
                        otherTransform->AddPosition(
                            Vector3(_mm_mul_ps(correction, _mm_set1_ps(-0.5f)))
                        );
                    }

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

                    if (wallIndex < 0 ||
                        wallIndex >= static_cast<std::ptrdiff_t>(wallSpans.size())) {
                        continue;
                    }

                    for (const WallSpan& span : wallSpans[wallIndex]) {
                        SphereIntersectsWallSpan(
                            *wall,
                            span,
                            selfRadius,
                            selfPosition,
                            *selfTransform,
                            *selfRigidbody
                        );
                    }
                }
            }

            {
                ZoneScopedN("Sector room clamp");

                const Vector2 feetPoint = {
                    selfTransform->position.x,
                    selfTransform->position.z
                };

                if (!Geometry::IsPointInPolygon(sector.vertices, feetPoint)) continue;

                const float bodyHeight =
                    std::max(std::abs(selfTransform->scale.y), selfRadius * 2.0f);

                const float feetHeight = selfTransform->position.y;
                const float headHeight = feetHeight + bodyHeight;
                const int floorIndex =
                    FindBestSectorFloor(sector, feetHeight, headHeight);

                if (floorIndex < 0) {
                    selfTransform->relativeHeight = selfTransform->position.y;
                    continue;
                }

                const SectorFloor& floor = sector.floors[floorIndex];
                const float floorHeight = floor.floor.height;
                const float ceilingHeight = floor.ceiling.height;

                if (selfTransform->position.y < floorHeight) {
                    const float correction = floorHeight - selfTransform->position.y;
                    selfTransform->AddPosition({0.0f, correction, 0.0f});

                    if (selfRigidbody->velocity.y < 0.0f) selfRigidbody->velocity.y = 0.0f;
                }

                const float correctedFeetHeight = selfTransform->position.y;
                const float correctedHeadHeight = correctedFeetHeight + bodyHeight;

                if (correctedHeadHeight > ceilingHeight) {
                    const float correction = ceilingHeight - correctedHeadHeight;
                    selfTransform->AddPosition({0.0f, correction, 0.0f});

                    if (selfRigidbody->velocity.y > 0.0f) selfRigidbody->velocity.y = 0.0f;
                }

                const float groundDistance =
                    selfTransform->position.y - floorHeight;

                if (groundDistance <= GROUND_CONTACT_SLOP &&
                    selfRigidbody->velocity.y <= MAX_GROUND_SEPARATING_SPEED) {
                    selfRigidbody->isGrounded = true;
                    selfRigidbody->groundNormal = {0.0f, 1.0f, 0.0f};
                }

                selfTransform->relativeHeight =
                    selfTransform->position.y - floorHeight;
            }
        }

        for (ComponentCollider& boxCollider : colliders.ActiveBoxes()) {
            (void)boxCollider;
            // TODO
        }
    }
}