//
// Created by berke on 10/26/2026.
//

#ifndef TILKY_ENGINE_EVENT_H
#define TILKY_ENGINE_EVENT_H

#include <cstdint>
#include <string>
#include <variant>

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

inline std::string lineEventTranslationKeys[static_cast<size_t>(LineEventType::COUNT)] = {
    "event.line.entity_collide",
    "event.line.entity_cross"
};

inline std::string sectorEventTranslationKeys[static_cast<size_t>(LineEventType::COUNT)] = {
    "event.sector.entity_enter",
    "event.sector.entity_exit"
};

struct Event {
    std::variant<LineEventType> eventType;
};

#endif //TILKY_ENGINE_EVENT_H