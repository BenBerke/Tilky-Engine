//
// Created by berke on 10/26/2026.
//

#ifndef TILKY_ENGINE_EVENT_HPP
#define TILKY_ENGINE_EVENT_HPP

#include <array>
#include <cstdint>
#include <variant>
#include <string>
#include <vector>
#include <type_traits>
#include <unordered_map>

#include <SDL3/SDL_scancode.h>

#include "Types.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector4.hpp"
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

// Everything but the level itself (World Settings).
constexpr EventOwnerMask OBJECT_EVENT_OWNERS = ALL_EVENT_OWNERS & ~OwnerBit(EventOwnerKind::LEVEL);

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
// Who a direct call (an event's, or a Relay's) runs the named dispatches of.
enum class CallTarget : uint8_t {
    SELF = 0,       // the object the event / relay is on
    INSTIGATOR = 1, // the entity that caused the event; nothing without one
    ENTITY = 2,     // a picked entity (targetID)
    SECTOR = 3,     // a picked sector
    WALL = 4,       // a picked wall
    LINEDEF = 5,    // a picked linedef
    TAGGED = 6      // every entity, sector and wall with the normal tag targetTag
};

// Runs every dispatch named `dispatch` on the target (several may share a
// name). Looked up by name, so renaming a dispatch leaves calls to the old
// name unanswered until they are re-picked.
struct DirectCall {
    CallTarget target = CallTarget::SELF;
    ID targetID = INVALID_ID; // ENTITY, SECTOR, WALL, LINEDEF
    TagId targetTag = 0;      // TAGGED
    std::string dispatch;

    // Runtime only, not saved: `dispatch` as a number, filled when the event
    // registry is built so a call never compares strings. 0 = no dispatch
    // in the level has that name.
    mutable uint32_t nameID = 0;

