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

    // A RayType value passed from Lua (nil = General); raises a Lua error
    // naming `function` on anything else. Defined in LuaGameBindings.cpp.
    RayType ToRayType(const sol::optional<int>& value, const char* function);
}

#endif //TILKY_ENGINE_LUARAYHIT_HPP
