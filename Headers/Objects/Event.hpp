//
// Created by berke on 10/26/2026.
//

#ifndef TILKY_ENGINE_EVENT_HPP
#define TILKY_ENGINE_EVENT_HPP

#include <cstdint>
#include <variant>
#include <string>
#include <vector>
#include <type_traits>
#include <unordered_map>

#include "Types.hpp"
#include "client/TracyThread.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Quaternion/Quaternion.hpp"
#include "RayHit.hpp"

#define transKey static constexpr const char*

// What a moving floor or ceiling does when it would hit an entity's collider
// (Lua: SectorCollision). Only a floor going up or a ceiling going down can
// hit one; checked by Level::UpdateSectorMovement.
enum SectorCollision {
    SECTOR_FORCE_MOVE, // move through entities, as if they weren't there
    SECTOR_STOP,       // stop the move at the last height that fit
    SECTOR_RETRACT,    // go back to the height the move started from
    SECTOR_DONT_MOVE   // hold still while blocked, carry on once clear
};

// What an event or dispatch is attached to
enum class EventOwnerKind : uint8_t {
    LINE = 0,
    SECTOR = 1,
    ENTITY = 2,
    LINEDEF = 3,
    LEVEL = 4, // the level itself (WorldSettings), edited in World Settings
};

// A set of EventOwnerKinds, one bit each: what a payload can be attached to.
using EventOwnerMask = uint8_t;

[[nodiscard]] constexpr EventOwnerMask OwnerBit(const EventOwnerKind kind) noexcept {
    return static_cast<EventOwnerMask>(1u << static_cast<uint8_t>(kind));
}

constexpr EventOwnerMask ALL_EVENT_OWNERS =
    OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::SECTOR) | OwnerBit(EventOwnerKind::ENTITY) |
    OwnerBit(EventOwnerKind::LINEDEF) | OwnerBit(EventOwnerKind::LEVEL);

// Which side of a wall or linedef something is on. A wall's front is its
// front sector's side; a linedef's is its normal side (the editor's tick).
enum class LineSide : uint8_t {
    FRONT = 0,
    BACK = 1,
    EITHER = 2
};

// One or both of a sector floor's two surfaces.
enum class SectorSurfacePick : uint8_t {
    FLOOR = 0,
    CEILING = 1,
    BOTH = 2
};

// What a dispatch does to an on/off setting.
enum class ToggleMode : uint8_t {
    ON = 0,
    OFF = 1,
    TOGGLE = 2
};

[[nodiscard]] constexpr bool ApplyToggle(const ToggleMode mode, const bool current) noexcept {
    switch (mode) {
        case ToggleMode::ON: return true;
        case ToggleMode::OFF: return false;
        case ToggleMode::TOGGLE: return !current;
    }
    return current;
}

// How a sector floor or ceiling move ended (Sector Move Finished).
enum class SectorMoveEndReason : uint8_t {
    REACHED = 0,   // arrived where it was going
    STOPPED = 1,   // stopped early: blocked with SECTOR_STOP, or a Stop Movement
    RETRACTED = 2  // was blocked, went back and arrived where it started
};

// One move that ended this frame, recorded by Level::UpdateSectorMovement and
// the Stop Movement dispatch, read by EventSystem::Update.
struct SectorMoveEnd {
    ID sectorID = INVALID_ID;
    int floorIndex = 0;
    bool ceiling = false;
    SectorMoveEndReason reason = SectorMoveEndReason::REACHED;
};

enum class TagType : uint8_t {
    INCLUDE, // Call event for entities with any of the tags
    EXCLUDE, // Call event for entities with none of the tags
    ALL, // ignore tags and call event for all entity
};
struct TagFilter {
    TagType tagType = TagType::INCLUDE;
    std::vector<TagId> tags;

    bool operator==(const TagFilter&) const = default;
};

