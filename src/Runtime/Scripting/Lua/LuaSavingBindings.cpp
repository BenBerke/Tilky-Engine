#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Runtime/SaveSystem.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"

#include <sol/sol.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

// Lua's Saving table - see SaveSystem for the files behind it.
namespace {
    using json = nlohmann::json;
    using namespace LuaBindingMetadata;

    // The four kinds a value can be saved as, by their type name in the file.
    constexpr const char* TYPE_NUMBER = "number";
    constexpr const char* TYPE_TEXT = "text";
    constexpr const char* TYPE_BOOL = "bool";
    constexpr const char* TYPE_VECTOR3 = "vector3";

    // "Number" for "number" - how Saving's functions name a kind.
    std::string KindName(const std::string& type) {
        if (type == TYPE_NUMBER) return "Number";
        if (type == TYPE_TEXT) return "Text";
        if (type == TYPE_BOOL) return "Bool";
        if (type == TYPE_VECTOR3) return "Vector3";
        return type;
    }

    std::string LuaTypeName(const sol::object& value) {
        return sol::type_name(value.lua_state(), value.get_type());
    }

    std::string CheckFileName(const sol::object& value, const std::string& function) {
        if (value.get_type() != sol::type::string)
            throw sol::error(function + ": the file name must be a string, got " + LuaTypeName(value));

        const std::string fileName = value.as<std::string>();
        if (const std::string problem = SaveSystem::FileNameProblem(fileName); !problem.empty())
            throw sol::error(function + ": " + problem);

        return fileName;
    }

    std::string CheckName(const sol::object& value, const std::string& function) {
        if (value.get_type() != sol::type::string)
            throw sol::error(function + ": the variable name must be a string, got " + LuaTypeName(value));

        std::string name = value.as<std::string>();
        if (name.empty()) throw sol::error(function + ": the variable name is empty");

        return name;
    }

    // Lua 3 and 3.0 are different numbers; the file keeps which one it was.
    json ToJsonNumber(const sol::object& value, const std::string& function) {
        lua_State* state = value.lua_state();
        value.push(state);
        const bool isInteger = lua_isinteger(state, -1);
        lua_pop(state, 1);

        if (isInteger) return value.as<std::int64_t>();

        const double number = value.as<double>();
        if (!std::isfinite(number)) throw sol::error(function + ": can't save nan or infinity");
        return number;
    }

    sol::object FromJsonNumber(const json& value, const sol::state_view lua) {
        if (value.is_number_integer()) return sol::make_object(lua, value.get<std::int64_t>());
        return sol::make_object(lua, value.get<double>());
    }

    // The saved value as a Lua value of its kind.
    sol::object ToLua(const std::string& type, const json& value, const sol::state_view lua) {
        if (type == TYPE_NUMBER) return FromJsonNumber(value, lua);
        if (type == TYPE_TEXT) return sol::make_object(lua, value.get<std::string>());
        if (type == TYPE_BOOL) return sol::make_object(lua, value.get<bool>());
        return sol::make_object(lua, Vector3{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()});
    }

    // Whether a Lua value is of the kind `type`.
    bool IsKind(const sol::object& value, const std::string& type) {
        if (type == TYPE_NUMBER) return value.get_type() == sol::type::number;
        if (type == TYPE_TEXT) return value.get_type() == sol::type::string;
        if (type == TYPE_BOOL) return value.get_type() == sol::type::boolean;
        return value.is<Vector3>();
    }

    sol::object Zero(const std::string& type, const sol::state_view lua) {
        if (type == TYPE_NUMBER) return sol::make_object(lua, 0);
        if (type == TYPE_TEXT) return sol::make_object(lua, std::string());
        if (type == TYPE_BOOL) return sol::make_object(lua, false);
        return sol::make_object(lua, Vector3{0.0f, 0.0f, 0.0f});
    }

    void Save(const std::string& type, const sol::object& fileName, const sol::object& name, const sol::object& value) {
        const std::string function = "Saving.Save" + KindName(type);
        const std::string file = CheckFileName(fileName, function);
        const std::string key = CheckName(name, function);

        if (!IsKind(value, type))
            throw sol::error(function + ": the value must be " + (type == TYPE_VECTOR3 ? std::string("a Vector3") : "a " + std::string(type == TYPE_TEXT ? "string" : type == TYPE_BOOL ? "boolean" : "number")) +
                             ", got " + LuaTypeName(value));

        json saved;
        if (type == TYPE_NUMBER) saved = ToJsonNumber(value, function);
        else if (type == TYPE_TEXT) saved = value.as<std::string>();
        else if (type == TYPE_BOOL) saved = value.as<bool>();
        else {
            const Vector3 vector = value.as<Vector3>();
            saved = json::array({vector.x, vector.y, vector.z});
        }

        SaveSystem::Set(file, key, type, std::move(saved));
    }

