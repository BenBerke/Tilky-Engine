//
// Created by berke on 11/2/2026.
//

#include "Headers/Runtime/Gameplay/MonsterAISystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/MathFunctions.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Runtime/Gameplay/FlipbookSystem.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include "Headers/Runtime/Gameplay/PathfinderSystem.hpp"
#include "Headers/Runtime/PhysicsSystem.hpp"
#include "Headers/TagRegistry.hpp"

namespace {
    // What each sector last heard (Doom's sector_t::soundtarget). Kept until a
    // new noise replaces it, so a monster that wakes up later still hears it.
    std::unordered_map<ID, ID> heardTargets;

    // Hears every decision. Set by Start.
    MonsterAISystem::NoticeHandler noticeHandler;

    // Decisions from before the first Update (a script's Start calling Wake)
    // wait for it: the events can't fire until the level has fully started.
    struct HeldNotice {
        ID ownerID;
        MonsterAISystem::NoticeKind kind;
        ID otherID;
    };
    std::vector<HeldNotice> heldNotices;
    bool holdingNotices = false;

    const MonsterAISystem::NoticeHandler heldNoticeHandler = [](const ID ownerID, const MonsterAISystem::NoticeKind kind, const ID otherID) {
        heldNotices.push_back({ownerID, kind, otherID});
    };

    // What Wake, Sleep and Hurt report to. A copy: a handler can start a level.
    MonsterAISystem::NoticeHandler CurrentHandler() {
        return holdingNotices ? heldNoticeHandler : noticeHandler;
    }

    // Holding a Pathfinder for as long as the AI is idle.
    constexpr float HOLD_FOREVER = std::numeric_limits<float>::max();

    // Doom's P_Random: 0..255.
    int RandomByte() { return static_cast<int>(MathFunctions::XorShift32() & 0xFFu); }

    bool RollPercent(const float percent) { return static_cast<float>(RandomByte()) / 256.0f * 100.0f < percent; }

    bool IsCorpse(const Level& level, const ID entityID) {
        const ComponentMonsterAI* ai = level.monsterAIs.Get(entityID);
        return ai != nullptr && ai->state == MonsterAIState::DEAD;
    }

    // A target that can still be chased.
    const Entity* LiveTarget(const Level& level, const ID targetID) {
        if (targetID == INVALID_ID) return nullptr;
        const Entity* entity = level.GetEntity(targetID);
        return entity != nullptr && entity->enabled && level.transforms.Get(targetID) != nullptr && !IsCorpse(level, targetID) ? entity : nullptr;
    }

