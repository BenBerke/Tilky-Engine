//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

#include "Headers/Math/Constants.hpp"
#include <immintrin.h>
#include <unordered_set>

static inline __m128 rcp_nr_ss(const __m128 x) {
    __m128 r = _mm_rcp_ss(x);
    // r = r * (2 - x * r)
    r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(2.0f), _mm_mul_ss(x, r)));
    return r;
}

static inline __m128 rsqrt_nr_ss(const __m128 x) {
    __m128 r = _mm_rsqrt_ss(x);
    // r = r * (1.5 - 0.5 * x * r * r)
    __m128 half = _mm_set_ss(0.5f);
    __m128 three_halves = _mm_set_ss(1.5f);
    r = _mm_mul_ss(r, _mm_sub_ss(three_halves, _mm_mul_ss(half, _mm_mul_ss(x, _mm_mul_ss(r, r)))));
    return r;
}

namespace {
    // ClosestPointOnSegment removed — unused, was polluting the I-cache.

    Vector3 ClosestPointOnTriangle(const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c) {
        __m128 ab = _mm_sub_ps(b.reg, a.reg);
        __m128 ac = _mm_sub_ps(c.reg, a.reg);
        __m128 ap = _mm_sub_ps(p.reg, a.reg);

        const float d1 = _mm_cvtss_f32(_mm_dp_ps(ab, ap, 0x71));
        const float d2 = _mm_cvtss_f32(_mm_dp_ps(ac, ap, 0x71));
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        __m128 bp = _mm_sub_ps(p.reg, b.reg);
        const float d3 = _mm_cvtss_f32(_mm_dp_ps(ab, bp, 0x71));
        const float d4 = _mm_cvtss_f32(_mm_dp_ps(ac, bp, 0x71));
        if (d3 >= 0.0f && d4 <= d3) return b;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            // v = d1 / (d1 - d3)  →  rcp_nr
            const float denom = d1 - d3;
            const float v = d1 * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            return Vector3(_mm_add_ps(a.reg, _mm_mul_ps(_mm_set1_ps(v), ab)));
        }

        __m128 cp = _mm_sub_ps(p.reg, c.reg);
        const float d5 = _mm_cvtss_f32(_mm_dp_ps(ab, cp, 0x71));
        const float d6 = _mm_cvtss_f32(_mm_dp_ps(ac, cp, 0x71));
        if (d6 >= 0.0f && d5 <= d6) return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            // w = d2 / (d2 - d6)  →  rcp_nr
            const float denom = d2 - d6;
            const float w = d2 * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            return Vector3(_mm_add_ps(a.reg, _mm_mul_ps(_mm_set1_ps(w), ac)));
        }

        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float num = d4 - d3;
            float denom = num + (d5 - d6);
            float w = num * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            __m128 bc = _mm_sub_ps(c.reg, b.reg);
            return Vector3(_mm_add_ps(b.reg, _mm_mul_ps(_mm_set1_ps(w), bc)));
        }

        const float denomInv = _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(va + vb + vc)));
        const float v = vb * denomInv;
        const float w = vc * denomInv;

        __m128 vReg = _mm_set1_ps(v);
        __m128 wReg = _mm_set1_ps(w);
        return Vector3(_mm_add_ps(a.reg, _mm_add_ps(_mm_mul_ps(ab, vReg), _mm_mul_ps(ac, wReg))));
    }
}

namespace PhysicsSystem {
    void Run(Level &level) {
        std::vector<Entity *> allEntities;
        std::vector<const Wall *> allWalls;
        allEntities.reserve(64);
        allWalls.reserve(64);

        for (ComponentCollider &selfCollider: level.colliders.components) {
            if (!selfCollider.isActive || selfCollider.isTrigger) [[unlikely]] continue;

            ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);
            if (selfTransform == nullptr) [[unlikely]] continue;

            ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);
            if (selfRb == nullptr || selfRb->isStatic) continue;

