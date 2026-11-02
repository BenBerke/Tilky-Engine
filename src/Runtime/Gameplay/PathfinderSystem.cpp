//
// Created by berke on 11/1/2026.
//

#include "Headers/Runtime/Gameplay/PathfinderSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/MathFunctions.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Runtime/EventSystem.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"

namespace {
    // Doom's dirtype_t, with +x east and +z north.
    enum Direction : std::uint8_t {
        DIR_EAST, DIR_NORTHEAST, DIR_NORTH, DIR_NORTHWEST,
        DIR_WEST, DIR_SOUTHWEST, DIR_SOUTH, DIR_SOUTHEAST,
        DIR_NONE
    };

    constexpr float DIAGONAL = 0.70710678f;
    constexpr float DIR_X[8] = {1.0f, DIAGONAL, 0.0f, -DIAGONAL, -1.0f, -DIAGONAL, 0.0f, DIAGONAL};
    constexpr float DIR_Z[8] = {0.0f, DIAGONAL, 1.0f, DIAGONAL, 0.0f, -DIAGONAL, -1.0f, -DIAGONAL};

    constexpr Direction OPPOSITE[9] = {
        DIR_WEST, DIR_SOUTHWEST, DIR_SOUTH, DIR_SOUTHEAST,
        DIR_EAST, DIR_NORTHEAST, DIR_NORTH, DIR_NORTHWEST, DIR_NONE
    };

    // Indexed ((dz < 0) << 1) + (dx > 0), as in P_NewChaseDir.
    constexpr Direction DIAGONALS[4] = {DIR_NORTHWEST, DIR_NORTHEAST, DIR_SOUTHWEST, DIR_SOUTHEAST};

    // A step physics cut to less than this share of its length was blocked.
    constexpr float BLOCKED_STEP_FRACTION = 0.5f;

    // Doom's tic rate: Push Friction is per tic.
    constexpr float TICS_PER_SECOND = 35.0f;

    // A push slower than this is over.
    constexpr float PUSH_STOP_SPEED = 1.0f;

    // Doom's P_Random: 0..255.
    int RandomByte() { return static_cast<int>(MathFunctions::XorShift32() & 0xFFu); }

    // The owner's physical size, from its base collider. Without one it's a
    // point with the default step size.
    struct Body {
        float radius = 0.0f;
        float height = 0.0f;
        float stepSize = ComponentCollider{}.stepSize;
        float feetY = 0.0f;       // world height of the collider's bottom
        float feetOffset = 0.0f;  // feetY - transform.position.y
        int sectorHint = -1;
        bool air = false;
        bool canDropOff = false;
    };

    // The floor and ceiling a point ahead stands under.
    struct Ground {
        float floor = 0.0f;
        float ceiling = 0.0f;
    };

    // What is under a point: the sector and which of its floors, for a door.
    struct GroundSource {
        ID sectorID = INVALID_ID;
        int floorIndex = 0;
    };

    // The floor/ceiling of `sector` under (x, z) for a body at `referenceY`.
    bool SampleSector(const Sector& sector, const float x, const float z, const float referenceY, Ground& out, GroundSource* source = nullptr) {
        const Vector2 point = {x, z};
        const int floorIndex = MapQueries::FindFloorAtHeight(sector, point, referenceY);
        if (floorIndex < 0) return false;

        const SectorFloor& floor = sector.floors[floorIndex];
        out.floor = MapQueries::SurfaceHeightAt(sector, floor.floor, point);
        out.ceiling = MapQueries::SurfaceHeightAt(sector, floor.ceiling, point);
        if (source != nullptr) *source = {sector.id, floorIndex};
        return true;
    }