    float Radius(const Level& level, const ID entityID) {
        const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, entityID);
        const ComponentTransform* transform = level.transforms.Get(entityID);
        return collider != nullptr && transform != nullptr ? collider->Radius(*transform) : 0.0f;
    }

    // A body with no collider is looked at and from this far above its
    // position, which is usually right on the floor: a ray there hits the floor.
    constexpr float NO_COLLIDER_SIGHT_HEIGHT = 1.0f;

    // Eye height over the feet, as in P_CheckSight: three quarters up.
    Vector3 EyeOf(const Level& level, const ID entityID, const ComponentTransform& transform) {
        const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, entityID);
        if (collider == nullptr) return {transform.position.x, transform.position.y + NO_COLLIDER_SIGHT_HEIGHT, transform.position.z};

        Vector3 eye = collider->Base(transform);
        eye.y += collider->Height(transform) * 0.75f;
        return eye;
    }

    // Line of sight to the target's middle, or failing that its top: P_CheckSight's
    // slopes reach any part of it. It sees over a low wall that hides the feet.
    bool HasLineOfSight(Level& level, const ID selfID, const ID targetID) {
        const ComponentTransform* self = level.transforms.Get(selfID);
        const ComponentTransform* target = level.transforms.Get(targetID);
        if (self == nullptr || target == nullptr) return false;

        const Vector3 eye = EyeOf(level, selfID, *self);

        Vector3 middle = {target->position.x, target->position.y + NO_COLLIDER_SIGHT_HEIGHT, target->position.z};
        Vector3 top = middle;
        if (const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, targetID)) {
            const Vector3 base = collider->Base(*target);
            const float height = collider->Height(*target);
            middle = {base.x, base.y + height * 0.5f, base.z};
            top = {base.x, base.y + height * 0.95f, base.z};
        }

        return GameFunctions::LineOfSight(level, eye, middle) || GameFunctions::LineOfSight(level, eye, top);
    }

    // Within the field of view of `self`, or close enough to notice anyway.
    bool InFieldOfView(const ComponentMonsterAI& ai, const ComponentTransform& self, const ComponentTransform& target) {
        const float toX = target.position.x - self.position.x;
        const float toZ = target.position.z - self.position.z;
        const float distance = std::hypot(toX, toZ);
        if (distance <= ai.nearRange) return true;

        const Vector3 forward = QuaternionMath::Rotate(self.rotation, QuaternionMath::LocalForward());
        const float forwardLength = std::hypot(forward.x, forward.z);
        if (forwardLength <= 0.0f || distance <= 0.0f) return true;

        const float cosine = std::clamp((forward.x * toX + forward.z * toZ) / (forwardLength * distance), -1.0f, 1.0f);
        return std::acos(cosine) * 2.0f * 180.0f / std::numbers::pi_v<float> <= ai.sightFov;
    }

    bool HasAnyTag(const Entity& entity, const std::vector<TagId>& tags) {
        return std::ranges::any_of(tags, [&](const TagId tag) {
            return std::ranges::find(entity.tagIds, tag) != entity.tagIds.end();
        });
    }

    // Doom's P_LookForPlayers. Nearest first; `allAround` ignores the field
    // of view, `needSight` the line of sight and range. INVALID_ID if none.
    ID LookForTarget(Level& level, const ComponentMonsterAI& ai, const bool allAround, const bool needSight) {
        std::vector<TagId> tags = ai.targetTags;
        if (tags.empty()) {
            const std::optional<TagId> playerTag = TagRegistry::Find(TagRegistry::PLAYER_TAG);
            if (!playerTag) return INVALID_ID;
            tags.push_back(*playerTag);
        }

        const ComponentTransform* self = level.transforms.Get(ai.ownerID);
        if (self == nullptr) return INVALID_ID;

        std::vector<std::pair<float, ID>> candidates;
        for (const Entity& entity : level.entities) {
            if (entity.id == ai.ownerID || !entity.enabled || !HasAnyTag(entity, tags) || IsCorpse(level, entity.id)) continue;

            const ComponentTransform* transform = level.transforms.Get(entity.id);
            if (transform == nullptr) continue;

            candidates.emplace_back(std::hypot(transform->position.x - self->position.x, transform->position.z - self->position.z), entity.id);
        }

        std::ranges::sort(candidates);

        for (const auto& [distance, id] : candidates) {
            if (!needSight) return id;
            if (ai.sightRange > 0.0f && distance > ai.sightRange) break;

            if (!allAround && !InFieldOfView(ai, *self, *level.transforms.Get(id))) continue;

            if (HasLineOfSight(level, ai.ownerID, id)) return id;
        }

        return INVALID_ID;
    }

    ComponentPathfinder* DrivenPathfinder(Level& level, const ComponentMonsterAI& ai) {
        return ai.drivePathfinder ? level.pathfinders.Get(ai.ownerID) : nullptr;
    }

    void HoldAtLeast(ComponentPathfinder* pathfinder, const float seconds) {
        if (pathfinder != nullptr) pathfinder->pauseTimer = std::max(pathfinder->pauseTimer, seconds);
    }

    void FaceTarget(Level& level, const ComponentMonsterAI& ai, const ID targetID) {
        ComponentTransform* self = level.transforms.Get(ai.ownerID);
        const ComponentTransform* target = level.transforms.Get(targetID);
        if (self == nullptr || target == nullptr) return;

        Quaternion facing;
        if (QuaternionMath::LookRotation({target->position.x - self->position.x, 0.0f, target->position.z - self->position.z}, true, facing))
            self->rotation = facing;

        if (ComponentPathfinder* pathfinder = level.pathfinders.Get(ai.ownerID)) pathfinder->facingOctant = 8; // turns from here again
    }

    void PlayAttackFlipbook(Level& level, ComponentMonsterAI& ai) {
        ComponentFlipbook* flipbook = level.flipbooks.Get(ai.ownerID);
        if (flipbook == nullptr || ai.attackFlipbook.empty()) return;

        if (ai.animation != MonsterAIAnimation::ATTACKING) {
            ai.beforeAttackFlipbook = flipbook->flipbookFileName;
            ai.flipbookWasPlaying = flipbook->playing;
        }

        FlipbookSystem::Play(*flipbook, ai.attackFlipbook, true);
        ai.animation = MonsterAIAnimation::ATTACKING;
    }

    // Only acts when what it should show changes, so a flipbook a script
    // started stays on until the next change.
    void UpdateFlipbook(Level& level, ComponentMonsterAI& ai) {
        ComponentFlipbook* flipbook = level.flipbooks.Get(ai.ownerID);
        if (flipbook == nullptr) return;

        bool attackEnded = false;
        if (ai.animation == MonsterAIAnimation::ATTACKING) {
            if (ai.attackLeft > 0.0f) return;
            attackEnded = true;
            ai.animation = MonsterAIAnimation::NONE;
        }

        if (ai.walkFlipbook.empty()) {
            if (!attackEnded) return;

            // Back to what it was showing before the attack.
            if (ai.flipbookWasPlaying) FlipbookSystem::Play(*flipbook, ai.beforeAttackFlipbook, false);
            else {
                FlipbookSystem::SetFlipbookFileName(*flipbook, ai.beforeAttackFlipbook);
                FlipbookSystem::Pause(*flipbook);
            }
            return;
        }

        const ComponentPathfinder* pathfinder = level.pathfinders.Get(ai.ownerID);
        const bool moving = pathfinder != nullptr && pathfinder->moving;
        const MonsterAIAnimation wanted = moving ? MonsterAIAnimation::WALKING : MonsterAIAnimation::STANDING;
        if (ai.animation == wanted) return;
        ai.animation = wanted;

        if (moving) FlipbookSystem::Play(*flipbook, ai.walkFlipbook, false);
        else {
            // Standing still holds the walk on a frame. A flipbook that isn't
            // the walk or its own attack is someone else's: left playing.
            if (attackEnded) FlipbookSystem::SetFlipbookFileName(*flipbook, ai.walkFlipbook);
            if (flipbook->flipbookFileName == ai.walkFlipbook) FlipbookSystem::Pause(*flipbook);
        }
    }

    // Doom's P_CheckMissileRange past the sight check.
    bool MissileRoll(ComponentMonsterAI& ai, const float distance) {
        if (ai.justHit) {
            ai.justHit = false;
            return true;
        }

        if (ai.reactionLeft > 0.0f) return false;
        if (ai.missileMinRange > 0.0f && distance < ai.missileMinRange) return false;
        if (ai.missileMaxRange > 0.0f && distance > ai.missileMaxRange) return false;

        float chanceAgainst = distance - ai.missileDistanceOffset;
        if (!ai.hasMelee) chanceAgainst -= 128.0f;
        chanceAgainst *= ai.missileDistanceScale;
        chanceAgainst = std::min(chanceAgainst, ai.missileDistanceCap);

        return static_cast<float>(RandomByte()) >= chanceAgainst;
    }
}

