//
// Created by berke on 11/1/2026.
//

#include "Headers/Runtime/Gameplay/PathfinderSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/MathFunctions.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
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
    };

    // The floor and ceiling a point ahead stands under.
    struct Ground {
        float floor = 0.0f;
        float ceiling = 0.0f;
    };

    // Finds the floor/ceiling under (x, z) for a body at `referenceY`. False
    // outside every sector.
    bool SampleGround(const Level& level, const int hint, const float x, const float z, const float referenceY, Ground& out) {
        const Vector2 point = {x, z};
        const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, point, hint);
        if (sectorIndex < 0) return false;

        const Sector& sector = level.sectors[sectorIndex];
        const int floorIndex = MapQueries::FindFloorAtHeight(sector, point, referenceY);
        if (floorIndex < 0) return false;

        const SectorFloor& floor = sector.floors[floorIndex];
        out.floor = MapQueries::SurfaceHeightAt(sector, floor.floor, point);
        out.ceiling = MapQueries::SurfaceHeightAt(sector, floor.ceiling, point);
        return true;
    }

    // Doom's P_TryMove / P_CheckPosition: the body's square bounding box moved
    // one step along `direction`, sampled at its centre, edges and corners.
    // The highest floor and lowest ceiling under it are what it must fit
    // between; `standingFloor` is the floor under the body now. `aheadOut`
    // gets those two (only when every sample is on the map).
    bool CanStep(const Level& level, const Body& body, const float x, const float z, const float standingFloor,
                 const Direction direction, const float stepLength, Ground* aheadOut = nullptr) {
        if (direction == DIR_NONE) return false;

        const float step = std::max(stepLength, 0.5f);
        const float centerX = x + DIR_X[direction] * step;
        const float centerZ = z + DIR_Z[direction] * step;

        constexpr float SAMPLE_X[9] = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f};
        constexpr float SAMPLE_Z[9] = {0.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};
        const int samples = body.radius > 0.0f ? 9 : 1;

        float highestFloor = -std::numeric_limits<float>::infinity();
        float lowestFloor = std::numeric_limits<float>::infinity();
        float lowestCeiling = std::numeric_limits<float>::infinity();

        for (int i = 0; i < samples; ++i) {
            Ground ground;
            // Off the map: a solid wall.
            if (!SampleGround(level, body.sectorHint, centerX + SAMPLE_X[i] * body.radius, centerZ + SAMPLE_Z[i] * body.radius, body.feetY, ground))
                return false;

            highestFloor = std::max(highestFloor, ground.floor);
            lowestFloor = std::min(lowestFloor, ground.floor);
            lowestCeiling = std::min(lowestCeiling, ground.ceiling);
        }

        if (aheadOut != nullptr) *aheadOut = {highestFloor, lowestCeiling};

        // Too tight to fit at any height.
        if (lowestCeiling - highestFloor < body.height) return false;

        // A flyer rises or sinks to fit instead (Doom's floatok).
        if (body.air) return true;

        if (highestFloor - standingFloor > body.stepSize) return false; // too high a step
        if (highestFloor - lowestFloor > body.stepSize) return false;   // would hang over a drop-off
        if (standingFloor - highestFloor > body.stepSize) return false; // stepping down off a ledge
        if (lowestCeiling - std::max(highestFloor, body.feetY) < body.height) return false; // head hits the ceiling

        return true;
    }

    struct ChaseContext {
        const Level& level;
        const Body& body;
        float x, z;
        float standingFloor;
        float stepLength;
    };

    // Doom's P_TryWalk: takes the direction if a step that way is possible and
    // holds it for a random 1..16 chase steps (movecount).
    bool TryWalk(ComponentPathfinder& pathfinder, const ChaseContext& context, const Direction direction) {
        if (!CanStep(context.level, context.body, context.x, context.z, context.standingFloor, direction, context.stepLength))
            return false;

        pathfinder.moveDirection = direction;
        pathfinder.moveTimer = static_cast<float>((RandomByte() & 15) + 1) * PathfinderSystem::CHASE_STEP_SECONDS;
        return true;
    }

    // Doom's P_NewChaseDir. `deadzone` is its 10-unit "close enough on this
    // axis" distance.
    void NewChaseDirection(ComponentPathfinder& pathfinder, const ChaseContext& context, const float dx, const float dz, const float deadzone) {
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
        if (RandomByte() > 200 || std::abs(dz) > std::abs(dx)) std::swap(d1, d2);

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

    // Stops moving the owner sideways. Its velocity is only zeroed when it
    // was walking, so a push from a script while it stands still is kept.
    void StopHorizontal(ComponentPathfinder& pathfinder, ComponentRigidbody* rigidbody) {
        if (pathfinder.driving && rigidbody != nullptr && pathfinder.moveDirection < DIR_NONE) {
            rigidbody->velocity.x = 0.0f;
            rigidbody->velocity.z = 0.0f;
        }
        pathfinder.moveDirection = DIR_NONE;
        pathfinder.expectedStep = 0.0f;
    }

    // Stops everything it drives, the flyer's height included, and lets go.
    void Release(ComponentPathfinder& pathfinder, ComponentRigidbody* rigidbody) {
        if (pathfinder.driving && rigidbody != nullptr && pathfinder.IsAirEntity()) rigidbody->velocity.y = 0.0f;
        StopHorizontal(pathfinder, rigidbody);
        pathfinder.driving = false;
    }

    float TargetRadius(const Level& level, const ID targetID) {
        const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, targetID);
        const ComponentTransform* transform = level.transforms.Get(targetID);
        return collider != nullptr && transform != nullptr ? collider->Radius(*transform) : 0.0f;
    }
}