    [[nodiscard]] bool operator==(const DirectCall& other) const {
        return target == other.target && targetID == other.targetID && targetTag == other.targetTag &&
               dispatch == other.dispatch;
    }
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

// An event variable's value as text: no decimals when it is whole (42),
// otherwise as short as it goes (2.5).
[[nodiscard]] inline std::string FormatEventNumber(const float number) {
    const float rounded = static_cast<float>(static_cast<long long>(number < 0.0f ? number - 0.5f : number + 0.5f));
    if (number - rounded < 0.0001f && rounded - number < 0.0001f) return std::to_string(static_cast<long long>(rounded));

    std::string text = std::to_string(number);
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

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

// Fires every frame for each entity whose Rigidbody is grounded while it is
// in the sector. The filter checks the entity's tags.
struct PayloadSectorEntityGrounded {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.sector.entity_grounded";
    transKey saveName = "sector.entity_grounded"; // level file name, never change
    bool operator==(const PayloadSectorEntityGrounded&) const = default;

    TagFilter filter;
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

// Fires every frame the entity's Rigidbody is grounded, while it is in a
// sector. The filter checks that sector's tags.
struct PayloadEntityGroundedInSector {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.entity.grounded_in_sector";
    transKey saveName = "entity.grounded_in_sector"; // level file name, never change
    bool operator==(const PayloadEntityGroundedInSector&) const = default;

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

// UI Button events: on a UI entity with a UI Button only (`needsUIButton`),
// while the cursor is unlocked and visible. The instigator is the player
// (the first enabled entity with the Player tag), so the filter checks its
// tags. They work while the game is paused, and their dispatches' delays and
// cooldowns run in real time. See UIButtonSystem.

// The left mouse button went down on the button.
struct PayloadUIButtonClick {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr bool needsUIButton = true;
    transKey translationKey = "event.ui_button.click";
    transKey saveName = "ui_button.click"; // level file name, never change
    bool operator==(const PayloadUIButtonClick&) const = default;

    TagFilter filter{TagType::ALL, {}};
};

// The left mouse button was let go after a click on the button, wherever the
// cursor is by then.
struct PayloadUIButtonRelease {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr bool needsUIButton = true;
    transKey translationKey = "event.ui_button.release";
    transKey saveName = "ui_button.release"; // level file name, never change
    bool operator==(const PayloadUIButtonRelease&) const = default;

    TagFilter filter{TagType::ALL, {}};
};

// The cursor came onto the button (the topmost one under it).
struct PayloadUIButtonHoverEnter {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr bool needsUIButton = true;
    transKey translationKey = "event.ui_button.hover_enter";
    transKey saveName = "ui_button.hover_enter"; // level file name, never change
    bool operator==(const PayloadUIButtonHoverEnter&) const = default;

    TagFilter filter{TagType::ALL, {}};
};

// The cursor left the button, or another button came on top of it there.
struct PayloadUIButtonHoverExit {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr bool needsUIButton = true;
    transKey translationKey = "event.ui_button.hover_exit";
    transKey saveName = "ui_button.hover_exit"; // level file name, never change
    bool operator==(const PayloadUIButtonHoverExit&) const = default;

    TagFilter filter{TagType::ALL, {}};
};

// "On Start", like a script's Start(): on everything in the level when it
// loads (after the scripts' Start callbacks; the level's own first, then
// walls, linedefs, sectors and entities), and on an entity spawned later
// (Spawn Prefab, Game.CreatePrefab), after its scripts' Start. Not on a
// disabled entity or linedef, then or later.
struct PayloadOnStart {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LEVEL) | OwnerBit(EventOwnerKind::LINE) |
                                             OwnerBit(EventOwnerKind::LINEDEF) | OwnerBit(EventOwnerKind::SECTOR) |
                                             OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.level.start";
    transKey saveName = "level.start"; // level file name, never change
    bool operator==(const PayloadOnStart&) const = default;
};
// What a Key Press / Key Release event listens to.
enum class KeyEventInput : uint8_t {
    KEY,           // the keyboard key in `key`
    MOUSE_LEFT,
    MOUSE_RIGHT,
    MOUSE_MIDDLE,
    MOUSE_BACK,    // side button 1
    MOUSE_FORWARD, // side button 2
};

// The key or mouse button went down this frame. No instigator; fires while
// the game is paused too (so a key can resume it). Not on a disabled entity
// or linedef.
struct PayloadKeyPress {
    static constexpr EventOwnerMask owners = OBJECT_EVENT_OWNERS;
    transKey translationKey = "event.key.press";
    transKey saveName = "key.press"; // level file name, never change
    bool operator==(const PayloadKeyPress&) const = default;

    KeyEventInput input = KeyEventInput::KEY;
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN; // KEY only; none never fires
};

// The key or mouse button came back up this frame. Like Key Press otherwise.
struct PayloadKeyRelease {
    static constexpr EventOwnerMask owners = OBJECT_EVENT_OWNERS;
    transKey translationKey = "event.key.release";
    transKey saveName = "key.release"; // level file name, never change
    bool operator==(const PayloadKeyRelease&) const = default;

    KeyEventInput input = KeyEventInput::KEY;
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
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
    HIGHEST_NEIGHBOR_CEILING = 5,
    // The closest neighbour height above / below the surface as it is now.
    NEXT_HIGHER_NEIGHBOR_FLOOR = 6,
    NEXT_LOWER_NEIGHBOR_FLOOR = 7,
    NEXT_HIGHER_NEIGHBOR_CEILING = 8,
    NEXT_LOWER_NEIGHBOR_CEILING = 9
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

// The ceiling (and with `split` the floor too) opens and closes, as `type`
// says. A door is shut while its ceiling sits on its floor.
struct PayloadDispatchSectorDoor {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.door";
    transKey saveName = "dispatch.sector.door"; // level file name, never change
    bool operator==(const PayloadDispatchSectorDoor&) const = default;

    // Open types open from where the door is now. Close types close an open
    // door down to its floor (split: both meet halfway) and reopen it to
    // where it was; on a shut door they do nothing. Toggle opens a shut door
    // and closes an open one, then every trigger reverses it.
    enum Type : uint8_t {
        OPEN_WAIT_CLOSE = 0,
        OPEN_STAY = 1,
        CLOSE_WAIT_OPEN = 2,
        CLOSE_STAY = 3,
        OPEN_CLOSE_TOGGLE = 4
    };

    enum OpenMode : uint8_t {
        LOWEST_NEIGHBOR_CEILING = 0, // minus `lip`
        FIXED_HEIGHT = 1             // `openHeight`
    };

    enum FloorOpenMode : uint8_t {
        LOWEST_NEIGHBOR_FLOOR = 0, // plus `floorLip`
        FIXED_FLOOR_HEIGHT = 1     // `floorOpenHeight`
    };

    // Triggered again during an Open, Wait, Close (Close, Wait, Open) cycle.
    enum Retrigger : uint8_t {
        RESTART = 0, // closing reopens (opening recloses), waiting restarts the wait
        TOGGLE = 1,  // heading back now, or turning round if already heading back
        IGNORE_WHILE_OPEN = 2 // nothing until the cycle has finished
    };

    Type type = OPEN_WAIT_CLOSE;

    OpenMode openMode = LOWEST_NEIGHBOR_CEILING;
    float lip = 4.0f;
    float openHeight = 0.0f;

    // The floor goes down as the ceiling goes up.
    bool split = false;
    FloorOpenMode floorOpenMode = LOWEST_NEIGHBOR_FLOOR;
    float floorLip = 0.0f;
    float floorOpenHeight = 0.0f;

    float openSpeed = 40.0f;
    float closeSpeed = 40.0f;
    float wait = 3.0f; // seconds open (Close, Wait, Open: seconds shut)

    int floorIndex = 0;
    SectorCollision closeCollision = SECTOR_RETRACT; // while closing; opening never checks
    Retrigger retrigger = RESTART;

    // Played from the sector's position when the door starts opening /
    // closing (reopening while it closes counts as opening). Empty: none.
    std::string openSound;
    std::string closeSound;
    float soundGain = 1.0f;
    float soundPitch = 1.0f;

    // Locked: an instigator without `requiredTag` doesn't move the door and
    // gets the locked sound / message instead. Runs with no instigator
    // entity (timers, Level Start, ...) always move it.
    bool locked = false;
    TagId requiredTag = 0;
    std::string lockedSound;
    std::string lockedMessage; // empty: none
    float lockedMessageDuration = 2.0f;
    Vector3 lockedMessageColor = {1.0f, 1.0f, 1.0f}; // 0-1
};

struct PayloadDispatchSectorStopMovement {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.stop_movement";
    transKey saveName = "dispatch.sector.stop_movement"; // level file name, never change
    bool operator==(const PayloadDispatchSectorStopMovement&) const = default;

    SectorSurfacePick surface = SectorSurfacePick::BOTH;
    int floorIndex = 0;
};

// The floor moves away from where it is, waits and comes back, or keeps
// bouncing between its low and high heights, as `type` says.
struct PayloadDispatchSectorLift {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.lift";
    transKey saveName = "dispatch.sector.lift"; // level file name, never change
    bool operator==(const PayloadDispatchSectorLift&) const = default;

    // Down/Up, Wait, back: to the low (high) height, then back to where it
    // started. Perpetual: low and high, waiting at each, until a Stop
    // Movement; it heads first for the one further away.
    enum Type : uint8_t {
        DOWN_WAIT_UP = 0,
        PERPETUAL = 1,
        UP_WAIT_DOWN = 2
    };

    enum LowMode : uint8_t {
        LOWEST_NEIGHBOR_FLOOR = 0, // plus `lowLip`
        FIXED_LOW = 1              // `lowHeight`
    };

    enum HighMode : uint8_t {
        HIGHEST_NEIGHBOR_FLOOR = 0, // plus `highLip`
        FIXED_HIGH = 1              // `highHeight`
    };

    Type type = DOWN_WAIT_UP;

    LowMode lowMode = LOWEST_NEIGHBOR_FLOOR;
    float lowLip = 0.0f;
    float lowHeight = 0.0f;

    HighMode highMode = HIGHEST_NEIGHBOR_FLOOR;
    float highLip = 0.0f;
    float highHeight = 0.0f;

    float speed = 40.0f;
    float wait = 3.0f; // seconds at the far end (Perpetual: at each end)

    int floorIndex = 0;
    SectorCollision collision = SECTOR_RETRACT; // while rising; going down never hits anything

    // Triggered again during a Down/Up, Wait, back cycle. Perpetual lifts
    // ignore it. Same names as the Door's.
    PayloadDispatchSectorDoor::Retrigger retrigger = PayloadDispatchSectorDoor::RESTART;

    // Played from the sector when the floor starts moving / arrives.
    std::string startSound;
    std::string stopSound;
    float soundGain = 1.0f;
    float soundPitch = 1.0f;

    // As on the Door.
    bool locked = false;
    TagId requiredTag = 0;
    std::string lockedSound;
    std::string lockedMessage; // empty: none
    float lockedMessageDuration = 2.0f;
    Vector3 lockedMessageColor = {1.0f, 1.0f, 1.0f}; // 0-1
};

// Doom-style stairs: this sector's floor rises `stepHeight`, the next step
// (the one neighbour with the same floor texture, unless `ignoreTexture`)
// rises to `stepHeight` above that, and so on until there is no next step
// or more than one.
struct PayloadDispatchSectorRaiseStairs {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.raise_stairs";
    transKey saveName = "dispatch.sector.raise_stairs"; // level file name, never change
    bool operator==(const PayloadDispatchSectorRaiseStairs&) const = default;

    float stepHeight = 8.0f;
    float speed = 10.0f;
    int floorIndex = 0; // the floor that rises, on every step
    bool ignoreTexture = false; // any single neighbour is the next step
    SectorCollision collision = SECTOR_STOP;
};

// Keeps changing the sector's light between its own light (as it was when
// the effect started) and a dimmer one, until a None effect or Set Light.
struct PayloadDispatchSectorLightEffect {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    transKey translationKey = "event.dispatch.sector.light_effect";
    transKey saveName = "dispatch.sector.light_effect"; // level file name, never change
    bool operator==(const PayloadDispatchSectorLightEffect&) const = default;

    enum Type : uint8_t {
        NONE = 0,         // stops the effect, the light goes back to the sector's own
        FLICKER = 1,      // random: bright up to brightTime, dim up to dimTime
        STROBE = 2,       // bright for brightTime, dim for dimTime, repeating
        GLOW = 3,         // smoothly down to dim and back up at `speed`
        FIRE_FLICKER = 4  // every `interval`, a random step between bright and dim
    };

    enum DimMode : uint8_t {
        COLOR = 0,          // `dimLight`
        LOWEST_NEIGHBOR = 1 // the darkest neighbouring sector's light
    };

    Type type = STROBE;
    DimMode dimMode = COLOR;
    Vector3 dimLight = {64.0f, 64.0f, 64.0f}; // 0-255, like the inspector

    float brightTime = 1.0f;  // seconds
    float dimTime = 0.25f;    // seconds
    float speed = 128.0f;     // Glow: light units (0-255) per second
    float interval = 0.1f;    // Fire Flicker: seconds between changes

    // Same-type, same-timing effects run on one shared clock, so their
    // sectors change together. Off: each starts at a random point.
    bool sync = false;
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

// The components a Set Field dispatch can change (fields: EventFields.hpp).
enum class FieldComponent : uint8_t {
    TRANSFORM = 0,
    SPRITE = 1,
    AUDIO_SOURCE = 2,
    SCRIPT = 3,
    PLAYER_CONTROLLER = 4,
    CAMERA = 5,
    COLLIDER = 6,
    RIGIDBODY = 7,
    MODEL = 8,
    FLIPBOOK = 9,
    UI_TRANSFORM = 10,
    UI_SPRITE = 11,
    UI_TEXT = 12,
    UI_BUTTON = 13
};

// What a field holds. Numbers, vectors and colours can change over time.
enum class FieldValueKind : uint8_t {
    FLOAT,
    INT,
    BOOL,
    STRING,
    VECTOR2,
    VECTOR3,
    VECTOR4,
    COLOR,   // RGBA, each 0..1
    TEXTURE, // asset path
    SOUND,   // asset path
    ENUM     // a script enum, stored as its int value
};

[[nodiscard]] constexpr bool IsNumericFieldKind(const FieldValueKind kind) noexcept {
    switch (kind) {
        case FieldValueKind::FLOAT: case FieldValueKind::INT: case FieldValueKind::VECTOR2:
        case FieldValueKind::VECTOR3: case FieldValueKind::VECTOR4: case FieldValueKind::COLOR:
            return true;
        default:
            return false;
    }
}

// How many of FieldValue::number's channels a kind uses (0: none).
[[nodiscard]] constexpr int FieldChannelCount(const FieldValueKind kind) noexcept {
    switch (kind) {
        case FieldValueKind::FLOAT: case FieldValueKind::INT: case FieldValueKind::ENUM: return 1;
        case FieldValueKind::VECTOR2: return 2;
        case FieldValueKind::VECTOR3: return 3;
        case FieldValueKind::VECTOR4: case FieldValueKind::COLOR: return 4;
        default: return 0;
    }
}

// A field's value, whatever its kind: numbers and vectors in `number` (a
// single number or an int in x), bools in `flag`, text and asset paths in
// `text`.
struct FieldValue {
    Vector4 number = {0.0f, 0.0f, 0.0f, 0.0f};
    bool flag = false;
    std::string text;

    bool operator==(const FieldValue&) const = default;
};

// Changes one field of one of the owner's components. Which component is
// its place among the owner's components of that type (as the inspector
// lists them), so it survives prefabs and duplicating.
struct PayloadDispatchEntitySetField {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.set_field";
    transKey saveName = "dispatch.entity.set_field"; // level file name, never change
    bool operator==(const PayloadDispatchEntitySetField&) const = default;

    // Add and Multiply work on numbers, vectors and colours (per channel),
    // Toggle on bools; everything can be Set.
    enum Operation : uint8_t {
        SET = 0,
        ADD = 1,
        MULTIPLY = 2,
        TOGGLE = 3
    };

    FieldComponent component = FieldComponent::SPRITE;
    int componentIndex = 0; // 0 = the owner's first of that type
    std::string field;      // an engine field's key (EventFields.hpp), or a script's public field name

    Operation operation = SET;

    // The value is an event variable's (every channel gets it; a bool is
    // true when it isn't 0; text gets the number written out).
    bool fromVariable = false;
    EventVariableID variable = 0;
    FieldValue value;

    // Numbers, vectors and colours slide there at `speed` units per second,
    // per channel. Everything else changes at once.
    bool overTime = false;
    float speed = 1.0f;

    // Goes back to the value from before the first run, `revertAfter`
    // seconds after it got there (sliding back at `speed` too). Running
    // again meanwhile restarts the wait and keeps that first value.
    bool revert = false;
    float revertAfter = 1.0f;
};

// Replaces the owner's first Sprite's 8 directional textures (slots as in
// the Sprite inspector). Does nothing without a Sprite.
struct PayloadDispatchEntitySetSprite {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.set_sprite";
    transKey saveName = "dispatch.entity.set_sprite"; // level file name, never change
    bool operator==(const PayloadDispatchEntitySetSprite&) const = default;

    std::array<std::string, 8> textures;
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

    // Instead of the tag: runs the named dispatches of one target.
    bool direct = false;
    DirectCall call;
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

// Like Game.Pause: game time stops (timeScale 0) and the cursor is unlocked
// and shown.
struct PayloadDispatchPause {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.pause";
    transKey saveName = "dispatch.pause"; // level file name, never change
    bool operator==(const PayloadDispatchPause&) const = default;
};

// Undoes a pause: timeScale back to 1 and the cursor back to the level's
// World Settings (Lock Cursor / Hide Cursor on Level Start).
struct PayloadDispatchResume {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.resume";
    transKey saveName = "dispatch.resume"; // level file name, never change
    bool operator==(const PayloadDispatchResume&) const = default;
};

// Like Game.Quit: at the end of the frame the exported game closes; in the
// editor, Play stops.
struct PayloadDispatchQuit {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.quit";
    transKey saveName = "dispatch.quit"; // level file name, never change
    bool operator==(const PayloadDispatchQuit&) const = default;
};

// Replaces the text of the owner's first UI Text. Does nothing without one.
struct PayloadDispatchSetUIText {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    transKey translationKey = "event.dispatch.entity.set_ui_text";
    transKey saveName = "dispatch.entity.set_ui_text"; // level file name, never change
    bool operator==(const PayloadDispatchSetUIText&) const = default;

    // Which of the UI Text's texts: its plain text, or the prefix / suffix
    // around a shown variable.
    enum Part : uint8_t {
        TEXT = 0,
        PREFIX = 1,
        SUFFIX = 2
    };

    Part part = TEXT;
    std::string text;
};

// Controls the level's music (World Settings > Music), like Lua's Music table.
struct PayloadDispatchMusic {
    static constexpr EventOwnerMask owners = ALL_EVENT_OWNERS;
    transKey translationKey = "event.dispatch.music";
    transKey saveName = "dispatch.music"; // level file name, never change
    bool operator==(const PayloadDispatchMusic&) const = default;

    enum Action : uint8_t {
        PLAY = 0,            // resumes a paused track, else starts the list
        STOP = 1,
        PAUSE = 2,
        SWITCH_TO_TRACK = 3, // `track`
        SET_VOLUME = 4       // `volume`
    };

    Action action = PLAY;
    int track = 0; // 0-based
    float volume = 1.0f;
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
        SUBTRACT = 2,
        MULTIPLY = 3,
        DIVIDE = 4,
    };

    EventVariableID variable = 0; // 0 = none picked, does nothing
    Operation operation = SET;
    float value = 0.0f;
};
//endregion

using EventPayload = std::variant<
    PayloadLineEntityCollide, PayloadLineEntityCross,
    PayloadSectorEntityEnter, PayloadSectorEntityExit, PayloadSectorEntityStay, PayloadSectorEntityGrounded, PayloadSectorMoveFinished,
    PayloadEntityChangeSector, PayloadEntityGroundedInSector, PayloadEntityCollide, PayloadEntityDisable, PayloadEntityEnable, PayloadEntityDestroyed,
    PayloadReceiveRay, PayloadConditionMet, PayloadTimer, PayloadLuaSignal, PayloadOnStart,
    PayloadUIButtonClick, PayloadUIButtonRelease, PayloadUIButtonHoverEnter, PayloadUIButtonHoverExit,
    PayloadKeyPress, PayloadKeyRelease>;

using DispatchPayload = std::variant<
    PayloadDispatchLineSetColor, PayloadDispatchLineSetTexture, PayloadDispatchLineSetCollision,
    PayloadDispatchLinedefSetEnabled,
    PayloadDispatchSectorMoveCeilingToFloor, PayloadDispatchSectorMoveFloorToCeiling, PayloadDispatchSectorMoveToHeight,
    PayloadDispatchSectorDoor, PayloadDispatchSectorLift, PayloadDispatchSectorRaiseStairs,
    PayloadDispatchSectorStopMovement, PayloadDispatchSectorSetLight, PayloadDispatchSectorLightEffect, PayloadDispatchSectorSetTexture,
    PayloadDispatchEntityCallFunction, PayloadDispatchEntityDestroy, PayloadDispatchEntityDisable, PayloadDispatchEntityEnable,
    PayloadDispatchEntityTeleport, PayloadDispatchEntityChangeVelocity, PayloadDispatchEntityMoveTo, PayloadDispatchEntitySetComponent,
    PayloadDispatchEntitySetField, PayloadDispatchEntitySetSprite,
    PayloadDispatchChangeTag, PayloadDispatchSpawnPrefab, PayloadDispatchPlaySound, PayloadDispatchShowMessage,
    PayloadDispatchRelay, PayloadDispatchResetEvent, PayloadDispatchLoadLevel, PayloadDispatchChangeVariable,
    PayloadDispatchPause, PayloadDispatchResume, PayloadDispatchQuit, PayloadDispatchSetUIText, PayloadDispatchMusic>;

template <typename Payload>
[[nodiscard]] constexpr EventOwnerMask GetOwners(const Payload& payload) noexcept {
    return std::visit([](const auto& p) { return std::decay_t<decltype(p)>::owners; }, payload);
}

template <typename Payload>
[[nodiscard]] constexpr bool CanAttachTo(const Payload& payload, const EventOwnerKind kind) noexcept {
    return (GetOwners(payload) & OwnerBit(kind)) != 0;
}

// Payloads that only go on a UI entity with a UI Button (the UI Button events).
template <typename Payload>
inline constexpr bool NeedsUIButton = requires { requires Payload::needsUIButton; };

template <typename Payload>
[[nodiscard]] constexpr const char* GetTranslationKey(const Payload& payload) noexcept {
    return std::visit([](const auto& p) { return std::decay_t<decltype(p)>::translationKey; }, payload);
}

struct Event {
    EventPayload payload;

    // Runs every dispatch listening on tagId, unless useTag is off.
    bool useTag = true;
    TagId tagId = 0;

    // And each of these.
    std::vector<DirectCall> calls;

    Condition condition;

    uint32_t maxTriggers = 0; // 0 = unlimited, 1 = once
    float chance = 100.0f;    // percentage, out of 100. A failed roll doesn't count as a trigger
    uint32_t triggerCount = 0; // runtime only: not saved, reset when play starts

    // Authored data only - triggerCount is left out.
    [[nodiscard]] bool operator==(const Event& other) const {
        return payload == other.payload && useTag == other.useTag && tagId == other.tagId && calls == other.calls &&
               condition == other.condition && maxTriggers == other.maxTriggers && chance == other.chance;
    }

    [[nodiscard]] constexpr bool IsLine() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINE); }
    [[nodiscard]] constexpr bool IsSector() const noexcept { return CanAttachTo(payload, EventOwnerKind::SECTOR); }
    [[nodiscard]] constexpr bool IsEntity() const noexcept { return CanAttachTo(payload, EventOwnerKind::ENTITY); }
    [[nodiscard]] constexpr bool IsLinedef() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINEDEF); }
    [[nodiscard]] constexpr bool IsLevel() const noexcept { return CanAttachTo(payload, EventOwnerKind::LEVEL); }
};

// What decides whether a flow step calls its next dispatch, checked once its
// wait is over. One type per kind, like the payloads: `translationKey` is
// its display name, `saveName` what level files store it as.
struct FlowAlways {
    transKey translationKey = "events.flow.condition.always";
    transKey saveName = "always"; // level file name, never change
    bool operator==(const FlowAlways&) const = default;
};

// Goes on when the event-variable comparison holds (same as an event's).
struct FlowVariable {
    transKey translationKey = "events.flow.condition.variable";
    transKey saveName = "variable"; // level file name, never change
    bool operator==(const FlowVariable&) const = default;