namespace MonsterAISystem {
    ID FindPlayer(const Level& level) {
        const std::optional<TagId> playerTag = TagRegistry::Find(TagRegistry::PLAYER_TAG);
        if (!playerTag) return INVALID_ID;

        for (const Entity& entity : level.entities)
            if (entity.enabled && std::ranges::find(entity.tagIds, *playerTag) != entity.tagIds.end()) return entity.id;

        return INVALID_ID;
    }

    bool CanSee(Level& level, const ComponentMonsterAI& ai, const ID targetID) {
        if (LiveTarget(level, targetID) == nullptr) return false;

        const ComponentTransform* self = level.transforms.Get(ai.ownerID);
        const ComponentTransform* target = level.transforms.Get(targetID);
        if (self == nullptr) return false;

        const float distance = std::hypot(target->position.x - self->position.x, target->position.z - self->position.z);

        if (ai.sightRange > 0.0f && distance > ai.sightRange) return false;
        if (!InFieldOfView(ai, *self, *target)) return false;

        return HasLineOfSight(level, ai.ownerID, targetID);
    }

    static void WakeWith(Level& level, ComponentMonsterAI& ai, const ID targetID, const NoticeHandler& onNotice) {
        if (ai.state == MonsterAIState::DEAD || targetID == ai.ownerID || LiveTarget(level, targetID) == nullptr) return;

        const bool wasIdle = ai.state == MonsterAIState::IDLE;

        ai.state = MonsterAIState::CHASING;
        ai.targetID = targetID;
        ai.started = true;
        ai.chaseTimer = 0.0f;

        if (wasIdle) ai.reactionLeft = ai.reactionTime;

        if (ComponentPathfinder* pathfinder = DrivenPathfinder(level, ai)) {
            PathfinderSystem::SetTarget(level, *pathfinder, targetID, pathfinder->offset);
            if (wasIdle) pathfinder->pauseTimer = 0.0f;
        }

        if (wasIdle && onNotice) onNotice(ai.ownerID, NoticeKind::TARGET_SPOTTED, targetID);
    }

