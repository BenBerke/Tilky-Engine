// Headers/Objects/EntityTypes.hpp

#ifndef TILKY_ENGINE_ENTITY_TYPES_HPP
#define TILKY_ENGINE_ENTITY_TYPES_HPP

#include <cstdint>

using ID = uint32_t;
using LevelID = ID; // Legacy. //todo remove
using UIElementID = ID;

constexpr ID INVALID_ID = std::numeric_limits<ID>::max();

#endif // TILKY_ENGINE_ENTITY_TYPES_HPP