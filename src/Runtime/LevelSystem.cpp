//
// Created by berke on 5/26/2026.
//

/// This script handles the runtime renderer. Does not run in the editor

#include "../../Headers/Runtime/LevelSystem.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

#include <tracy/Tracy.hpp>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Runtime/ScriptSystem.hpp"

namespace {
    ComponentPlayerController *GetActivePlayerController(Level &level) {
        for (ComponentPlayerController &controller: level.playerControllers.components)
            if (controller.isActive) return &controller;

        return nullptr;
    }

    const ComponentCamera *GetActiveCamera(const Level &level) {
        for (const ComponentCamera &camera: level.cameras.components)
            if (camera.isActive) return &camera;

        return nullptr;
    }

    ComponentTransform *GetActiveCameraTransform(Level &level) {
        const ComponentCamera *camera = GetActiveCamera(level);

        if (camera == nullptr) return nullptr;

        return level.transforms.Get(camera->ownerID);
    }

    ComponentTransform *GetActivePlayerTransform(Level &level) {
        ComponentPlayerController *controller = GetActivePlayerController(level);

        if (controller == nullptr) return nullptr;

        return level.transforms.Get(controller->ownerID);
    }

    void SetActiveCamera(const EntityID entityID, Level &level) {
        bool found = false;

        for (ComponentCamera &camera: level.cameras.components) {
            camera.isActive = camera.ownerID == entityID;

            if (camera.isActive) found = true;
        }

        if (!found) {
            spdlog::warn(
                "Tried to set active camera to entity {}, but that entity has no camera component",
                entityID
            );
        }
    }

    void SetActivePlayerController(const EntityID entityID, Level &level) {
        bool found = false;

        for (ComponentPlayerController &controller: level.playerControllers.components) {
            controller.isActive = controller.ownerID == entityID;

            if (controller.isActive) {
                found = true;
            }
        }

        if (!found)
            spdlog::warn(
                "Tried to set active player controller to entity {}, but that entity has no player controller component",
                entityID
            );
    }

    ComponentPlayerController *activeController;

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
            return Vector3{ a.x + v * ab.x, a.y + v * ab.y, a.z + v * ab.z };
        }

        const Vector3 cp = p - c;
        const float d5 = Vector3Math::Dot(ab, cp);
        const float d6 = Vector3Math::Dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            const float w = d2 / (d2 - d6);
            return Vector3{ a.x + w * ac.x, a.y + w * ac.y, a.z + w * ac.z };
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            Vector3 bc = { c.x - b.x, c.y - b.y, c.z - b.z };
            return Vector3{ b.x + w * bc.x, b.y + w * bc.y, b.z + w * bc.z };
        }

        // P is inside face region
        const float denom = 1.0f / (va + vb + vc);
        const float v = vb * denom;
        const float w = vc * denom;
        return Vector3{ a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w };
    }
}

namespace LevelSystem {
    ComponentCamera *GetActiveCamera(Level &level) {
        for (ComponentCamera &camera: level.cameras.components)
            if (camera.isActive) return &camera;


        return nullptr;
    }