struct Condition {
    // An event variable or a constant. A variable's ID says its table:
    // GLOBAL_EVENT_VARIABLE_BIT set = GlobalEventVariables, clear = the
    // level's WorldSettings::eventVariables.
    using InputField = std::variant<EventVariableID, float>;
    enum ConditionType : uint8_t {
        NONE = 0,
        EQUAL = 1,
        LESS_THAN = 2,
        GREATER_THAN = 3,
        LESS_THAN_OR_EQUAL = 4,
        GREATER_THAN_OR_EQUAL = 5,

        COUNT
    };

    // The editor always puts a variable on the left (0 = none picked, reads
    // as 0); the right is a variable or a number.
    InputField left = EventVariableID{0};
    ConditionType type = NONE;
    InputField right = 0.0f;

    bool operator==(const Condition&) const = default;

    // A missing variable (deleted, or from another level) reads as 0.
    // Level::CheckCondition passes the current tables.
    [[nodiscard]] bool Check(const std::unordered_map<EventVariableID, float>& levelVariables,
                             const std::unordered_map<EventVariableID, float>& globalVariables) const {
        auto to_float = [&](const InputField& field) -> float {
            if (const float* constant = std::get_if<float>(&field)) return *constant;

            const EventVariableID id = std::get<EventVariableID>(field);
            const auto& variables = IsGlobalEventVariable(id) ? globalVariables : levelVariables;
            const auto it = variables.find(id);
            return it != variables.end() ? it->second : 0.0f;
        };

        const float lhs = to_float(left);
        const float rhs = to_float(right);

        switch (type) {
            case NONE: return true;
            case EQUAL: return lhs == rhs;
            case LESS_THAN: return lhs < rhs;
            case GREATER_THAN: return lhs > rhs;
            case LESS_THAN_OR_EQUAL: return lhs <= rhs;
            case GREATER_THAN_OR_EQUAL: return lhs >= rhs;
            default: return false;
        }
    }

    transKey transNone = "event.condition.none";
    transKey transEqual = "event.condition.equal";
};

// Each payload type is the event/dispatch kind itself: `owners` is what it
// can be attached to, `translationKey` its display name, `saveName` what level
// files store it as.

//region Event Payloads
struct PayloadLineEntityCollide {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::LINEDEF);
    transKey translationKey = "event.line.entity_collide";
    transKey saveName = "line.entity_collide"; // level file name, never change
    bool operator==(const PayloadLineEntityCollide&) const = default;

    TagFilter filter;

    // The side the entity's centre is on when it touches the line.
    LineSide side = LineSide::EITHER;
};

struct PayloadLineEntityCross {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::LINEDEF);
    transKey translationKey = "event.line.entity_cross";
    transKey saveName = "line.entity_cross"; // level file name, never change
    bool operator==(const PayloadLineEntityCross&) const = default;

    enum EntityCrossDirection {
        FORWARD = 0,
        BACKWARD = 1,
        EITHER = 2
    };

    TagFilter filter;

    EntityCrossDirection direction = EITHER;
};

struct PayloadSectorEntityEnter {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.sector.entity_enter";
    transKey saveName = "sector.entity_enter"; // level file name, never change
    bool operator==(const PayloadSectorEntityEnter&) const = default;

    TagFilter filter;
};

struct PayloadSectorEntityExit {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.sector.entity_exit";
    transKey saveName = "sector.entity_exit"; // level file name, never change
    bool operator==(const PayloadSectorEntityExit&) const = default;

    TagFilter filter;
};

// Fires every `interval` seconds for each entity inside, each on its own
// clock started when it entered.
struct PayloadSectorEntityStay {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.sector.entity_stay";
    transKey saveName = "sector.entity_stay"; // level file name, never change
    bool operator==(const PayloadSectorEntityStay&) const = default;

    TagFilter filter;
    float interval = 1.0f; // seconds
};

// Any end of a floor or ceiling move of the sector. No instigator.
struct PayloadSectorMoveFinished {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.sector.move_finished";
    transKey saveName = "sector.move_finished"; // level file name, never change
    bool operator==(const PayloadSectorMoveFinished&) const = default;

    enum Reason : uint8_t {
        ANY = 0,
        REACHED = 1,
        STOPPED = 2,
        RETRACTED = 3
    };

