//
// Created by berke on 6/2/2026.
//

#include "Headers/Runtime/Gameplay/GameFunctions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/WallPieces.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Runtime/EventSystem.hpp"

namespace {
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

    // An upright capsule: the side between its cap centres, then each cap.
    std::optional<float> RayCapsuleIntersection(
        const Vector3& origin,
        const Vector3& dir,
        const Vector3& lowCentre,
        const float highCentreY,
        const float radius,
        const float maxDistance
    ) {
        std::optional<float> best;

        const auto consider = [&](const std::optional<float> t) {
            if (t.has_value() && (!best.has_value() || *t < *best)) best = t;
        };

        // The side: an infinite vertical cylinder, kept between the caps.
        const float ox = origin.x - lowCentre.x;
        const float oz = origin.z - lowCentre.z;
        const float a = dir.x * dir.x + dir.z * dir.z;

        if (a > Constants::Epsilon) {
            const float b = ox * dir.x + oz * dir.z;
            const float c = ox * ox + oz * oz - radius * radius;
            const float discriminant = b * b - a * c;

            if (discriminant >= 0.0f) {
                const float sqrtDiscriminant = std::sqrt(discriminant);

                for (const float t : {(-b - sqrtDiscriminant) / a, (-b + sqrtDiscriminant) / a}) {
                    if (t < 0.0f || t > maxDistance) continue;

                    const float y = origin.y + dir.y * t;
                    if (y >= lowCentre.y && y <= highCentreY) consider(t);
                }
            }
        }

        consider(RaySphereIntersection(origin, dir, lowCentre, radius, maxDistance));
        consider(RaySphereIntersection(origin, dir, {lowCentre.x, highCentreY, lowCentre.z}, radius, maxDistance));

        return best;
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

        const auto checkAxis = [&](const float originAxis, const float dirAxis, const float minAxis, const float maxAxis) {
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

    // Where the ray's top-down (x, z) path crosses segment start-end, as a
    // distance along the ray, if that's within maxDistance. `dir` is the
    // normalized 3D direction, so the distance along the path is the same as
    // along the ray. Slightly generous at the ends, since this only decides
    // which walls get the exact 3D test and which sectors the ray walks into.
    std::optional<float> RaySegmentCrossing2D(const Vector3& origin, const Vector3& dir, const Vector2& start,
                                              const Vector2& end, const float maxDistance) {
        constexpr float tolerance = 1e-4f;

        const float edgeX = end.x - start.x;
        const float edgeZ = end.y - start.y;

        // Parallel in top-down view (or a straight up/down ray): the ray runs
        // along the wall's plane, which the 3D test never counts as a hit.
        const float denominator = dir.x * edgeZ - dir.z * edgeX;
        if (denominator == 0.0f) return std::nullopt;

        const float toStartX = start.x - origin.x;
        const float toStartZ = start.y - origin.z;

        const float along = (toStartX * edgeZ - toStartZ * edgeX) / denominator;
        const float onSegment = (toStartX * dir.z - toStartZ * dir.x) / denominator;

        const float slack = tolerance * (1.0f + std::abs(along));
        if (along < -slack || along > maxDistance + slack) return std::nullopt;
        if (onSegment < -tolerance || onSegment > 1.0f + tolerance) return std::nullopt;

        return along;
    }

    // Top-down distance from `point` to segment start-end.
    float PointSegmentDistance2D(const Vector2& point, const Vector2& start, const Vector2& end) {
        const float edgeX = end.x - start.x;
        const float edgeZ = end.y - start.y;
        const float lengthSquared = edgeX * edgeX + edgeZ * edgeZ;

        float t = 0.0f;
        if (lengthSquared > 0.0f)
            t = std::clamp(((point.x - start.x) * edgeX + (point.y - start.y) * edgeZ) / lengthSquared, 0.0f, 1.0f);

        const float dx = point.x - (start.x + edgeX * t);
        const float dz = point.y - (start.y + edgeZ * t);
        return std::sqrt(dx * dx + dz * dz);
    }

    // Distance along the ray to a floor or ceiling's plane, slope included,
    // if it's within maxDistance. Same parallel cutoff as RayTriangleIntersection.
    std::optional<float> RayFlatDistance(const Vector3& origin, const Vector3& dir, const SectorSurface& surface,
                                         const WallPieces::SectorBounds& bounds, const float maxDistance) {
        // A slope is linear in map X/Z, so three samples give the plane
        // height = base + slopeX * x + slopeZ * z exactly, from the same
        // function the renderer's geometry uses.
        const float base = WallPieces::GetSurfaceHeight(surface, bounds, {0.0f, 0.0f});
        const float slopeX = WallPieces::GetSurfaceHeight(surface, bounds, {1.0f, 0.0f}) - base;
        const float slopeZ = WallPieces::GetSurfaceHeight(surface, bounds, {0.0f, 1.0f}) - base;

        const float denominator = dir.y - slopeX * dir.x - slopeZ * dir.z;
        if (std::abs(denominator) < Constants::Epsilon) return std::nullopt;

        const float t = (base + slopeX * origin.x + slopeZ * origin.z - origin.y) / denominator;
        if (t < 0.0f || t > maxDistance) return std::nullopt;

        return t;
    }

    // Inclusive, either winding. A zero-area triangle contains nothing, same
    // as the ray-triangle test.
    bool PointInTriangle2D(const Vector2& p, const Triangle& triangle) {
        const Vector2& a = triangle.a;
        const Vector2& b = triangle.b;
        const Vector2& c = triangle.c;

        if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) == 0.0f) return false;

        const auto side = [&p](const Vector2& from, const Vector2& to) {
            return (to.x - from.x) * (p.y - from.y) - (to.y - from.y) * (p.x - from.x);
        };

        const float ab = side(triangle.a, triangle.b);
        const float bc = side(triangle.b, triangle.c);
        const float ca = side(triangle.c, triangle.a);

        const bool hasNegative = ab < 0.0f || bc < 0.0f || ca < 0.0f;
        const bool hasPositive = ab > 0.0f || bc > 0.0f || ca > 0.0f;

        return !(hasNegative && hasPositive);
    }

