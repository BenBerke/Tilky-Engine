//
// Created by berke on 6/2/2026.
//

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

#include "Headers/Math/Constants.hpp"

namespace {
    std::optional<float> RayTriangleIntersection(const Vector3 &origin, const Vector3 &dir,
                                                 const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                                 const float maxDistance) {
        const Vector3 edge1 = v1 - v0;
        const Vector3 edge2 = v2 - v0;

        const Vector3 pvec = Vector3Math::Cross(dir, edge2);
        const float det = Vector3Math::Dot(pvec, edge1);

        if (std::abs(det) < Constants::Epsilon) return std::nullopt;

        const float invDet = 1.0f / det;

        const Vector3 tvec = origin - v0;
        const float u = Vector3Math::Dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f) return std::nullopt;

        const Vector3 qvec = Vector3Math::Cross(tvec, edge1);
        const float v = Vector3Math::Dot(dir, qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f) return std::nullopt;

        const float t = Vector3Math::Dot(edge2, qvec) * invDet;
        if (t < 0.0f || t > maxDistance) return std::nullopt;

        return t;
    }

    std::optional<float> RaySphereIntersection(const Vector3 &origin, const Vector3 &dir,
                                               const Vector3 &sphereCenter, const float radius,
                                               const float maxDistance) {
        const Vector3 oc = origin - sphereCenter;

        const float b = Vector3Math::Dot(oc, dir);
        const float c = Vector3Math::Dot(oc, oc) - radius * radius;

        const float discriminant = b * b - c;

        if (discriminant < 0.0f) return std::nullopt;

        const float sqrtDisc = std::sqrt(discriminant);

        float t = -b - sqrtDisc;

        // Ray starts inside the sphere.
        if (t < 0.0f) t = -b + sqrtDisc;

        if (t < 0.0f || t > maxDistance) return std::nullopt;

        return t;
    }

    std::optional<float> RayAABBIntersection(
        const Vector3 &rayOrigin,
        const Vector3 &rayDir,
        const Vector3 &boxMin,
        const Vector3 &boxMax,
        const float maxDistance
    ) {
        float tMin = 0.0f;
        float tMax = maxDistance;

        auto checkAxis = [&](
            const float originAxis,
            const float dirAxis,
            const float minAxis,
            const float maxAxis
        ) -> bool {
            if (std::abs(dirAxis) < Constants::Epsilon) return originAxis >= minAxis && originAxis <= maxAxis;

            float t1 = (minAxis - originAxis) / dirAxis;
            float t2 = (maxAxis - originAxis) / dirAxis;

            if (t1 > t2) std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);

            return tMin <= tMax;
        };

        if (!checkAxis(rayOrigin.x, rayDir.x, boxMin.x, boxMax.x)) return std::nullopt;
        if (!checkAxis(rayOrigin.y, rayDir.y, boxMin.y, boxMax.y)) return std::nullopt;
        if (!checkAxis(rayOrigin.z, rayDir.z, boxMin.z, boxMax.z)) return std::nullopt;
        if (tMin < 0.0f || tMin > maxDistance) return std::nullopt;

        return tMin;
    }
}

