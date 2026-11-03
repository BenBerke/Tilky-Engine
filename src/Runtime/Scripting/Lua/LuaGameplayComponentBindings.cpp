// Part of LuaScriptSystem::RegisterComponentBindings (LuaComponentBindings.cpp):
// the gameplay component usertypes, in their own file only so each part
// compiles faster.

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include "Headers/Runtime/Gameplay/UIButtonSystem.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaRayHit.hpp"

#include <optional>
#include <tuple>

void LuaScriptSystem::RegisterGameplayComponentBindings(sol::state& lua) {
    lua.new_usertype<ScriptRigidbody>(
        "Rigidbody",

        "isValid", sol::property(&ScriptRigidbody::IsValid),

        "isGrounded", sol::property(
          &ScriptRigidbody::GetIsGrounded,
          &ScriptRigidbody::SetIsGrounded
        ),

        "isStatic", sol::property(
            &ScriptRigidbody::GetIsStatic,
            &ScriptRigidbody::SetIsStatic
        ),

        "mass", sol::property(
            &ScriptRigidbody::GetMass,
            &ScriptRigidbody::SetMass
        ),

        "gravityScale", sol::property(
            &ScriptRigidbody::GetGravityScale,
            &ScriptRigidbody::SetGravityScale
        ),

        "friction", sol::property(
            &ScriptRigidbody::GetFriction,
            &ScriptRigidbody::SetFriction
        ),

        "velocity", sol::property(
            &ScriptRigidbody::GetVelocity,
            &ScriptRigidbody::SetVelocity
        ),

        "AddVelocity", &ScriptRigidbody::AddVelocity,
        "AddImpulse", &ScriptRigidbody::AddImpulse,
        "Stop", &ScriptRigidbody::Stop
    );

    lua.new_usertype<ScriptModel>(
        "Model",

        "offset", sol::property(&ScriptModel::GetOffset, &ScriptModel::SetOffset),

        "isValid", sol::property(&ScriptModel::IsValid),

        "fileName", sol::property(
            &ScriptModel::GetFileName,
            &ScriptModel::SetFileName
        ),

        "ClearFileName", &ScriptModel::ClearFileName
    );

#define TILKY_EMITTER_FIELD(name) \
        #name, sol::property( \
            [](const ScriptParticleEmitter& self) { const auto* c = self.GetComponent(); return c != nullptr ? c->name : ComponentParticleEmitter{}.name; }, \
            [](const ScriptParticleEmitter& self, const decltype(ComponentParticleEmitter::name)& value) { if (auto* c = self.GetComponent()) c->name = value; })

    lua.new_usertype<ScriptParticleEmitter>(
        "ParticleEmitter",

        "isValid", sol::property(&ScriptParticleEmitter::IsValid),
        "isPlaying", sol::property([](const ScriptParticleEmitter& self) { const auto* c = self.GetComponent(); return c != nullptr && c->playing; }),

        TILKY_EMITTER_FIELD(isActive), TILKY_EMITTER_FIELD(rate), TILKY_EMITTER_FIELD(burstMin), TILKY_EMITTER_FIELD(burstMax),
        TILKY_EMITTER_FIELD(offset), TILKY_EMITTER_FIELD(direction), TILKY_EMITTER_FIELD(spread),
        TILKY_EMITTER_FIELD(speedMin), TILKY_EMITTER_FIELD(speedMax), TILKY_EMITTER_FIELD(gravity), TILKY_EMITTER_FIELD(drag),
        TILKY_EMITTER_FIELD(lifetimeMin), TILKY_EMITTER_FIELD(lifetimeMax),
        TILKY_EMITTER_FIELD(sizeMin), TILKY_EMITTER_FIELD(sizeMax), TILKY_EMITTER_FIELD(sizeEnd),
        TILKY_EMITTER_FIELD(colorStart), TILKY_EMITTER_FIELD(colorEnd), TILKY_EMITTER_FIELD(bounciness),

        "Play", [](const ScriptParticleEmitter& self) { if (auto* c = self.GetComponent()) c->playing = true; },
        "Stop", [](const ScriptParticleEmitter& self) { if (auto* c = self.GetComponent()) { c->playing = false; c->spawnDebt = 0.0f; } },
        "Emit", [](const ScriptParticleEmitter& self, const sol::optional<int> count, const sol::optional<Vector3> position, const sol::optional<Vector3> direction) -> int {
            const ComponentParticleEmitter* c = self.GetComponent();
            if (c == nullptr || !c->isActive) return 0;
            const ComponentTransform* transform = self.level->transforms.Get(c->ownerID);
            const Vector3 at = position.value_or(transform != nullptr ? transform->LocalToWorld(c->offset) : Vector3{0.0f, 0.0f, 0.0f});
            return ParticleSystem::Emit(*self.level, *c, at, direction.value_or(Vector3{0.0f, 0.0f, 0.0f}), count.value_or(-1));
        }
    );