    // Finds the floor/ceiling under (x, z) for a body at `referenceY`. False
    // outside every sector.
    bool SampleGround(const Level& level, const int hint, const float x, const float z, const float referenceY, Ground& out, GroundSource* source = nullptr) {
        const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, {x, z}, hint);
        if (sectorIndex < 0) return false;
        return SampleSector(level.sectors[sectorIndex], x, z, referenceY, out, source);
    }

    // The walls and blocking linedefs near a body, gathered once a frame so
    // its few step checks don't each go through the whole level.
    struct NearbyLines {
        std::vector<const Wall*> walls;
        std::vector<const Linedef*> linedefs;
    };

    template <typename Line>
    bool LineNear(const Line& line, const float x, const float z, const float reach) {
        return std::min(line.start.x, line.end.x) <= x + reach && std::max(line.start.x, line.end.x) >= x - reach &&
               std::min(line.start.y, line.end.y) <= z + reach && std::max(line.start.y, line.end.y) >= z - reach;
    }

    void GatherLines(const Level& level, const float x, const float z, const float reach, NearbyLines& out) {
        out.walls.clear();
        out.linedefs.clear();

        for (const Wall& wall : level.walls)
            if (LineNear(wall, x, z, reach)) out.walls.push_back(&wall);

        for (const Linedef& linedef : level.linedefs)
            if (linedef.enabled && linedef.blocksPathfinders && LineNear(linedef, x, z, reach)) out.linedefs.push_back(&linedef);
    }

    // The point of segment a-b closest to (x, z), and how far it is.
    float ClosestOnSegment(const Vector2& a, const Vector2& b, const float x, const float z, float& outX, float& outZ) {
        const float abX = b.x - a.x;
        const float abZ = b.y - a.y;
        const float lengthSq = abX * abX + abZ * abZ;
        const float t = lengthSq > 0.0f ? std::clamp(((x - a.x) * abX + (z - a.y) * abZ) / lengthSq, 0.0f, 1.0f) : 0.0f;
        outX = a.x + abX * t;
        outZ = a.y + abZ * t;
        return std::hypot(x - outX, z - outZ);
    }

    // True when segment a-b crosses the move from (x0, z0) to (x1, z1).
    bool SegmentsCross(const Vector2& a, const Vector2& b, const float x0, const float z0, const float x1, const float z1) {
        const auto side = [](const float ax, const float az, const float bx, const float bz, const float px, const float pz) {
            return (bx - ax) * (pz - az) - (bz - az) * (px - ax);
        };
        const float d1 = side(x0, z0, x1, z1, a.x, a.y);
        const float d2 = side(x0, z0, x1, z1, b.x, b.y);
        const float d3 = side(a.x, a.y, b.x, b.y, x0, z0);
        const float d4 = side(a.x, a.y, b.x, b.y, x1, z1);
        return ((d1 > 0.0f) != (d2 > 0.0f)) && ((d3 > 0.0f) != (d4 > 0.0f));
    }

    // Whether a move from (x0, z0) to (x1, z1) takes a body of `radius` into
    // this line. A line it is already touching only counts when the move goes
    // closer, so it can always get away from one. `closestX/Z` is the nearest
    // point on the line after the move.
    bool MoveTouchesLine(const Vector2& a, const Vector2& b, const float x0, const float z0, const float x1, const float z1,
                         const float radius, float& closestX, float& closestZ) {
        const float after = ClosestOnSegment(a, b, x1, z1, closestX, closestZ);

        if (SegmentsCross(a, b, x0, z0, x1, z1)) return true;
        if (after >= radius) return false;

        float ignoredX, ignoredZ;
        const float before = ClosestOnSegment(a, b, x0, z0, ignoredX, ignoredZ);
        return after < before - Constants::Epsilon;
    }

    struct ChaseContext {
        const Level& level;
        const Body& body;
        const NearbyLines& lines;
        ID ownerID;
        float x, z;
        float standingFloor;
        float stepLength;
        std::vector<EventSystem::WalkerDoorRequest>& doorRequests;
        bool waitingForDoor = false;
    };

    // Doom's P_TryMove / P_CheckPosition: the body's square bounding box moved
    // one step along `direction`, sampled at its centre, edges and corners,
    // plus every line the body would touch. The highest floor and lowest
    // ceiling found are what it must fit between; `standingFloor` is the floor
    // under the body now. `aheadOut` gets those two (only when every sample is
    // on the map). `tightOut` gets what has the lowest ceiling, when it is
    // too tight a fit: a door to try.
    bool CanStep(const ChaseContext& context, const Direction direction, const float stepLength,
                 Ground* aheadOut = nullptr, GroundSource* tightOut = nullptr) {
        if (direction == DIR_NONE) return false;

        const Level& level = context.level;
        const Body& body = context.body;

        const float step = std::max(stepLength, 0.5f);
        const float centerX = context.x + DIR_X[direction] * step;
        const float centerZ = context.z + DIR_Z[direction] * step;

        constexpr float SAMPLE_X[9] = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f};
        constexpr float SAMPLE_Z[9] = {0.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};
        const int samples = body.radius > 0.0f ? 9 : 1;

        float highestFloor = -std::numeric_limits<float>::infinity();
        float lowestFloor = std::numeric_limits<float>::infinity();
        float lowestCeiling = std::numeric_limits<float>::infinity();
        GroundSource tightest;

        const auto take = [&](const Ground& ground, const GroundSource& source) {
            highestFloor = std::max(highestFloor, ground.floor);
            lowestFloor = std::min(lowestFloor, ground.floor);
            if (ground.ceiling < lowestCeiling) {
                lowestCeiling = ground.ceiling;
                tightest = source;
            }
        };

        // Lines first: a wall or linedef blocks whatever the heights.
        for (const Linedef* linedef : context.lines.linedefs) {
            float closestX, closestZ;
            if (MoveTouchesLine(linedef->start, linedef->end, context.x, context.z, centerX, centerZ, body.radius, closestX, closestZ))
                return false;
        }

        for (const Wall* wall : context.lines.walls) {
            float closestX, closestZ;
            if (!MoveTouchesLine(wall->start, wall->end, context.x, context.z, centerX, centerZ, body.radius, closestX, closestZ))
                continue;

            if (wall->blocksPathfinders || !wall->IsPortal() || (wall->HasMiddle() && wall->middleHasCollision)) return false;

            // A portal: both sides' heights count, even a sector too thin
            // for any sample to land in.
            for (const ID sectorID : {wall->frontSector, wall->backSector}) {
                const Sector* sector = MapQueries::GetSectorByID(level, sectorID);
                if (sector == nullptr) return false;

                Ground ground;
                GroundSource source;
                if (!SampleSector(*sector, closestX, closestZ, body.feetY, ground, &source)) return false;
                take(ground, source);
            }
        }

        for (int i = 0; i < samples; ++i) {
            Ground ground;
            GroundSource source;
            // Off the map: a solid wall.
            if (!SampleGround(level, body.sectorHint, centerX + SAMPLE_X[i] * body.radius, centerZ + SAMPLE_Z[i] * body.radius, body.feetY, ground, &source))
                return false;

            take(ground, source);
        }

        if (aheadOut != nullptr) *aheadOut = {highestFloor, lowestCeiling};

        // Too tight to fit at any height.
        if (lowestCeiling - highestFloor < body.height) {
            if (tightOut != nullptr) *tightOut = tightest;
            return false;
        }

        // A flyer rises or sinks to fit instead (Doom's floatok).
        if (body.air) return true;

        if (highestFloor - context.standingFloor > body.stepSize) return false; // too high a step

        if (!body.canDropOff) {
            if (highestFloor - lowestFloor > body.stepSize) return false;           // would hang over a drop-off
            if (context.standingFloor - highestFloor > body.stepSize) return false; // stepping down off a ledge
        }

        if (lowestCeiling - std::max(highestFloor, body.feetY) < body.height) { // head hits the ceiling
            if (tightOut != nullptr) *tightOut = tightest;
            return false;
        }

        return true;
    }

    // Doom's P_TryWalk: takes the direction if a step that way is possible and
    // holds it for a random number of chase steps (movecount). Like P_Move, a
    // door in the way that can open counts as success: it waits there.
    bool TryWalk(ComponentPathfinder& pathfinder, ChaseContext& context, const Direction direction) {
        GroundSource tight;

        if (!CanStep(context, direction, context.stepLength, nullptr, &tight)) {
            if (!pathfinder.OpensDoors() || tight.sectorID == INVALID_ID) return false;

            const EventSystem::WalkerDoor door = EventSystem::CheckDoorForWalker(context.level, tight.sectorID, tight.floorIndex, context.ownerID);
            if (door == EventSystem::WalkerDoor::NONE) return false;

            if (door == EventSystem::WalkerDoor::OPEN)
                context.doorRequests.push_back({tight.sectorID, tight.floorIndex, context.ownerID});

            context.waitingForDoor = true;
            pathfinder.moveDirection = DIR_NONE;
            pathfinder.moveTimer = 0.0f;
            return true;
        }

        const float low = std::max(0.0f, std::min(pathfinder.holdMinSteps, pathfinder.holdMaxSteps));
        const float high = std::max(0.0f, std::max(pathfinder.holdMinSteps, pathfinder.holdMaxSteps));
        const float steps = std::floor(low + static_cast<float>(RandomByte()) / 256.0f * (high - low + 1.0f));

        pathfinder.moveDirection = direction;
        pathfinder.moveTimer = steps * std::max(0.0f, pathfinder.chaseStep);
        return true;
    }

    // Doom's P_NewChaseDir. `deadzone` is its 10-unit "close enough on this
    // axis" distance.
    void NewChaseDirection(ComponentPathfinder& pathfinder, ChaseContext& context, const float dx, const float dz, const float deadzone) {
        const Direction oldDirection = pathfinder.moveDirection < DIR_NONE ? static_cast<Direction>(pathfinder.moveDirection) : DIR_NONE;
        const Direction turnaround = OPPOSITE[oldDirection];

        Direction d1 = dx > deadzone ? DIR_EAST : dx < -deadzone ? DIR_WEST : DIR_NONE;
        Direction d2 = dz < -deadzone ? DIR_SOUTH : dz > deadzone ? DIR_NORTH : DIR_NONE;

        // Try the diagonal straight at the destination.
        if (d1 != DIR_NONE && d2 != DIR_NONE) {
            const Direction diagonal = DIAGONALS[((dz < 0.0f) << 1) + (dx > 0.0f)];
            if (diagonal != turnaround && TryWalk(pathfinder, context, diagonal)) return;
        }

        // Then the two axes, the longer one first (sometimes swapped at random).
        const bool swapRoll = static_cast<float>(RandomByte()) / 256.0f * 100.0f < pathfinder.swapChance;
        if (swapRoll || std::abs(dz) > std::abs(dx)) std::swap(d1, d2);

        if (d1 == turnaround) d1 = DIR_NONE;
        if (d2 == turnaround) d2 = DIR_NONE;

        if (d1 != DIR_NONE && TryWalk(pathfinder, context, d1)) return;
        if (d2 != DIR_NONE && TryWalk(pathfinder, context, d2)) return;

        // Keep going the old way.
        if (oldDirection != DIR_NONE && TryWalk(pathfinder, context, oldDirection)) return;

        // Any direction but back, swept from a random end.
        if (RandomByte() & 1) {
            for (int direction = DIR_EAST; direction <= DIR_SOUTHEAST; ++direction)
                if (direction != turnaround && TryWalk(pathfinder, context, static_cast<Direction>(direction))) return;
        }
        else {
            for (int direction = DIR_SOUTHEAST; direction >= DIR_EAST; --direction)
                if (direction != turnaround && TryWalk(pathfinder, context, static_cast<Direction>(direction))) return;
        }

        // Last resort: turn around.
        if (turnaround != DIR_NONE && TryWalk(pathfinder, context, turnaround)) return;

        pathfinder.moveDirection = DIR_NONE; // stuck
    }

    // Stops walking. Any push goes on.
    void StopHorizontal(ComponentPathfinder& pathfinder, ComponentRigidbody* rigidbody) {
        if (pathfinder.driving && rigidbody != nullptr) {
            rigidbody->velocity.x = pathfinder.pushX;
            rigidbody->velocity.z = pathfinder.pushZ;
        }
        pathfinder.moveDirection = DIR_NONE;
        pathfinder.expectedStep = 0.0f;
    }

    // Stops everything it drives, the flyer's height included, and lets go.
    void Release(ComponentPathfinder& pathfinder, ComponentRigidbody* rigidbody) {
        if (pathfinder.driving && rigidbody != nullptr && pathfinder.IsAirEntity()) rigidbody->velocity.y = 0.0f;
        StopHorizontal(pathfinder, rigidbody);
        pathfinder.driving = false;
        pathfinder.arrived = false;
        pathfinder.moving = false;
        pathfinder.stuck = false;
        pathfinder.stuckTimer = 0.0f;
        pathfinder.pushX = pathfinder.pushZ = 0.0f;
    }

    float TargetRadius(const Level& level, const ID targetID) {
        const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, targetID);
        const ComponentTransform* transform = level.transforms.Get(targetID);
        return collider != nullptr && transform != nullptr ? collider->Radius(*transform) : 0.0f;
    }

    // Doom's A_Chase turn: one octant toward where it walks, per chase step.
    void TurnStep(ComponentPathfinder& pathfinder, ComponentTransform& transform, const float deltaTime) {
        if (pathfinder.moveDirection >= DIR_NONE) return;

        pathfinder.turnTimer -= deltaTime;
        if (pathfinder.facingOctant < DIR_NONE && pathfinder.turnTimer > 0.0f) return;
        pathfinder.turnTimer = std::max(pathfinder.chaseStep, deltaTime);

        if (pathfinder.facingOctant >= DIR_NONE) pathfinder.facingOctant = pathfinder.moveDirection;
        else {
            const int delta = (pathfinder.moveDirection - pathfinder.facingOctant + 8) % 8;
            if (delta == 0) return;
            pathfinder.facingOctant = static_cast<std::uint8_t>((pathfinder.facingOctant + (delta <= 4 ? 1 : 7)) % 8);
        }

        Quaternion facing;
        if (QuaternionMath::LookRotation({DIR_X[pathfinder.facingOctant], 0.0f, DIR_Z[pathfinder.facingOctant]}, true, facing))
            transform.rotation = facing;
    }
}