    SectorSurfacePick surface = SectorSurfacePick::BOTH; // BOTH = either surface
    int floorIndex = -1; // -1 = any floor
    Reason reason = ANY;
};

struct PayloadEntityCollide {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.entity.collide";
    transKey saveName = "entity.collide"; // level file name, never change
    bool operator==(const PayloadEntityCollide&) const = default;

    TagFilter filter;
};

struct PayloadEntityChangeSector {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.entity.change_sector";
    transKey saveName = "entity.change_sector"; // level file name, never change
    bool operator==(const PayloadEntityChangeSector&) const = default;

    TagFilter filter;
};

struct PayloadEntityEnable {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.entity.enable";
    transKey saveName = "entity.enable"; // level file name, never change
    bool operator==(const PayloadEntityEnable&) const = default;
};

struct PayloadEntityDisable { // Nothing needed
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.entity.disable";
    transKey saveName = "entity.disable"; // level file name, never change
    bool operator==(const PayloadEntityDisable&) const = default;
};

// Fires on the entity being destroyed, before it is removed, whatever
// destroyed it. Not when the level unloads or play stops.
struct PayloadEntityDestroyed {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.entity.destroyed";
    transKey saveName = "entity.destroyed"; // level file name, never change
    bool operator==(const PayloadEntityDestroyed&) const = default;
};

// A typed ray (RayType, not GENERAL) hit the owner: a wall, linedef, sector
// floor/ceiling or entity. The instigator is the entity that cast it.
struct PayloadReceiveRay {
    static constexpr EventOwnerMask owners =
        OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::LINEDEF) |
        OwnerBit(EventOwnerKind::SECTOR) | OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.receive_ray";
    transKey saveName = "receive_ray"; // level file name, never change
    bool operator==(const PayloadReceiveRay&) const = default;

    TagFilter filter;
    RayType rayType = RayType::INTERACT;

    // Only the first of a run of hits fires: re-armed once a frame passes
    // without that caster's ray of this type hitting the owner.
    bool firstHitOnly = true;

    // Lines only: the side the ray came from.
    LineSide side = LineSide::EITHER;
};

// Fires when the event's own Condition goes from false to true. No instigator.
struct PayloadConditionMet {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.condition_met";
    transKey saveName = "condition_met"; // level file name, never change
    bool operator==(const PayloadConditionMet&) const = default;
};

// Every `interval` seconds, the first time after `firstDelay`, counted from
// play start. Paused while its entity or linedef is disabled. No instigator.
struct PayloadTimer {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.timer";
    transKey saveName = "timer"; // level file name, never change
    bool operator==(const PayloadTimer&) const = default;

    float interval = 1.0f;
    float firstDelay = 0.0f;
};

// Game.Signal(name, instigator?) from Lua fires every one with that name.
struct PayloadLuaSignal {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.lua_signal";
    transKey saveName = "lua_signal"; // level file name, never change
    bool operator==(const PayloadLuaSignal&) const = default;

    std::string name;

    // Checked against the instigator Lua passed (none = no tags).
    TagFilter filter{TagType::ALL, {}};
};

// Every time the level loads, after the scripts' start callbacks.
struct PayloadLevelStart {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LEVEL);
    transKey translationKey = "event.level.start";
    transKey saveName = "level.start"; // level file name, never change
    bool operator==(const PayloadLevelStart&) const = default;
};
//endregion

//region Dispatch Payloads
// Probably wont be used much but still, good to have
struct PayloadDispatchLineSetColor {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE);
    transKey translationKey = "event.dispatch.line.set_color";
    transKey saveName = "dispatch.line.set_color"; // level file name, never change
    bool operator==(const PayloadDispatchLineSetColor&) const = default;

    Vector3 color;
    float speed = 10.0f; // color units (0-1) per second, per channel. 0 or less = instant
};

// One of a wall's three texture pieces.
enum class WallPiece : uint8_t {
    TOP = 0,
    MIDDLE = 1,
    BOTTOM = 2
};

