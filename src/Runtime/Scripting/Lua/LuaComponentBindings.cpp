//
// Created by berke on 6/20/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"

#include "Headers/Objects/LuaWrappers.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Runtime/Gameplay/GameFunctions.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaRayHit.hpp"

#include <optional>
#include <tuple>

namespace {
    using namespace LuaBindingMetadata;

    void RegisterComponentMetadata() {
        RegisterType(Type("ColliderType", "Enum: Sphere or Box - see Collider.type.", {
            Prop("Sphere", "integer", true),
            Prop("Box", "integer", true),
        }));

        RegisterType(Type("AudioSource", "OpenAL audio source component.", {
            Prop("isValid", "boolean", true),
            Prop("offset", "Vector3", false, "Local position relative to the Entity's Transform, turned with its rotation. Lets several sit at different spots on one Entity."),
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
            Method("ClearSoundFileName"),
            Method("Play", {}, {}, "Plays soundFileName from the start, restarting it if it is already playing."),
            Method("Stop", {}, {}, "Stops the sound and rewinds it."),
            Method("Pause", {}, {}, "Pauses the sound where it is. Resume continues it."),
            Method("Resume", {}, {}, "Continues a paused sound. Does nothing unless the sound is paused."),
            Method("IsPlaying", {}, "boolean", "True while the sound is playing (false while paused or stopped)."),
            Method("SetSourcePosition", {Param("position", "Vector3")}),
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
            Method("AddVelocity", {Param("velocity", "Vector3")}),
            Method("AddImpulse", {Param("impulse", "Vector3")}, {},
                   "Adds impulse / mass to velocity, so heavier bodies move less. A mass of 0 or less counts as 1."),
            Method("Stop", {}, {}, "Sets velocity to zero."),
        }));

        RegisterType(Type("Model", "Static 3D model drawn at the Entity's Transform.", {
            Prop("isValid", "boolean", true),
            Prop("offset", "Vector3", false, "Local position relative to the Entity's Transform, turned with its rotation. Lets several sit at different spots on one Entity."),
            Prop("fileName", "string", false, "Model file relative to Assets, with extension (e.g. \"Models/crate.glb\"). Changing it swaps the rendered model next frame."),
        }, {
            Method("ClearFileName"),
        }));

        RegisterType(Type("Flipbook", "Plays a flipbook (.fpk) on one of the Entity's Sprites (or UI Sprites, on a UI entity) by swapping its textures.", {
            Prop("isValid", "boolean", true),
            Prop("isPlaying", "boolean", true, "True while frames are advancing (false when paused, stopped or a Once flipbook finished)."),
            Prop("flipbookFileName", "string", false, "The .fpk file relative to Assets, with extension (e.g. \"Animations/walk.fpk\"). Changing it rewinds to the first frame without changing whether it plays."),
            Prop("speed", "number", false, "Playback rate. 1 = the flipbook's own timing, 2 = twice as fast, 0 = frozen."),
        }, {
            Method("Play", {Param("flipbook", "string?"), Param("restart", "boolean?")}, {},
                   "Plays the given .fpk (or the current one). Does nothing if it is already playing, unless restart is true. A different file starts from its first frame."),
            Method("Pause", {}, {}, "Stops advancing frames and keeps the current one. Resume continues."),
            Method("Resume", {}, {}, "Continues from the current frame."),
            Method("Stop", {}, {}, "Stops and rewinds to the first frame, which the Sprite then shows."),
            Method("SetFrame", {Param("frameName", "string")}, {},
                   "Jumps to the frame with this name without firing its event. An unknown name is reported and changes nothing."),
            Method("GetFrame", {}, "string", "Name of the frame being shown (empty if there is no flipbook)."),
        }));

        RegisterType(Type("Collider", "Sphere or box collision volume.", {
            Prop("isValid", "boolean", true),
            Prop("offset", "Vector3", false, "Local position relative to the Entity's Transform, turned with its rotation. Lets several sit at different spots on one Entity."),
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
            Prop("acceleration", "number", false, "Units/s^2 the horizontal velocity speeds up toward the move direction while a movement key is held."),
            Prop("deceleration", "number", false, "Units/s^2 the horizontal velocity slows toward zero with no movement key held."),
            Prop("airControl", "number", false, "Multiplies acceleration and deceleration while airborne. 1 = full control, 0 = none."),
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
            Prop("forward", "Vector3", true, "Updated when a frame is drawn. Camera:Raycast uses yaw/pitch directly instead."),
            Prop("target", "Vector3", true),
        }, {
            Method("Raycast", {Param("length", "number?"), Param("requireCollider", "boolean?")}, "table",
                   "Shoots a ray from GetEyePosition() through the middle of the screen, ignoring this camera's own entity. "
                   "length defaults to farPlane, requireCollider to false. Returns nil on a miss, else the same table as Game.Raycast."),
            Method("GetEyePosition", {}, "Vector3",
                   "Where the camera sees from: the Transform position, raised by eyeHeight when the entity has an active PlayerController."),
            Method("WorldToScreen", {Param("point", "Vector3")}, "Vector2?",
                   "Where `point` appears on screen, normalized: (0, 0) top-left, (1, 1) bottom-right, like UITransform anchors. "
                   "Values outside 0..1 are off screen. nil when the point is behind the camera."),
            Method("ScreenToRay", {Param("x", "number"), Param("y", "number")}, "Vector3, Vector3",
                   "Returns origin, direction of the ray from the eye through screen point (x, y) (normalized, like WorldToScreen). "
                   "Feed them to Game.Raycast."),
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
            Prop("textureIndex", "string", false, "The image's path relative to Assets, with extension (e.g. \"Textures/UI/icon.png\")."),
            Prop("isActive", "boolean", false, "false = not drawn."),
        }));

        RegisterType(Type("UIText", "A UI element's text label.", {
            Prop("isValid", "boolean", true),
            Prop("text", "string", false, "UTF-8. A new line (\\n) starts a new line of text."),
            Prop("font", "string", false, "The font file's path relative to Assets, with extension (e.g. \"Fonts/title.ttf\"). \"\" = the engine's default font."),
            Prop("fontSize", "number", false, "Glyph size in pixels at the project's UI Reference Height; scales with the window height. 0 or less hides the text."),
        }));

        RegisterType(Type("Transform", "Position/rotation/scale in world space.", {
            Prop("isValid", "boolean", true),
            Prop("position", "Vector3"),
            Prop("rotation", "Vector4", false, "Quaternion, stored as (x, y, z, w)."),
            Prop("scale", "Vector3"),
            Prop("relativeHeight", "number", false, "Height above the current sector floor."),
            Prop("sectorIndex", "integer", true),
            Prop("isDirty", "boolean"),
            Prop("forward", "Vector3", true, "Unit vector the entity faces (local +Z), from rotation."),
            Prop("right", "Vector3", true, "Unit vector to the entity's right, from rotation."),
            Prop("up", "Vector3", true, "Unit vector out of the top of the entity (local +Y), from rotation."),
        }, {
            Method("AddPosition", {Param("position", "Vector3")}),
            Method("LookAt", {Param("point", "Vector3"), Param("yawOnly", "boolean?")}, {},
                   "Turns the entity so its local +Z faces a world point. yawOnly (default true) only turns left/right; false also tilts up/down."),
            Method("LookDirection", {Param("direction", "Vector3"), Param("yawOnly", "boolean?")}, {},
                   "Turns the entity so its local +Z faces along a direction. yawOnly (default true) only turns left/right; false also tilts up/down."),
            Method("DistanceTo", {Param("target", "Entity|Vector3")}, "number", "Straight-line distance to an entity's position or a point."),
            Method("DistanceTo2D", {Param("target", "Entity|Vector3")}, "number", "Like DistanceTo, but ignores height (y)."),
            Method("DirectionTo", {Param("target", "Entity|Vector3")}, "Vector3",
                   "Unit vector toward an entity's position or a point. Zero when the target is on top of this transform."),
            Method("DirectionTo2D", {Param("target", "Entity|Vector3")}, "Vector3",
                   "Like DirectionTo, but flat: y is 0. Handy for LookDirection and ground movement."),
        }));

        RegisterType(Type("Sprite", "Billboard/multi-directional sprite component.", {
            Prop("isValid", "boolean", true),
            Prop("offset", "Vector3", false, "Local position relative to the Entity's Transform, turned with its rotation. Lets several sit at different spots on one Entity."),
            Prop("isActive", "boolean", false, "false = not drawn."),
            Prop("sideCount", "integer", false, "0 = single, 1 = 8-sided (45 deg steps), 2 = 4-sided (90 deg steps). Other values are ignored."),
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
            Method("GetTextureFileName", {Param("slot", "integer")}, "string"),
            Method("SetTextureFileName", {Param("slot", "integer"), Param("fileName", "string")}),
            Method("ClearTextureFileName", {Param("slot", "integer")}),
            Method("ClearAllTextureFileNames"),
        }));
    }
}

