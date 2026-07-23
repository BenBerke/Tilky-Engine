//
// Created by berke on 6/2/2026.
//

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Constants.hpp"

namespace {
    constexpr float MIN_WALL_HEIGHT = 0.0001f;

    struct WallSpan {
        float bottom;
        float top;
    };

    std::optional<float> RayTriangleIntersection(
        const Vector3& origin,
        const Vector3& dir,
        const Vector3& v0,
        const Vector3& v1,
        const Vector3& v2,
        const float maxDistance
    ) {
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

    std::optional<float> RaySphereIntersection(
        const Vector3& origin,
        const Vector3& dir,
        const Vector3& sphereCenter,
        const float radius,
        const float maxDistance
    ) {
        const Vector3 oc = origin - sphereCenter;
        const float b = Vector3Math::Dot(oc, dir);
        const float c = Vector3Math::Dot(oc, oc) - radius * radius;
        const float discriminant = b * b - c;

        if (discriminant < 0.0f) return std::nullopt;

        const float sqrtDiscriminant = std::sqrt(discriminant);
        float t = -b - sqrtDiscriminant;

        if (t < 0.0f) t = -b + sqrtDiscriminant;
        if (t < 0.0f || t > maxDistance) return std::nullopt;

        return t;
    }

    std::optional<float> RayAABBIntersection(
        const Vector3& rayOrigin,
        const Vector3& rayDir,
        const Vector3& boxMin,
        const Vector3& boxMax,
        const float maxDistance
    ) {
        float tMin = 0.0f;
        float tMax = maxDistance;

        const auto checkAxis = [&](
            const float originAxis,
            const float dirAxis,
            const float minAxis,
            const float maxAxis
        ) {
            if (std::abs(dirAxis) < Constants::Epsilon) {
                return originAxis >= minAxis && originAxis <= maxAxis;
            }

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

    bool IsSectorOpenAtHeight(const Sector& sector, const float height) {
        for (const SectorFloor& floor : sector.floors) {
            if (height > floor.floor.height && height < floor.ceiling.height) return true;
        }

        return false;
    }

    void AddSectorBoundaryHeights(const Sector& sector, std::vector<float>& heights) {
        for (const SectorFloor& floor : sector.floors) {
            heights.push_back(floor.floor.height);
            heights.push_back(floor.ceiling.height);
        }
    }

    std::vector<WallSpan> BuildWallSpans(const Sector* frontSector, const Sector* backSector) {
        std::vector<float> heights;

        if (frontSector != nullptr) AddSectorBoundaryHeights(*frontSector, heights);
        if (backSector != nullptr) AddSectorBoundaryHeights(*backSector, heights);

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

            if (!spans.empty() && std::abs(spans.back().top - bottom) <= MIN_WALL_HEIGHT) {
                spans.back().top = top;
            }
            else {
                spans.push_back({bottom, top});
            }
        }

        return spans;
    }
}

namespace GameFunctions {
    std::optional<RayHit> Raycast(
        Level& level,
        const Vector3 pos,
        const Vector3& dir,
        const float length,
        const ID ignoredEntity,
        const bool requireCollider
    ) {
        if (dir.IsZero()) return std::nullopt;

        const Vector3 normalizedDir = Vector3Math::Normalized(dir);

        std::optional<RayHit> rayHit;
        float closestDistance = length;

        const auto submitWallHit = [&](Wall& wall, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.type = RayHitType::Wall;
            hit.wall = &wall;
            hit.entity = nullptr;
            hit.sector = nullptr;
            hit.sectorFloorIndex = -1;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        const auto submitEntityHit = [&](Entity& entity, const float distance) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.type = RayHitType::Entity;
            hit.wall = nullptr;
            hit.entity = &entity;
            hit.sector = nullptr;
            hit.sectorFloorIndex = -1;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        const auto submitSectorHit = [&](
            Sector& sector,
            const int floorIndex,
            const RayHitType type,
            const float distance
        ) {
            if (distance >= closestDistance) return;

            closestDistance = distance;

            RayHit hit;
            hit.type = type;
            hit.wall = nullptr;
            hit.entity = nullptr;
            hit.sector = &sector;
            hit.sectorFloorIndex = floorIndex;
            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        for (Wall& wall : level.walls) {
            const Sector* frontSector = MapQueries::GetSectorByID(level, wall.frontSector);
            const Sector* backSector = MapQueries::GetSectorByID(level, wall.backSector);

            if (frontSector == backSector) backSector = nullptr;

            std::vector<WallSpan> spans = BuildWallSpans(frontSector, backSector);

            if (spans.empty() && frontSector == nullptr && backSector == nullptr) {
                spans.push_back({0.0f, 32.0f});
            }

            for (const WallSpan& span : spans) {
                const Vector3 bottomStart = {wall.start.x, span.bottom, wall.start.y};
                const Vector3 bottomEnd = {wall.end.x, span.bottom, wall.end.y};
                const Vector3 topEnd = {wall.end.x, span.top, wall.end.y};
                const Vector3 topStart = {wall.start.x, span.top, wall.start.y};

                const std::optional<float> firstHit = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    bottomStart,
                    bottomEnd,
                    topEnd,
                    closestDistance
                );

                if (firstHit.has_value()) submitWallHit(wall, *firstHit);

                const std::optional<float> secondHit = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    bottomStart,
                    topEnd,
                    topStart,
                    closestDistance
                );

                if (secondHit.has_value()) submitWallHit(wall, *secondHit);
            }
        }

        for (Entity& entity : level.entities) {
            if (entity.id == ignoredEntity) continue;

            const ComponentTransform* transform = level.transforms.Get(entity.id);
            if (transform == nullptr) [[unlikely]] continue;

            if (!requireCollider) {
                const Vector3 halfSize = transform->scale * 0.5f;

                const Vector3 boxMin = {
                    transform->position.x - halfSize.x,
                    transform->position.y,
                    transform->position.z - halfSize.z
                };

                const Vector3 boxMax = {
                    transform->position.x + halfSize.x,
                    transform->position.y + transform->scale.y,
                    transform->position.z + halfSize.z
                };

                const std::optional<float> hitDistance = RayAABBIntersection(
                    pos,
                    normalizedDir,
                    boxMin,
                    boxMax,
                    closestDistance
                );

                if (hitDistance.has_value()) submitEntityHit(entity, *hitDistance);
                continue;
            }

            const ComponentCollider* collider = level.colliders.Get(entity.id);

            if (collider == nullptr || !collider->isActive || collider->isTrigger) continue;

            if (collider->type == COLLIDERTYPE_SPHERE) {
                const float radius = std::max(0.0f, collider->scale.x);

                const Vector3 center = {
                    transform->position.x,
                    transform->position.y + radius,
                    transform->position.z
                };

                const std::optional<float> hitDistance = RaySphereIntersection(
                    pos,
                    normalizedDir,
                    center,
                    radius,
                    closestDistance
                );

                if (hitDistance.has_value()) submitEntityHit(entity, *hitDistance);
            }
            else if (collider->type == COLLIDERTYPE_BOX) {
                const Vector3 halfSize = collider->scale * 0.5f;

                const Vector3 boxMin = {
                    transform->position.x - halfSize.x,
                    transform->position.y,
                    transform->position.z - halfSize.z
                };

                const Vector3 boxMax = {
                    transform->position.x + halfSize.x,
                    transform->position.y + collider->scale.y,
                    transform->position.z + halfSize.z
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
            for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
                const SectorFloor& floor = sector.floors[floorIndex];

                for (const Triangle& triangle : sector.triangles) {
                    const Vector3 floorA = {triangle.a.x, floor.floor.height, triangle.a.y};
                    const Vector3 floorB = {triangle.b.x, floor.floor.height, triangle.b.y};
                    const Vector3 floorC = {triangle.c.x, floor.floor.height, triangle.c.y};

                    const std::optional<float> floorHit = RayTriangleIntersection(
                        pos,
                        normalizedDir,
                        floorA,
                        floorB,
                        floorC,
                        closestDistance
                    );

                    if (floorHit.has_value()) {
                        submitSectorHit(
                            sector,
                            floorIndex,
                            RayHitType::SectorFloor,
                            *floorHit
                        );
                    }

                    const Vector3 ceilingA = {triangle.a.x, floor.ceiling.height, triangle.a.y};
                    const Vector3 ceilingB = {triangle.b.x, floor.ceiling.height, triangle.b.y};
                    const Vector3 ceilingC = {triangle.c.x, floor.ceiling.height, triangle.c.y};

                    const std::optional<float> ceilingHit = RayTriangleIntersection(
                        pos,
                        normalizedDir,
                        ceilingA,
                        ceilingB,
                        ceilingC,
                        closestDistance
                    );

                    if (ceilingHit.has_value()) {
                        submitSectorHit(
                            sector,
                            floorIndex,
                            RayHitType::SectorCeiling,
                            *ceilingHit
                        );
                    }
                }
            }
        }

        return rayHit;
    }
}