#undef TILKY_EMITTER_FIELD

    lua.new_usertype<ScriptFlipbook>(
        "Flipbook",

        "isValid", sol::property(&ScriptFlipbook::IsValid),
        "isPlaying", sol::property(&ScriptFlipbook::IsPlaying),

        "flipbookFileName", sol::property(
            &ScriptFlipbook::GetFlipbookFileName,
            &ScriptFlipbook::SetFlipbookFileName
        ),

        "speed", sol::property(&ScriptFlipbook::GetSpeed, &ScriptFlipbook::SetSpeed),

        "Play", [](const ScriptFlipbook& self, const sol::optional<std::string>& fileName,
                   const sol::optional<bool> restart) {
            self.Play(fileName.value_or(""), restart.value_or(false));
        },
        "Pause", &ScriptFlipbook::Pause,
        "Resume", &ScriptFlipbook::Resume,
        "Stop", &ScriptFlipbook::Stop,
        "SetFrame", &ScriptFlipbook::SetFrame,
        "GetFrame", &ScriptFlipbook::GetFrame
    );

    lua.new_usertype<ScriptPathfinder>(
        "Pathfinder",

        "isValid", sol::property(&ScriptPathfinder::IsValid),
        "isActive", sol::property(&ScriptPathfinder::GetIsActive, &ScriptPathfinder::SetIsActive),
        "hasArrived", sol::property(&ScriptPathfinder::GetHasArrived),

        "input", sol::property(&ScriptPathfinder::GetInput),
        "destination", sol::property(&ScriptPathfinder::GetDestination),
        "target", sol::property(
            [](const ScriptPathfinder& self, const sol::this_state state) -> sol::object {
                const ID targetID = self.GetTargetID();
                if (targetID == INVALID_ENTITY_ID) return sol::make_object(state, sol::nil);
                return sol::make_object(state, ScriptEntity{self.level, targetID});
            }
        ),
        "offset", sol::property(&ScriptPathfinder::GetOffset, &ScriptPathfinder::SetOffset),

        "speed", sol::property(&ScriptPathfinder::GetSpeed, &ScriptPathfinder::SetSpeed),
        "range", sol::property(&ScriptPathfinder::GetRange, &ScriptPathfinder::SetRange),
        "isAirEntity", sol::property(&ScriptPathfinder::GetIsAirEntity, &ScriptPathfinder::SetIsAirEntity),
        "isRangedEntity", sol::property(&ScriptPathfinder::GetIsRangedEntity, &ScriptPathfinder::SetIsRangedEntity),

        "SetDestination", &ScriptPathfinder::SetDestination,
        "SetTarget", [](const ScriptPathfinder& self, const ScriptEntity& target, const sol::optional<Vector3> offset) {
            if (target.GetEntity() == nullptr) {
                spdlog::warn("Pathfinder.SetTarget: the entity is gone");
                return;
            }
            self.SetTargetID(target.ownerID, offset ? std::optional<Vector3>(*offset) : std::nullopt);
        },
        "Stop", &ScriptPathfinder::Stop,
        "Resume", &ScriptPathfinder::Resume,
        "Pause", &ScriptPathfinder::Pause,
        "PickNewDirection", &ScriptPathfinder::PickNewDirection,

        "isMoving", sol::property(&ScriptPathfinder::GetIsMoving),
        "isStuck", sol::property(&ScriptPathfinder::GetIsStuck),
        "moveDirection", sol::property(&ScriptPathfinder::GetMoveDirection),

        "keepMovingInRange", sol::property(
            [](const ScriptPathfinder& self) { const auto* c = self.GetComponent(); return c != nullptr && c->KeepsMovingInRange(); },
            [](const ScriptPathfinder& self, const bool on) { if (auto* c = self.GetComponent()) c->SetFlag(ComponentPathfinder::KEEP_MOVING_IN_RANGE, on); }),
        "canDropOff", sol::property(
            [](const ScriptPathfinder& self) { const auto* c = self.GetComponent(); return c != nullptr && c->CanDropOff(); },
            [](const ScriptPathfinder& self, const bool on) { if (auto* c = self.GetComponent()) c->SetFlag(ComponentPathfinder::CAN_DROP_OFF, on); }),
        "opensDoors", sol::property(
            [](const ScriptPathfinder& self) { const auto* c = self.GetComponent(); return c != nullptr && c->OpensDoors(); },
            [](const ScriptPathfinder& self, const bool on) { if (auto* c = self.GetComponent()) c->SetFlag(ComponentPathfinder::OPENS_DOORS, on); }),
        "facing", sol::property(
            [](const ScriptPathfinder& self) { const auto* c = self.GetComponent(); return c != nullptr ? static_cast<int>(c->facing) : 0; },
            [](const ScriptPathfinder& self, const int value) { if (auto* c = self.GetComponent()) c->facing = static_cast<PathfinderFacing>(std::clamp(value, 0, 2)); }),
        "turning", sol::property(
            [](const ScriptPathfinder& self) { const auto* c = self.GetComponent(); return c != nullptr ? static_cast<int>(c->turning) : 0; },
            [](const ScriptPathfinder& self, const int value) { if (auto* c = self.GetComponent()) c->turning = static_cast<PathfinderTurning>(std::clamp(value, 0, 1)); }),

#define TILKY_PATHFINDER_NUMBER(name) \
        #name, sol::property( \
            [](const ScriptPathfinder& self) { const auto* c = self.GetComponent(); return c != nullptr ? c->name : 0.0f; }, \
            [](const ScriptPathfinder& self, const float value) { if (auto* c = self.GetComponent()) c->name = std::max(value, 0.0f); })

        TILKY_PATHFINDER_NUMBER(chaseStep),
        TILKY_PATHFINDER_NUMBER(holdMinSteps),
        TILKY_PATHFINDER_NUMBER(holdMaxSteps),
        TILKY_PATHFINDER_NUMBER(deadzone),
        TILKY_PATHFINDER_NUMBER(swapChance),
        TILKY_PATHFINDER_NUMBER(floatSpeed),
        TILKY_PATHFINDER_NUMBER(floatRatio),
        TILKY_PATHFINDER_NUMBER(pushFriction),
        TILKY_PATHFINDER_NUMBER(stuckTime)
