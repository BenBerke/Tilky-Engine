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

#include "Types.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
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

// What an event or dispatch is attached to
enum class EventOwnerKind : uint8_t {
    LINE = 0,
    SECTOR = 1,
    ENTITY = 2,
    LINEDEF = 3,
};

// A set of EventOwnerKinds, one bit each: what a payload can be attached to.
using EventOwnerMask = uint8_t;

[[nodiscard]] constexpr EventOwnerMask OwnerBit(const EventOwnerKind kind) noexcept {
    return static_cast<EventOwnerMask>(1u << static_cast<uint8_t>(kind));
}

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

// Each payload type is the event/dispatch kind itself: `owners` is what it
// can be attached to, `translationKey` its display name, `saveName` what level
// files store it as.

//region Event Payloads
struct PayloadLineEntityCollide {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::LINEDEF);
    static constexpr const char* translationKey = "event.line.entity_collide";
    static constexpr const char* saveName = "line.entity_collide"; // level file name, never change
    bool operator==(const PayloadLineEntityCollide&) const = default;

    TagFilter filter;
};

struct PayloadLineEntityCross {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE) | OwnerBit(EventOwnerKind::LINEDEF);
    static constexpr const char* translationKey = "event.line.entity_cross";
    static constexpr const char* saveName = "line.entity_cross"; // level file name, never change
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
    static constexpr const char* translationKey = "event.sector.entity_enter";
    static constexpr const char* saveName = "sector.entity_enter"; // level file name, never change
    bool operator==(const PayloadSectorEntityEnter&) const = default;

    TagFilter filter;
};

struct PayloadSectorEntityExit {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    static constexpr const char* translationKey = "event.sector.entity_exit";
    static constexpr const char* saveName = "sector.entity_exit"; // level file name, never change
    bool operator==(const PayloadSectorEntityExit&) const = default;

    TagFilter filter;
};

struct PayloadEntityCollide {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.entity.collide";
    static constexpr const char* saveName = "entity.collide"; // level file name, never change
    bool operator==(const PayloadEntityCollide&) const = default;

    TagFilter filter;
};

struct PayloadEntityChangeSector {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.entity.change_sector";
    static constexpr const char* saveName = "entity.change_sector"; // level file name, never change
    bool operator==(const PayloadEntityChangeSector&) const = default;

    TagFilter filter;
};

struct PayloadEntityEnable {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.entity.enable";
    static constexpr const char* saveName = "entity.enable"; // level file name, never change
    bool operator==(const PayloadEntityEnable&) const = default;
};

struct PayloadEntityDisable { // Nothing needed
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.entity.disable";
    static constexpr const char* saveName = "entity.disable"; // level file name, never change
    bool operator==(const PayloadEntityDisable&) const = default;
};
//endregion

//region Dispatch Payloads
// Probably wont be used much but still, good to have
struct PayloadDispatchLineSetColor {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::LINE);
    static constexpr const char* translationKey = "event.dispatch.line.set_color";
    static constexpr const char* saveName = "dispatch.line.set_color"; // level file name, never change
    bool operator==(const PayloadDispatchLineSetColor&) const = default;

    Vector3 color;
    float speed = 10.0f; // color units (0-1) per second, per channel. 0 or less = instant
};

struct PayloadDispatchSectorMoveFloorToCeiling {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    static constexpr const char* translationKey = "event.dispatch.sector.move_floor_to_ceiling";
    static constexpr const char* saveName = "dispatch.sector.move_floor_to_ceiling"; // level file name, never change
    bool operator==(const PayloadDispatchSectorMoveFloorToCeiling&) const = default;

    float gap = .0f, speed = 10.0f;
    int floorIndex = 0; // which of the sector's floors (slabs) moves

    SectorCollision collisionType = SECTOR_RETRACT;
};

struct PayloadDispatchSectorMoveCeilingToFloor {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::SECTOR);
    static constexpr const char* translationKey = "event.dispatch.sector.move_ceiling_to_floor";
    static constexpr const char* saveName = "dispatch.sector.move_ceiling_to_floor"; // level file name, never change
    bool operator==(const PayloadDispatchSectorMoveCeilingToFloor&) const = default;

    float gap = .0f, speed = 10.0f;
    int floorIndex = 0; // which of the sector's floors (slabs) moves

    SectorCollision collisionType = SECTOR_RETRACT;
};

