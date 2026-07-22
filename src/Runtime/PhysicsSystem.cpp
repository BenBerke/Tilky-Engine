//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

#include "Headers/Math/Constants.hpp"
#include "Headers/Math/SIMD/SSECompat.hpp"

#include <tracy/Tracy.hpp>  // adjust to your Tracy include path

// ─── Newton-Raphson refined reciprocal (1 iteration) ────────────────────────
static inline __m128 rcp_nr_ss(const __m128 x) {
    __m128 r = _mm_rcp_ss(x);
    r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(2.0f), _mm_mul_ss(x, r)));
    return r;
}

// ─── Newton-Raphson refined reciprocal square root (1 iteration) ─────────────
static inline __m128 rsqrt_nr_ss(const __m128 x) {
    __m128 r = _mm_rsqrt_ss(x);
    const __m128 half = _mm_set_ss(0.5f);
    const __m128 three_halves = _mm_set_ss(1.5f);
    r = _mm_mul_ss(r, _mm_sub_ss(three_halves,
                                 _mm_mul_ss(half, _mm_mul_ss(x, _mm_mul_ss(r, r)))));
    return r;
}

// ─── SSE2 helpers ────────────────────────────────────────────────────────────

// Dot product of 3 lanes (x·y·z), result in lane 0.
static inline __m128 dot3_ss(const __m128 a, const __m128 b) {
    const __m128 mul = _mm_mul_ps(a, b);
    const __m128 shuf1 = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(1,1,1,1));
    const __m128 sum1 = _mm_add_ss(mul, shuf1);
    const __m128 shuf2 = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2,2,2,2));
    return _mm_add_ss(sum1, shuf2);
}

// Dot product of 2 lanes (x·y), result in lane 0.
static inline __m128 dot2_ss(const __m128 a, const __m128 b) {
    const __m128 mul = _mm_mul_ps(a, b);
    const __m128 shuf1 = TILKY_MM_SHUFFLE_PS(mul, mul, _MM_SHUFFLE(2,2,2,2));
    return _mm_add_ss(mul, shuf1);
}

// SSE2 replacement for _mm_blendv_ps(a, b, mask).
static inline __m128 blend_ps(const __m128 a, const __m128 b, const __m128 mask) {
    return _mm_or_ps(_mm_and_ps(mask, b), _mm_andnot_ps(mask, a));
}