#undef TILKY_PATHFINDER_NUMBER
    );

    lua.new_usertype<ScriptMonsterAI>(
        "MonsterAI",

        "isValid", sol::property(&ScriptMonsterAI::IsValid),

        "state", sol::property(
            [](const ScriptMonsterAI& self) { const auto* c = self.GetComponent(); return static_cast<int>(c != nullptr ? c->state : MonsterAIState::IDLE); }),
        "target", sol::property(
            [](const ScriptMonsterAI& self, const sol::this_state state) -> sol::object {
                const auto* c = self.GetComponent();
                if (c == nullptr || c->state != MonsterAIState::CHASING || self.level->GetEntity(c->targetID) == nullptr)
                    return sol::make_object(state, sol::nil);
                return sol::make_object(state, ScriptEntity{self.level, c->targetID});
            }),
        "canSeeTarget", sol::property(
            [](const ScriptMonsterAI& self) {
                const auto* c = self.GetComponent();
                return c != nullptr && MonsterAISystem::CanSee(*self.level, *c, c->targetID);
            }),

#define TILKY_MONSTER_NUMBER(name) \
        #name, sol::property( \
            [](const ScriptMonsterAI& self) { const auto* c = self.GetComponent(); return c != nullptr ? c->name : 0.0f; }, \
            [](const ScriptMonsterAI& self, const float value) { if (auto* c = self.GetComponent()) c->name = std::max(value, 0.0f); })
#define TILKY_MONSTER_TEXT(name) \
        #name, sol::property( \
            [](const ScriptMonsterAI& self) { const auto* c = self.GetComponent(); return c != nullptr ? c->name : std::string{}; }, \
            [](const ScriptMonsterAI& self, const std::string& value) { if (auto* c = self.GetComponent()) c->name = value; })
