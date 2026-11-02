//
// Created by berke on 6/20/2026.
//

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

namespace {
    using namespace LuaBindingMetadata;

    void RegisterComponentMetadata() {
        RegisterType(Type("ColliderType", "Enum: Sphere, Box or Capsule - see Collider.type.", {
            Prop("Sphere", "integer", true),
            Prop("Box", "integer", true),
            Prop("Capsule", "integer", true),
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

        RegisterType(Type("PathfinderInput", "Enum: Position or Entity - see Pathfinder.input.", {
            Prop("Position", "integer", true),
            Prop("Entity", "integer", true),
        }));

        RegisterType(Type("Pathfinder", "Walks the Entity to a point or after another Entity, Doom style: along 8 directions, stepping around whatever blocks it. No baked paths.", {
            Prop("isValid", "boolean", true),
            Prop("isActive", "boolean", false, "False stops it where it is (and lets go of its Rigidbody). Same as Stop/Resume."),
            Prop("hasArrived", "boolean", true, "True while it is at its destination (within range when ranged). OnPathfinderArrived is called each time this turns true."),
            Prop("input", "PathfinderInput", true, "Position or Entity. SetDestination and SetTarget switch it."),
            Prop("destination", "Vector3", true, "Where it is heading. Following an entity: the target's position + offset, or where the target last was while it is disabled."),
            Prop("target", "Entity?", true, "The entity it follows. nil when walking to a position, or once the target is destroyed."),
            Prop("offset", "Vector3", false, "Added to the target's position on the world axes (Entity input only)."),
            Prop("speed", "number", false, "World units per second, slowed and paused by the time scale."),
            Prop("range", "number", false, "How close a ranged entity gets before it stops."),
            Prop("isAirEntity", "boolean", false, "Flies: ignores step height and drop-offs, and rises or sinks toward the destination's height."),
            Prop("isRangedEntity", "boolean", false, "Arrives once the destination is within range. See keepMovingInRange."),
            Prop("keepMovingInRange", "boolean", false, "Ranged entities: keeps walking toward it once in range, as Doom's monsters do. False stops in range."),
            Prop("canDropOff", "boolean", false, "Walks off ledges higher than its step size instead of refusing to."),
            Prop("opensDoors", "boolean", false, "A closed door in the way gets opened: the sector's Door dispatch, unless locked without the tag or Pathfinders Can Open is off."),
            Prop("facing", "PathfinderFacing", false, "What it turns to face."),
            Prop("turning", "PathfinderTurning", false, "How it turns to face where it walks."),
            Prop("chaseStep", "number", false, "Seconds per chase step (Doom: 3/35): how often it turns, and what the direction holds count."),
            Prop("holdMinSteps", "number", false, "Chase steps it holds a direction at least (Doom: 1)."),
            Prop("holdMaxSteps", "number", false, "Chase steps it holds a direction at most (Doom: 16)."),
            Prop("deadzone", "number", false, "This close on an axis counts as there (Doom: 10). Also the reach for arriving."),
            Prop("swapChance", "number", false, "Percent chance to try the shorter axis first (Doom: about 21.5)."),
            Prop("floatSpeed", "number", false, "Air only: units per second up and down (Doom: 140)."),
            Prop("floatRatio", "number", false, "Air only: changes height once the height difference times this is more than the distance (Doom: 3). 0 = always."),
            Prop("pushFriction", "number", false, "How much of a push's speed is left per tic, 1/35 of a second (Doom: 0.90625)."),
            Prop("stuckTime", "number", false, "Seconds without progress before it counts as stuck. 0 = never."),
            Prop("isMoving", "boolean", true, "True while it is walking."),
            Prop("isStuck", "boolean", true, "True once it has made no progress for stuckTime."),
            Prop("moveDirection", "Vector3", true, "The way it is walking, one of 8 compass directions, or zero when it isn't."),
        }, {
            Method("SetDestination", {Param("position", "Vector3")}, {},
                   "Walks to this world position (switches input to Position)."),
            Method("SetTarget", {Param("entity", "Entity"), Param("offset", "Vector3?")}, {},
                   "Follows this entity, keeping up as it moves (switches input to Entity). offset is added to its position on the world axes; left out, the current offset is kept."),
            Method("Stop", {}, {}, "Stops moving: isActive = false."),
            Method("Resume", {}, {}, "Starts moving again: isActive = true."),
            Method("Pause", {Param("seconds", "number")}, {}, "Holds still for this long, keeping an air entity's height and any push, then carries on. 0 carries on now."),
            Method("PickNewDirection", {}, {}, "Chooses a direction again at its next update."),
        }));

        RegisterType(Type("PathfinderFacing", "Enum: MoveDirection, Target or None - see Pathfinder.facing.", {
            Prop("MoveDirection", "integer", true),
            Prop("Target", "integer", true),
            Prop("None", "integer", true),
        }));

        RegisterType(Type("PathfinderTurning", "Enum: Steps45 or Snap - see Pathfinder.turning.", {
            Prop("Steps45", "integer", true),
            Prop("Snap", "integer", true),
        }));

        RegisterType(Type("MonsterAIState", "Enum: Idle, Chasing or Dead - see MonsterAI.state.", {
            Prop("Idle", "integer", true),
            Prop("Chasing", "integer", true),
            Prop("Dead", "integer", true),
        }));

        RegisterType(Type("MonsterAI", "A Doom monster's brain: notices targets by sight and sound, decides when to attack and who to fight. It never attacks: OnMeleeReady, OnMissileReady and the events of the same name are where the attack goes.", {
            Prop("isValid", "boolean", true),
            Prop("isActive", "boolean", false, "False stops it thinking."),
            Prop("state", "MonsterAIState", true, "Idle (looking and listening), Chasing or Dead."),
            Prop("health", "number", false, "It dies at 0: it stops thinking and walking and stays as a corpse. Raising it afterward doesn't bring it back."),
            Prop("walkFlipbook", "string", false, "Played on the first Flipbook while its Pathfinder walks, held while it stands still. Relative to Assets, with extension. Empty: left alone."),
            Prop("attackFlipbook", "string", false, "Played from its start when it decides to attack, until attackPause ends. Empty: left alone."),
            Prop("dieFlipbook", "string", false, "Played from its start when it dies. Empty: the flipbook just stops."),
            Prop("target", "Entity?", true, "Who it is chasing. nil while idle."),
            Prop("canSeeTarget", "boolean", true, "True when it could see its target now: range, field of view and line of sight."),
            Prop("sightFov", "number", false, "Field of view in degrees (Doom: 180)."),
            Prop("sightRange", "number", false, "How far it sees. 0 = no limit."),
            Prop("nearRange", "number", false, "Sees all around within this (Doom: 64)."),
            Prop("lookInterval", "number", false, "Seconds between looks while idle (Doom: 10/35)."),
            Prop("chaseStep", "number", false, "Seconds per decision while chasing (Doom: 3/35)."),
            Prop("reactionTime", "number", false, "Seconds chasing before its first missile (Doom: 24/35)."),
            Prop("meleeRange", "number", false, "Melee reach, plus the target's radius (Doom: 44)."),
            Prop("missileMinRange", "number", false, "No missile closer than this. 0 = no limit."),
            Prop("missileMaxRange", "number", false, "No missile further than this. 0 = no limit."),
            Prop("missileDistanceOffset", "number", false, "Taken off the distance before the missile roll (Doom: 64). 128 more without a melee attack."),
            Prop("missileDistanceScale", "number", false, "Then multiplied: lower fires more (Doom: 1, the Cyberdemon's 0.5)."),
            Prop("missileDistanceCap", "number", false, "At most this many of 256 rolls fail (Doom: 200)."),
            Prop("attackPause", "number", false, "Seconds it stands still after deciding to attack."),
            Prop("painChance", "number", false, "Percent chance Hurt causes pain (the Imp's 78.4)."),
            Prop("painPause", "number", false, "Seconds it stands still in pain."),
            Prop("targetLockTime", "number", false, "Seconds a new attacker can't turn it (Doom's threshold: 300/35)."),
            Prop("activeChance", "number", false, "Percent chance per chase step of OnActive (Doom: about 1.17)."),
            Prop("hasMelee", "boolean"),
            Prop("hasMissile", "boolean"),
            Prop("hearing", "boolean", false, "Wakes on a Noise Alert."),
            Prop("ambush", "boolean", false, "A noise only wakes it with the target in sight (Doom's Ambush flag)."),
            Prop("aggressive", "boolean", false, "Attacks while walking, with no gap between attacks (Doom's -fast)."),
            Prop("infights", "boolean", false, "Hurt turns it on its attacker."),
            Prop("drivePathfinder", "boolean", false, "Sends this entity's Pathfinder after the target and holds it while idle."),
            Prop("faceTargetOnAttack", "boolean", false, "Turns to the target when it attacks."),
        }, {
            Method("Wake", {Param("target", "Entity")}, {}, "Starts chasing target, or switches to it if already chasing."),
            Method("Sleep", {}, {}, "Back to idle, target forgotten."),
            Method("Hurt", {Param("attacker", "Entity?"), Param("damage", "number?")}, {}, "Takes damage off its health (none if left out). At 0 it dies and OnDeath runs. Otherwise rolls painChance for OnPain, and turns it on attacker unless locked on another target."),
        }));

        RegisterType(Type("Collider", "Sphere, capsule or box collision volume. It is the body for every physics contact.", {
            Prop("isValid", "boolean", true),
            Prop("offset", "Vector3", false, "Local position of the collider's bottom relative to the Entity's Transform, multiplied by its scale and turned with its rotation. Lets several sit at different spots on one Entity."),
            Prop("type", "ColliderType"),
            Prop("isActive", "boolean"),
            Prop("isTrigger", "boolean"),
            Prop("scale", "Vector3", false, "Local size, multiplied by the Transform's scale. Box: full extents. Sphere: scale.x is the radius (times Transform scale.x). Capsule: scale.x is the radius (times Transform scale.x), scale.y the total height (times Transform scale.y)."),
            Prop("stepSize", "number", false, "Max step-up height in world units, not scaled by the Transform."),
            Prop("maxSlope", "number", false, "Steepest floor in degrees the collider can stand on. Steeper floors aren't ground: the body slides down."),
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
            Method("Raycast", {Param("length", "number?"), Param("requireCollider", "boolean?"), Param("rayType", "RayType?")}, "table",
                   "Shoots a ray from GetEyePosition() through the middle of the screen, ignoring this camera's own entity. "
                   "length defaults to farPlane, requireCollider to false. A rayType other than RayType.General (the default) "
                   "fires the Receive Ray events of what it hits, with this camera's entity as the instigator. "
                   "Returns nil on a miss, else the same table as Game.Raycast."),
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
            Prop("textureIndex", "string", false, "The image's path relative to Assets, with extension (e.g. \"Textures/UI/icon.png\"). \"\" = a solid rectangle of color."),
            Prop("color", "Vector4", false, "RGBA, each 0..1, multiplied with the texture. Alpha below 1 makes the image see-through."),
            Prop("isActive", "boolean", false, "false = not drawn."),
        }));

        RegisterType(Type("UIButton", "Makes a UI element clickable. Each state tints the Entity's first UISprite and can swap its texture.", {
            Prop("isValid", "boolean", true),
            Prop("interactable", "boolean", false, "false = no events or callbacks, drawn with the disabled look. It still blocks clicks to buttons under it."),
            Prop("isHovered", "boolean", true, "The cursor is over it, and it is the topmost button there."),
            Prop("isPressed", "boolean", true, "Clicked, and the left mouse button hasn't been let go yet."),
            Prop("normalColor", "Vector4", false, "RGBA, each 0..1, multiplied with the UISprite's color when nothing is happening."),
            Prop("hoverColor", "Vector4", false, "Multiplied with the UISprite's color while the cursor is over it."),
            Prop("pressedColor", "Vector4", false, "Multiplied with the UISprite's color while it is held down."),
            Prop("disabledColor", "Vector4", false, "Multiplied with the UISprite's color while interactable is false."),
            Prop("normalTexture", "string", false, "Drawn instead of the UISprite's texture in that state. \"\" = the sprite's own texture."),
            Prop("hoverTexture", "string", false),
            Prop("pressedTexture", "string", false),
            Prop("disabledTexture", "string", false),
        }));

        RegisterType(Type("UIText", "A UI element's text label.", {
            Prop("isValid", "boolean", true),
            Prop("text", "string", false, "UTF-8. A new line (\\n) starts a new line of text. Not shown while the text shows an event variable (set in the editor): it is shown again if the variable is removed."),
            Prop("prefix", "string", false, "Shown before the event variable's value, when the text shows one."),
            Prop("suffix", "string", false, "Shown after the event variable's value, when the text shows one."),
            Prop("font", "string", false, "The font file's path relative to Assets, with extension (e.g. \"Fonts/title.ttf\"). \"\" = the engine's default font."),
            Prop("fontSize", "number", false, "Glyph size in pixels at the project's UI Reference Height; scales with the window height. 0 or less hides the text."),
            Prop("color", "Vector4", false, "RGBA, each 0..1. Alpha below 1 makes the text see-through."),
        }));

        RegisterType(Type("Transform", "Position/rotation/scale in world space.", {
            Prop("isValid", "boolean", true),
            Prop("position", "Vector3"),
            Prop("rotation", "Vector4", false, "Quaternion, stored as (x, y, z, w)."),
            Prop("scale", "Vector3"),
            Prop("relativeHeight", "number", false, "Height of the lowest collider's bottom above the current sector floor."),
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

    lua.new_enum( "ColliderType","Sphere", COLLIDERTYPE_SPHERE, "Box", COLLIDERTYPE_BOX, "Capsule", COLLIDERTYPE_CAPSULE);
    lua.new_enum("PathfinderInput",
                 "Position", static_cast<int>(PathfinderInput::POSITION),
                 "Entity", static_cast<int>(PathfinderInput::ENTITY));
    lua.new_enum("PathfinderFacing",
                 "MoveDirection", static_cast<int>(PathfinderFacing::MOVE_DIRECTION),
                 "Target", static_cast<int>(PathfinderFacing::TARGET),
                 "None", static_cast<int>(PathfinderFacing::NONE));
    lua.new_enum("PathfinderTurning",
                 "Steps45", static_cast<int>(PathfinderTurning::STEPS_45),
                 "Snap", static_cast<int>(PathfinderTurning::SNAP));
    lua.new_enum("MonsterAIState",
                 "Idle", static_cast<int>(MonsterAIState::IDLE),
                 "Chasing", static_cast<int>(MonsterAIState::CHASING),
                 "Dead", static_cast<int>(MonsterAIState::DEAD));

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

    RegisterGameplayComponentBindings(lua); // LuaGameplayComponentBindings.cpp

    RegisterUIComponentBindings(lua); // LuaUIComponentBindings.cpp

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