    static void SleepWith(Level& level, ComponentMonsterAI& ai, const NoticeHandler& onNotice) {
        if (ai.state == MonsterAIState::DEAD) return;

        const bool wasChasing = ai.state == MonsterAIState::CHASING;

        ai.state = MonsterAIState::IDLE;
        ai.targetID = INVALID_ID;
        ai.started = true;
        ai.lockLeft = 0.0f;
        ai.attackLeft = 0.0f;
        ai.justAttacked = false;
        ai.justHit = false;
        ai.lookTimer = ai.lookInterval;

        if (ComponentPathfinder* pathfinder = DrivenPathfinder(level, ai)) pathfinder->pauseTimer = HOLD_FOREVER;

        if (wasChasing && onNotice) onNotice(ai.ownerID, NoticeKind::TARGET_LOST, INVALID_ID);
    }

    // Doom's P_KillMobj: it stops thinking and walking, and stays as a corpse.
    static void Die(Level& level, ComponentMonsterAI& ai, const ID attackerID, const NoticeHandler& onNotice) {
        ai.state = MonsterAIState::DEAD;
        ai.health = 0.0f;
        ai.targetID = INVALID_ID;
        ai.started = true;
        ai.lockLeft = 0.0f;
        ai.attackLeft = 0.0f;
        ai.justAttacked = false;
        ai.justHit = false;

        // Off rather than held, so a flyer's corpse falls.
        if (ComponentPathfinder* pathfinder = level.pathfinders.Get(ai.ownerID)) {
            pathfinder->isActive = false;
            pathfinder->pauseTimer = 0.0f;
        }

        if (ComponentFlipbook* flipbook = level.flipbooks.Get(ai.ownerID)) {
            if (!ai.dieFlipbook.empty()) FlipbookSystem::Play(*flipbook, ai.dieFlipbook, true);
            else if (ai.animation != MonsterAIAnimation::NONE) FlipbookSystem::Pause(*flipbook); // not walking on the spot
        }
        ai.animation = MonsterAIAnimation::NONE;

        if (onNotice) onNotice(ai.ownerID, NoticeKind::DIED, attackerID);
    }

    void Wake(Level& level, ComponentMonsterAI& ai, const ID targetID) {
        const NoticeHandler handler = CurrentHandler();
        WakeWith(level, ai, targetID, handler);
    }

    void Sleep(Level& level, ComponentMonsterAI& ai) {
        const NoticeHandler handler = CurrentHandler();
        SleepWith(level, ai, handler);
    }

    void Hurt(Level& level, ComponentMonsterAI& ai, const ID attackerID, const float damage) {
        if (!ai.isActive || !ai.ownerEnabled || ai.state == MonsterAIState::DEAD) return;

        const NoticeHandler onNotice = CurrentHandler();

        ai.health = std::max(0.0f, ai.health - std::max(damage, 0.0f));
        if (ai.health <= 0.0f) {
            Die(level, ai, attackerID, onNotice);
            return;
        }

        // Reaches its first attack at once.
        ai.reactionLeft = 0.0f;

        if (RollPercent(ai.painChance)) {
            ai.justHit = true;
            HoldAtLeast(DrivenPathfinder(level, ai), ai.painPause);
            if (onNotice) onNotice(ai.ownerID, NoticeKind::PAIN, attackerID);
        }

        if (!ai.infights || ai.lockLeft > 0.0f || attackerID == ai.ownerID || LiveTarget(level, attackerID) == nullptr) return;

        ai.lockLeft = ai.targetLockTime;
        WakeWith(level, ai, attackerID, onNotice);
    }

    void NoiseAlert(Level& level, const ID targetID, const int sectorIndex) {
        if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) return;

        // Which walls each sector has as a portal.
        std::unordered_map<ID, std::vector<const Wall*>> portals;
        for (const Wall& wall : level.walls) {
            if (!wall.IsPortal()) continue;
            portals[wall.frontSector].push_back(&wall);
            portals[wall.backSector].push_back(&wall);
        }

        // P_RecursiveSound, with its soundtraversed: a sector reached having
        // crossed fewer sound-block walls is gone through again.
        std::unordered_map<ID, int> traversed;

