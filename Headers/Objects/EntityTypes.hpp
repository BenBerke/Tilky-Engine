// Headers/Objects/EntityTypes.hpp

#ifndef TILKY_ENGINE_ENTITY_TYPES_HPP
#define TILKY_ENGINE_ENTITY_TYPES_HPP

#include <cstdint>

using ID = uint32_t;
using LevelID = ID; // Legacy. //todo remove
using UIElementID = ID;

constexpr ID INVALID_ID = std::numeric_limits<ID>::max();
constexpr ID INVALID_ENTITY_ID = INVALID_ID;

using ScriptInstanceID = std::uint64_t;
constexpr ScriptInstanceID INVALID_SCRIPT_INSTANCE_ID = 0;

#endif // TILKY_ENGINE_ENTITY_TYPES_HPP