    void Start(Level &level) {
        SoundManager::SetListenerGain(level.listenerSettings.masterGain);
        SoundManager::SetListenerDopplerFactor(level.listenerSettings.dopplerFactor);
        SoundManager::SetListenerSpeedOfSound(level.listenerSettings.speedOfSound);
        SoundManager::SetListenerDistanceModel(level.listenerSettings.distanceModel);

        spdlog::info(
            "Level audio settings applied. Gain: {}, Doppler: {}, Speed of sound: {}, Distance model: {}",
            level.listenerSettings.masterGain,
            level.listenerSettings.dopplerFactor,
            level.listenerSettings.speedOfSound,
            static_cast<int>(level.listenerSettings.distanceModel)
        );

        for (const ComponentCamera &cam: level.cameras.components) {
            if (cam.isActive) {
                //todo support multiple cameras
                SetActiveCamera(cam.ownerID, level);
                break;
            }
        }

        for (Entity &entity: level.entities) {
            entity.Start(); // Currently does nothing
        }

        for (ComponentTransform &transform: level.transforms.components) {
            transform.UpdateObjectSectorAndFloor(level.sectors, level.GetEntity(transform.ownerID));
        }

        activeController = GetActivePlayerController(level);

        if (activeController != nullptr) {
            ComponentTransform *playerTransform = level.transforms.Get(activeController->ownerID);
            const ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(activeController->ownerID);

            if (playerTransform != nullptr) [[likely]]{
                ComponentCamera *activeCamera = GetActiveCamera(level);

                if (activeCamera != nullptr) {
                    PlayerControllerSystem::Start(
                        *activeController,
                        *playerTransform,
                        *playerRigidbody,
                        *activeCamera,
                        level.sectors
                    );
                } else spdlog::warn("Level::Start skipped player controller: no active camera");
            } else
                spdlog::error("Level::Start skipped player controller: entity {} has no transform",
                              activeController->ownerID);
        }

        ScriptSystem::Start(level);

        // Future level start systems will run here.
    }

