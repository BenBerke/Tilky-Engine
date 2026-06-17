//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

#include "Headers/Math/Constants.hpp"
#include <emmintrin.h> // SSE2

// ─── Newton-Raphson refined reciprocal (1 iteration) ────────────────────────
static inline __m128 rcp_nr_ss(const __m128 x) {
    __m128 r = _mm_rcp_ss(x);
    // r = r * (2 - x * r)
    r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(2.0f), _mm_mul_ss(x, r)));
    return r;
}

// ─── Newton-Raphson refined reciprocal square root (1 iteration) ─────────────
static inline __m128 rsqrt_nr_ss(const __m128 x) {
    __m128 r = _mm_rsqrt_ss(x);
    // r = r * (1.5 - 0.5 * x * r * r)
    const __m128 half         = _mm_set_ss(0.5f);
    const __m128 three_halves = _mm_set_ss(1.5f);
    r = _mm_mul_ss(r, _mm_sub_ss(three_halves,
            _mm_mul_ss(half, _mm_mul_ss(x, _mm_mul_ss(r, r)))));
    return r;
}

// ─── SSE2 helpers ────────────────────────────────────────────────────────────

// Dot product of 3 lanes (x·y·z), result in lane 0.
static inline __m128 dot3_ss(const __m128 a, const __m128 b) {
    const __m128 mul   = _mm_mul_ps(a, b);                              // [ax*bx, ay*by, az*bz, ?]
    const __m128 shuf1 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,1,1,1)); // [ay*by, ...]
    const __m128 sum1  = _mm_add_ss(mul, shuf1);                         // [ax*bx + ay*by, ...]
    const __m128 shuf2 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2,2,2,2)); // [az*bz, ...]
    return _mm_add_ss(sum1, shuf2);                                      // [ax*bx + ay*by + az*bz, ...]
}

// Dot product of 2 lanes (x·y), result in lane 0.
static inline __m128 dot2_ss(const __m128 a, const __m128 b) {
    const __m128 mul   = _mm_mul_ps(a, b);                              // [ax*bx, ay*by, ?, ?]
    const __m128 shuf1 = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(1,1,1,1)); // [ay*by, ...]
    return _mm_add_ss(mul, shuf1);                                       // [ax*bx + ay*by, ...]
}

// SSE2 replacement for _mm_blendv_ps(a, b, mask):
//   selects b lane where mask bit is 1, a lane where mask bit is 0.
static inline __m128 blend_ps(const __m128 a, const __m128 b, const __m128 mask) {
    // (b & mask) | (a & ~mask)
    return _mm_or_ps(_mm_and_ps(mask, b), _mm_andnot_ps(mask, a));
}

namespace {
    Vector3 ClosestPointOnTriangle(const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c) {
        const __m128 ab = _mm_sub_ps(b.reg, a.reg);
        const __m128 ac = _mm_sub_ps(c.reg, a.reg);
        const __m128 ap = _mm_sub_ps(p.reg, a.reg);

        const float d1 = _mm_cvtss_f32(dot3_ss(ab, ap));
        const float d2 = _mm_cvtss_f32(dot3_ss(ac, ap));
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        const __m128 bp = _mm_sub_ps(p.reg, b.reg);
        const float d3  = _mm_cvtss_f32(dot3_ss(ab, bp));
        const float d4  = _mm_cvtss_f32(dot3_ss(ac, bp));
        if (d3 >= 0.0f && d4 <= d3) return b;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            const float denom = d1 - d3;
            const float v     = d1 * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            return Vector3(_mm_add_ps(a.reg, _mm_mul_ps(_mm_set1_ps(v), ab)));
        }

        const __m128 cp = _mm_sub_ps(p.reg, c.reg);
        const float d5  = _mm_cvtss_f32(dot3_ss(ab, cp));
        const float d6  = _mm_cvtss_f32(dot3_ss(ac, cp));
        if (d6 >= 0.0f && d5 <= d6) return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            const float denom = d2 - d6;
            const float w     = d2 * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            return Vector3(_mm_add_ps(a.reg, _mm_mul_ps(_mm_set1_ps(w), ac)));
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            const float num   = d4 - d3;
            const float denom = num + (d5 - d6);
            const float w     = num * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            const __m128 bc   = _mm_sub_ps(c.reg, b.reg);
            return Vector3(_mm_add_ps(b.reg, _mm_mul_ps(_mm_set1_ps(w), bc)));
        }

        const float denomInv = _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(va + vb + vc)));
        const float v = vb * denomInv;
        const float w = vc * denomInv;

        return Vector3(_mm_add_ps(a.reg,
            _mm_add_ps(_mm_mul_ps(ab, _mm_set1_ps(v)),
                       _mm_mul_ps(ac, _mm_set1_ps(w)))));
    }
}