namespace GameFunctions {
    std::optional<RayHit> Raycast(
        Level& level,
        Vector3 pos,
        const Vector3& dir,
        float length,
        ID ignoredEntity,
        bool requireCollider
    ) {
        if (dir.IsZero()) return std::nullopt;

        const Vector3 normalizedDir = Vector3Math::Normalized(dir);

        std::optional<RayHit> rayHit;
        float closestDistance = length;

        auto submitWallHit = [&](Wall &wall, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.type = RayHitType::Wall;
            hit.wall = &wall;
            hit.entity = nullptr;
            hit.sector = nullptr;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        auto submitEntityHit = [&](Entity &entity, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.type = RayHitType::Entity;
            hit.wall = nullptr;
            hit.entity = &entity;
            hit.sector = nullptr;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        auto submitSectorHit = [&](Sector &sector, const RayHitType type, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.type = type;
            hit.wall = nullptr;
            hit.entity = nullptr;
            hit.sector = &sector;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };


        //todo binary space partioning for visible wall check
        for (Wall& wall: level.walls) {
            for (int j = 0; j < wall.quad3DCount; ++j) {
                const auto &quad = wall.quads3D[j];

                const Vector3 &bottomStart = quad[0];
                const Vector3 &bottomEnd = quad[1];
                const Vector3 &topEnd = quad[2];
                const Vector3 &topStart = quad[3];

                const std::optional<float> hitA = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    bottomStart,
                    bottomEnd,
                    topEnd,
                    closestDistance
                );

                if (hitA.has_value()) submitWallHit(wall, *hitA);

                const std::optional<float> hitB = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    bottomStart,
                    topEnd,
                    topStart,
                    closestDistance
                );

                if (hitB.has_value()) submitWallHit(wall, *hitB);
            }
        }

        // Entity positions use:
        // position.x = world X
        // position.y = world Z / horizontal depth
        // position.z = world Y
        for (Entity& entity: level.entities) {
            if (entity.id == ignoredEntity) continue;

            const ComponentTransform *transform = level.transforms.Get(entity.id);
            if (transform == nullptr) [[unlikely]] continue;

            // If requireCollider is false, raycast every entity as an AABB.
            // transform->scale is Vector2:
            // scale.x = width along world X
            // scale.y = depth along world Z AND height upward
            if (!requireCollider) {
                const Vector3 boxSize = {
                    transform->scale.x,
                    transform->scale.y,
                    transform->scale.y
                };

                const Vector3 halfSize = boxSize * 0.5f;

                const Vector3 boxMin = {
                    transform->position.x - halfSize.x,
                    transform->position.y - halfSize.y,
                    transform->position.z
                };

                const Vector3 boxMax = {
                    transform->position.x + halfSize.x,
                    transform->position.y + halfSize.y,
                    transform->position.z + boxSize.z
                };

                const std::optional<float> hitDistance = RayAABBIntersection(
                    pos,
                    normalizedDir,
                    boxMin,
                    boxMax,
                    closestDistance
                );

                if (hitDistance.has_value())
                    submitEntityHit(entity, *hitDistance);

                continue;
            }

            // Normal collider-required behavior.
            const ComponentCollider *collider = level.colliders.Get(entity.id);

            if (collider == nullptr) continue;
            if (collider->isTrigger) continue;

            if (collider->type == COLLIDERTYPE_SPHERE) {
                const float radius = transform->scale.x;

                // transform->position.z is treated as the entity bottom/feet height,
                // so the sphere center is radius units above that.
                const Vector3 center = {
                    transform->position.x,
                    transform->position.y,
                    transform->position.z + radius
                };

                const std::optional<float> hitDistance = RaySphereIntersection(
                    pos,
                    normalizedDir,
                    center,
                    radius,
                    closestDistance
                );

                if (hitDistance.has_value())
                    submitEntityHit(entity, *hitDistance);
            }
            else if (collider->type == COLLIDERTYPE_BOX) {
                // Box collider convention:
                // collider->scale.x = width along world X
                // collider->scale.y = depth along world Z
                // collider->scale.z = height upward
                //
                // transform->position.z is the bottom of the entity relative to the floor.
                const Vector3 halfSize = collider->scale * 0.5f;

                const Vector3 boxMin = {
                    transform->position.x - halfSize.x,
                    transform->position.y - halfSize.y,
                    transform->position.z
                };

                const Vector3 boxMax = {
                    transform->position.x + halfSize.x,
                    transform->position.y + halfSize.y,
                    transform->position.z + collider->scale.z
                };

                const std::optional<float> hitDistance = RayAABBIntersection(
                    pos,
                    normalizedDir,
                    boxMin,
                    boxMax,
                    closestDistance
                );

                if (hitDistance.has_value()) submitEntityHit(entity, *hitDistance);
            }
        }

        for (Sector& sector : level.sectors) {
            for (const Triangle& triangle : sector.triangles) {
                const Vector3 floorA = { triangle.a.x, triangle.a.y, sector.floorHeight };
                const Vector3 floorB = { triangle.b.x, triangle.b.y, sector.floorHeight };
                const Vector3 floorC = { triangle.c.x, triangle.c.y, sector.floorHeight };

                const std::optional<float> floorHit = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    floorA,
                    floorB,
                    floorC,
                    closestDistance
                );

                if (floorHit.has_value()) submitSectorHit(sector, RayHitType::SectorFloor, *floorHit);

                const Vector3 ceilingA = { triangle.a.x, triangle.a.y, sector.ceilingHeight };
                const Vector3 ceilingB = { triangle.b.x, triangle.b.y, sector.ceilingHeight };
                const Vector3 ceilingC = { triangle.c.x, triangle.c.y, sector.ceilingHeight };

                const std::optional<float> ceilingHit = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    ceilingA,
                    ceilingB,
                    ceilingC,
                    closestDistance
                );

                if (ceilingHit.has_value()) submitSectorHit(sector, RayHitType::SectorCeiling, *ceilingHit);
            }
        }

        return rayHit;
    }
}