    void Update(Level &level) {
        // for (Entity &entity: level.entities) {
        //     entity.Update(); // currently does nothing
        // }

        {
            ZoneScopedN("Transform setup");
            for (ComponentTransform &transform: level.transforms.components) {
                Entity *owner = level.GetEntity(transform.ownerID);

                if (!owner) [[unlikely]] {
                    spdlog::error("Transform owner {} does not exist", transform.ownerID);
                    continue;
                }

                transform.UpdateObjectSectorAndFloor(level.sectors, owner);
            }
        }

        if (activeController != nullptr && activeController->isActive) {
            const EntityID ownerID = activeController->ownerID;

            ComponentTransform *playerTransform = level.transforms.Get(ownerID);
            if (!playerTransform) [[unlikely]] {
                spdlog::error("Player controller entity {} has no transform", ownerID);
                return;
            }

            ComponentRigidbody *playerRigidbody = level.rigidbodies.Get(ownerID);
            if (!playerRigidbody) [[unlikely]] {
                spdlog::error("Player controller entity {} has no rigidbody", ownerID);
                return;
            }

            ComponentCollider *playerCollider = level.colliders.Get(ownerID);

            ComponentCamera *activeCamera = GetActiveCamera(level);
            if (!activeCamera) [[unlikely]] {
                spdlog::warn("LevelSystem::Update skipped player controller: no active camera");
                return;
            }

            PlayerControllerSystem::Update(
                *activeController,
                *playerTransform,
                *activeCamera,
                *playerRigidbody,
                playerCollider,
                level.sectors
            );
        }

        {
            ZoneScopedN("Scripts");
            ScriptSystem::Update(level);
        }

        {
            ZoneScopedN("Physics");

            for (ComponentRigidbody &r: level.rigidbodies.components) {
                ComponentTransform *transform = level.transforms.Get(r.ownerID);

                if (!transform) [[unlikely]] {
                    spdlog::error("Rigidbody entity {} has no transform", r.ownerID);
                    continue;
                }

                ComponentCollider *collider = level.colliders.Get(r.ownerID);

                const bool isOnCurrentFloor = transform->position.z <= .00001f;

                // Apply gravity if airborne, falling, or moving upward.
                //r.ApplyGravity(level.worldSettings.gravity);

                if (!r.velocity.IsZero()) transform->AddPosition(r.velocity * GameTime::deltaTime);

                // Apply Rb's base friction
                r.ApplyFriction(0);
                r.ApplyAirResistance(0);
            }
        }

        {
            //todo sort entities where sphere colliders are in the beggining of the vector to optimize for branch prediction
            ZoneScopedN("Collision");

            //todo make this a world setting

            constexpr int COLLISION_ITERATIONS = 6;

            for (int i = 0; i < COLLISION_ITERATIONS; i++) {
                for (ComponentCollider &selfCollider: level.colliders.components) {
                    if (!selfCollider.isActive) continue;
                    if (selfCollider.isTrigger) continue;

                    ComponentTransform *selfTransform = level.transforms.Get(selfCollider.ownerID);

                    if (selfTransform == nullptr) [[unlikely]] continue;

                    ComponentRigidbody *selfRb = level.rigidbodies.Get(selfCollider.ownerID);

                    if (selfRb == nullptr) continue;
                    if (selfRb->isStatic) continue;

                    //selfTransform->isDirty = true;

                    if (selfTransform->sectorIndex < 0 ||
                        selfTransform->sectorIndex >= static_cast<int>(level.sectors.size())) {
                        continue;
                    }

                    Sector &sector = level.sectors[selfTransform->sectorIndex];
                    Entity *selfEntity = level.GetEntity(selfCollider.ownerID);

                    if (selfEntity == nullptr) [[unlikely]] continue;

                    std::vector<Entity *> allEntities;

                    allEntities.reserve(sector.entitiesInside.size() + 16);
                    allEntities.insert(allEntities.end(), sector.entitiesInside.begin(), sector.entitiesInside.end());

                    for (const Sector *nSector: sector.neighbors) {
                        if (nSector == nullptr) [[unlikely]] continue;

                        allEntities.insert(allEntities.end(), nSector->entitiesInside.begin(),
                                           nSector->entitiesInside.end());
                    }

                    std::vector<EntityID> processedOtherIDs;

                    processedOtherIDs.reserve(allEntities.size());

                    Vector3 selfPos = {
                        selfTransform->position.x,
                        selfTransform->position.y,
                        selfTransform->absHeight
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

                    for (const Wall* wall : sector.walls) {
                        if (wall == nullptr) continue;
                        for (int i = 0; i < wall->quad3DCount; ++i) {
                            const auto& quad = wall->quads3D[i];

                            // Split the quad into two triangles: (V0, V1, V2) and (V0, V2, V3)
                            const Vector3 closestT1 = ClosestPointOnTriangle(selfPos, quad[0], quad[1], quad[2]);
                            const Vector3 closestT2 = ClosestPointOnTriangle(selfPos, quad[0], quad[2], quad[3]);

                            // Determine which triangle yielded the absolute closest point to the sphere center
                            Vector3 diff1 = { selfPos.x - closestT1.x, selfPos.y - closestT1.y, selfPos.z - closestT1.z };
                            Vector3 diff2 = { selfPos.x - closestT2.x, selfPos.y - closestT2.y, selfPos.z - closestT2.z };

                            float distSq1 = Vector3Math::LengthSquared(diff1);
                            float distSq2 = Vector3Math::LengthSquared(diff2);

                            Vector3 absoluteClosestPoint = (distSq1 < distSq2) ? closestT1 : closestT2;
                            float minDistanceSq = (distSq1 < distSq2) ? distSq1 : distSq2;

                            if (minDistanceSq <= (selfCollider.scale.x * selfCollider.scale.x)) {
                                // Wall collision detected
                                if (selfRb->isStatic) break;

                                float distance = std::sqrt(minDistanceSq);
                                float penetrationDepth = selfCollider.scale.x - distance;

                                Vector3 bestDiff = (distSq1 < distSq2) ? diff1 : diff2;
                                Vector3 collisionNormal = { 0.0f, 0.0f, 1.0f }; // Fallback
                                if (distance > 0.0001f) {
                                    collisionNormal = Vector3{
                                        bestDiff.x / distance,
                                        bestDiff.y / distance,
                                        bestDiff.z / distance
                                    };
                                }
                                else {
                                    // Edge case: Sphere center is perfectly on the quad face.
                                    // Generate a fallback normal using the quad's winding direction
                                    Vector3 edge1 = quad[1] - quad[0];
                                    Vector3 edge2 = quad[2] - quad[0];

                                    Vector3 cross = Vector3Math::Cross(edge1, edge2);
                                    collisionNormal = Vector3Math::Normalized(cross);
                                }

                                selfTransform->AddPosition(collisionNormal * penetrationDepth);
                                // Resolve collision here or break out
                            }
                        }
                    }
                }
            }
        } // Zone Collision

        for (ComponentTransform &transform: level.transforms.components) transform.isDirty = false;
    }
}