struct PayloadDispatchLineSetTexture {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE);
    transKey translationKey = "event.dispatch.line.set_texture";
    transKey saveName = "dispatch.line.set_texture"; // level file name, never change
    bool operator==(const PayloadDispatchLineSetTexture&) const = default;

    WallPiece piece = WallPiece::MIDDLE;
    std::string texture; // empty removes it (an empty middle is an opening)
};

// The wall's middle collision (Wall::middleHasCollision).
struct PayloadDispatchLineSetCollision {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE);
    transKey translationKey = "event.dispatch.line.set_collision";
    transKey saveName = "dispatch.line.set_collision"; // level file name, never change
    bool operator==(const PayloadDispatchLineSetCollision&) const = default;

    ToggleMode mode = ToggleMode::TOGGLE;
};

// A disabled linedef fires no events and typed rays pass it by.
struct PayloadDispatchLinedefSetEnabled {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINEDEF);
    transKey translationKey = "event.dispatch.linedef.set_enabled";
    transKey saveName = "dispatch.linedef.set_enabled"; // level file name, never change
    bool operator==(const PayloadDispatchLinedefSetEnabled&) const = default;

    ToggleMode mode = ToggleMode::TOGGLE;
};

struct PayloadDispatchSectorMoveFloorToCeiling {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.move_floor_to_ceiling";
    transKey saveName = "dispatch.sector.move_floor_to_ceiling"; // level file name, never change
    bool operator==(const PayloadDispatchSectorMoveFloorToCeiling&) const = default;

    float gap = .0f, speed = 10.0f;
    int floorIndex = 0; // which of the sector's floors (slabs) moves

    SectorCollision collisionType = SECTOR_RETRACT;
};

struct PayloadDispatchSectorMoveCeilingToFloor {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.move_ceiling_to_floor";
    transKey saveName = "dispatch.sector.move_ceiling_to_floor"; // level file name, never change
    bool operator==(const PayloadDispatchSectorMoveCeilingToFloor&) const = default;

    float gap = .0f, speed = 10.0f;
    int floorIndex = 0; // which of the sector's floors (slabs) moves

    SectorCollision collisionType = SECTOR_RETRACT;
};

// Where a Move to Height or a Door heads. Neighbours are the sectors across
// the sector's two-sided walls; their floor is their lowest floor, their
// ceiling their highest ceiling.
enum class SectorHeightMode : uint8_t {
    ABSOLUTE_HEIGHT = 0,               // `height` is a world height
    RELATIVE_OFFSET = 1,               // `height` is added to where the surface is now
    LOWEST_NEIGHBOR_FLOOR = 2,  // the rest are plus `height`
    HIGHEST_NEIGHBOR_FLOOR = 3,
    LOWEST_NEIGHBOR_CEILING = 4,
    HIGHEST_NEIGHBOR_CEILING = 5
};

struct PayloadDispatchSectorMoveToHeight {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.move_to_height";
    transKey saveName = "dispatch.sector.move_to_height"; // level file name, never change
    bool operator==(const PayloadDispatchSectorMoveToHeight&) const = default;

    bool ceiling = false; // which surface moves
    SectorHeightMode mode = SectorHeightMode::ABSOLUTE_HEIGHT;
    float height = 0.0f; // the height, or the offset (see SectorHeightMode)
    float speed = 10.0f;
    int floorIndex = 0;

    SectorCollision collisionType = SECTOR_RETRACT;
};

// The ceiling opens, waits, then closes back to where it was.
struct PayloadDispatchSectorDoor {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.door";
    transKey saveName = "dispatch.sector.door"; // level file name, never change
    bool operator==(const PayloadDispatchSectorDoor&) const = default;

    enum OpenMode : uint8_t {
        LOWEST_NEIGHBOR_CEILING = 0, // minus `lip`
        FIXED_HEIGHT = 1             // `openHeight`
    };

    // Triggered again while the door is open or moving.
    enum Retrigger : uint8_t {
        RESTART = 0, // closing reopens, open restarts the wait
        TOGGLE = 1,  // opening or open closes now, closing reopens
        IGNORE_WHILE_OPEN = 2 // nothing until it has closed
    };