struct PayloadDispatchEntityDestroy { // Nothing needed
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.dispatch.entity.destroy";
    static constexpr const char* saveName = "dispatch.entity.destroy"; // level file name, never change
    bool operator==(const PayloadDispatchEntityDestroy&) const = default;
};

struct PayloadDispatchEntityTeleport {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.dispatch.entity.teleport";
    static constexpr const char* saveName = "dispatch.entity.teleport"; // level file name, never change
    bool operator==(const PayloadDispatchEntityTeleport&) const = default;

    Vector3 position;
    Quaternion rotation;
};

struct PayloadDispatchEntityDisable {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.dispatch.entity.disable";
    static constexpr const char* saveName = "dispatch.entity.disable"; // level file name, never change
    bool operator==(const PayloadDispatchEntityDisable&) const = default;
};

struct PayloadDispatchEntityEnable {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.dispatch.entity.enable";
    static constexpr const char* saveName = "dispatch.entity.enable"; // level file name, never change
    bool operator==(const PayloadDispatchEntityEnable&) const = default;
};

// Supporting parameters would be too complicated (?)
struct PayloadDispatchEntityCallFunction {
    static constexpr EventOwnerMask owners = OwnerBit(EventOwnerKind::ENTITY);
    static constexpr const char* translationKey = "event.dispatch.entity.call_function";
    static constexpr const char* saveName = "dispatch.entity.call_function"; // level file name, never change
    bool operator==(const PayloadDispatchEntityCallFunction&) const = default;

    std::string functionName;
};
//endregion

using EventPayload = std::variant<
    PayloadLineEntityCollide, PayloadLineEntityCross,
    PayloadSectorEntityEnter, PayloadSectorEntityExit,
    PayloadEntityChangeSector, PayloadEntityCollide, PayloadEntityDisable, PayloadEntityEnable>;

using DispatchPayload = std::variant<
    PayloadDispatchLineSetColor,
    PayloadDispatchSectorMoveCeilingToFloor, PayloadDispatchSectorMoveFloorToCeiling,
    PayloadDispatchEntityCallFunction, PayloadDispatchEntityDestroy, PayloadDispatchEntityDisable, PayloadDispatchEntityEnable, PayloadDispatchEntityTeleport>;

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

    uint32_t maxTriggers = 0; // 0 = unlimited, 1 = once
    float chance = 100.0f;    // percentage, out of 100. A failed roll doesn't count as a trigger
    uint32_t triggerCount = 0; // runtime only: not saved, reset when play starts

    // Authored data only - triggerCount is left out.
    [[nodiscard]] bool operator==(const Event& other) const {
        return payload == other.payload && tagId == other.tagId &&
               maxTriggers == other.maxTriggers && chance == other.chance;
    }

    [[nodiscard]] constexpr bool IsLine() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINE); }
    [[nodiscard]] constexpr bool IsSector() const noexcept { return CanAttachTo(payload, EventOwnerKind::SECTOR); }
    [[nodiscard]] constexpr bool IsEntity() const noexcept { return CanAttachTo(payload, EventOwnerKind::ENTITY); }
    [[nodiscard]] constexpr bool IsLinedef() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINEDEF); }
};

struct Dispatch {
    DispatchPayload payload;

    TagId tagId = 0;

    bool operator==(const Dispatch&) const = default;

    [[nodiscard]] constexpr bool IsLine() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINE); }
    [[nodiscard]] constexpr bool IsSector() const noexcept { return CanAttachTo(payload, EventOwnerKind::SECTOR); }
    [[nodiscard]] constexpr bool IsEntity() const noexcept { return CanAttachTo(payload, EventOwnerKind::ENTITY); }
    [[nodiscard]] constexpr bool IsLinedef() const noexcept { return CanAttachTo(payload, EventOwnerKind::LINEDEF); }
};

#endif //TILKY_ENGINE_EVENT_HPP