namespace PhysicsSystem {
    void Run(Level &level) {
        ColliderStorage &col = level.colliders;

        std::vector<Entity *>      allEntities;
        std::vector<const Wall *>  allWalls;
        allEntities.reserve(64);
        allWalls.reserve(64);

        // ═══════════════════════════════════════════════════════════════════
        // PASS 1 — Sphere vs Sphere
        // ═══════════════════════════════════════════════════════════════════
        for (size_t si = 0; si < col.firstBoxIndex; ++si) {
            ComponentCollider &selfCollider = col.components[si];
            if (selfCollider.isTrigger) continue;

            ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);
            if (selfTransform == nullptr) [[unlikely]] continue;

            ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);
            if (selfRb == nullptr || selfRb->isStatic) continue;

            if (selfTransform->sectorIndex < 0 ||
                selfTransform->sectorIndex >= static_cast<int>(level.sectors.size()))
                continue;

            Sector &sector     = level.sectors[selfTransform->sectorIndex];
            Entity *selfEntity = level.GetEntity(selfCollider.ownerID);
            if (selfEntity == nullptr) [[unlikely]] continue;

            allEntities.clear();
            allWalls.clear();

            allEntities.insert(allEntities.end(),
                               sector.entitiesInside.begin(), sector.entitiesInside.end());
            allWalls.insert(allWalls.end(),
                            sector.walls.begin(), sector.walls.end());

            for (const Sector *nSector : sector.neighbors) {
                if (nSector == nullptr) [[unlikely]] continue;
                allEntities.insert(allEntities.end(),
                                   nSector->entitiesInside.begin(), nSector->entitiesInside.end());
                allWalls.insert(allWalls.end(),
                                nSector->walls.begin(), nSector->walls.end());
            }

            std::vector<ID> processedOtherIDs;
            processedOtherIDs.reserve(allEntities.size());

            Vector3 selfPos = {
                selfTransform->position.x,
                selfTransform->position.y,
                selfTransform->position.z + (selfTransform->scale.y * 0.5f)
            };

            for (const Entity *otherEntity : allEntities) {
                if (otherEntity == nullptr) [[unlikely]] continue;
                if (otherEntity->id == selfEntity->id) continue;

                auto insertPos = std::lower_bound(processedOtherIDs.begin(),
                                                  processedOtherIDs.end(), otherEntity->id);
                if (insertPos != processedOtherIDs.end() && *insertPos == otherEntity->id) continue;
                processedOtherIDs.insert(insertPos, otherEntity->id);

                ComponentCollider *otherCollider = level.colliders.Get(otherEntity->id);
                if (otherCollider == nullptr || !otherCollider->isActive || otherCollider->isTrigger) continue;
                if (otherCollider->type != COLLIDERTYPE_SPHERE) continue;

                ComponentTransform *otherTransform = level.transforms.Get(otherEntity->id);
                if (otherTransform == nullptr) [[unlikely]] continue;

                const ComponentRigidbody *otherRb       = level.rigidbodies.Get(otherEntity->id);
                const bool                otherIsStatic = (otherRb == nullptr || otherRb->isStatic);

                if (!otherIsStatic && selfEntity->id > otherEntity->id) continue;

                const float selfRadius  = std::max(0.0f, selfCollider.scale.x);
                const float otherRadius = std::max(0.0f, otherCollider->scale.x);
                const float radiusSum   = selfRadius + otherRadius;

                Vector3 otherPos = {
                    otherTransform->position.x,
                    otherTransform->position.y,
                    otherTransform->position.z + (otherTransform->scale.y * 0.5f)
                };

                const __m128 deltaPos    = _mm_sub_ps(selfPos.reg, otherPos.reg);
                const __m128 distSqrReg  = dot3_ss(deltaPos, deltaPos);
                const float  distSqr     = _mm_cvtss_f32(distSqrReg);

                if (distSqr >= radiusSum * radiusSum) continue;

                const __m128 safeDistSqr = _mm_max_ss(distSqrReg, _mm_set_ss(Constants::Epsilon));
                __m128 invDistReg        = rsqrt_nr_ss(safeDistSqr);
                invDistReg               = _mm_shuffle_ps(invDistReg, invDistReg, _MM_SHUFFLE(0,0,0,0));
                const float distance     = distSqr * _mm_cvtss_f32(invDistReg);

                const float penetration = radiusSum - distance;
                if (penetration <= Constants::Epsilon) continue;

                const __m128 calculatedDir = _mm_mul_ps(deltaPos, invDistReg);
                const __m128 fallbackDir   = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
                // _mm_blendv_ps replacement: select calculatedDir where distSqr > Epsilon
                const __m128 isSafe        = _mm_cmpgt_ss(distSqrReg, _mm_set_ss(Constants::Epsilon));
                const __m128 isSafeBroad   = _mm_shuffle_ps(isSafe, isSafe, _MM_SHUFFLE(0,0,0,0));
                const __m128 pushDirReg    = blend_ps(fallbackDir, calculatedDir, isSafeBroad);

                constexpr float PENETRATION_SLOP    = 0.001f;
                const float     correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);
                const __m128    corrPenReg           = _mm_set1_ps(correctedPenetration);