    // Index of the sector with this ID, or -1.
    int SectorIndexOf(Level& level, const ID sectorID) {
        const Sector* sector = MapQueries::GetSectorByID(level, sectorID);
        return sector == nullptr ? -1 : static_cast<int>(sector - level.sectors.data());
    }

    // Which hit wins when two are at exactly the same distance: the one a scan
    // of the whole level would have found first. Walls come first (in level
    // order), then entities, then floors and ceilings (in sector order, floor
    // before ceiling), so walking the map in any order gives the same answer.
    struct HitOrder {
        int category = 0;
        std::size_t index = 0;
        int sub = 0;

        bool operator<(const HitOrder& other) const {
            if (category != other.category) return category < other.category;
            if (index != other.index) return index < other.index;
            return sub < other.sub;
        }
    };

    constexpr int ORDER_WALL = 0;
    constexpr int ORDER_ENTITY = 1;
    constexpr int ORDER_FLAT = 2;

    // Scratch space reused by every ray on this thread, so a ray allocates
    // nothing once these have grown.
    struct RayScratch {
        std::vector<WallPieces::WallPiece> pieces;

        std::vector<int> sectorStack;
        std::vector<int> visitedSectors;

        // visitMark[i] == visitGeneration means sector i was reached by the
        // current ray; bumping the generation clears every mark at once.
        std::vector<unsigned> visitMark;
        unsigned visitGeneration = 0;

        // Sector the last ray started in: the first guess for the next one.
        int lastOriginSector = -1;

        void BeginWalk(const std::size_t sectorCount) {
            sectorStack.clear();
            visitedSectors.clear();

            if (visitMark.size() < sectorCount) visitMark.resize(sectorCount, 0);

            if (++visitGeneration == 0) {
                std::ranges::fill(visitMark, 0u);
                visitGeneration = 1;
            }
        }

        // True the first time sector `index` is reached by this ray.
        bool Visit(const int index) {
            if (visitMark[index] == visitGeneration) return false;
            visitMark[index] = visitGeneration;
            return true;
        }
    };