    OpenMode openMode = LOWEST_NEIGHBOR_CEILING;
    float lip = 4.0f;
    float openHeight = 0.0f;

    float openSpeed = 40.0f;
    float closeSpeed = 40.0f;
    float wait = 3.0f; // seconds open

    int floorIndex = 0;
    SectorCollision closeCollision = SECTOR_RETRACT; // while closing; opening never checks
    Retrigger retrigger = RESTART;
};

struct PayloadDispatchSectorStopMovement {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.stop_movement";
    transKey saveName = "dispatch.sector.stop_movement"; // level file name, never change
    bool operator==(const PayloadDispatchSectorStopMovement&) const = default;

    SectorSurfacePick surface = SectorSurfacePick::BOTH;
    int floorIndex = 0;
};

struct PayloadDispatchSectorSetLight {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.set_light";
    transKey saveName = "dispatch.sector.set_light"; // level file name, never change
    bool operator==(const PayloadDispatchSectorSetLight&) const = default;

    Vector3 light = {255.0f, 255.0f, 255.0f}; // 0-255, like the inspector
    float speed = 100.0f; // light units (0-255) per second. 0 or less = instant
};

struct PayloadDispatchSectorSetTexture {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.set_texture";
    transKey saveName = "dispatch.sector.set_texture"; // level file name, never change
    bool operator==(const PayloadDispatchSectorSetTexture&) const = default;

    SectorSurfacePick surface = SectorSurfacePick::FLOOR;
    int floorIndex = 0;
    std::string texture;
};

// Adds or removes one of the owner's own tags (not a link tag).
struct PayloadDispatchChangeTag {
    static constexpr EventOwnerMask owners =
        OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::SECTOR) | OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.change_tag";
    transKey saveName = "dispatch.change_tag"; // level file name, never change
    bool operator==(const PayloadDispatchChangeTag&) const = default;

    bool remove = false;
    TagId tag = 0;
};

struct PayloadDispatchEntityDestroy { // Nothing needed
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.destroy";
    transKey saveName = "dispatch.entity.destroy"; // level file name, never change
    bool operator==(const PayloadDispatchEntityDestroy&) const = default;
};

struct PayloadDispatchEntityTeleport {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.teleport";
    transKey saveName = "dispatch.entity.teleport"; // level file name, never change
    bool operator==(const PayloadDispatchEntityTeleport&) const = default;

    Vector3 position;
    Quaternion rotation;
};

struct PayloadDispatchEntityDisable {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.disable";
    transKey saveName = "dispatch.entity.disable"; // level file name, never change
    bool operator==(const PayloadDispatchEntityDisable&) const = default;
};

struct PayloadDispatchEntityEnable {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.enable";
    transKey saveName = "dispatch.entity.enable"; // level file name, never change
    bool operator==(const PayloadDispatchEntityEnable&) const = default;
};

// Supporting parameters would be too complicated (?)
struct PayloadDispatchEntityCallFunction {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.call_function";
    transKey saveName = "dispatch.entity.call_function"; // level file name, never change
    bool operator==(const PayloadDispatchEntityCallFunction&) const = default;

    std::string functionName;
};

// The owner's Rigidbody velocity (a player controller moves its Rigidbody too).
struct PayloadDispatchEntityChangeVelocity {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.change_velocity";
    transKey saveName = "dispatch.entity.change_velocity"; // level file name, never change
    bool operator==(const PayloadDispatchEntityChangeVelocity&) const = default;

    bool set = false; // false = add to the velocity
    Vector3 velocity;
};

// Glides the owner to a position. A new one replaces one still running.
struct PayloadDispatchEntityMoveTo {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.move_to";
    transKey saveName = "dispatch.entity.move_to"; // level file name, never change
    bool operator==(const PayloadDispatchEntityMoveTo&) const = default;

    Vector3 position;
    bool relative = false; // `position` is an offset from where it is when the dispatch runs

    bool useDuration = false; // false = `speed` units per second, true = takes `duration` seconds
    float speed = 10.0f;
    float duration = 1.0f;

    bool rotate = false;
    Vector3 rotation; // Euler degrees, turned to over the same time
};

