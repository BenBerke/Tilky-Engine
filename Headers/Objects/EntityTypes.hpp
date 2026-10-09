// Headers/Objects/EntityTypes.hpp

#ifndef TILKY_ENGINE_ENTITY_TYPES_HPP
#define TILKY_ENGINE_ENTITY_TYPES_HPP

#include <cstdint>
#include <limits>

using ID = uint32_t;
using LevelID = ID; // Legacy. //todo remove
using UIElementID = ID;

constexpr ID INVALID_ID = std::numeric_limits<ID>::max();
constexpr ID INVALID_ENTITY_ID = INVALID_ID;

using ScriptInstanceID = std::uint64_t;
constexpr ScriptInstanceID INVALID_SCRIPT_INSTANCE_ID = 0;

// Identifies one component among all components of its type in a level
// (see ComponentStorage). Saved with the level.
using ComponentInstanceID = std::uint64_t;
constexpr ComponentInstanceID INVALID_COMPONENT_INSTANCE_ID = 0;

#endif // TILKY_ENGINE_ENTITY_TYPES_HPP