    sol::object Load(const std::string& type, const sol::object& fileName, const sol::object& name,
                     const sol::object& fallback, const sol::this_state state) {
        const std::string function = "Saving.Load" + KindName(type);
        const std::string file = CheckFileName(fileName, function);
        const std::string key = CheckName(name, function);

        const bool hasFallback = fallback.get_type() != sol::type::lua_nil && fallback.get_type() != sol::type::none;
        if (hasFallback && !IsKind(fallback, type))
            throw sol::error(function + ": the default must be the same kind of value, got " + LuaTypeName(fallback));

        const json* saved = SaveSystem::Find(file, key);
        if (saved == nullptr) return hasFallback ? fallback : Zero(type, state);

        const std::string savedType = saved->value("type", "");
        if (savedType != type)
            throw sol::error(function + ": \"" + key + "\" in \"" + file + "\" was saved with Save" + KindName(savedType) +
                             " - load it with Load" + KindName(savedType));

        try {
            return ToLua(type, saved->at("value"), state);
        }
        catch (const json::exception&) {
            throw sol::error(function + ": \"" + key + "\" in \"" + file + "\" is damaged");
        }
    }

    void RegisterSavingMetadata() {
        std::vector<MethodDoc> methods;

        for (const auto& [kind, luaType, example] : {
                 std::tuple{"Number", "number", "a number (whole numbers stay whole)"},
                 std::tuple{"Text", "string", "a string"},
                 std::tuple{"Bool", "boolean", "true or false"},
                 std::tuple{"Vector3", "Vector3", "a Vector3"}}) {
            methods.push_back(Method(std::string("Save") + kind,
                {Param("fileName", "string"), Param("name", "string"), Param("value", luaType)}, {},
                std::string("Saves ") + example + " as `name` in the save file `fileName` (made if needed). Written to disk at the end of the frame."));
            methods.push_back(Method(std::string("Load") + kind,
                {Param("fileName", "string"), Param("name", "string"), Param("default", std::string(luaType) + "?")}, luaType,
                std::string("The value saved as `name` with Save") + kind + ", or `default` if there is none (" +
                (std::string(kind) == "Number" ? "0" : std::string(kind) == "Text" ? "\"\"" : std::string(kind) == "Bool" ? "false" : "Vector3(0, 0, 0)") +
                " without one). Raises an error if it was saved as another kind."));
        }

        methods.push_back(Method("HasKey", {Param("fileName", "string"), Param("name", "string")}, "boolean",
                                 "True if `name` is saved in the file."));
        methods.push_back(Method("DeleteKey", {Param("fileName", "string"), Param("name", "string")}, "boolean",
                                 "Removes `name` from the file. False if it wasn't there."));
        methods.push_back(Method("FileExists", {Param("fileName", "string")}, "boolean", "True if the save file exists."));
        methods.push_back(Method("DeleteFile", {Param("fileName", "string")}, "boolean",
                                 "Deletes the whole save file. False if there was none."));
        methods.push_back(Method("GetFiles", {}, "string[]", "The name of every save file, sorted."));

        RegisterType(GlobalTable("Saving",
            "Values that stay when the game closes, in named save files. Changes are written at the end of each frame.",
            {}, std::move(methods)));
    }
}

void LuaScriptSystem::RegisterSavingBindings(sol::state& lua) {
    RegisterSavingMetadata();

    sol::table saving = lua.create_named_table("Saving");

    for (const char* type : {TYPE_NUMBER, TYPE_TEXT, TYPE_BOOL, TYPE_VECTOR3}) {
        const std::string kind = KindName(type);

        saving.set_function("Save" + kind, [type](const sol::object fileName, const sol::object name, const sol::object value) {
            Save(type, fileName, name, value);
        });

        saving.set_function("Load" + kind, [type](const sol::object fileName, const sol::object name, const sol::object fallback,
                                                  const sol::this_state state) {
            return Load(type, fileName, name, fallback, state);
        });
    }

    saving.set_function("HasKey", [](const sol::object fileName, const sol::object name) {
        return SaveSystem::Find(CheckFileName(fileName, "Saving.HasKey"), CheckName(name, "Saving.HasKey")) != nullptr;
    });

    saving.set_function("DeleteKey", [](const sol::object fileName, const sol::object name) {
        return SaveSystem::DeleteKey(CheckFileName(fileName, "Saving.DeleteKey"), CheckName(name, "Saving.DeleteKey"));
    });

    saving.set_function("FileExists", [](const sol::object fileName) {
        return SaveSystem::FileExists(CheckFileName(fileName, "Saving.FileExists"));
    });

    saving.set_function("DeleteFile", [](const sol::object fileName) {
        return SaveSystem::DeleteSaveFile(CheckFileName(fileName, "Saving.DeleteFile"));
    });

    saving.set_function("GetFiles", []() {
        return sol::as_table(SaveSystem::GetFiles());
    });
}
