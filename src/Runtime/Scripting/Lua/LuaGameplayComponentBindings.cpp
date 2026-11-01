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
        "Resume", &ScriptPathfinder::Resume
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