            if (selfTransform->sectorIndex < 0 ||
                selfTransform->sectorIndex >= static_cast<int>(level.sectors.size()))
                continue;

            Sector &sector = level.sectors[selfTransform->sectorIndex];
            Entity *selfEntity = level.GetEntity(selfCollider.ownerID);
            if (selfEntity == nullptr) [[unlikely]] continue;

            // Reuse allocations — clear is O(N) element destruction but no free/malloc.
            allEntities.clear();
            allWalls.clear();

            allEntities.insert(allEntities.end(),
                               sector.entitiesInside.begin(), sector.entitiesInside.end());
            allWalls.insert(allWalls.end(),
                            sector.walls.begin(), sector.walls.end());

            for (const Sector *nSector: sector.neighbors) {
                if (nSector == nullptr) [[unlikely]] continue;
                allEntities.insert(allEntities.end(),
                                   nSector->entitiesInside.begin(), nSector->entitiesInside.end());
                allWalls.insert(allWalls.end(),
                                nSector->walls.begin(), nSector->walls.end());
            }

            // O(1) duplicate check vs O(N) linear scan.
            std::unordered_set<ID> processedOtherIDs;
            processedOtherIDs.reserve(allEntities.size());

            Vector3 selfPos = {
                selfTransform->position.x,
                selfTransform->position.y,
                selfTransform->position.z + (selfTransform->scale.y * 0.5f)
            };

