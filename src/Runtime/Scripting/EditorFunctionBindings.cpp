//
// Created by berke on 6/22/2026.
//

#include "Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "sol/sol.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

namespace {
   std::string LuaArgsToString(sol::variadic_args args) {
      std::ostringstream out;

      bool first = true;

      for (const sol::object& arg : args) {
         if (!first) out << " ";
         first = false;

         if (arg == sol::nil) out << "nil";
         else if (arg.is<std::string>()) out << arg.as<std::string>();
         else if (arg.is<const char*>()) out << arg.as<const char*>();
         else if (arg.is<int>()) out << arg.as<int>();
         else if (arg.is<float>()) out << arg.as<float>();
         else if (arg.is<double>()) out << arg.as<double>();
         else if (arg.is<bool>()) out << (arg.as<bool>() ? "true" : "false");
         else out << "<object>";

      }

      return out.str();
   }
}

void ScriptSystem::RegisterEditorFunctionBindings(sol::state& lua) {
   sol::table debug = lua.create_table();

   debug.set_function("Print", [](const sol::variadic_args &args)->void {
       EditorFunctions::Print(LuaArgsToString(args));
   });

   debug.set_function("LogInfo", [](const sol::variadic_args &args)->void {
       spdlog::info("{}", LuaArgsToString(args));
   });

   debug.set_function("LogError", [](const sol::variadic_args &args)->void {
       spdlog::error("{}", LuaArgsToString(args));
   });

   debug.set_function("LogCritical", [](const sol::variadic_args &args)->void {
       spdlog::critical("{}", LuaArgsToString(args));
   });

   debug.set_function("LogWarning", [](const sol::variadic_args &args)->void {
       spdlog::warn("{}", LuaArgsToString(args));
   });

   lua["Debug"] = debug;
}