    thread_local RayScratch rayScratch;

    std::optional<RayHit> CastRay(
        Level& level,
        const Vector3 pos,
        const Vector3& dir,
        const float length,
        const ID ignoredEntity,
        const bool requireCollider,
        const bool hitEntities
    ) {
        if (dir.IsZero()) return std::nullopt;

        const Vector3 normalizedDir = Vector3Math::Normalized(dir);
        RayScratch& scratch = rayScratch;

        std::optional<RayHit> rayHit;
        HitOrder bestOrder;
        float closestDistance = length;

        // Strictly closer wins; at exactly the same distance HitOrder decides.
        const auto beats = [&](const float distance, const HitOrder& order) {
            if (distance < closestDistance) return true;
            return distance == closestDistance && rayHit.has_value() && order < bestOrder;
        };

        const auto submit = [&](const float distance, const HitOrder& order, RayHit hit) {
            closestDistance = distance;
            bestOrder = order;

            hit.position = pos + normalizedDir * distance;
            hit.distance = distance;

            rayHit = hit;
        };

        // Slopes are measured from each sector's bounds. Only sectors the ray
        // gets near are looked up, so they're computed on demand per ray.
        WallPieces::SectorBoundsCache boundsCache;

        // The exact 3D test for a wall the ray's top-down path crosses,
        // against the pieces the renderer draws, sloped edges included.
        const auto testWall = [&](Wall& wall) {
            scratch.pieces.clear();
            WallPieces::Build(level, wall, boundsCache, scratch.pieces);

            const HitOrder order{ORDER_WALL, static_cast<std::size_t>(&wall - level.walls.data()), 0};

            const auto submitWallHit = [&](const float distance) {
                if (!beats(distance, order)) return;

                RayHit hit;
                hit.type = RayHitType::Wall;
                hit.wall = &wall;
                submit(distance, order, hit);
            };

            for (const WallPieces::WallPiece& piece : scratch.pieces) {
                if (piece.slot == WallSurfaceSlot::Middle && wall.middleIgnoredByRaycast) continue;

                const Vector2 pieceStart = piece.Start(wall);
                const Vector2 pieceEnd = piece.End(wall);

                const Vector3 bottomStart = {pieceStart.x, piece.bottomStart, pieceStart.y};
                const Vector3 bottomEnd = {pieceEnd.x, piece.bottomEnd, pieceEnd.y};
                const Vector3 topEnd = {pieceEnd.x, piece.topEnd, pieceEnd.y};
                const Vector3 topStart = {pieceStart.x, piece.topStart, pieceStart.y};

                const std::optional<float> firstHit = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    bottomStart,
                    bottomEnd,
                    topEnd,
                    closestDistance
                );

                if (firstHit.has_value()) submitWallHit(*firstHit);

                const std::optional<float> secondHit = RayTriangleIntersection(
                    pos,
                    normalizedDir,
                    bottomStart,
                    topEnd,
                    topStart,
                    closestDistance
                );

                if (secondHit.has_value()) submitWallHit(*secondHit);
            }
        };

        // Floors and ceilings are planes (tilted when sloped), so the ray
        // meets each one at a single point: find it once, then check whether
        // it's inside the sector, instead of a 3D ray-triangle test against
        // every triangle.
        const auto testFlat = [&](Sector& sector, const int floorIndex, const SectorSurface& surface, const RayHitType type) {
            const std::optional<float> distance =
                RayFlatDistance(pos, normalizedDir, surface, boundsCache.Get(&sector), closestDistance);
            if (!distance.has_value()) return;

            const HitOrder order{
                ORDER_FLAT,
                static_cast<std::size_t>(&sector - level.sectors.data()),
                floorIndex * 2 + (type == RayHitType::SectorCeiling ? 1 : 0)
            };

            if (!beats(*distance, order)) return;

            const Vector2 point = {pos.x + normalizedDir.x * *distance, pos.z + normalizedDir.z * *distance};

            for (const Triangle& triangle : sector.triangles) {
                if (!PointInTriangle2D(point, triangle)) continue;

                RayHit hit;
                hit.type = type;
                hit.sector = &sector;
                hit.sectorFloorIndex = floorIndex;
                submit(*distance, order, hit);
                return;
            }
        };

