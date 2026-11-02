//
// Created by berke on 10/31/2026.
//

#ifndef TILKY_ENGINE_EVENTFIELDS_HPP
#define TILKY_ENGINE_EVENTFIELDS_HPP

#include <optional>
#include <string_view>

#include "Event.hpp"
#include "ScriptPublicType.hpp"

// The engine fields a Set Field dispatch can change, hand-picked: things a
// level wants to change while playing, never runtime-only state. Script
// fields come from the script's public fields instead. `key` is what level
// files store, so never change one; EventSystem reads and writes them.
struct EventFieldInfo {
    FieldComponent component;
    const char* key;
    const char* translationKey;
    FieldValueKind kind;
};

inline constexpr EventFieldInfo EVENT_FIELDS[] = {
    {FieldComponent::TRANSFORM, "position", "events.field.position", FieldValueKind::VECTOR3},
    {FieldComponent::TRANSFORM, "rotation", "events.field.rotation", FieldValueKind::VECTOR3},
    {FieldComponent::TRANSFORM, "scale", "events.field.scale", FieldValueKind::VECTOR3},

    {FieldComponent::SPRITE, "color", "events.field.color", FieldValueKind::COLOR},
    {FieldComponent::SPRITE, "isActive", "events.field.is_active", FieldValueKind::BOOL},
    {FieldComponent::SPRITE, "offset", "events.field.offset", FieldValueKind::VECTOR3},

    {FieldComponent::AUDIO_SOURCE, "sound", "events.field.sound", FieldValueKind::SOUND},
    {FieldComponent::AUDIO_SOURCE, "gain", "events.field.gain", FieldValueKind::FLOAT},
    {FieldComponent::AUDIO_SOURCE, "pitch", "events.field.pitch", FieldValueKind::FLOAT},
    {FieldComponent::AUDIO_SOURCE, "looping", "events.field.looping", FieldValueKind::BOOL},
    {FieldComponent::AUDIO_SOURCE, "referenceDistance", "events.field.reference_distance", FieldValueKind::FLOAT},
    {FieldComponent::AUDIO_SOURCE, "maxDistance", "events.field.max_distance", FieldValueKind::FLOAT},
    {FieldComponent::AUDIO_SOURCE, "rollOffFactor", "events.field.rolloff", FieldValueKind::FLOAT},
    {FieldComponent::AUDIO_SOURCE, "offset", "events.field.offset", FieldValueKind::VECTOR3},

    {FieldComponent::PLAYER_CONTROLLER, "speed", "events.field.speed", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "runningSpeed", "events.field.running_speed", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "jumpPower", "events.field.jump_power", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "eyeHeight", "events.field.eye_height", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "acceleration", "events.field.acceleration", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "deceleration", "events.field.deceleration", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "airControl", "events.field.air_control", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "sensitivityX", "events.field.sensitivity_x", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "sensitivityY", "events.field.sensitivity_y", FieldValueKind::FLOAT},
    {FieldComponent::PLAYER_CONTROLLER, "useReach", "events.field.use_reach", FieldValueKind::FLOAT},

    {FieldComponent::CAMERA, "fov", "events.field.fov", FieldValueKind::FLOAT},
    {FieldComponent::CAMERA, "smoothStep", "events.field.smooth_step", FieldValueKind::BOOL},

    {FieldComponent::COLLIDER, "isActive", "events.field.is_active", FieldValueKind::BOOL},
    {FieldComponent::COLLIDER, "isTrigger", "events.field.is_trigger", FieldValueKind::BOOL},
    {FieldComponent::COLLIDER, "scale", "events.field.size", FieldValueKind::VECTOR3},
    {FieldComponent::COLLIDER, "offset", "events.field.offset", FieldValueKind::VECTOR3},
    {FieldComponent::COLLIDER, "stepSize", "events.field.step_size", FieldValueKind::FLOAT},
    {FieldComponent::COLLIDER, "maxSlope", "events.field.max_slope", FieldValueKind::FLOAT},

    {FieldComponent::RIGIDBODY, "isStatic", "events.field.is_static", FieldValueKind::BOOL},
    {FieldComponent::RIGIDBODY, "mass", "events.field.mass", FieldValueKind::FLOAT},
    {FieldComponent::RIGIDBODY, "gravityScale", "events.field.gravity_scale", FieldValueKind::FLOAT},
    {FieldComponent::RIGIDBODY, "friction", "events.field.friction", FieldValueKind::FLOAT},

    {FieldComponent::MODEL, "offset", "events.field.offset", FieldValueKind::VECTOR3},

    {FieldComponent::FLIPBOOK, "speed", "events.field.playback_speed", FieldValueKind::FLOAT},

    {FieldComponent::UI_TRANSFORM, "position", "events.field.position", FieldValueKind::VECTOR2},
    {FieldComponent::UI_TRANSFORM, "scale", "events.field.scale", FieldValueKind::VECTOR2},
    {FieldComponent::UI_TRANSFORM, "rotation", "events.field.rotation", FieldValueKind::FLOAT},

    {FieldComponent::UI_SPRITE, "texture", "events.field.texture", FieldValueKind::TEXTURE},
    {FieldComponent::UI_SPRITE, "color", "events.field.color", FieldValueKind::COLOR},
    {FieldComponent::UI_SPRITE, "isActive", "events.field.is_active", FieldValueKind::BOOL},

    {FieldComponent::UI_TEXT, "text", "events.field.text", FieldValueKind::STRING},
    {FieldComponent::UI_TEXT, "prefix", "events.field.prefix", FieldValueKind::STRING},
    {FieldComponent::UI_TEXT, "suffix", "events.field.suffix", FieldValueKind::STRING},
    {FieldComponent::UI_TEXT, "fontSize", "events.field.font_size", FieldValueKind::FLOAT},
    {FieldComponent::UI_TEXT, "color", "events.field.color", FieldValueKind::COLOR},

    {FieldComponent::UI_BUTTON, "interactable", "events.field.interactable", FieldValueKind::BOOL},
    {FieldComponent::UI_BUTTON, "normalColor", "events.field.normal_color", FieldValueKind::COLOR},
    {FieldComponent::UI_BUTTON, "hoverColor", "events.field.hover_color", FieldValueKind::COLOR},
    {FieldComponent::UI_BUTTON, "pressedColor", "events.field.pressed_color", FieldValueKind::COLOR},
    {FieldComponent::UI_BUTTON, "disabledColor", "events.field.disabled_color", FieldValueKind::COLOR},

    {FieldComponent::PATHFINDER, "isActive", "events.field.is_active", FieldValueKind::BOOL},
    {FieldComponent::PATHFINDER, "speed", "events.field.speed", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "range", "events.field.range", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "offset", "events.field.target_offset", FieldValueKind::VECTOR3},
    {FieldComponent::PATHFINDER, "isAirEntity", "events.field.air_entity", FieldValueKind::BOOL},
    {FieldComponent::PATHFINDER, "isRangedEntity", "events.field.ranged_entity", FieldValueKind::BOOL},
    {FieldComponent::PATHFINDER, "canDropOff", "component.pathfinder.can_drop_off", FieldValueKind::BOOL},
    {FieldComponent::PATHFINDER, "opensDoors", "component.pathfinder.opens_doors", FieldValueKind::BOOL},
    {FieldComponent::PATHFINDER, "keepMovingInRange", "component.pathfinder.keep_moving", FieldValueKind::BOOL},
    {FieldComponent::PATHFINDER, "chaseStep", "component.pathfinder.chase_step", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "holdMinSteps", "component.pathfinder.hold_min", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "holdMaxSteps", "component.pathfinder.hold_max", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "deadzone", "component.pathfinder.deadzone", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "swapChance", "component.pathfinder.swap_chance", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "floatSpeed", "component.pathfinder.float_speed", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "floatRatio", "component.pathfinder.float_ratio", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "pushFriction", "component.pathfinder.push_friction", FieldValueKind::FLOAT},
    {FieldComponent::PATHFINDER, "stuckTime", "component.pathfinder.stuck_time", FieldValueKind::FLOAT},

    {FieldComponent::MONSTER_AI, "isActive", "events.field.is_active", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "health", "component.monster_ai.health", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "sightFov", "component.monster_ai.sight_fov", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "sightRange", "component.monster_ai.sight_range", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "nearRange", "component.monster_ai.near_range", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "lookInterval", "component.monster_ai.look_interval", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "chaseStep", "component.monster_ai.chase_step", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "reactionTime", "component.monster_ai.reaction_time", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "meleeRange", "component.monster_ai.melee_range", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "missileMinRange", "component.monster_ai.missile_min_range", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "missileMaxRange", "component.monster_ai.missile_max_range", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "missileDistanceOffset", "component.monster_ai.missile_distance_offset", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "missileDistanceScale", "component.monster_ai.missile_distance_scale", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "missileDistanceCap", "component.monster_ai.missile_distance_cap", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "attackPause", "component.monster_ai.attack_pause", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "painChance", "component.monster_ai.pain_chance", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "painPause", "component.monster_ai.pain_pause", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "targetLockTime", "component.monster_ai.target_lock_time", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "activeChance", "component.monster_ai.active_chance", FieldValueKind::FLOAT},
    {FieldComponent::MONSTER_AI, "hasMelee", "component.monster_ai.has_melee", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "hasMissile", "component.monster_ai.has_missile", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "hearing", "component.monster_ai.hearing", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "ambush", "component.monster_ai.ambush", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "aggressive", "component.monster_ai.aggressive", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "infights", "component.monster_ai.infights", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "drivePathfinder", "component.monster_ai.drive_pathfinder", FieldValueKind::BOOL},
    {FieldComponent::MONSTER_AI, "faceTargetOnAttack", "component.monster_ai.face_target", FieldValueKind::BOOL},
};

