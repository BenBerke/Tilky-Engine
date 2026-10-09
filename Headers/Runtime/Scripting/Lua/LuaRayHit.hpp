//
// Created by berke on 10/6/2026.
//

#ifndef TILKY_ENGINE_LUARAYHIT_HPP
#define TILKY_ENGINE_LUARAYHIT_HPP

#include <optional>

#include <sol/sol.hpp>

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/RayHit.hpp"

namespace LuaRayHit {
    // The table Game.Raycast and Camera:Raycast return: type/typeID/position/
    // distance/entityID/wallID/sectorID and entity/wall/sector, or nil on a miss.
    // Defined in LuaGameBindings.cpp.
    sol::object ToLua(sol::state_view lua, Level& level, const std::optional<RayHit>& hit);
}

#endif //TILKY_ENGINE_LUARAYHIT_HPP
