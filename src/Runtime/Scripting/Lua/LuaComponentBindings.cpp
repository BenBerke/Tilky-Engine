//
// Created by berke on 6/20/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"

namespace {
    using namespace LuaBindingMetadata;

    void RegisterComponentMetadata() {
        RegisterType(Type("ColliderType", "Enum: Sphere or Box - see Collider.type.", {
            Prop("Sphere", "integer", true),
            Prop("Box", "integer", true),
        }));

        RegisterType(Type("AudioSource", "OpenAL audio source component.", {
            Prop("isValid", "boolean", true),
            Prop("name", "string", true, "OpenAL source name."),
            Prop("soundFileName", "string"),
            Prop("pitch", "number"),
            Prop("gain", "number"),
            Prop("looping", "boolean"),
            Prop("playOnStart", "boolean"),
            Prop("referenceDistance", "number"),
            Prop("maxDistance", "number"),
            Prop("rollOffFactor", "number"),
            Prop("innerConeAngle", "number"),
            Prop("outerConeAngle", "number"),
            Prop("outerGain", "number"),
        }, {
            Method("clearSoundFileName"),
            Method("play"),
            Method("setSourcePosition", {Param("position", "Vector3")}),
        }));

        RegisterType(Type("Rigidbody", "Physics component - velocity/mass/gravity.", {
            Prop("isValid", "boolean", true),
            Prop("isGrounded", "boolean"),
            Prop("isStatic", "boolean"),
            Prop("mass", "number"),
            Prop("gravityScale", "number"),
            Prop("friction", "number"),
            Prop("velocity", "Vector3"),
        }, {
            Method("addVelocity", {Param("velocity", "Vector3")}),
        }));

        RegisterType(Type("Collider", "Sphere or box collision volume.", {
            Prop("isValid", "boolean", true),
            Prop("type", "ColliderType"),
            Prop("isActive", "boolean"),
            Prop("isTrigger", "boolean"),
            Prop("scale", "Vector3", false, "Box: full extents. Sphere: scale.x is the radius."),
            Prop("stepSize", "number"),
        }));

        RegisterType(Type("PlayerController", "First-person player movement/look component.", {
            Prop("isValid", "boolean", true),
            Prop("isActive", "boolean"),
            Prop("speed", "number"),
            Prop("runningSpeed", "number"),
            Prop("jumpPower", "number"),
            Prop("eyeHeight", "number"),
            Prop("friction", "number"),
            Prop("sensitivityX", "number"),
            Prop("sensitivityY", "number"),
            Prop("noClip", "boolean"),
            Prop("velocity", "Vector3", true),
            Prop("currentSpeed", "number", true),
            Prop("currentEyeHeight", "number", true),
        }));

        RegisterType(Type("Camera", "Perspective camera component.", {
            Prop("isValid", "boolean", true),
            Prop("isActive", "boolean"),
            Prop("yaw", "number"),
            Prop("pitch", "number"),
            Prop("fov", "number"),
            Prop("aspectRatio", "number"),
            Prop("nearPlane", "number"),
            Prop("farPlane", "number"),
            Prop("forward", "Vector3", true),
            Prop("target", "Vector3", true),
        }));

        RegisterType(Type("UITransform", "Anchor/pivot-based transform for UI elements.", {
            Prop("isValid", "boolean", true),
            Prop("anchorMin", "Vector2"),
            Prop("anchorMax", "Vector2"),
            Prop("pivot", "Vector2"),
            Prop("position", "Vector2"),
            Prop("scale", "Vector2"),
            Prop("rotation", "number"),
            Prop("resolvedPosition", "Vector2", true, "Final on-screen position after anchor/pivot resolution."),
            Prop("resolvedSize", "Vector2", true),
        }));

        RegisterType(Type("UISprite", "A UI element's texture.", {
            Prop("isValid", "boolean", true),
            Prop("textureIndex", "string"),
        }));

        RegisterType(Type("UIText", "A UI element's text label.", {
            Prop("isValid", "boolean", true),
            Prop("text", "string"),
        }));

        RegisterType(Type("Transform", "Position/rotation/scale in world space.", {
            Prop("isValid", "boolean", true),
            Prop("position", "Vector3"),
            Prop("rotation", "Vector4", false, "Quaternion, stored as (x, y, z, w)."),
            Prop("scale", "Vector3"),
            Prop("relativeHeight", "number", false, "Height above the current sector floor."),
            Prop("forward", "Vector2"),
            Prop("sectorIndex", "integer", true),
            Prop("isDirty", "boolean"),
        }, {
            Method("addPosition", {Param("position", "Vector3")}),
        }));

        RegisterType(Type("Sprite", "Billboard/multi-directional sprite component.", {
            Prop("isValid", "boolean", true),
            Prop("sideCount", "integer", false, "1 (single), 4 (90 deg steps) or 8 (45 deg steps)."),
            Prop("color", "Vector4"),
            Prop("northTextureFileName", "string"),
            Prop("northEastTextureFileName", "string"),
            Prop("eastTextureFileName", "string"),
            Prop("southEastTextureFileName", "string"),
            Prop("southTextureFileName", "string"),
            Prop("southWestTextureFileName", "string"),
            Prop("westTextureFileName", "string"),
            Prop("northWestTextureFileName", "string"),
        }, {
            Method("getTextureFileName", {Param("slot", "integer")}, "string"),
            Method("setTextureFileName", {Param("slot", "integer"), Param("fileName", "string")}),
            Method("clearTextureFileName", {Param("slot", "integer")}),
            Method("clearAllTextureFileNames"),
        }));
    }
}

void LuaScriptSystem::RegisterComponentBindings(sol::state& lua) {
    RegisterComponentMetadata();

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

        "rotation", sol::property(
            &ScriptTransform::GetRotation,
            &ScriptTransform::SetRotation
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

        "color", sol::property(
            &ScriptSprite::GetColor,
            &ScriptSprite::SetColor
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
}