        const auto testSectorFlats = [&](Sector& sector) {
            for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
                const SectorFloor& floor = sector.floors[floorIndex];

                testFlat(sector, floorIndex, floor.floor, RayHitType::SectorFloor);
                testFlat(sector, floorIndex, floor.ceiling, RayHitType::SectorCeiling);
            }
        };

        const auto submitEntityHit = [&](const ID entityID, const float distance) {
            Entity* entity = level.GetEntity(entityID);
            if (entity == nullptr) [[unlikely]] return;

            const HitOrder order{ORDER_ENTITY, static_cast<std::size_t>(entity - level.entities.data()), 0};
            if (!beats(distance, order)) return;

            RayHit hit;
            hit.type = RayHitType::Entity;
            hit.entity = entity;
            submit(distance, order, hit);
        };

        // ---- Walls: walk the sectors the ray passes over ----

        // Only the walls of sectors the ray's top-down path actually passes
        // over can be hit, so start in the sector holding the origin and step
        // into the next sector through each wall the path crosses. A wall
        // with no sector on the far side is the edge of the map: a ray that
        // gets past one before hitting anything could reach any sector, so
        // then (and when the origin is outside the map) every wall is tested.
        int hintSector = scratch.lastOriginSector;
        if (const ComponentTransform* caster = level.transforms.Get(ignoredEntity)) hintSector = caster->sectorIndex;

        const int originSector = MapQueries::FindSectorContainingPoint(level.sectors, {pos.x, pos.z}, hintSector);
        if (originSector >= 0) scratch.lastOriginSector = originSector;

        bool walkedAllWalls = false;

        // An origin this close to a wall also walks into the sector behind it.
        // Right on an edge, which sector holds the origin depends on rounding,
        // and the polygon test that picked originSector doesn't always agree
        // with the floor triangles. Without this, a straight up/down ray (which
        // crosses no wall) started on an edge could miss the neighbour's floor.
        // Walking into an extra sector never changes the answer, it only tests more.
        constexpr float ORIGIN_EDGE_SLACK = 0.01f;
        const Vector2 origin2D = {pos.x, pos.z};

        if (originSector >= 0) {
            scratch.BeginWalk(level.sectors.size());
            scratch.Visit(originSector);
            scratch.sectorStack.push_back(originSector);

            float leftMapAt = std::numeric_limits<float>::max();

            while (!scratch.sectorStack.empty()) {
                const int sectorIndex = scratch.sectorStack.back();
                scratch.sectorStack.pop_back();
                scratch.visitedSectors.push_back(sectorIndex);

                for (Wall* wall : level.sectors[sectorIndex].walls) {
                    if (wall == nullptr) [[unlikely]] continue;

                    const std::optional<float> crossing =
                        RaySegmentCrossing2D(pos, normalizedDir, wall->start, wall->end, closestDistance);
                    const bool nearOrigin = PointSegmentDistance2D(origin2D, wall->start, wall->end) <= ORIGIN_EDGE_SLACK;

                    if (!crossing.has_value() && !nearOrigin) continue;

                    if (crossing.has_value()) testWall(*wall);

                    const int front = SectorIndexOf(level, wall->frontSector);
                    const int back = SectorIndexOf(level, wall->backSector);
                    const int beyond = front == sectorIndex ? back : front;

                    if (beyond >= 0) {
                        if (scratch.Visit(beyond)) scratch.sectorStack.push_back(beyond);
                    }
                    else if (crossing.has_value()) leftMapAt = std::min(leftMapAt, *crossing);
                }
            }

            // Got past the edge of the map before the nearest hit. The slack
            // keeps a ray that simply hits that edge wall on the fast path.
            const float slack = 1e-3f * (1.0f + std::abs(closestDistance));
            walkedAllWalls = leftMapAt + slack >= closestDistance;
        }

        if (!walkedAllWalls) {
            for (Wall& wall : level.walls) {
                // Walls are vertical, so a ray can only hit one whose line its
                // top-down path crosses. Checked first: almost every wall fails it.
                if (!RaySegmentCrossing2D(pos, normalizedDir, wall.start, wall.end, closestDistance)) continue;
                testWall(wall);
            }
        }

        // ---- Entities ----

        if (hitEntities && requireCollider) {
            // Straight through the active colliders, so entities without one
            // cost nothing. Every collider on an entity counts, each at its
            // own offset.
            const auto testCollider = [&](const ComponentCollider& collider) {
                if (collider.isTrigger || collider.ownerID == ignoredEntity) return;

                const ComponentTransform* transform = level.transforms.Get(collider.ownerID);
                if (transform == nullptr) [[unlikely]] return;

                const Vector3 base = collider.Base(*transform);
                std::optional<float> hitDistance;

                if (collider.type == COLLIDERTYPE_SPHERE) {
                    const float radius = collider.Radius(*transform);
                    const Vector3 center = {base.x, base.y + radius, base.z};

                    hitDistance = RaySphereIntersection(pos, normalizedDir, center, radius, closestDistance);
                }
                else if (collider.type == COLLIDERTYPE_CAPSULE) {
                    const float radius = collider.Radius(*transform);
                    const Vector3 lowCentre = {base.x, base.y + radius, base.z};
                    const float highCentreY = base.y + collider.Height(*transform) - radius;

                    hitDistance = RayCapsuleIntersection(pos, normalizedDir, lowCentre, highCentreY, radius, closestDistance);
                }
                else if (collider.type == COLLIDERTYPE_BOX) {
                    const Vector3 size = collider.WorldSize(*transform);
                    const Vector3 halfSize = size * 0.5f;
                    const Vector3 boxMin = {base.x - halfSize.x, base.y, base.z - halfSize.z};
                    const Vector3 boxMax = {base.x + halfSize.x, base.y + size.y, base.z + halfSize.z};

                    hitDistance = RayAABBIntersection(pos, normalizedDir, boxMin, boxMax, closestDistance);
                }

                if (hitDistance.has_value()) submitEntityHit(collider.ownerID, *hitDistance);
            };

            for (const ComponentCollider& collider : level.colliders.ActiveRound()) testCollider(collider);
            for (const ComponentCollider& collider : level.colliders.ActiveBoxes()) testCollider(collider);
        }
        else if (hitEntities) {
            for (const Entity& entity : level.entities) {
                // Rays pass through disabled entities.
                if (entity.id == ignoredEntity || !entity.enabled) continue;

                const ComponentTransform* transform = level.transforms.Get(entity.id);
                if (transform == nullptr) [[unlikely]] continue;

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

                if (hitDistance.has_value()) submitEntityHit(entity.id, *hitDistance);
            }
        }

        // ---- Floors and ceilings: only those of the sectors walked over ----

        if (walkedAllWalls) {
            for (const int sectorIndex : scratch.visitedSectors) testSectorFlats(level.sectors[sectorIndex]);
        }
        else {
            for (Sector& sector : level.sectors) testSectorFlats(sector);
        }

        return rayHit;
    }
}

namespace GameFunctions {
    std::optional<RayHit> Raycast(
        Level& level,
        const Vector3 pos,
        const Vector3& dir,
        const float length,
        const ID ignoredEntity,
        const bool requireCollider,
        const RayType type
    ) {
        std::optional<RayHit> hit = CastRay(level, pos, dir, length, ignoredEntity, requireCollider, true);

        // The ignored entity is the one casting it.
        EventSystem::ReportRay(level, type, ignoredEntity, pos, dir, length, hit);
        return hit;
    }

    std::optional<RayHit> RaycastGeometry(Level& level, const Vector3 pos, const Vector3& dir, const float length) {
        return CastRay(level, pos, dir, length, INVALID_ENTITY_ID, false, false);
    }

    bool LineOfSight(Level& level, const Vector3& from, const Vector3& to) {
        const Vector3 delta = to - from;
        const float distance = Vector3Math::Length(delta);

        if (distance < Constants::Epsilon) return true;

        return !CastRay(level, from, delta, distance, INVALID_ENTITY_ID, false, false).has_value();
    }
}
