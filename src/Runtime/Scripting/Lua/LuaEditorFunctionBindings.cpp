//
// Created by berke on 6/22/2026.
//

#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "sol/sol.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"

#include <sstream>
#include <string>

#include <spdlog/spdlog.h>

namespace {
    using namespace LuaBindingMetadata;

    void RegisterDebugMetadata() {
        RegisterType(Type("Debug", "Global logging table. Every function accepts any number of arguments, "
            "space-joined via tostring() (like Lua's print).", {}, {
            Method("Print", {}, "", "Shows the message in the in-editor console."),
            Method("LogInfo"),
            Method("LogError"),
            Method("LogCritical"),
            Method("LogWarning"),
        }));
    }

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
    RegisterDebugMetadata();

    sol::table debug = GetOrCreateTable(lua, "Debug");

    debug.set_function("Print", [](const sol::this_state state, const sol::variadic_args args) {
        const std::string message = LuaArgsToString(state, args);
        EditorFunctions::Print(message);
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