// The components an Enable/Disable Component dispatch can switch.
enum class ToggleableComponent : uint8_t {
    SPRITE = 0,
    SCRIPT = 1,
    PLAYER_CONTROLLER = 2,
    CAMERA = 3,
    COLLIDER = 4,
    UI_SPRITE = 5
};

// Every component of the type on the owner.
struct PayloadDispatchEntitySetComponent {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.set_component";
    transKey saveName = "dispatch.entity.set_component"; // level file name, never change
    bool operator==(const PayloadDispatchEntitySetComponent&) const = default;

    ToggleableComponent component = ToggleableComponent::COLLIDER;
    ToggleMode mode = ToggleMode::TOGGLE;
};

// Spawns at the owner: an entity's transform, a wall or linedef's middle on
// the floor, a sector's bounds centre on the floor, the level's origin.
struct PayloadDispatchSpawnPrefab {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.spawn_prefab";
    transKey saveName = "dispatch.spawn_prefab"; // level file name, never change
    bool operator==(const PayloadDispatchSpawnPrefab&) const = default;

    std::string prefab;
    Vector3 offset;
    Vector3 rotation; // Euler degrees
    bool worldPosition = false; // `offset` is a world position instead
};

// Played from the owner's position (see Spawn Prefab); the level's own plays
// unpositioned.
struct PayloadDispatchPlaySound {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.play_sound";
    transKey saveName = "dispatch.play_sound"; // level file name, never change
    bool operator==(const PayloadDispatchPlaySound&) const = default;

    std::string sound;
    float gain = 1.0f;
    float pitch = 1.0f;
};

// A line of text at the top of the screen. A new one replaces the last.
struct PayloadDispatchShowMessage {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.show_message";
    transKey saveName = "dispatch.show_message"; // level file name, never change
    bool operator==(const PayloadDispatchShowMessage&) const = default;

    std::string text;
    float duration = 3.0f;
    Vector3 color = {1.0f, 1.0f, 1.0f}; // 0-1
};

// Runs every dispatch linked to `target`, with this one's instigator.
struct PayloadDispatchRelay {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.relay";
    transKey saveName = "dispatch.relay"; // level file name, never change
    bool operator==(const PayloadDispatchRelay&) const = default;

    TagId target = 0;
};

// Sets the trigger count of every event linked to `target` back to 0.
struct PayloadDispatchResetEvent {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.reset_event";
    transKey saveName = "dispatch.reset_event"; // level file name, never change
    bool operator==(const PayloadDispatchResetEvent&) const = default;

    TagId target = 0;
};

// Switches level at the end of the frame, like Game.LoadLevel.
struct PayloadDispatchLoadLevel {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.load_level";
    transKey saveName = "dispatch.load_level"; // level file name, never change
    bool operator==(const PayloadDispatchLoadLevel&) const = default;

    std::string levelName;
};

// Changes an event variable (level or global, by the ID's scope bit). Acts on
// the variable, not the owner: the owner only decides which events reach it.
struct PayloadDispatchChangeVariable {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.change_variable";
    transKey saveName = "dispatch.change_variable"; // level file name, never change
    bool operator==(const PayloadDispatchChangeVariable&) const = default;

    enum Operation : uint8_t {
        SET = 0,
        ADD = 1,
        SUBTRACT = 2
    };

    EventVariableID variable = 0; // 0 = none picked, does nothing
    Operation operation = SET;
    float value = 0.0f;
};
//endregion

using EventPayload = std::variant<
    PayloadLineEntityCollide, PayloadLineEntityCross,
    PayloadSectorEntityEnter, PayloadSectorEntityExit, PayloadSectorEntityStay, PayloadSectorMoveFinished,
    PayloadEntityChangeSector, PayloadEntityCollide, PayloadEntityDisable, PayloadEntityEnable, PayloadEntityDestroyed,
    PayloadReceiveRay, PayloadConditionMet, PayloadTimer, PayloadLuaSignal, PayloadLevelStart>;