    Condition condition{EventVariableID{0}, Condition::EQUAL, 0.0f};
};

// Goes on with a `chance` out of 100, rolled each time.
struct FlowChance {
    transKey translationKey = "events.flow.condition.chance";
    transKey saveName = "chance"; // level file name, never change
    bool operator==(const FlowChance&) const = default;

    float chance = 50.0f;
};

using FlowCondition = std::variant<FlowAlways, FlowVariable, FlowChance>;

// One step after a dispatch runs: `wait` seconds later (0 = the next frame),
// if the condition holds, runs the named dispatches of the call's target.
// Self is the dispatch's own object; the instigator stays the one that
// started the chain. Nothing follows a dispatch that didn't run (a disabled
// entity, a locked door).
struct DispatchFlow {
    DirectCall call;
    float wait = 0.0f; // seconds, counted from when the dispatch runs
    FlowCondition condition = FlowAlways{};

    bool operator==(const DispatchFlow&) const = default;
};

struct Dispatch {
    DispatchPayload payload;

    // What direct calls run it by. Several dispatches of one object may
    // share a name. Empty: only tags reach it.
    std::string name;

    // Listens on tagId, unless useTag is off (then only direct calls run it).
    bool useTag = true;
    TagId tagId = 0;

    // Seconds between the event firing and this running. Each trigger
    // queues its own run, so quick triggers give one run each.
    float delay = 0.0f;

    // Seconds after a call during which calls do nothing (0 = none). The
    // cooldown starts once every `cooldownTriggers` calls (at least 1).
    float cooldown = 0.0f;
    int cooldownTriggers = 1;

    // What runs after it, each step on its own (several may be stacked).
    std::vector<DispatchFlow> flows;

    bool operator==(const Dispatch&) const = default;

    [[nodiscard]] constexpr bool IsLine() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINE); }
    [[nodiscard]] constexpr bool IsSector() const noexcept { return CanAttachTo(payload, EventOwnerKind::SECTOR); }
    [[nodiscard]] constexpr bool IsEntity() const noexcept { return CanAttachTo(payload, EventOwnerKind::ENTITY); }
    [[nodiscard]] constexpr bool IsLinedef() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINEDEF); }
    [[nodiscard]] constexpr bool IsLevel() const noexcept { return CanAttachTo(payload, EventOwnerKind::LEVEL); }
};

#endif //TILKY_ENGINE_EVENT_HPP