// The engine field `key` of `component`, nullptr if there isn't one.
[[nodiscard]] constexpr const EventFieldInfo* FindEventField(const FieldComponent component, const std::string_view key) {
    for (const EventFieldInfo& info : EVENT_FIELDS)
        if (info.component == component && key == info.key) return &info;
    return nullptr;
}

// The kind a Set Field dispatch treats a script's public field as, nullopt
// for the types it can't change (references, flipbooks).
[[nodiscard]] inline std::optional<FieldValueKind> ScriptFieldKindOf(const ScriptPublicField& field) {
    switch (field.type) {
        case ScriptValueType::Int: return FieldValueKind::INT;
        case ScriptValueType::Float: return FieldValueKind::FLOAT;
        case ScriptValueType::Bool: return FieldValueKind::BOOL;
        case ScriptValueType::String: return FieldValueKind::STRING;
        case ScriptValueType::Vector2: return FieldValueKind::VECTOR2;
        case ScriptValueType::Vector3: return FieldValueKind::VECTOR3;
        case ScriptValueType::Vector4: return FieldValueKind::VECTOR4;
        case ScriptValueType::Enum: return FieldValueKind::ENUM;
        case ScriptValueType::Asset:
            if (field.assetKind == ScriptAssetKind::Texture) return FieldValueKind::TEXTURE;
            return std::nullopt;
        default: return std::nullopt;
    }
}

#endif //TILKY_ENGINE_EVENTFIELDS_HPP