#define TILKY_MONSTER_FLAG(name) \
        #name, sol::property( \
            [](const ScriptMonsterAI& self) { const auto* c = self.GetComponent(); return c != nullptr && c->name; }, \
            [](const ScriptMonsterAI& self, const bool value) { if (auto* c = self.GetComponent()) c->name = value; })

        TILKY_MONSTER_FLAG(isActive),
        TILKY_MONSTER_NUMBER(health),
        TILKY_MONSTER_TEXT(walkFlipbook),
        TILKY_MONSTER_TEXT(attackFlipbook),
        TILKY_MONSTER_TEXT(dieFlipbook),
        TILKY_MONSTER_NUMBER(sightFov),
        TILKY_MONSTER_NUMBER(sightRange),
        TILKY_MONSTER_NUMBER(nearRange),
        TILKY_MONSTER_NUMBER(lookInterval),
        TILKY_MONSTER_NUMBER(chaseStep),
        TILKY_MONSTER_NUMBER(reactionTime),
        TILKY_MONSTER_NUMBER(meleeRange),
        TILKY_MONSTER_NUMBER(missileMinRange),
        TILKY_MONSTER_NUMBER(missileMaxRange),
        TILKY_MONSTER_NUMBER(missileDistanceOffset),
        TILKY_MONSTER_NUMBER(missileDistanceScale),
        TILKY_MONSTER_NUMBER(missileDistanceCap),
        TILKY_MONSTER_NUMBER(attackPause),
        TILKY_MONSTER_NUMBER(painChance),
        TILKY_MONSTER_NUMBER(painPause),
        TILKY_MONSTER_NUMBER(targetLockTime),
        TILKY_MONSTER_NUMBER(activeChance),
        TILKY_MONSTER_FLAG(hasMelee),
        TILKY_MONSTER_FLAG(hasMissile),
        TILKY_MONSTER_FLAG(hearing),
        TILKY_MONSTER_FLAG(ambush),
        TILKY_MONSTER_FLAG(aggressive),
        TILKY_MONSTER_FLAG(infights),
        TILKY_MONSTER_FLAG(drivePathfinder),
        TILKY_MONSTER_FLAG(faceTargetOnAttack),
