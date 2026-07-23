//
// Created by berke on 6/20/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/Components.hpp"

void LuaScriptSystem::RegisterComponentBindings(sol::state& lua) {
    lua.new_enum( "ColliderType","Sphere", COLLIDERTYPE_SPHERE, "Box", COLLIDERTYPE_BOX);

    lua.new_usertype<ScriptAudioSource>(
        "AudioSource",

        "isValid", sol::property(
            &ScriptAudioSource::IsValid
        ),

        "name", sol::property(
            &ScriptAudioSource::GetName
        ),

        "soundFileName", sol::property(
            &ScriptAudioSource::GetSoundFileName,
            &ScriptAudioSource::SetSoundFileName
        ),

        "clearSoundFileName",
        &ScriptAudioSource::ClearSoundFileName,

        "pitch", sol::property(
            &ScriptAudioSource::GetPitch,
            &ScriptAudioSource::SetPitch
        ),

        "gain", sol::property(
            &ScriptAudioSource::GetGain,
            &ScriptAudioSource::SetGain
        ),

        "looping", sol::property(
            &ScriptAudioSource::GetLooping,
            &ScriptAudioSource::SetLooping
        ),

        "playOnStart", sol::property(
            &ScriptAudioSource::GetPlayOnStart,
            &ScriptAudioSource::SetPlayOnStart
        ),

        "referenceDistance", sol::property(
            &ScriptAudioSource::GetReferenceDistance,
            &ScriptAudioSource::SetReferenceDistance
        ),

        "maxDistance", sol::property(
            &ScriptAudioSource::GetMaxDistance,
            &ScriptAudioSource::SetMaxDistance
        ),

        "rollOffFactor", sol::property(
            &ScriptAudioSource::GetRollOffFactor,
            &ScriptAudioSource::SetRollOffFactor
        ),

        "innerConeAngle", sol::property(
            &ScriptAudioSource::GetInnerConeAngle,
            &ScriptAudioSource::SetInnerConeAngle
        ),

        "outerConeAngle", sol::property(
            &ScriptAudioSource::GetOuterConeAngle,
            &ScriptAudioSource::SetOuterConeAngle
        ),

        "outerGain", sol::property(
            &ScriptAudioSource::GetOuterGain,
            &ScriptAudioSource::SetOuterGain
        ),

        "play",
        &ScriptAudioSource::PlaySound,

        "setSourcePosition",
        &ScriptAudioSource::SetSourcePosition
    );

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

            "addVelocity", &ScriptRigidbody::AddVelocity
        );

        lua.new_usertype<ScriptCollider>(
            "Collider",

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

            "friction", sol::property(
                &ScriptPlayerController::GetFriction,
                &ScriptPlayerController::SetFriction
            ),

            "sensitivityX", sol::property(
                &ScriptPlayerController::GetSensitivityX,
                &ScriptPlayerController::SetSensitivityX
            ),

            "sensitivityY", sol::property(
                &ScriptPlayerController::GetSensitivityY,
                &ScriptPlayerController::SetSensitivityY
            ),

            "noClip", sol::property(
                &ScriptPlayerController::GetNoClip,
                &ScriptPlayerController::SetNoClip
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
            "target", sol::property(&ScriptCamera::GetTarget)
        );

        lua.new_usertype<ScriptScript>(
            "Script",

            "isValid", sol::property(&ScriptScript::IsValid),

            "fileName", sol::property(
                &ScriptScript::GetFileName,
                &ScriptScript::SetFileName
            ),

            "enabled", sol::property(
                &ScriptScript::GetEnabled,
                &ScriptScript::SetEnabled
            )
        );

        lua.new_usertype<ScriptUITransform>(
            "UITransform",

            "isValid", sol::property(&ScriptUITransform::IsValid),

            "anchorMin", sol::property(
                &ScriptUITransform::GetAnchorMin,
                &ScriptUITransform::SetAnchorMin
            ),

            "anchorMax", sol::property(
                &ScriptUITransform::GetAnchorMax,
                &ScriptUITransform::SetAnchorMax
            ),

            "pivot", sol::property(
                &ScriptUITransform::GetPivot,
                &ScriptUITransform::SetPivot
            ),

            "position", sol::property(
                &ScriptUITransform::GetPosition,
                &ScriptUITransform::SetPosition
            ),

            "scale", sol::property(
                &ScriptUITransform::GetScale,
                &ScriptUITransform::SetScale
            ),

            "rotation", sol::property(
                &ScriptUITransform::GetRotation,
                &ScriptUITransform::SetRotation
            ),

            "resolvedPosition", sol::property(&ScriptUITransform::GetResolvedPosition),
            "resolvedSize", sol::property(&ScriptUITransform::GetResolvedSize)
        );

        lua.new_usertype<ScriptUISprite>(
            "UISprite",

            "isValid", sol::property(&ScriptUISprite::IsValid),

            "textureIndex", sol::property(
                &ScriptUISprite::GetTextureIndex,
                &ScriptUISprite::SetTextureIndex
            )
        );

        lua.new_usertype<ScriptUIText>(
            "UIText",

            "isValid", sol::property(&ScriptUIText::IsValid),

            "text", sol::property(
                &ScriptUIText::GetText,
                &ScriptUIText::SetText
            )
        );

        lua.new_usertype<ScriptTransform>(
            "Transform",

            "isValid", sol::property(&ScriptTransform::IsValid),

            "position", sol::property(
                &ScriptTransform::GetPosition,
                &ScriptTransform::SetPosition
            ),

            "scale", sol::property(
                &ScriptTransform::GetScale,
                &ScriptTransform::SetScale
            ),

            "relativeHeight", sol::property(
                &ScriptTransform::GetRelativeHeight,
                &ScriptTransform::SetRelativeHeight
            ),

            "forward", sol::property(
                &ScriptTransform::GetForward,
                &ScriptTransform::SetForward
            ),

            "sectorIndex", sol::property(&ScriptTransform::GetSectorIndex),

            "isDirty", sol::property(
                &ScriptTransform::GetIsDirty,
                &ScriptTransform::SetIsDirty
            ),

            "addPosition", &ScriptTransform::AddPosition
    );

    lua.new_usertype<ScriptSprite>(
        "Sprite",

        "isValid", sol::property(&ScriptSprite::IsValid),

        "sideCount", sol::property(
            &ScriptSprite::GetSideCount,
            &ScriptSprite::SetSideCount
        ),

        "getTextureFileName",
        &ScriptSprite::GetTextureFileName,

        "setTextureFileName",
        &ScriptSprite::SetTextureFileName,

        "clearTextureFileName",
        &ScriptSprite::ClearTextureFileName,

        "clearAllTextureFileNames",
        &ScriptSprite::ClearAllTextureFileNames,

        "northTextureFileName", sol::property(
            &ScriptSprite::GetNorthTextureFileName,
            &ScriptSprite::SetNorthTextureFileName
        ),

        "northEastTextureFileName", sol::property(
            &ScriptSprite::GetNorthEastTextureFileName,
            &ScriptSprite::SetNorthEastTextureFileName
        ),

        "eastTextureFileName", sol::property(
            &ScriptSprite::GetEastTextureFileName,
            &ScriptSprite::SetEastTextureFileName
        ),

        "southEastTextureFileName", sol::property(
            &ScriptSprite::GetSouthEastTextureFileName,
            &ScriptSprite::SetSouthEastTextureFileName
        ),

        "southTextureFileName", sol::property(
            &ScriptSprite::GetSouthTextureFileName,
            &ScriptSprite::SetSouthTextureFileName
        ),

        "southWestTextureFileName", sol::property(
            &ScriptSprite::GetSouthWestTextureFileName,
            &ScriptSprite::SetSouthWestTextureFileName
        ),

        "westTextureFileName", sol::property(
            &ScriptSprite::GetWestTextureFileName,
            &ScriptSprite::SetWestTextureFileName
        ),

        "northWestTextureFileName", sol::property(
            &ScriptSprite::GetNorthWestTextureFileName,
            &ScriptSprite::SetNorthWestTextureFileName
        )
    );

        lua.new_usertype<ScriptDecal>(
            "Decal",

            "wallIndex", sol::property(
                &ScriptDecal::GetWallIndex,
                &ScriptDecal::SetWallIndex
            ),

            "verticalPos", sol::property(
                &ScriptDecal::GetVerticalPos,
                &ScriptDecal::SetVerticalPos
            ),

            "horizontalPos", sol::property(
                &ScriptDecal::GetHorizontalPos,
                &ScriptDecal::SetHorizontalPos
            ),

            "wallNormalOffset", sol::property(
                &ScriptDecal::GetWallNormalOffset,
                &ScriptDecal::SetWallNormalOffset
            ),

            "wallT", sol::property(
                &ScriptDecal::GetWallT,
                &ScriptDecal::SetWallT
            ),

            "baseHeight", sol::property(
                &ScriptDecal::GetBaseHeight,
                &ScriptDecal::SetBaseHeight
            ),

            "absHeight", sol::property(
                &ScriptDecal::GetAbsHeight,
                &ScriptDecal::SetAbsHeight
            )
        );
}