        const auto hasRoom = [&level](const Wall& wall) {
            const Sector* front = MapQueries::GetSectorByID(level, wall.frontSector);
            const Sector* back = MapQueries::GetSectorByID(level, wall.backSector);
            if (front == nullptr || back == nullptr) return false;

            const Vector2 middle = {(wall.start.x + wall.end.x) * 0.5f, (wall.start.y + wall.end.y) * 0.5f};

            for (const SectorFloor& a : front->floors) {
                for (const SectorFloor& b : back->floors) {
                    const float floor = std::max(MapQueries::SurfaceHeightAt(*front, a.floor, middle), MapQueries::SurfaceHeightAt(*back, b.floor, middle));
                    const float ceiling = std::min(MapQueries::SurfaceHeightAt(*front, a.ceiling, middle), MapQueries::SurfaceHeightAt(*back, b.ceiling, middle));
                    if (ceiling > floor) return true;
                }
            }

            return false;
        };

        struct Visit {
            ID sectorID;
            int blocks;
        };
        std::vector<Visit> stack{{level.sectors[sectorIndex].id, 0}};

        while (!stack.empty()) {
            const auto [sectorID, blocks] = stack.back();
            stack.pop_back();

            const auto seen = traversed.find(sectorID);
            if (seen != traversed.end() && seen->second <= blocks) continue;

            traversed[sectorID] = blocks;
            heardTargets[sectorID] = targetID;

            const auto walls = portals.find(sectorID);
            if (walls == portals.end()) continue;

            for (const Wall* wall : walls->second) {
                if (!hasRoom(*wall)) continue; // a closed door

                const ID other = wall->frontSector == sectorID ? wall->backSector : wall->frontSector;

                if (!wall->blocksSound) stack.push_back({other, blocks});
                else if (blocks == 0) stack.push_back({other, 1});
            }
        }
    }

    void Start(Level& level, NoticeHandler onNotice) {
        heardTargets.clear();
        noticeHandler = std::move(onNotice);
        heldNotices.clear();
        holdingNotices = true;

        for (ComponentMonsterAI& ai : level.monsterAIs.components) {
            ai.state = MonsterAIState::IDLE;
            ai.targetID = INVALID_ID;
            ai.lookTimer = 0.0f;
            ai.chaseTimer = 0.0f;
            ai.reactionLeft = 0.0f;
            ai.lockLeft = 0.0f;
            ai.attackLeft = 0.0f;
            ai.justAttacked = false;
            ai.justHit = false;
            ai.started = false;
            ai.animation = MonsterAIAnimation::NONE;
            ai.flipbookWasPlaying = false;
            ai.beforeAttackFlipbook.clear();
        }
    }

    void Update(Level& level, const float deltaTime) {
        if (deltaTime <= 0.0f) return;

        if (holdingNotices) {
            holdingNotices = false;

            const std::vector<HeldNotice> held = std::move(heldNotices);
            heldNotices.clear();

            const NoticeHandler handler = noticeHandler;
            if (handler)
                for (const HeldNotice& notice : held) handler(notice.ownerID, notice.kind, notice.otherID);
        }

        // Kept until every AI has thought: a handler can add or remove
        // components, which would shift the storage under the loop.
        struct Notice {
            ID ownerID;
            NoticeKind kind;
            ID otherID;
        };
        std::vector<Notice> notices;

        const NoticeHandler queue = [&notices](const ID ownerID, const NoticeKind kind, const ID otherID) {
            notices.push_back({ownerID, kind, otherID});
        };

        for (ComponentMonsterAI& ai : level.monsterAIs.components) {
            if (!ai.ownerEnabled || !ai.isActive) continue;
            if (level.monsterAIs.Get(ai.ownerID) != &ai) continue; // only the first thinks

            const ComponentTransform* self = level.transforms.Get(ai.ownerID);
            if (self == nullptr) continue;

            if (ai.state == MonsterAIState::DEAD) continue;

            // Health set to 0 by a script or a Set Field, not through Hurt.
            if (ai.health <= 0.0f) {
                Die(level, ai, INVALID_ID, queue);
                continue;
            }

            ComponentPathfinder* pathfinder = DrivenPathfinder(level, ai);

            // Drive Pathfinder was turned off while it held it for being idle.
            if (pathfinder == nullptr) {
                ComponentPathfinder* own = level.pathfinders.Get(ai.ownerID);
                if (own != nullptr && own->pauseTimer == HOLD_FOREVER) own->pauseTimer = 0.0f;
            }

            // Set up on its first frame, so a spawned monster gets this too.
            if (!ai.started) {
                ai.started = true;

                if (ai.startAwake) {
                    WakeWith(level, ai, LookForTarget(level, ai, true, false), queue);
                    if (ai.state != MonsterAIState::CHASING) SleepWith(level, ai, queue);
                }
                else SleepWith(level, ai, queue);
            }

            ai.lockLeft = std::max(0.0f, ai.lockLeft - deltaTime);

            // ── Idle: A_Look ────────────────────────────────────────────
            if (ai.state == MonsterAIState::IDLE) {
                ai.lookTimer -= deltaTime;
                if (ai.lookTimer > 0.0f) continue;
                ai.lookTimer = std::max(ai.lookInterval, deltaTime);

                ID found = INVALID_ID;

                if (ai.hearing && self->sectorIndex >= 0 && self->sectorIndex < static_cast<int>(level.sectors.size())) {
                    const auto heard = heardTargets.find(level.sectors[self->sectorIndex].id);

                    if (heard != heardTargets.end() && heard->second != ai.ownerID && LiveTarget(level, heard->second) != nullptr &&
                        (!ai.ambush || CanSee(level, ai, heard->second)))
                        found = heard->second;
                }

                if (found == INVALID_ID) found = LookForTarget(level, ai, false, true);

                if (found != INVALID_ID) WakeWith(level, ai, found, queue);
                continue;
            }

            // ── Chasing: A_Chase ────────────────────────────────────────
            ai.reactionLeft = std::max(0.0f, ai.reactionLeft - deltaTime);

            // In its attack: nothing else until that's over.
            if (ai.attackLeft > 0.0f) {
                ai.attackLeft -= deltaTime;
                continue;
            }

            ai.chaseTimer -= deltaTime;
            if (ai.chaseTimer > 0.0f) continue;
            ai.chaseTimer = std::max(ai.chaseStep, deltaTime);

            // Its target is gone: anyone else, or back to idle.
            if (LiveTarget(level, ai.targetID) == nullptr) {
                const ID next = LookForTarget(level, ai, true, true);

                if (next != INVALID_ID) WakeWith(level, ai, next, queue);
                else {
                    SleepWith(level, ai, queue);
                    continue;
                }
            }

            if (ai.justAttacked) {
                ai.justAttacked = false;
                if (!ai.aggressive) {
                    if (ComponentPathfinder* own = level.pathfinders.Get(ai.ownerID)) PathfinderSystem::PickNewDirection(*own);
                    continue;
                }
            }

            const ComponentTransform* target = level.transforms.Get(ai.targetID);
            const float distance = std::hypot(target->position.x - self->position.x, target->position.z - self->position.z);

            const auto attack = [&](const NoticeKind kind) {
                ai.justAttacked = true;
                ai.attackLeft = ai.attackPause;
                HoldAtLeast(pathfinder, ai.attackPause);
                if (ai.faceTargetOnAttack) FaceTarget(level, ai, ai.targetID);
                PlayAttackFlipbook(level, ai);
                queue(ai.ownerID, kind, ai.targetID);
            };

            // Melee: P_CheckMeleeRange.
            if (ai.hasMelee && distance < ai.meleeRange + Radius(level, ai.targetID) && HasLineOfSight(level, ai.ownerID, ai.targetID)) {
                attack(NoticeKind::MELEE_READY);
                continue;
            }

            // Missile: only as a held direction ends (A_Chase's movecount
            // check), unless aggressive.
            if (ai.hasMissile) {
                const ComponentPathfinder* own = level.pathfinders.Get(ai.ownerID);
                const bool midStride = !ai.aggressive && own != nullptr && own->moving && own->moveTimer > own->chaseStep;

                if (!midStride && HasLineOfSight(level, ai.ownerID, ai.targetID) && MissileRoll(ai, distance)) {
                    attack(NoticeKind::MISSILE_READY);
                    continue;
                }
            }

            if (RollPercent(ai.activeChance)) queue(ai.ownerID, NoticeKind::ACTIVE, ai.targetID);
        }

        for (ComponentMonsterAI& ai : level.monsterAIs.components) {
            if (!ai.ownerEnabled || !ai.isActive || ai.state == MonsterAIState::DEAD) continue;
            if (level.monsterAIs.Get(ai.ownerID) == &ai) UpdateFlipbook(level, ai);
        }

        const NoticeHandler handler = noticeHandler;
        if (handler)
            for (const Notice& notice : notices) handler(notice.ownerID, notice.kind, notice.otherID);
    }
}