namespace PathfinderSystem {
    void Start(Level& level) {
        for (ComponentPathfinder& pathfinder : level.pathfinders.components) {
            pathfinder.moveDirection = DIR_NONE;
            pathfinder.moveTimer = 0.0f;
            pathfinder.expectedStep = 0.0f;
            pathfinder.driving = false;
        }
    }

    void Update(Level& level, const float deltaTime) {
        if (deltaTime <= 0.0f) return;

        for (ComponentPathfinder& pathfinder : level.pathfinders.components) {
            // A disabled entity is frozen, its velocity kept.
            if (!pathfinder.ownerEnabled) continue;

            const ID ownerID = pathfinder.ownerID;
            ComponentRigidbody* rigidbody = level.rigidbodies.Get(ownerID);

            // Only an entity's first pathfinder moves it.
            if (level.pathfinders.Get(ownerID) != &pathfinder) continue;

            ComponentTransform* transform = level.transforms.Get(ownerID);
            if (transform == nullptr || !pathfinder.isActive) {
                Release(pathfinder, rigidbody);
                continue;
            }

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
            const float chaseStep = speed * CHASE_STEP_SECONDS;
            const float deadzone = std::max(chaseStep, 1.0f);

            // Did physics cut the last step short (a wall, another body)?
            bool blocked = false;
            if (pathfinder.driving && rigidbody != nullptr && pathfinder.expectedStep > Constants::Epsilon) {
                const float moved = std::hypot(x - pathfinder.lastX, z - pathfinder.lastZ);
                blocked = moved < pathfinder.expectedStep * BLOCKED_STEP_FRACTION;
            }

            const float dx = pathfinder.destination.x - x;
            const float dy = pathfinder.destination.y - body.feetY;
            const float dz = pathfinder.destination.z - z;
            const float horizontal = std::hypot(dx, dz);

            // ── Arrived? ───────────────────────────────────────────────
            bool arrived = lost || speed <= 0.0f;
            if (!arrived) {
                if (pathfinder.IsRangedEntity()) {
                    const float distance = body.air ? std::sqrt(horizontal * horizontal + dy * dy) : horizontal;
                    arrived = distance <= pathfinder.range;
                }
                else arrived = horizontal <= deadzone + body.radius + targetRadius;
            }

            Ground ahead = standing;

            if (arrived) StopHorizontal(pathfinder, rigidbody);
            else {
                // ── Direction (A_Chase) ─────────────────────────────────
                const ChaseContext context{level, body, x, z, standing.floor, frameStep};

                pathfinder.moveTimer -= deltaTime;

                const bool canContinue = pathfinder.moveDirection < DIR_NONE &&
                    CanStep(level, body, x, z, standing.floor, static_cast<Direction>(pathfinder.moveDirection), frameStep, &ahead);

                // A flyer held back by a ledge it is still rising over keeps going.
                const bool rising = body.air && canContinue && ahead.floor > body.feetY + Constants::Epsilon;

                const bool keep = canContinue && pathfinder.moveTimer >= 0.0f && (!blocked || rising);

                if (!keep) {
                    NewChaseDirection(pathfinder, context, dx, dz, deadzone);
                    if (pathfinder.moveDirection < DIR_NONE)
                        CanStep(level, body, x, z, standing.floor, static_cast<Direction>(pathfinder.moveDirection), frameStep, &ahead);
                }

                // ── Move ────────────────────────────────────────────────
                if (pathfinder.moveDirection < DIR_NONE) {
                    const float dirX = DIR_X[pathfinder.moveDirection];
                    const float dirZ = DIR_Z[pathfinder.moveDirection];

                    if (rigidbody != nullptr) {
                        rigidbody->velocity.x = dirX * speed;
                        rigidbody->velocity.z = dirZ * speed;
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

                    Quaternion facing;
                    if (QuaternionMath::LookRotation({dirX, 0.0f, dirZ}, true, facing)) transform->rotation = facing;
                }
                else StopHorizontal(pathfinder, rigidbody);
            }

            // ── Height (flyers only) ────────────────────────────────────
            // Toward the destination's height, kept between the floor and
            // ceiling it's over or heading into (rising over a ledge first).
            if (body.air) {
                const float lowest = std::max(standing.floor, ahead.floor);
                const float highest = std::min(standing.ceiling, ahead.ceiling) - body.height;
                float targetY = pathfinder.destination.y;
                if (lowest <= highest) targetY = std::clamp(targetY, lowest, highest);

                const float rise = targetY - body.feetY;

                if (rigidbody != nullptr) {
                    // Set every frame, so gravity's pull since the last one is undone.
                    rigidbody->velocity.y = std::clamp(rise / deltaTime, -speed, speed);
                }
                else transform->AddPosition({0.0f, std::clamp(rise, -frameStep, frameStep), 0.0f});
            }

            pathfinder.driving = rigidbody != nullptr;
        }
    }
}
