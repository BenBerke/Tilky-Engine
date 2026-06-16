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
    std::optional<RayHit> Raycast(
        Level &level,
        const Vector3 pos,
        const Vector3 dir,
        const float length,
        const ID ignoredEntity,
        const bool requireCollider
    ) {
        if (dir.IsZero()) return std::nullopt;

        const Vector3 normalizedDir = Vector3Math::Normalized(dir);

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

                if (hitA.has_value()) {
                    submitWallHit(wall, *hitA);
                }

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
        // position.z = height above the current sector floor
        for (Entity &entity: level.entities) {
            if (entity.id == ignoredEntity) continue;

            const ComponentTransform *transform = level.transforms.Get(entity.id);
            if (transform == nullptr) [[unlikely]] continue;

            float sectorFloorHeight = 0.0f;

            if (
                transform->sectorIndex >= 0 &&
                transform->sectorIndex < static_cast<int>(level.sectors.size())
            ) {
                sectorFloorHeight = level.sectors[transform->sectorIndex].floorHeight;
            }

            const float entityBottomWorldHeight =
                    sectorFloorHeight + transform->position.z;

            const Vector3 entityWorldBasePosition = {
                transform->position.x,
                transform->position.y,
                entityBottomWorldHeight
            };

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
                    entityWorldBasePosition.x - halfSize.x,
                    entityWorldBasePosition.y - halfSize.y,
                    entityWorldBasePosition.z
                };

                const Vector3 boxMax = {
                    entityWorldBasePosition.x + halfSize.x,
                    entityWorldBasePosition.y + halfSize.y,
                    entityWorldBasePosition.z + boxSize.z
                };

                const std::optional<float> hitDistance = RayAABBIntersection(
                    pos,
                    normalizedDir,
                    boxMin,
                    boxMax,
                    closestDistance
                );

                if (hitDistance.has_value()) {
                    submitEntityHit(entity, *hitDistance);
                }

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
                    entityWorldBasePosition.x,
                    entityWorldBasePosition.y,
                    entityWorldBasePosition.z + radius
                };

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
            } else if (collider->type == COLLIDERTYPE_BOX) {
                // Box collider convention:
                // collider->scale.x = width along world X
                // collider->scale.y = depth along world Z
                // collider->scale.z = height upward
                //
                // transform->position.z is the bottom of the entity relative to the floor.
                const Vector3 halfSize = collider->scale * 0.5f;

                const Vector3 boxMin = {
                    entityWorldBasePosition.x - halfSize.x,
                    entityWorldBasePosition.y - halfSize.y,
                    entityWorldBasePosition.z
                };

                const Vector3 boxMax = {
                    entityWorldBasePosition.x + halfSize.x,
                    entityWorldBasePosition.y + halfSize.y,
                    entityWorldBasePosition.z + collider->scale.z
                };

                const std::optional<float> hitDistance = RayAABBIntersection(
                    pos,
                    normalizedDir,
                    boxMin,
                    boxMax,
                    closestDistance
                );

                if (hitDistance.has_value()) {
                    submitEntityHit(entity, *hitDistance);
                }
            }
        }

        return rayHit;
    }
}