using DispatchPayload = std::variant<
    PayloadDispatchLineSetColor, PayloadDispatchLineSetTexture, PayloadDispatchLineSetCollision,
    PayloadDispatchLinedefSetEnabled,
    PayloadDispatchSectorMoveCeilingToFloor, PayloadDispatchSectorMoveFloorToCeiling, PayloadDispatchSectorMoveToHeight,
    PayloadDispatchSectorDoor, PayloadDispatchSectorStopMovement, PayloadDispatchSectorSetLight, PayloadDispatchSectorSetTexture,
    PayloadDispatchEntityCallFunction, PayloadDispatchEntityDestroy, PayloadDispatchEntityDisable, PayloadDispatchEntityEnable,
    PayloadDispatchEntityTeleport, PayloadDispatchEntityChangeVelocity, PayloadDispatchEntityMoveTo, PayloadDispatchEntitySetComponent,
    PayloadDispatchChangeTag, PayloadDispatchSpawnPrefab, PayloadDispatchPlaySound, PayloadDispatchShowMessage,
    PayloadDispatchRelay, PayloadDispatchResetEvent, PayloadDispatchLoadLevel, PayloadDispatchChangeVariable>;

template <typename Payload>
[[nodiscard]] constexpr EventOwnerMask GetOwners(const Payload& payload) noexcept {
    return std::visit([](const auto& p) { return std::decay_t<decltype(p)>::owners; }, payload);
}

template <typename Payload>
[[nodiscard]] constexpr bool CanAttachTo(const Payload& payload, const EventOwnerKind kind) noexcept {
    return (GetOwners(payload) & OwnerBit(kind)) != 0;
}

template <typename Payload>
[[nodiscard]] constexpr const char* GetTranslationKey(const Payload& payload) noexcept {
    return std::visit([](const auto& p) { return std::decay_t<decltype(p)>::translationKey; }, payload);
}

struct Event {
    EventPayload payload;

    TagId tagId = 0;

    Condition condition;

    uint32_t maxTriggers = 0; // 0 = unlimited, 1 = once
    float chance = 100.0f;    // percentage, out of 100. A failed roll doesn't count as a trigger
    uint32_t triggerCount = 0; // runtime only: not saved, reset when play starts

    // Authored data only - triggerCount is left out.
    [[nodiscard]] bool operator==(const Event& other) const {
        return payload == other.payload && tagId == other.tagId && condition == other.condition &&
               maxTriggers == other.maxTriggers && chance == other.chance;
    }

    [[nodiscard]] constexpr bool IsLine() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINE); }
    [[nodiscard]] constexpr bool IsSector() const noexcept { return CanAttachTo(payload, EventOwnerKind::SECTOR); }
    [[nodiscard]] constexpr bool IsEntity() const noexcept { return CanAttachTo(payload, EventOwnerKind::ENTITY); }
    [[nodiscard]] constexpr bool IsLinedef() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINEDEF); }
    [[nodiscard]] constexpr bool IsLevel() const noexcept { return CanAttachTo(payload, EventOwnerKind::LEVEL); }
};

struct Dispatch {
    DispatchPayload payload;

    TagId tagId = 0;

    // Seconds between the event firing and this running. Each trigger
    // queues its own run, so quick triggers give one run each.
    float delay = 0.0f;

    // Seconds after a call during which calls do nothing (0 = none). The
    // cooldown starts once every `cooldownTriggers` calls (at least 1).
    float cooldown = 0.0f;
    int cooldownTriggers = 1;

    bool operator==(const Dispatch&) const = default;

    [[nodiscard]] constexpr bool IsLine() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINE); }
    [[nodiscard]] constexpr bool IsSector() const noexcept { return CanAttachTo(payload, EventOwnerKind::SECTOR); }
    [[nodiscard]] constexpr bool IsEntity() const noexcept { return CanAttachTo(payload, EventOwnerKind::ENTITY); }
    [[nodiscard]] constexpr bool IsLinedef() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINEDEF); }
    [[nodiscard]] constexpr bool IsLevel() const noexcept { return CanAttachTo(payload, EventOwnerKind::LEVEL); }
};

#endif //TILKY_ENGINE_EVENT_HPP