            // ── Entity vs Entity ─────────────────────────────────────────────────
            for (const Entity *otherEntity: allEntities) {
                if (otherEntity == nullptr) [[unlikely]] continue;
                if (otherEntity->id == selfEntity->id) continue;

                auto insertPos = std::lower_bound(processedOtherIDs.begin(), processedOtherIDs.end(), otherEntity->id);
                if (insertPos != processedOtherIDs.end() && *insertPos == otherEntity->id) continue;
                processedOtherIDs.insert(insertPos, otherEntity->id);

                ComponentCollider *otherCollider = level.colliders.Get(otherEntity->id);
                if (otherCollider == nullptr ||
                    !otherCollider->isActive ||
                    otherCollider->isTrigger)
                    continue;

                ComponentTransform *otherTransform = level.transforms.Get(otherEntity->id);
                if (otherTransform == nullptr) [[unlikely]] continue;

                const ComponentRigidbody *otherRb = level.rigidbodies.Get(otherEntity->id);
                const bool otherIsStatic = (otherRb == nullptr || otherRb->isStatic);

                if (!otherIsStatic && selfEntity->id > otherEntity->id) continue;

                if (selfCollider.type == COLLIDERTYPE_SPHERE &&
                    otherCollider->type == COLLIDERTYPE_SPHERE) [[likely]] {
                    float selfRadius = std::max(0.0f, selfCollider.scale.x);
                    float otherRadius = std::max(0.0f, otherCollider->scale.x);
                    float radiusSum = selfRadius + otherRadius;

                    Vector3 otherPos = {
                        otherTransform->position.x,
                        otherTransform->position.y,
                        otherTransform->position.z + (otherTransform->scale.y * 0.5f)
                    };

                    __m128 deltaPos = _mm_sub_ps(selfPos.reg, otherPos.reg);
                    __m128 distSqrReg = _mm_dp_ps(deltaPos, deltaPos, 0x71);
                    float distSqr = _mm_cvtss_f32(distSqrReg);

                    if (distSqr >= radiusSum * radiusSum) continue;

                    __m128 safeDistSqr = _mm_max_ss(distSqrReg, _mm_set_ss(Constants::Epsilon));
                    __m128 invDistReg = rsqrt_nr_ss(safeDistSqr);
                    invDistReg = _mm_shuffle_ps(invDistReg, invDistReg, _MM_SHUFFLE(0,0,0,0));
                    float distance = distSqr * _mm_cvtss_f32(invDistReg);

                    float penetration = radiusSum - distance;
                    if (penetration <= Constants::Epsilon) continue;

                    // Branchless push direction: blend normalised delta vs. fixed fallback.
                    __m128 calculatedDir = _mm_mul_ps(deltaPos, invDistReg);
                    __m128 fallbackDir = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
                    __m128 isSafe = _mm_cmpgt_ss(distSqrReg, _mm_set_ss(Constants::Epsilon));
                    isSafe = _mm_shuffle_ps(isSafe, isSafe, _MM_SHUFFLE(0,0,0,0));
                    __m128 pushDirReg = _mm_blendv_ps(fallbackDir, calculatedDir, isSafe);
                    Vector3 pushDirection(pushDirReg);

                    constexpr float PENETRATION_SLOP = 0.001f;
                    float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);

                    __m128 corrPenReg = _mm_set1_ps(correctedPenetration);

                    float selfPush = otherIsStatic ? 1.0f : 0.5f;
                    Vector3 selfCorrection(
                        _mm_mul_ps(pushDirReg, _mm_mul_ps(corrPenReg, _mm_set1_ps(selfPush))));
                    selfTransform->AddPosition(selfCorrection);

                    if (!otherIsStatic) {
                        float otherPush = 0.5f;
                        Vector3 otherCorrection(
                            _mm_mul_ps(pushDirReg, _mm_mul_ps(corrPenReg, _mm_set1_ps(-otherPush))));
                        otherTransform->AddPosition(otherCorrection);
                    }

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->position.z
                    };
                }
            }

            // ── Entity vs Walls ──────────────────────────────────────────────────
            for (const Wall *wall: allWalls) {
                if (wall == nullptr) continue;

                for (int i = 0; i < wall->quad3DCount; ++i) {
                    const auto &quad = wall->quads3D[i];

                    __m128 edge1 = _mm_sub_ps(quad[1].reg, quad[0].reg);
                    __m128 edge2 = _mm_sub_ps(quad[2].reg, quad[0].reg);

                    __m128 e1_yzx = _mm_shuffle_ps(edge1, edge1, _MM_SHUFFLE(3, 0, 2, 1));
                    __m128 e2_zxy = _mm_shuffle_ps(edge2, edge2, _MM_SHUFFLE(3, 1, 0, 2));
                    __m128 e1_zxy = _mm_shuffle_ps(edge1, edge1, _MM_SHUFFLE(3, 1, 0, 2));
                    __m128 e2_yzx = _mm_shuffle_ps(edge2, edge2, _MM_SHUFFLE(3, 0, 2, 1));
                    __m128 crossReg = _mm_sub_ps(
                        _mm_mul_ps(e1_yzx, e2_zxy),
                        _mm_mul_ps(e1_zxy, e2_yzx));

                    __m128 quadNormSq = _mm_dp_ps(crossReg, crossReg, 0x71);
                    __m128 quadNormInvLen = rsqrt_nr_ss(quadNormSq);
                    quadNormInvLen = _mm_shuffle_ps(quadNormInvLen, quadNormInvLen, _MM_SHUFFLE(0,0,0,0));
                    __m128 quadNormalReg = _mm_mul_ps(crossReg, quadNormInvLen);

                    float quadNormalZ = _mm_cvtss_f32(
                        _mm_shuffle_ps(quadNormalReg, quadNormalReg, _MM_SHUFFLE(2,2,2,2)));
                    if (std::abs(quadNormalZ) > 0.75f) continue;

                    Vector3 closestT1 = ClosestPointOnTriangle(selfPos, quad[0], quad[1], quad[2]);
                    Vector3 closestT2 = ClosestPointOnTriangle(selfPos, quad[0], quad[2], quad[3]);

                    __m128 diff1 = _mm_sub_ps(selfPos.reg, closestT1.reg);
                    __m128 diff2 = _mm_sub_ps(selfPos.reg, closestT2.reg);
                    float distSq1 = _mm_cvtss_f32(_mm_dp_ps(diff1, diff1, 0x71));
                    float distSq2 = _mm_cvtss_f32(_mm_dp_ps(diff2, diff2, 0x71));

                    const bool use1 = distSq1 < distSq2;
                    float minDistSq = use1 ? distSq1 : distSq2;
                    __m128 bestDiff = use1 ? diff1 : diff2;

                    float radius = selfCollider.scale.x;
                    if (minDistSq > radius * radius) continue;
                    if (selfRb->isStatic) break;

                    __m128 minDistSqReg = _mm_set_ss(minDistSq);
                    __m128 invDistanceReg = rsqrt_nr_ss(minDistSqReg);
                    float distance = minDistSq * _mm_cvtss_f32(invDistanceReg);
                    float penetrationDepth = radius - distance;

                    const __m128 xyMask = _mm_castsi128_ps(_mm_setr_epi32(-1, -1, 0, 0));

                    // Branchless normal selection: blend computed normal vs. quad fallback.
                    __m128 invD = _mm_shuffle_ps(invDistanceReg, invDistanceReg, _MM_SHUFFLE(0,0,0,0));
                    __m128 calculatedNormal = _mm_and_ps(_mm_mul_ps(bestDiff, invD), xyMask);
                    __m128 fallbackNormal = _mm_and_ps(quadNormalReg, xyMask);
                    __m128 isSafe = _mm_cmpgt_ss(_mm_set_ss(distance), _mm_set_ss(0.0001f));
                    isSafe = _mm_shuffle_ps(isSafe, isSafe, _MM_SHUFFLE(0,0,0,0));
                    __m128 collisionNormalReg = _mm_blendv_ps(fallbackNormal, calculatedNormal, isSafe);

                    // Re-normalise horizontal component.
                    __m128 horizLenSqReg = _mm_dp_ps(collisionNormalReg, collisionNormalReg, 0x31);
                    float horizontalLenSq = _mm_cvtss_f32(horizLenSqReg);
                    if (horizontalLenSq <= 0.000001f) continue;

                    __m128 invHorizLen = rsqrt_nr_ss(horizLenSqReg);
                    invHorizLen = _mm_shuffle_ps(invHorizLen, invHorizLen, _MM_SHUFFLE(0,0,0,0));
                    collisionNormalReg = _mm_mul_ps(collisionNormalReg, invHorizLen);

                    selfTransform->AddPosition(Vector3(collisionNormalReg) * penetrationDepth);

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->position.z
                    };
                }
            }

            // ── Floor / ceiling clamping ─────────────────────────────────────────
            {
                const Vector2 feetPoint2D = {
                    selfTransform->position.x,
                    selfTransform->position.y
                };
                if (!Geometry::IsPointInPolygon(sector.vertices, feetPoint2D)) continue;

                float storeyHeight = sector.ceilingHeight - sector.floorHeight;
                float currentFloorWorld = sector.floorHeight
                                          + storeyHeight * static_cast<float>(selfTransform->floor);
                float currentCeilingWorld = currentFloorWorld + storeyHeight;
                float feetWorld = selfTransform->position.z;
                float headWorld = feetWorld + selfTransform->scale.y;

                if (feetWorld < currentFloorWorld) {
                    selfTransform->AddPosition({0.0f, 0.0f, currentFloorWorld - feetWorld});
                    if (selfRb->velocity.z < 0.0f) selfRb->velocity.z = 0.0f;
                }
                if (headWorld > currentCeilingWorld) {
                    selfTransform->AddPosition({0.0f, 0.0f, currentCeilingWorld - headWorld});
                    if (selfRb->velocity.z > 0.0f) selfRb->velocity.z = 0.0f;
                }

                selfTransform->relativeHeight = selfTransform->position.z - currentFloorWorld;
            }
        }
    }
}
