//
// Created by berke on 6/22/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

#include <sstream>
#include <string>

#include <spdlog/spdlog.h>

namespace {
    sol::table GetOrCreateTable(sol::state& lua, const char* name) {
        const sol::object existing = lua[name];

        if (existing.get_type() == sol::type::table) {
            return existing.as<sol::table>();
        }

        if (existing.get_type() != sol::type::nil) {
            spdlog::warn("Replacing Lua global '{}' because it is not a table", name);
        }

        return lua.create_named_table(name);
    }

    std::string LuaArgsToString(const sol::this_state state, const sol::variadic_args args) {
        const sol::state_view lua(state);
        const sol::protected_function toString = lua["tostring"];
        std::ostringstream out;

        bool first = true;

        for (const sol::object& arg : args) {
            if (!first) out << " ";
            first = false;

            if (!toString.valid()) {
                out << "<unprintable>";
                continue;
            }

            const sol::protected_function_result result = toString(arg);

            if (!result.valid()) {
                out << "<unprintable>";
                continue;
            }

            out << result.get<std::string>();
        }

        return out.str();
    }
}

void LuaScriptSystem::RegisterEditorFunctionBindings(sol::state& lua) {
    sol::table debug = GetOrCreateTable(lua, "Debug");

    debug.set_function("Print", [](const sol::this_state state, const sol::variadic_args args) {
        const std::string message = LuaArgsToString(state, args);
        EditorFunctions::Print(message);
        spdlog::info("[Lua] {}", message);
    });

    debug.set_function("LogInfo", [](const sol::this_state state, const sol::variadic_args args) {
        spdlog::info("{}", LuaArgsToString(state, args));
    });

    debug.set_function("LogError", [](const sol::this_state state, const sol::variadic_args args) {
        spdlog::error("{}", LuaArgsToString(state, args));
    });

    debug.set_function("LogCritical", [](const sol::this_state state, const sol::variadic_args args) {
        spdlog::critical("{}", LuaArgsToString(state, args));
    });

    debug.set_function("LogWarning", [](const sol::this_state state, const sol::variadic_args args) {
        spdlog::warn("{}", LuaArgsToString(state, args));
    });
}