namespace {
    Vector3 EntityPosition(const ScriptEntity& entity) {
        const ComponentTransform* transform = entity.GetTransform().GetComponent();
        if (transform == nullptr) throw sol::error("Entity has no Transform");
        return transform->position;
    }
}

void LuaScriptSystem::RegisterComponentBindings(sol::state& lua) {
    RegisterComponentMetadata();

    lua.new_enum( "ColliderType","Sphere", COLLIDERTYPE_SPHERE, "Box", COLLIDERTYPE_BOX);

    lua.new_usertype<ScriptAudioSource>(
        "AudioSource",

        "offset", sol::property(&ScriptAudioSource::GetOffset, &ScriptAudioSource::SetOffset),

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

        "ClearSoundFileName",
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

        "Play", &ScriptAudioSource::PlaySound,
        "Stop", &ScriptAudioSource::StopSound,
        "Pause", &ScriptAudioSource::PauseSound,
        "Resume", &ScriptAudioSource::ResumeSound,
        "IsPlaying", &ScriptAudioSource::IsPlaying,

        "SetSourcePosition",
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
            "target", sol::property(&ScriptCamera::GetTarget),

            "Raycast", [](sol::this_state state, const ScriptCamera& self,
                          const sol::optional<float> length, const sol::optional<bool> requireCollider) -> sol::object {
                const ComponentCamera* camera = self.GetComponent();
                if (camera == nullptr) return sol::make_object(state, sol::nil);

                return LuaRayHit::ToLua(state, *self.level, GameFunctions::Raycast(
                    *self.level,
                    self.GetEyePosition(),
                    self.GetViewForward(),
                    length.value_or(camera->farPlane),
                    self.ownerID,
                    requireCollider.value_or(false)
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

        "isActive", sol::property(
            &ScriptUISprite::GetIsActive,
            &ScriptUISprite::SetIsActive
        ),

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
        ),

        "font", sol::property(
            &ScriptUIText::GetFont,
            &ScriptUIText::SetFont
        ),

        "fontSize", sol::property(
            &ScriptUIText::GetFontSize,
            &ScriptUIText::SetFontSize
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

        "sectorIndex", sol::property(&ScriptTransform::GetSectorIndex),

        "isDirty", sol::property(
            &ScriptTransform::GetIsDirty,
            &ScriptTransform::SetIsDirty
        ),

        "forward", sol::property(&ScriptTransform::GetForward),
        "right", sol::property(&ScriptTransform::GetRight),
        "up", sol::property(&ScriptTransform::GetUp),

        "AddPosition", &ScriptTransform::AddPosition,

        "LookAt", [](const ScriptTransform& self, const Vector3& point, const sol::optional<bool> yawOnly) {
            self.LookAt(point, yawOnly.value_or(true));
        },

        "LookDirection", [](const ScriptTransform& self, const Vector3& direction, const sol::optional<bool> yawOnly) {
            self.LookDirection(direction, yawOnly.value_or(true));
        },

        // Each takes a point or an Entity (meaning its position).
        "DistanceTo", sol::overload(
            &ScriptTransform::DistanceTo,
            [](const ScriptTransform& self, const ScriptEntity& e) { return self.DistanceTo(EntityPosition(e)); }
        ),
        "DistanceTo2D", sol::overload(
            &ScriptTransform::DistanceTo2D,
            [](const ScriptTransform& self, const ScriptEntity& e) { return self.DistanceTo2D(EntityPosition(e)); }
        ),
        "DirectionTo", sol::overload(
            &ScriptTransform::DirectionTo,
            [](const ScriptTransform& self, const ScriptEntity& e) { return self.DirectionTo(EntityPosition(e)); }
        ),
        "DirectionTo2D", sol::overload(
            &ScriptTransform::DirectionTo2D,
            [](const ScriptTransform& self, const ScriptEntity& e) { return self.DirectionTo2D(EntityPosition(e)); }
        )
    );

    lua.new_usertype<ScriptSprite>(
        "Sprite",

        "offset", sol::property(&ScriptSprite::GetOffset, &ScriptSprite::SetOffset),

        "isValid", sol::property(&ScriptSprite::IsValid),

        "isActive", sol::property(
            &ScriptSprite::GetIsActive,
            &ScriptSprite::SetIsActive
        ),

        "sideCount", sol::property(
            &ScriptSprite::GetSideCount,
            &ScriptSprite::SetSideCount
        ),

        "color", sol::property(
            &ScriptSprite::GetColor,
            &ScriptSprite::SetColor
        ),

        "GetTextureFileName",
        &ScriptSprite::GetTextureFileName,

        "SetTextureFileName",
        &ScriptSprite::SetTextureFileName,

        "ClearTextureFileName",
        &ScriptSprite::ClearTextureFileName,

        "ClearAllTextureFileNames",
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