                const float selfPush = otherIsStatic ? 1.0f : 0.5f;
                selfTransform->AddPosition(Vector3(
                    _mm_mul_ps(pushDirReg, _mm_mul_ps(corrPenReg, _mm_set1_ps(selfPush)))));

                if (!otherIsStatic) {
                    otherTransform->AddPosition(Vector3(
                        _mm_mul_ps(pushDirReg, _mm_mul_ps(corrPenReg, _mm_set1_ps(-0.5f)))));
                }

                selfPos = {
                    selfTransform->position.x,
                    selfTransform->position.y,
                    selfTransform->position.z
                };
            }

            // ── Sphere vs Wall ────────────────────────────────────────────
            for (const Wall *wall : allWalls) {
                if (wall == nullptr) continue;

                for (int i = 0; i < wall->quad3DCount; ++i) {
                    const auto &quad = wall->quads3D[i];

                    const __m128 edge1 = _mm_sub_ps(quad[1].reg, quad[0].reg);
                    const __m128 edge2 = _mm_sub_ps(quad[2].reg, quad[0].reg);

                    // Cross product (edge1 × edge2) — already SSE2, unchanged
                    const __m128 e1_yzx    = _mm_shuffle_ps(edge1, edge1, _MM_SHUFFLE(3,0,2,1));
                    const __m128 e2_zxy    = _mm_shuffle_ps(edge2, edge2, _MM_SHUFFLE(3,1,0,2));
                    const __m128 e1_zxy    = _mm_shuffle_ps(edge1, edge1, _MM_SHUFFLE(3,1,0,2));
                    const __m128 e2_yzx    = _mm_shuffle_ps(edge2, edge2, _MM_SHUFFLE(3,0,2,1));
                    const __m128 crossReg  = _mm_sub_ps(
                        _mm_mul_ps(e1_yzx, e2_zxy),
                        _mm_mul_ps(e1_zxy, e2_yzx));

                    // Normalise the cross product
                    const __m128 quadNormSq     = dot3_ss(crossReg, crossReg);
                    __m128       quadNormInvLen  = rsqrt_nr_ss(quadNormSq);
                    quadNormInvLen               = _mm_shuffle_ps(quadNormInvLen, quadNormInvLen,
                                                                  _MM_SHUFFLE(0,0,0,0));
                    const __m128 quadNormalReg   = _mm_mul_ps(crossReg, quadNormInvLen);

                    const float quadNormalZ = _mm_cvtss_f32(
                        _mm_shuffle_ps(quadNormalReg, quadNormalReg, _MM_SHUFFLE(2,2,2,2)));
                    if (std::abs(quadNormalZ) > 0.75f) continue;

                    const Vector3 closestT1 = ClosestPointOnTriangle(selfPos, quad[0], quad[1], quad[2]);
                    const Vector3 closestT2 = ClosestPointOnTriangle(selfPos, quad[0], quad[2], quad[3]);

                    const __m128 diff1   = _mm_sub_ps(selfPos.reg, closestT1.reg);
                    const __m128 diff2   = _mm_sub_ps(selfPos.reg, closestT2.reg);
                    const float  distSq1 = _mm_cvtss_f32(dot3_ss(diff1, diff1));
                    const float  distSq2 = _mm_cvtss_f32(dot3_ss(diff2, diff2));

                    const bool   use1      = distSq1 < distSq2;
                    const float  minDistSq = use1 ? distSq1 : distSq2;
                    const __m128 bestDiff  = use1 ? diff1 : diff2;

                    const float radius = selfCollider.scale.x;
                    if (minDistSq > radius * radius) continue;
                    if (selfRb->isStatic) break;

                    const __m128 minDistSqReg    = _mm_set_ss(minDistSq);
                    const __m128 invDistanceReg  = rsqrt_nr_ss(minDistSqReg);
                    const float  distance        = minDistSq * _mm_cvtss_f32(invDistanceReg);
                    const float  penetrationDepth = radius - distance;

                    // Zero out z lane (keep only XY for wall push)
                    const __m128 xyMask = _mm_castsi128_ps(_mm_setr_epi32(-1, -1, 0, 0));

                    const __m128 invD            = _mm_shuffle_ps(invDistanceReg, invDistanceReg,
                                                                   _MM_SHUFFLE(0,0,0,0));
                    const __m128 calculatedNormal = _mm_and_ps(_mm_mul_ps(bestDiff, invD), xyMask);
                    const __m128 fallbackNormal   = _mm_and_ps(quadNormalReg, xyMask);
                    // _mm_blendv_ps replacement
                    const __m128 isSafe2          = _mm_cmpgt_ss(_mm_set_ss(distance),
                                                                  _mm_set_ss(0.0001f));
                    const __m128 isSafe2Broad     = _mm_shuffle_ps(isSafe2, isSafe2,
                                                                    _MM_SHUFFLE(0,0,0,0));
                    const __m128 collisionNormalReg = blend_ps(fallbackNormal, calculatedNormal,
                                                               isSafe2Broad);

                    // Horizontal length — 2-lane dot on XY
                    const __m128 horizLenSqReg    = dot2_ss(collisionNormalReg, collisionNormalReg);
                    const float  horizontalLenSq  = _mm_cvtss_f32(horizLenSqReg);
                    if (horizontalLenSq <= 0.000001f) continue;

                    __m128 invHorizLen = rsqrt_nr_ss(horizLenSqReg);
                    invHorizLen        = _mm_shuffle_ps(invHorizLen, invHorizLen, _MM_SHUFFLE(0,0,0,0));
                    const __m128 normalisedCollisionNormal = _mm_mul_ps(collisionNormalReg, invHorizLen);

                    selfTransform->AddPosition(Vector3(normalisedCollisionNormal) * penetrationDepth);

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->position.z
                    };
                }
            }

            // ── Floor / ceiling clamping ──────────────────────────────────
            {
                const Vector2 feetPoint2D = {
                    selfTransform->position.x,
                    selfTransform->position.y
                };
                if (!Geometry::IsPointInPolygon(sector.vertices, feetPoint2D)) continue;

                const float storeyHeight       = sector.ceilingHeight - sector.floorHeight;
                const float currentFloorWorld  = sector.floorHeight +
                                                 storeyHeight * static_cast<float>(selfTransform->floor);
                const float currentCeilingWorld = currentFloorWorld + storeyHeight;
                const float feetWorld           = selfTransform->position.z;
                const float headWorld           = feetWorld + selfTransform->scale.y;

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

        // ═══════════════════════════════════════════════════════════════════
        // PASS 2 — Box vs Sphere
        // ═══════════════════════════════════════════════════════════════════
        for (size_t bi = col.firstBoxIndex; bi < col.firstInactiveIndex; ++bi) {
            // TODO
        }

        // ═══════════════════════════════════════════════════════════════════
        // PASS 3 — Box vs Wall
        // ═══════════════════════════════════════════════════════════════════
        for (size_t bi = col.firstBoxIndex; bi < col.firstInactiveIndex; ++bi) {
            // TODO
        }

        // ═══════════════════════════════════════════════════════════════════
        // PASS 4 — Box vs Box
        // ═══════════════════════════════════════════════════════════════════
        for (size_t bi = col.firstBoxIndex; bi < col.firstInactiveIndex; ++bi) {
            // TODO
        }
    }
}