namespace PathfinderSystem {
    void Start(Level& level) {
        for (ComponentPathfinder& pathfinder : level.pathfinders.components) {
            pathfinder.moveDirection = DIR_NONE;
            pathfinder.facingOctant = DIR_NONE;
            pathfinder.moveTimer = 0.0f;
            pathfinder.turnTimer = 0.0f;
            pathfinder.pauseTimer = 0.0f;
            pathfinder.stuckTimer = 0.0f;
            pathfinder.blockedCooldown = 0.0f;
            pathfinder.expectedStep = 0.0f;
            pathfinder.pushX = pathfinder.pushZ = 0.0f;
            pathfinder.driving = false;
            pathfinder.arrived = false;
            pathfinder.moving = false;
            pathfinder.stuck = false;
        }
    }

    void Update(Level& level, const float deltaTime, const NoticeHandler& onNotice) {
        if (deltaTime <= 0.0f) return;

        // Kept until every pathfinder has moved: a handler can add or remove
        // components, which would shift the storage under the loop. Doors too,
        // since a door's dispatch can do the same.
        struct Notice {
            ID ownerID;
            NoticeKind kind;
            ID targetID;
        };
        std::vector<Notice> notices;
        std::vector<EventSystem::WalkerDoorRequest> doorRequests;

        NearbyLines lines;

        for (ComponentPathfinder& pathfinder : level.pathfinders.components) {
            // A disabled entity is frozen, its velocity kept.
            if (!pathfinder.ownerEnabled) continue;

            const ID ownerID = pathfinder.ownerID;
            ComponentRigidbody* rigidbody = level.rigidbodies.Get(ownerID);

            // Only an entity's first pathfinder moves it.
            if (level.pathfinders.Get(ownerID) != &pathfinder) continue;

            ComponentTransform* transform = level.transforms.Get(ownerID);
            if (transform == nullptr || !pathfinder.isActive) {
                const bool wasMoving = pathfinder.moving;
                Release(pathfinder, rigidbody);
                if (wasMoving) notices.push_back({ownerID, NoticeKind::STOPPED_MOVING, INVALID_ID});
                continue;
            }

            // ── Push ─────────────────────────────────────────────────────
            // What changed the velocity since physics left it wasn't the
            // walking: it stays on top of it, fading as in Doom.
            if (rigidbody != nullptr) {
                if (pathfinder.driving) {
                    pathfinder.pushX += rigidbody->velocity.x - pathfinder.settledX;
                    pathfinder.pushZ += rigidbody->velocity.z - pathfinder.settledZ;
                }
                else {
                    pathfinder.pushX = rigidbody->velocity.x;
                    pathfinder.pushZ = rigidbody->velocity.z;
                }

                const float keep = std::pow(std::clamp(pathfinder.pushFriction, 0.0f, 1.0f), deltaTime * TICS_PER_SECOND);
                pathfinder.pushX *= keep;
                pathfinder.pushZ *= keep;

                if (std::hypot(pathfinder.pushX, pathfinder.pushZ) < PUSH_STOP_SPEED) pathfinder.pushX = pathfinder.pushZ = 0.0f;
            }

            const bool pushed = pathfinder.pushX != 0.0f || pathfinder.pushZ != 0.0f;

            // ── Destination ──────────────────────────────────────────────
            float targetRadius = 0.0f;
            bool lost = false;

            if (pathfinder.input == PathfinderInput::ENTITY) {
                const Entity* target = pathfinder.targetEntityID == ownerID ? nullptr : level.GetEntity(pathfinder.targetEntityID);
                const ComponentTransform* targetTransform = target != nullptr ? level.transforms.Get(target->id) : nullptr;

                // Destroyed (or never set): stop. Disabled: head for where it was.
                if (targetTransform == nullptr) lost = true;
                else {
                    if (target->enabled) pathfinder.destination = targetTransform->position + pathfinder.offset;
                    targetRadius = TargetRadius(level, target->id);
                }
            }

            // ── Body ────────────────────────────────────────────────────
            Body body;
            body.air = pathfinder.IsAirEntity();
            body.canDropOff = pathfinder.CanDropOff();
            body.sectorHint = transform->sectorIndex;
            body.feetY = transform->position.y;

            if (const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, ownerID)) {
                body.radius = collider->Radius(*transform);
                body.height = collider->Height(*transform);
                body.stepSize = collider->stepSize;
                body.feetY = collider->Base(*transform).y;
            }
            body.feetOffset = body.feetY - transform->position.y;

            const float x = transform->position.x;
            const float z = transform->position.z;

            Ground standing;
            const bool onMap = SampleGround(level, body.sectorHint, x, z, body.feetY, standing);
            if (!onMap) standing = {body.feetY, body.feetY + body.height};

            const float speed = std::max(0.0f, pathfinder.speed);
            const float frameStep = speed * deltaTime;
            const float deadzone = std::max(pathfinder.deadzone, 0.0f);

            // Did physics cut the last step short (a wall, another body)? A
            // push moves it by more or less than it walked, so can't say.
            bool blocked = false;
            if (pathfinder.driving && rigidbody != nullptr && pathfinder.expectedStep > Constants::Epsilon && !pushed) {
                const float moved = std::hypot(x - pathfinder.lastX, z - pathfinder.lastZ);
                blocked = moved < pathfinder.expectedStep * BLOCKED_STEP_FRACTION;
            }

            const float dx = pathfinder.destination.x - x;
            const float dy = pathfinder.destination.y - body.feetY;
            const float dz = pathfinder.destination.z - z;
            const float horizontal = std::hypot(dx, dz);

            const bool paused = pathfinder.pauseTimer > 0.0f;
            if (paused) pathfinder.pauseTimer = std::max(0.0f, pathfinder.pauseTimer - deltaTime);

            // ── Arrived? ───────────────────────────────────────────────
            // Touching it, within reach. A ranged flyer in reach but out of
            // range is above or below it: it still hasn't arrived.
            const bool ranged = pathfinder.IsRangedEntity();
            const bool contact = !lost && horizontal <= std::max(deadzone, frameStep) + body.radius + targetRadius;
            const float distance = body.air ? std::sqrt(horizontal * horizontal + dy * dy) : horizontal;
            const bool inRange = !lost && ranged && distance <= pathfinder.range;
            const bool reached = inRange || (contact && !(ranged && body.air));

            if (reached && !pathfinder.arrived)
                notices.push_back({ownerID, NoticeKind::ARRIVED, pathfinder.input == PathfinderInput::ENTITY ? pathfinder.targetEntityID : INVALID_ID});
            pathfinder.arrived = reached;

            // Doom's monsters don't stop for being in range.
            const bool stopHere = contact || (inRange && !pathfinder.KeepsMovingInRange());
            const bool standStill = stopHere || lost || speed <= 0.0f || paused;

            Ground ahead = standing;

            pathfinder.blockedCooldown -= deltaTime;

            bool waitingForDoor = false;

            if (standStill) StopHorizontal(pathfinder, rigidbody);
            else {
                // ── Direction (A_Chase) ─────────────────────────────────
                GatherLines(level, x, z, body.radius + std::max(frameStep, 0.5f) + Constants::Epsilon, lines);

                ChaseContext context{level, body, lines, ownerID, x, z, standing.floor, frameStep, doorRequests};

                pathfinder.moveTimer -= deltaTime;

                const bool hadDirection = pathfinder.moveDirection < DIR_NONE;
                const bool canContinue = hadDirection &&
                    CanStep(context, static_cast<Direction>(pathfinder.moveDirection), frameStep, &ahead);

                // A flyer held back by a ledge it is still rising over keeps going.
                const bool rising = body.air && canContinue && ahead.floor > body.feetY + Constants::Epsilon;

                const bool keep = canContinue && pathfinder.moveTimer >= 0.0f && (!blocked || rising);

                if (!keep) {
                    if (hadDirection && (!canContinue || blocked) && pathfinder.blockedCooldown <= 0.0f) {
                        notices.push_back({ownerID, NoticeKind::BLOCKED, INVALID_ID});
                        pathfinder.blockedCooldown = pathfinder.chaseStep;
                    }

                    NewChaseDirection(pathfinder, context, dx, dz, deadzone);
                    waitingForDoor = context.waitingForDoor;

                    if (pathfinder.moveDirection < DIR_NONE)
                        CanStep(context, static_cast<Direction>(pathfinder.moveDirection), frameStep, &ahead);
                }

                // ── Move ────────────────────────────────────────────────
                if (pathfinder.moveDirection < DIR_NONE) {
                    const float dirX = DIR_X[pathfinder.moveDirection];
                    const float dirZ = DIR_Z[pathfinder.moveDirection];

                    if (rigidbody != nullptr) {
                        rigidbody->velocity.x = dirX * speed + pathfinder.pushX;
                        rigidbody->velocity.z = dirZ * speed + pathfinder.pushZ;
                        pathfinder.lastX = x;
                        pathfinder.lastZ = z;
                        pathfinder.expectedStep = frameStep;
                    }
                    else {
                        // Moved directly: a walker stays on the floor it steps onto.
                        Vector3 position = transform->position;
                        position.x += dirX * frameStep;
                        position.z += dirZ * frameStep;

                        Ground landing;
                        if (!body.air && SampleGround(level, body.sectorHint, position.x, position.z, body.feetY, landing))
                            position.y = landing.floor - body.feetOffset;

                        transform->SetPosition(position);
                    }
                }
                else StopHorizontal(pathfinder, rigidbody);
            }

            // ── Facing ──────────────────────────────────────────────────
            if (pathfinder.facing == PathfinderFacing::MOVE_DIRECTION) {
                if (pathfinder.turning == PathfinderTurning::STEPS_45) TurnStep(pathfinder, *transform, deltaTime);
                else if (pathfinder.moveDirection < DIR_NONE) {
                    pathfinder.facingOctant = pathfinder.moveDirection;

                    Quaternion facing;
                    if (QuaternionMath::LookRotation({DIR_X[pathfinder.moveDirection], 0.0f, DIR_Z[pathfinder.moveDirection]}, true, facing))
                        transform->rotation = facing;
                }
            }
            else if (pathfinder.facing == PathfinderFacing::TARGET && !lost && horizontal > Constants::Epsilon) {
                Quaternion facing;
                if (QuaternionMath::LookRotation({dx, 0.0f, dz}, true, facing)) transform->rotation = facing;
            }

            // ── Height (flyers only) ────────────────────────────────────
            // Toward the destination's height, once the height difference is
            // large against the distance (P_ZMovement) or it is there, and
            // kept between the floor and ceiling it's over or heading into
            // (rising over a ledge first). Paused, it holds where it is.
            if (body.air) {
                const float lowest = std::max(standing.floor, ahead.floor);
                const float highest = std::min(standing.ceiling, ahead.ceiling) - body.height;

                float targetY = pathfinder.destination.y;
                if (paused) targetY = body.feetY;
                else if (!contact && pathfinder.floatRatio > 0.0f && std::abs(dy) * pathfinder.floatRatio <= horizontal) targetY = body.feetY;

                if (lowest <= highest) targetY = std::clamp(targetY, lowest, highest);

                const float rise = targetY - body.feetY;
                const float floatSpeed = std::max(0.0f, pathfinder.floatSpeed);
                const float floatStep = floatSpeed * deltaTime;

                if (rigidbody != nullptr) {
                    // Set every frame, so gravity's pull since the last one is undone.
                    rigidbody->velocity.y = std::clamp(rise / deltaTime, -floatSpeed, floatSpeed);
                }
                else transform->AddPosition({0.0f, std::clamp(rise, -floatStep, floatStep), 0.0f});
            }

            pathfinder.driving = rigidbody != nullptr;

            // ── Moving, stuck ───────────────────────────────────────────
            const bool nowMoving = !standStill && pathfinder.moveDirection < DIR_NONE;
            if (nowMoving != pathfinder.moving) {
                pathfinder.moving = nowMoving;
                notices.push_back({ownerID, nowMoving ? NoticeKind::STARTED_MOVING : NoticeKind::STOPPED_MOVING, INVALID_ID});
            }

            if (standStill || waitingForDoor || (nowMoving && !blocked)) {
                pathfinder.stuckTimer = 0.0f;
                pathfinder.stuck = false;
            }
            else {
                pathfinder.stuckTimer += deltaTime;
                if (!pathfinder.stuck && pathfinder.stuckTime > 0.0f && pathfinder.stuckTimer >= pathfinder.stuckTime) {
                    pathfinder.stuck = true;
                    notices.push_back({ownerID, NoticeKind::STUCK, INVALID_ID});
                }
            }
        }

