//
// Created by berke on 6/15/2026.
//

#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"

namespace {
        Vector3 ClosestPointOnSegment(const Vector3& p, const Vector3& a, const Vector3& b) {
        const Vector3 ab = b - a;
        const Vector3 ap = p - a;
        float t = Vector3Math::Dot(ap, ab) / Vector3Math::Dot(ab, ab);
        t = std::max(0.0f, std::min(1.0f, t)); // Clamp to segment bounds
        return Vector3{ a.x + t * ab.x, a.y + t * ab.y, a.z + t * ab.z };
    }
        Vector3 ClosestPointOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c) {
        const Vector3 ab = b - a;
        const Vector3 ac = c - a;
        const Vector3 ap = p - a;

        const float d1 = Vector3Math::Dot(ab, ap);
        const float d2 = Vector3Math::Dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        const Vector3 bp = p - b;
        const float d3 = Vector3Math::Dot(ab, bp);
        const float d4 = Vector3Math::Dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            const float v = d1 / (d1 - d3);
            return a + ab * v;
        }

        const Vector3 cp = p - c;
        const float d5 = Vector3Math::Dot(ab, cp);
        const float d6 = Vector3Math::Dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            const float w = d2 / (d2 - d6);
            return a + ac * w;
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            const Vector3 bc = c - b;
            return b + bc * w;
        }

        // P is inside face region
        const float denom = 1.0f / (va + vb + vc);
        const float v = vb * denom;
        const float w = vc * denom;
        return Vector3{ a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w };
    }
}

namespace CollisionSystem {
    void Run(Level& level) {
        for (ComponentCollider &selfCollider: level.colliders.components) {
            if (!selfCollider.isActive) continue;
            if (selfCollider.isTrigger) continue;

            ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);

            if (selfTransform == nullptr) [[unlikely]] continue;

            ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);

            if (selfRb == nullptr) continue;
            if (selfRb->isStatic) continue;

            //selfTransform->isDirty = true;

            if (selfTransform->sectorIndex < 0 || selfTransform->sectorIndex >= static_cast<int>(level.sectors.size()))
                continue;

            Sector &sector = level.sectors[selfTransform->sectorIndex];
            Entity *selfEntity = level.GetEntity(selfCollider.ownerID);

            if (selfEntity == nullptr) [[unlikely]] continue;

            std::vector<Entity*> allEntities;
            std::vector<const Wall*> allWalls;

            allEntities.reserve(sector.entitiesInside.size() + 16);
            allEntities.insert(allEntities.end(), sector.entitiesInside.begin(), sector.entitiesInside.end());

            allEntities.reserve(32);
            allWalls.insert(allWalls.end(), sector.walls.begin(), sector.walls.end());

            for (const Sector *nSector: sector.neighbors) {
                if (nSector == nullptr) [[unlikely]] continue;

                allEntities.insert(allEntities.end(), nSector->entitiesInside.begin(),
                                   nSector->entitiesInside.end());

                allWalls.insert(allWalls.end(), nSector->walls.begin(),
                                   nSector->walls.end());
            }

            std::vector<ID> processedOtherIDs;

            processedOtherIDs.reserve(allEntities.size());

            Vector3 selfPos = {
                selfTransform->position.x,
                selfTransform->position.y,
                selfTransform->absHeight + (selfTransform->scale.y * .5f)
            };