#undef TILKY_MONSTER_NUMBER
#undef TILKY_MONSTER_TEXT
#undef TILKY_MONSTER_FLAG

        "Wake", [](const ScriptMonsterAI& self, const ScriptEntity& target) {
            if (auto* c = self.GetComponent()) MonsterAISystem::Wake(*self.level, *c, target.ownerID);
        },
        "Sleep", [](const ScriptMonsterAI& self) {
            if (auto* c = self.GetComponent()) MonsterAISystem::Sleep(*self.level, *c);
        },
        "Hurt", [](const ScriptMonsterAI& self, const sol::optional<ScriptEntity> attacker, const sol::optional<float> damage) {
            if (auto* c = self.GetComponent()) MonsterAISystem::Hurt(*self.level, *c, attacker ? attacker->ownerID : INVALID_ID, damage.value_or(0.0f));
        }
    );

    lua.new_usertype<ScriptCollider>(
        "Collider",

        "offset", sol::property(&ScriptCollider::GetOffset, &ScriptCollider::SetOffset),

        "isValid", sol::property(&ScriptCollider::IsValid),

        "type", sol::property(
            &ScriptCollider::GetType,
            &ScriptCollider::SetType
        ),

        "isActive", sol::property(
            &ScriptCollider::GetIsActive,
            &ScriptCollider::SetIsActive
        ),

        "isTrigger", sol::property(
            &ScriptCollider::GetIsTrigger,
            &ScriptCollider::SetIsTrigger
        ),

        "scale", sol::property(
            &ScriptCollider::GetScale,
            &ScriptCollider::SetScale
        ),

        "stepSize", sol::property(
            &ScriptCollider::GetStepSize,
            &ScriptCollider::SetStepSize
        ),

        "maxSlope", sol::property(
            &ScriptCollider::GetMaxSlope,
            &ScriptCollider::SetMaxSlope
        )
    );

    lua.new_usertype<ScriptPlayerController>(
        "PlayerController",

        "isValid", sol::property(&ScriptPlayerController::IsValid),

        "isActive", sol::property(
            &ScriptPlayerController::GetIsActive,
            &ScriptPlayerController::SetIsActive
        ),

        "speed", sol::property(
            &ScriptPlayerController::GetSpeed,
            &ScriptPlayerController::SetSpeed
        ),

        "runningSpeed", sol::property(
            &ScriptPlayerController::GetRunningSpeed,
            &ScriptPlayerController::SetRunningSpeed
        ),

        "jumpPower", sol::property(
            &ScriptPlayerController::GetJumpPower,
            &ScriptPlayerController::SetJumpPower
        ),

        "eyeHeight", sol::property(
            &ScriptPlayerController::GetEyeHeight,
            &ScriptPlayerController::SetEyeHeight
        ),

        "acceleration", sol::property(
            &ScriptPlayerController::GetAcceleration,
            &ScriptPlayerController::SetAcceleration
        ),

        "deceleration", sol::property(
            &ScriptPlayerController::GetDeceleration,
            &ScriptPlayerController::SetDeceleration
        ),

        "airControl", sol::property(
            &ScriptPlayerController::GetAirControl,
            &ScriptPlayerController::SetAirControl
        ),

        "sensitivityX", sol::property(
            &ScriptPlayerController::GetSensitivityX,
            &ScriptPlayerController::SetSensitivityX
        ),

        "sensitivityY", sol::property(
            &ScriptPlayerController::GetSensitivityY,
            &ScriptPlayerController::SetSensitivityY
        ),

        "velocity", sol::property(&ScriptPlayerController::GetVelocity),
        "currentSpeed", sol::property(&ScriptPlayerController::GetCurrentSpeed),
        "currentEyeHeight", sol::property(&ScriptPlayerController::GetCurrentEyeHeight)
    );

    lua.new_usertype<ScriptCamera>(
        "Camera",

        "isValid", sol::property(&ScriptCamera::IsValid),

        "isActive", sol::property(
            &ScriptCamera::GetIsActive,
            &ScriptCamera::SetIsActive
        ),

        "yaw", sol::property(
            &ScriptCamera::GetYaw,
            &ScriptCamera::SetYaw
        ),

        "pitch", sol::property(
            &ScriptCamera::GetPitch,
            &ScriptCamera::SetPitch
        ),

        "fov", sol::property(
            &ScriptCamera::GetFov,
            &ScriptCamera::SetFov
        ),

        "aspectRatio", sol::property(
            &ScriptCamera::GetAspectRatio,
            &ScriptCamera::SetAspectRatio
        ),

        "nearPlane", sol::property(
            &ScriptCamera::GetNearPlane,
            &ScriptCamera::SetNearPlane
        ),

        "farPlane", sol::property(
            &ScriptCamera::GetFarPlane,
            &ScriptCamera::SetFarPlane
        ),

        "forward", sol::property(&ScriptCamera::GetForward),
        "target", sol::property(&ScriptCamera::GetTarget),

        "Raycast", [](sol::this_state state, const ScriptCamera& self, const sol::optional<float> length,
                      const sol::optional<bool> requireCollider, const sol::optional<int> rayType) -> sol::object {
            const ComponentCamera* camera = self.GetComponent();
            if (camera == nullptr) return sol::make_object(state, sol::nil);

            return LuaRayHit::ToLua(state, *self.level, GameFunctions::Raycast(
                *self.level,
                self.GetEyePosition(),
                self.GetViewForward(),
                length.value_or(camera->farPlane),
                self.ownerID,
                requireCollider.value_or(false),
                LuaRayHit::ToRayType(rayType, "Camera:Raycast")
            ));
        },

        "GetEyePosition", &ScriptCamera::GetEyePosition,

        "WorldToScreen", [](sol::this_state state, const ScriptCamera& self, const Vector3& point) -> sol::object {
            const std::optional<Vector2> screen = self.WorldToScreen(point);
            if (!screen.has_value()) return sol::make_object(state, sol::nil);
            return sol::make_object(state, *screen);
        },

        "ScreenToRay", [](const ScriptCamera& self, const float x, const float y) {
            return std::make_tuple(self.GetEyePosition(), self.ScreenToDirection(x, y));
        }
    );
}
