//
// Created by berke on 10/26/2026.
//

#ifndef TILKY_ENGINE_EVENT_HPP
#define TILKY_ENGINE_EVENT_HPP

#include <cstdint>
#include <variant>
#include <string>

#include "Types.hpp"
#include "Headers/Math/Quaternion/Quaternion.hpp"

// What a moving floor or ceiling does when it would hit an entity's collider
// (Lua: SectorCollision). Only a floor going up or a ceiling going down can
// hit one; checked by Level::UpdateSectorMovement.
enum SectorCollision {
    SECTOR_FORCE_MOVE, // move through entities, as if they weren't there
    SECTOR_STOP,       // stop the move at the last height that fit
    SECTOR_RETRACT,    // go back to the height the move started from
    SECTOR_DONT_MOVE   // hold still while blocked, carry on once clear
};

// Events that can be attached to walls (and linedefs in the future)
enum class LineEventType : uint8_t {
    ENTITY_COLLIDE = 0,
    ENTITY_CROSS = 1,

    COUNT,
};

enum class SectorEventType : uint8_t {
    ENTITY_ENTER = 0,
    ENTITY_EXIT = 1,

    COUNT,
};

enum class EntityEventType : uint8_t {
    COLLIDE = 0,
    CHANGE_SECTOR = 1,
    ENABLE = 2,
    DISABLE = 3,

    COUNT
};

// Probably wont be used much but still, good to have
enum class LineDispatchType : uint8_t {
    SET_COLOR = 0,

    COUNT
};

enum class SectorDispatchType : uint8_t {
    MOVE_FLOOR_TO_CEILING = 0,
    MOVE_CEILING_TO_FLOOR = 1,

    COUNT
};

enum class EntityDispatchType : uint8_t {
    DESTROY = 0,
    TELEPORT = 1,
    DISABLE = 2,
    CALL_FUNCTION = 3,

    COUNT
};

//region Translation Arrays
#define index(a) static_cast<size_t>(a)
#define arr inline constexpr const char*

arr lineEventTranslationKeys[index(LineEventType::COUNT)] = {
    "event.line.entity_collide",
    "event.line.entity_cross"
};

arr sectorEventTranslationKeys[index(SectorEventType::COUNT)] = {
    "event.sector.entity_enter",
    "event.sector.entity_exit"
};

arr entityEventTranslationKeys[index(EntityEventType::COUNT)] = {
    "event.entity.collide",
    "event.entity.change_sector",
    "event.entity.enable",
    "event.entity.disable",
};

arr sectorDispatchTranslationKeys[index(SectorDispatchType::COUNT)] = {
    "event.dispatch.sector.move_floor_to_ceiling",
    "event.dispatch.sector.move_ceiling_to_floor"
};

arr lineDispatchTranslationKeys[index(LineDispatchType::COUNT)] = {
    "event.dispatch.line.set_color"
};

arr entityDispatchTranslationKeys[index(EntityDispatchType::COUNT)] = {
    "event.dispatch.entity.destroy",
    "event.dispatch.entity.teleport",
    "event.dispatch.entity.disable",
    "event.dispatch.entity.call_function"
};

#undef arr
#undef index

//endregion

enum class TagType : uint8_t {
    INCLUDE, // Call event for entities with the tag
    EXCLUDE, // Call event for entities without the tag
    ALL, // ignore tag and call event for all entity
};
struct TagFilter {
    TagType tagType = TagType::INCLUDE;
    TagId tag = 0;
};

//region Payloads
struct PayloadEmpty {};

struct PayloadLineEntityCollide {
    TagFilter filter;
};

struct PayloadLineEntityCross {
    enum EntityCrossDirection {
        FORWARD = 0,
        BACKWARD = 1,
        EITHER = 2
    };

    TagFilter filter;

    EntityCrossDirection direction = EITHER;
};

struct PayloadSectorEntityEnter {
    TagFilter filter;
};

struct PayloadSectorEntityExit {
    TagFilter filter;
};

struct PayloadEntityCollide {
    TagFilter filter;
};

struct PayloadEntityChangeSector {
    TagFilter filter;
};

struct PayloadEntityEnable {}; // Nothing needed

struct PayloadEntityDisable {}; // Nothing needed

struct PayloadDispatchEmpty {};

struct PayloadDispatchSectorMoveFloorToCeiling {
    float gap = .0f, speed = 10.0f;

    SectorCollision collisionType = SECTOR_RETRACT;
};

struct PayloadDispatchSectorMoveCeilingToFloor {
    float gap, speed;

    SectorCollision collisionType;
};

struct PayloadDispatchLineSetColor {
    Vector3 color;
    float speed;
};

struct PayloadDispatchEntityDestroy {}; // Nothing needed

struct PayloadDispatchEntityTeleport {
    Vector3 position;
    Quaternion rotation;
};

struct PayloadDispatchEntityDisable {};

// Supporting parameters would be too complicated (?)
struct PayloadDispatchEntityCallFunction {
    std::string functionName;
};

//endregion

using EventPayload = std::variant<PayloadEmpty,
    PayloadLineEntityCollide, PayloadLineEntityCross,
    PayloadSectorEntityEnter, PayloadSectorEntityExit,
    PayloadEntityChangeSector, PayloadEntityCollide, PayloadEntityDisable, PayloadEntityEnable>;

using DispatchPayload = std::variant<PayloadDispatchEmpty,
    PayloadDispatchLineSetColor,
    PayloadDispatchSectorMoveCeilingToFloor, PayloadDispatchSectorMoveFloorToCeiling,
    PayloadDispatchEntityCallFunction, PayloadDispatchEntityDestroy, PayloadDispatchEntityDisable, PayloadDispatchEntityTeleport>;

struct Event {
    std::variant<LineEventType, SectorEventType, EntityEventType> type;

    EventId tagId;

    EventPayload payload;

    [[nodiscard]] constexpr bool IsLine() const noexcept {
        return std::holds_alternative<LineEventType>(type);
    }

    [[nodiscard]] constexpr bool IsSector() const noexcept {
        return std::holds_alternative<SectorEventType>(type);
    }

    [[nodiscard]] constexpr bool IsEntity() const noexcept {
        return std::holds_alternative<EntityEventType>(type);
    }
};

struct Dispatch {
    std::variant<LineDispatchType, SectorDispatchType, EntityDispatchType> type;

    EventId tagId;

    DispatchPayload payload;

    [[nodiscard]] constexpr bool IsLine() const noexcept {
        return std::holds_alternative<LineDispatchType>(type);
    }

    [[nodiscard]] constexpr bool IsSector() const noexcept {
        return std::holds_alternative<SectorDispatchType>(type);
    }

    [[nodiscard]] constexpr bool IsEntity() const noexcept {
        return std::holds_alternative<EntityDispatchType>(type);
    }
};

#endif //TILKY_ENGINE_EVENT_HPP