        for (const EventSystem::WalkerDoorRequest& request : doorRequests) EventSystem::OpenDoorForWalker(level, request);

        if (onNotice)
            for (const Notice& notice : notices) onNotice(notice.ownerID, notice.kind, notice.targetID);
    }

    void AfterPhysics(Level& level) {
        for (ComponentPathfinder& pathfinder : level.pathfinders.components) {
            if (!pathfinder.driving) continue;

            if (const ComponentRigidbody* rigidbody = level.rigidbodies.Get(pathfinder.ownerID)) {
                pathfinder.settledX = rigidbody->velocity.x;
                pathfinder.settledZ = rigidbody->velocity.z;
            }
        }
    }

    void SetDestination(ComponentPathfinder& pathfinder, const Vector3& destination) {
        const bool same = pathfinder.input == PathfinderInput::POSITION && pathfinder.destination.x == destination.x &&
                          pathfinder.destination.y == destination.y && pathfinder.destination.z == destination.z;
        if (same) return;

        pathfinder.input = PathfinderInput::POSITION;
        pathfinder.destination = destination;
        pathfinder.arrived = false;
    }

    void SetTarget(Level& level, ComponentPathfinder& pathfinder, const ID targetID, const Vector3& offset) {
        const bool same = pathfinder.input == PathfinderInput::ENTITY && pathfinder.targetEntityID == targetID &&
                          pathfinder.offset.x == offset.x && pathfinder.offset.y == offset.y && pathfinder.offset.z == offset.z;

        pathfinder.input = PathfinderInput::ENTITY;
        pathfinder.targetEntityID = targetID;
        pathfinder.offset = offset;
        if (!same) pathfinder.arrived = false;

        if (const ComponentTransform* target = level.transforms.Get(targetID))
            pathfinder.destination = target->position + offset;
    }

    void Pause(ComponentPathfinder& pathfinder, const float seconds) {
        pathfinder.pauseTimer = std::max(0.0f, seconds);
    }

    void PickNewDirection(ComponentPathfinder& pathfinder) {
        pathfinder.moveTimer = -1.0f;
    }

    Vector3 MoveDirection(const ComponentPathfinder& pathfinder) {
        if (pathfinder.moveDirection >= DIR_NONE || !pathfinder.moving) return {0.0f, 0.0f, 0.0f};
        return {DIR_X[pathfinder.moveDirection], 0.0f, DIR_Z[pathfinder.moveDirection]};
    }
}