namespace {
    Vector3 ClosestPointOnTriangle(const Vector3 &p,
                                   const Vector3 &a,
                                   const Vector3 &b,
                                   const Vector3 &c) {
        const __m128 ab = _mm_sub_ps(b.reg, a.reg);
        const __m128 ac = _mm_sub_ps(c.reg, a.reg);
        const __m128 ap = _mm_sub_ps(p.reg, a.reg);

        const float d1 = _mm_cvtss_f32(dot3_ss(ab, ap));
        const float d2 = _mm_cvtss_f32(dot3_ss(ac, ap));
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        const __m128 bp = _mm_sub_ps(p.reg, b.reg);
        const float d3 = _mm_cvtss_f32(dot3_ss(ab, bp));
        const float d4 = _mm_cvtss_f32(dot3_ss(ac, bp));
        if (d3 >= 0.0f && d4 <= d3) return b;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            const float v = d1 * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(d1 - d3)));
            return Vector3(_mm_add_ps(a.reg, _mm_mul_ps(_mm_set1_ps(v), ab)));
        }

        const __m128 cp = _mm_sub_ps(p.reg, c.reg);
        const float d5 = _mm_cvtss_f32(dot3_ss(ab, cp));
        const float d6 = _mm_cvtss_f32(dot3_ss(ac, cp));
        if (d6 >= 0.0f && d5 <= d6) return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            const float w = d2 * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(d2 - d6)));
            return Vector3(_mm_add_ps(a.reg, _mm_mul_ps(_mm_set1_ps(w), ac)));
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            const float num = d4 - d3;
            const float denom = num + (d5 - d6);
            const float w = num * _mm_cvtss_f32(rcp_nr_ss(_mm_set_ss(denom)));
            const __m128 bc = _mm_sub_ps(c.reg, b.reg);
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
        ZoneScopedN("PhysicsSystem::Run");

        ColliderStorage &col = level.colliders;

        std::vector<ID> allEntities;
        std::vector<const Wall *> allWalls;
        allEntities.reserve(64);
        allWalls.reserve(64);

        for (ComponentCollider &selfCollider: col.ActiveSpheres()) {
            ZoneScopedN("Sphere iteration");

            if (selfCollider.isTrigger) continue;

            ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);
            if (selfTransform == nullptr) [[unlikely]] continue;

            ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);
            if (selfRb == nullptr || selfRb->isStatic) continue;

            const int si = selfTransform->sectorIndex;
            if (si < 0 || si >= static_cast<int>(level.sectors.size())) continue;

            Sector &sector = level.sectors[si];

            // ── Gather candidates from current sector + neighbours ────────────
            {
                ZoneScopedN("Candidate gather");

                allEntities.clear();
                allWalls.clear();

                allEntities.insert(allEntities.end(),
                                   sector.entitiesInside.begin(), sector.entitiesInside.end());
                allWalls.insert(allWalls.end(),
                                sector.walls.begin(), sector.walls.end());

                for (const Sector *nb: sector.neighbors) {
                    if (nb == nullptr) [[unlikely]] continue;
                    allEntities.insert(allEntities.end(),
                                       nb->entitiesInside.begin(), nb->entitiesInside.end());
                    allWalls.insert(allWalls.end(),
                                    nb->walls.begin(), nb->walls.end());
                }
            }

            const float selfRadius = std::max(0.0f, selfCollider.scale.x);

            Vector3 selfPos = {
                selfTransform->position.x,
                selfTransform->position.y + selfRadius,
                selfTransform->position.z
            };

            // ── Sphere vs Sphere ──────────────────────────────────────────────
            {
                ZoneScopedN("Entity narrowphase");

                for (const ID otherID: allEntities) {
                    if (otherID == selfCollider.ownerID) continue;

                    ComponentCollider *otherCollider = level.colliders.Get(otherID);
                    if (otherCollider == nullptr || !otherCollider->isActive || otherCollider->isTrigger) continue;
                    if (otherCollider->type != COLLIDERTYPE_SPHERE) continue;

                    ComponentTransform *otherTransform = level.transforms.Get(otherID);
                    if (otherTransform == nullptr) [[unlikely]] continue;

                    const ComponentRigidbody *otherRb = level.rigidbodies.Get(otherID);
                    const bool otherIsStatic = (otherRb == nullptr || otherRb->isStatic);

                    // Skip one half of each dynamic pair to avoid resolving it twice.
                    // processedOtherIDs is not needed — this guard is sufficient.
                    if (!otherIsStatic && selfCollider.ownerID > otherID) continue;

                    const float otherRadius = std::max(0.0f, otherCollider->scale.x);
                    const float radiusSum = selfRadius + otherRadius;

                    Vector3 otherPos = {
                        otherTransform->position.x,
                        otherTransform->position.y + otherRadius,
                        otherTransform->position.z
                    };

                    const __m128 xzMask = _mm_castsi128_ps(_mm_setr_epi32(-1, 0, -1, 0));

                    const __m128 deltaPos = _mm_and_ps(_mm_sub_ps(selfPos.reg, otherPos.reg), xzMask);
                    const __m128 distSqrReg = dot3_ss(deltaPos, deltaPos);
                    const float distSqr = _mm_cvtss_f32(distSqrReg);

                    if (distSqr >= radiusSum * radiusSum) continue;

                    const __m128 safeDistSqr = _mm_max_ss(distSqrReg, _mm_set_ss(Constants::Epsilon));
                    __m128 invDistReg = rsqrt_nr_ss(safeDistSqr);
                    invDistReg = TILKY_MM_SHUFFLE_PS(invDistReg, invDistReg, _MM_SHUFFLE(0,0,0,0));
                    const float distance = distSqr * _mm_cvtss_f32(invDistReg);
                    const float penetration = radiusSum - distance;
                    if (penetration <= Constants::Epsilon) continue;

                    const __m128 calculatedDir = _mm_mul_ps(deltaPos, invDistReg);
                    const __m128 fallbackDir = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
                    const __m128 isSafe = _mm_cmpgt_ss(distSqrReg, _mm_set_ss(Constants::Epsilon));
                    const __m128 isSafeBroad = TILKY_MM_SHUFFLE_PS(isSafe, isSafe, _MM_SHUFFLE(0,0,0,0));
                    const __m128 pushDirReg = blend_ps(fallbackDir, calculatedDir, isSafeBroad);

                    constexpr float PENETRATION_SLOP = 0.001f;
                    const float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);
                    const __m128 corrPenReg = _mm_set1_ps(correctedPenetration);

                    const float selfPush = otherIsStatic ? 1.0f : 0.5f;
                    selfTransform->AddPosition(Vector3(
                        _mm_mul_ps(pushDirReg, _mm_mul_ps(corrPenReg, _mm_set1_ps(selfPush)))));

                    if (!otherIsStatic)
                        otherTransform->AddPosition(Vector3(
                            _mm_mul_ps(pushDirReg, _mm_mul_ps(corrPenReg, _mm_set1_ps(-0.5f)))));

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y + selfRadius,
                        selfTransform->position.z
                    };
                }
            }

            // ── Sphere vs Wall ────────────────────────────────────────────────
            {
                ZoneScopedN("Wall narrowphase");

                for (const Wall *wall: allWalls) {
                    if (wall == nullptr) continue;

                    // wall->normal is a unit X/Z vector stored as Vector2 {x, z}.
                    // Convert it to a 3D horizontal normal with Y = 0.
                    const __m128 quadNormalReg = _mm_set_ps(0.0f, wall->normal.y, 0.0f, wall->normal.x);

                    for (int i = 0; i < wall->quad3DCount; ++i) {
                        ZoneScopedN("Quad test");
                        const auto &quad = wall->quads3D[i];

                        // ── AABB early-out (3D, expanded by radius) ───────────
                        // Requires quadAabbMin/quadAabbMax on Wall — see Wall.hpp patch below.
                        if (selfPos.x < wall->quadAabbMin[i].x - selfRadius ||
                            selfPos.x > wall->quadAabbMax[i].x + selfRadius)
                            continue;
                        if (selfPos.y < wall->quadAabbMin[i].y - selfRadius ||
                            selfPos.y > wall->quadAabbMax[i].y + selfRadius)
                            continue;
                        if (selfPos.z < wall->quadAabbMin[i].z - selfRadius ||
                            selfPos.z > wall->quadAabbMax[i].z + selfRadius)
                            continue;

                        // ── Plane-distance early-out ──────────────────────────
                        // Wall normal is horizontal so dot is purely XZ.
                        const float planeDist = (selfPos.x - quad[0].x) * wall->normal.x +
                                                (selfPos.z - quad[0].z) * wall->normal.y;

                        if (std::abs(planeDist) > selfRadius) continue;

                        // ── Closest-point test ────────────────────────────────
                        {
                            ZoneScopedN("ClosestPointOnTriangle");

                            const Vector3 closestT1 = ClosestPointOnTriangle(selfPos, quad[0], quad[1], quad[2]);
                            const Vector3 closestT2 = ClosestPointOnTriangle(selfPos, quad[0], quad[2], quad[3]);

                            const __m128 diff1 = _mm_sub_ps(selfPos.reg, closestT1.reg);
                            const __m128 diff2 = _mm_sub_ps(selfPos.reg, closestT2.reg);
                            const float distSq1 = _mm_cvtss_f32(dot3_ss(diff1, diff1));
                            const float distSq2 = _mm_cvtss_f32(dot3_ss(diff2, diff2));

                            const bool use1 = distSq1 < distSq2;
                            const float minDistSq = use1 ? distSq1 : distSq2;
                            const __m128 bestDiff = use1 ? diff1 : diff2;

                            if (minDistSq > selfRadius * selfRadius) continue;
                            if (selfRb->isStatic) break;

                            const __m128 minDistSqReg = _mm_set_ss(minDistSq);
                            const __m128 invDistanceReg = rsqrt_nr_ss(minDistSqReg);
                            const float distance = minDistSq * _mm_cvtss_f32(invDistanceReg);
                            const float penetrationDepth = selfRadius - distance;

                            const __m128 xzMask = _mm_castsi128_ps(_mm_setr_epi32(-1, 0, -1, 0));
                            const __m128 invD = TILKY_MM_SHUFFLE_PS(invDistanceReg, invDistanceReg,
                                                                    _MM_SHUFFLE(0,0,0,0));
                            const __m128 calculatedNormal = _mm_and_ps(_mm_mul_ps(bestDiff, invD), xzMask);
                            const __m128 fallbackNormal = _mm_and_ps(quadNormalReg, xzMask);
                            const __m128 isSafe2 = _mm_cmpgt_ss(_mm_set_ss(distance), _mm_set_ss(0.0001f));
                            const __m128 isSafe2Broad = TILKY_MM_SHUFFLE_PS(isSafe2, isSafe2, _MM_SHUFFLE(0,0,0,0));
                            const __m128 collisionNormalReg = blend_ps(fallbackNormal, calculatedNormal, isSafe2Broad);

                            const __m128 horizLenSqReg = dot2_ss(collisionNormalReg, collisionNormalReg);
                            const float horizontalLenSq = _mm_cvtss_f32(horizLenSqReg);
                            if (horizontalLenSq <= 0.000001f) continue;

                            __m128 invHorizLen = rsqrt_nr_ss(horizLenSqReg);
                            invHorizLen = TILKY_MM_SHUFFLE_PS(invHorizLen, invHorizLen, _MM_SHUFFLE(0,0,0,0));
                            const __m128 normCollNormal = _mm_mul_ps(collisionNormalReg, invHorizLen);

                            selfTransform->AddPosition(Vector3(normCollNormal) * penetrationDepth);

                            const auto normal = Vector3(normCollNormal);
                            const float intoWall = selfRb->velocity.x * normal.x
                                                   + selfRb->velocity.z * normal.z;
                            if (intoWall < 0.0f) {
                                selfRb->velocity.x -= normal.x * intoWall;
                                selfRb->velocity.z -= normal.z * intoWall;
                            }

                            selfPos = {
                                selfTransform->position.x,
                                selfTransform->position.y + selfRadius,
                                selfTransform->position.z
                            };
                        }
                    }
                }
            }

            // ── Floor / ceiling clamp ─────────────────────────────────────────
            {
                ZoneScopedN("Sector Clamp");

                const Vector2 feetPoint2D = {selfTransform->position.x, selfTransform->position.z};

                if (!Geometry::IsPointInPolygon(sector.vertices, feetPoint2D)) continue;

                const float floorWorld = sector.floorHeight;
                const float ceilingWorld = sector.ceilingHeight;
                const float feetWorld = selfTransform->position.y;

                if (feetWorld < floorWorld) {
                    selfTransform->AddPosition({0.0f, floorWorld - feetWorld, 0.0f});
                    if (selfRb->velocity.y < 0.0f) selfRb->velocity.y = 0.0f;
                }

                const float correctedFeetWorld = selfTransform->position.y;
                const float correctedHeadWorld = correctedFeetWorld + selfTransform->scale.y;

                if (correctedHeadWorld > ceilingWorld) {
                    selfTransform->AddPosition({0.0f, ceilingWorld - correctedHeadWorld, 0.0f});
                    if (selfRb->velocity.y > 0.0f) selfRb->velocity.y = 0.0f;
                }

                selfTransform->relativeHeight = selfTransform->position.y - floorWorld;
            }
        }

        for (ComponentCollider &boxCollider: col.ActiveBoxes()) {
            // TODO
        }
    }
} // namespace PhysicsSystem