            for (const Entity *otherEntity: allEntities) {
                if (otherEntity == nullptr) [[unlikely]] continue;
                if (otherEntity->id == selfEntity->id) continue;

                if (std::ranges::find(processedOtherIDs, otherEntity->id) != processedOtherIDs.end()) continue;

                processedOtherIDs.push_back(otherEntity->id);

                ComponentCollider *otherCollider = level.colliders.Get(otherEntity->id);

                if (otherCollider == nullptr) continue;
                if (!otherCollider->isActive || otherCollider->isTrigger) continue;

                ComponentTransform *otherTransform = level.transforms.Get(otherEntity->id);
                if (otherTransform == nullptr) [[unlikely]] continue;

                const ComponentRigidbody *otherRb = level.rigidbodies.Get(otherEntity->id);

                // Treat missing rigidbodies as static colliders.
                const bool otherIsStatic = (otherRb == nullptr || otherRb->isStatic);

                if (!otherIsStatic && selfEntity->id > otherEntity->id) continue;

                Vector3 otherPos = {
                    otherTransform->position.x,
                    otherTransform->position.y,
                    otherTransform->absHeight
                };

                // This block currently only supports sphere-vs-sphere collision.
                if (selfCollider.type == COLLIDERTYPE_SPHERE && otherCollider->type == COLLIDERTYPE_SPHERE) {
                    const float selfRadius = std::max(0.0f, selfCollider.scale.x);
                    const float otherRadius = std::max(0.0f, otherCollider->scale.x);

                    const float radiusSum = selfRadius + otherRadius;

                    const float distSqr = Vector3Math::DistanceSquared(selfPos, otherPos);

                    if (distSqr >= radiusSum * radiusSum) continue;

                    constexpr float EPSILON = 0.00001f;

                    const float distance = std::sqrt(std::max(distSqr, EPSILON));

                    const float penetration = radiusSum - distance;

                    if (penetration <= EPSILON) continue;

                    // Choose a stable fallback normal when both sphere centers are almost identical.
                    Vector3 pushDirection = (distSqr <= EPSILON)
                                                ? Vector3{1.0f, 0.0f, 0.0f}
                                                : (selfPos - otherPos) * (1.0f / distance);

                    const float selfPush = otherIsStatic ? 1.0f : 0.5f;
                    const float otherPush = otherIsStatic ? 0.0f : 0.5f;

                    // Add a tiny slop so resting contacts do not constantly micro-correct.
                    constexpr float PENETRATION_SLOP = 0.001f;
                    const float correctedPenetration = std::max(0.0f, penetration - PENETRATION_SLOP);

                    const Vector3 selfCorrection = pushDirection * (correctedPenetration * selfPush);
                    const Vector3 otherCorrection = pushDirection * (correctedPenetration * otherPush);

                    selfTransform->AddPosition(selfCorrection);

                    if (!otherIsStatic) otherTransform->AddPosition(-otherCorrection);

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->absHeight
                    };
                }
            }

            for (const Wall *wall: allWalls) {
                if (wall == nullptr) continue;

                for (int i = 0; i < wall->quad3DCount; ++i) {
                    const auto &quad = wall->quads3D[i];

                    const Vector3 edge1 = quad[1] - quad[0];
                    const Vector3 edge2 = quad[2] - quad[0];

                    Vector3 quadNormal = Vector3Math::Normalized(Vector3Math::Cross(edge1, edge2));

                    // Skip floor/ceiling-style horizontal quads.
                    // Wall collision should only handle mostly vertical faces.
                    if (std::abs(quadNormal.z) > 0.75f) continue;

                    const Vector3 closestT1 = ClosestPointOnTriangle(selfPos, quad[0], quad[1], quad[2]);
                    const Vector3 closestT2 = ClosestPointOnTriangle(selfPos, quad[0], quad[2], quad[3]);

                    Vector3 diff1 = selfPos - closestT1;
                    Vector3 diff2 = selfPos - closestT2;

                    const float distSq1 = Vector3Math::LengthSquared(diff1);
                    const float distSq2 = Vector3Math::LengthSquared(diff2);

                    const float minDistanceSq = (distSq1 < distSq2) ? distSq1 : distSq2;

                    const float radius = selfCollider.scale.x;

                    if (minDistanceSq <= radius * radius) {
                        if (selfRb->isStatic) break;

                        const float distance = std::sqrt(minDistanceSq);
                        const float penetrationDepth = radius - distance;

                        Vector3 bestDiff = (distSq1 < distSq2) ? diff1 : diff2;

                        Vector3 collisionNormal = {0.0f, 0.0f, 0.0f};

                        if (distance > 0.0001f) {
                            collisionNormal = {
                                bestDiff.x / distance,
                                bestDiff.y / distance,
                                0.0f
                            };
                        } else {
                            collisionNormal = {
                                quadNormal.x,
                                quadNormal.y,
                                0.0f
                            };
                        }

                        const float horizontalNormalLengthSq =
                                collisionNormal.x * collisionNormal.x +
                                collisionNormal.y * collisionNormal.y;

                        if (horizontalNormalLengthSq <= 0.000001f) {
                            continue;
                        }

                        const float invHorizontalNormalLength =
                                1.0f / std::sqrt(horizontalNormalLengthSq);

                        collisionNormal.x *= invHorizontalNormalLength;
                        collisionNormal.y *= invHorizontalNormalLength;
                        collisionNormal.z = 0.0f;

                        selfTransform->AddPosition(collisionNormal * penetrationDepth);

                        selfPos = {
                            selfTransform->position.x,
                            selfTransform->position.y,
                            selfTransform->absHeight
                        };
                    }
                }
            }

            // Floor/ceiling collision.
            // The floor and ceiling shape is the sector polygon/triangulated area in XY.
            // Z collision is handled separately because sector floors/ceilings are flat heights.
            {
                const Vector2 feetPoint2D = {
                    selfTransform->position.x,
                    selfTransform->position.y
                };

                // If the entity is not horizontally inside this sector shape,
                // do not apply this sector's floor/ceiling clamp.
                if (!Geometry::IsPointInPolygon(sector.vertices, feetPoint2D)) {
                    continue;
                }

                // Feet height relative to this sector's floor.
                const float feetRelativeHeight = selfTransform->position.z;

                // Head height relative to this sector's floor.
                const float headRelativeHeight =
                        selfTransform->position.z + selfTransform->scale.y;

                // If ceilingHeight is absolute, convert it to floor-relative height.
                const float ceilingRelativeHeight =
                        sector.ceilingHeight - sector.floorHeight;

                // Floor collision: feet cannot go below the sector floor.
                if (feetRelativeHeight < 0.0f) {
                    selfTransform->AddPosition({
                        0.0f,
                        0.0f,
                        -feetRelativeHeight
                    });

                    if (selfRb->velocity.z < 0.0f) {
                        selfRb->velocity.z = 0.0f;
                    }

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->absHeight
                    };
                }

                // Ceiling collision: head cannot go above the sector ceiling.
                if (headRelativeHeight > ceilingRelativeHeight) {
                    const float ceilingCorrection =
                            ceilingRelativeHeight - headRelativeHeight;

                    selfTransform->AddPosition({
                        0.0f,
                        0.0f,
                        ceilingCorrection
                    });

                    if (selfRb->velocity.z > 0.0f) {
                        selfRb->velocity.z = 0.0f;
                    }

                    selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->absHeight
                    };
                }
            }
        }
    }
}