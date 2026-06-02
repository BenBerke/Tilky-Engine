//
// Created by berke on 6/2/2026.
//

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

namespace {
    constexpr float EPSILON = 0.000001f;

    std::optional<float> RayTriangleIntersection(const Vector3 &origin, const Vector3 &dir,
                                                 const Vector3 &v0, const Vector3 &v1, const Vector3 &v2,
                                                 const float maxDistance) {
        const Vector3 edge1 = v1 - v0;
        const Vector3 edge2 = v2 - v0;

        const Vector3 pvec = Vector3Math::Cross(dir, edge2);
        const float det = Vector3Math::Dot(pvec, edge1);

        if (std::abs(det) < EPSILON) return std::nullopt;

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
            if (std::abs(dirAxis) < EPSILON) return originAxis >= minAxis && originAxis <= maxAxis;

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
    std::optional<RayHit> Raycast(Level &level, const Vector3 pos, const Vector3 dir, const float length, const EntityID ignoredEntity) {
        if (dir.IsZero()) return std::nullopt;

        Vector3 normalizedDir = Vector3Math::Normalized(dir);

        std::optional<RayHit> rayHit;
        float closestDistance = length;

        auto submitWallHit = [&](Wall &wall, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.wall = &wall;
            hit.entity = nullptr;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        auto submitEntityHit = [&](Entity &entity, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.wall = nullptr;
            hit.entity = &entity;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        for (Wall &wall: level.walls) {
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

        //todo sort entities by colliderful/colliderless to optimize for branch prediction
        for (Entity &entity: level.entities) {
            const ComponentTransform *transform = level.transforms.Get(entity.id);

            if (transform == nullptr) [[unlikely]] continue;

            const ComponentCollider *collider = level.colliders.Get(entity.id);

            if (collider == nullptr) continue;
            if (collider->isTrigger) continue;

            if (collider->type == COLLIDERTYPE_SPHERE) {
                const Vector3 center = transform->position;
                const float radius = transform->scale.x;

                const std::optional<float> hitDistance = RaySphereIntersection(
                    pos,
                    normalizedDir,
                    center,
                    radius,
                    closestDistance
                );

                if (hitDistance.has_value()) {
                    submitEntityHit(entity, *hitDistance);
                }
            }

            if (collider->type == COLLIDERTYPE_BOX) {
                //todo make it so transform scale also affects collider scale
                const Vector3 halfSize = collider->scale * .5f;

                const Vector3 boxMin = transform->position - halfSize;
                const Vector3 boxMax = transform->position + halfSize;

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

        return rayHit;
    }
}
