//
// Created by berke on 6/20/2026.
//

#include "Headers/Engine/InputManager.hpp"
#include "../../../../Headers/Runtime/Scripting/Lua/LuaScripting.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "sol/sol.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace {
    // Every key scripts can ask about, in scancode order: letters, digits,
    // then the rest. Its value in the Lua `Key` table is its SDL scancode, so
    // a Key value goes straight to InputManager. The GetAnyKey functions
    // return the first key of this list that matches.
    struct LuaKey {
        const char* name;
        SDL_Scancode scancode;
    };

    constexpr LuaKey LUA_KEYS[] = {
        {"A", SDL_SCANCODE_A}, {"B", SDL_SCANCODE_B}, {"C", SDL_SCANCODE_C}, {"D", SDL_SCANCODE_D},
        {"E", SDL_SCANCODE_E}, {"F", SDL_SCANCODE_F}, {"G", SDL_SCANCODE_G}, {"H", SDL_SCANCODE_H},
        {"I", SDL_SCANCODE_I}, {"J", SDL_SCANCODE_J}, {"K", SDL_SCANCODE_K}, {"L", SDL_SCANCODE_L},
        {"M", SDL_SCANCODE_M}, {"N", SDL_SCANCODE_N}, {"O", SDL_SCANCODE_O}, {"P", SDL_SCANCODE_P},
        {"Q", SDL_SCANCODE_Q}, {"R", SDL_SCANCODE_R}, {"S", SDL_SCANCODE_S}, {"T", SDL_SCANCODE_T},
        {"U", SDL_SCANCODE_U}, {"V", SDL_SCANCODE_V}, {"W", SDL_SCANCODE_W}, {"X", SDL_SCANCODE_X},
        {"Y", SDL_SCANCODE_Y}, {"Z", SDL_SCANCODE_Z},

        // The number row. Not "Num", which would read as the numpad.
        {"Alpha1", SDL_SCANCODE_1}, {"Alpha2", SDL_SCANCODE_2}, {"Alpha3", SDL_SCANCODE_3},
        {"Alpha4", SDL_SCANCODE_4}, {"Alpha5", SDL_SCANCODE_5}, {"Alpha6", SDL_SCANCODE_6},
        {"Alpha7", SDL_SCANCODE_7}, {"Alpha8", SDL_SCANCODE_8}, {"Alpha9", SDL_SCANCODE_9},
        {"Alpha0", SDL_SCANCODE_0},

        {"Enter", SDL_SCANCODE_RETURN},
        {"Escape", SDL_SCANCODE_ESCAPE},
        {"Backspace", SDL_SCANCODE_BACKSPACE},
        {"Tab", SDL_SCANCODE_TAB},
        {"Space", SDL_SCANCODE_SPACE},

        {"Right", SDL_SCANCODE_RIGHT},
        {"Left", SDL_SCANCODE_LEFT},
        {"Down", SDL_SCANCODE_DOWN},
        {"Up", SDL_SCANCODE_UP},

        {"LCtrl", SDL_SCANCODE_LCTRL},
        {"LShift", SDL_SCANCODE_LSHIFT},
        {"LAlt", SDL_SCANCODE_LALT},
        {"RCtrl", SDL_SCANCODE_RCTRL},
        {"RShift", SDL_SCANCODE_RSHIFT},
        {"RAlt", SDL_SCANCODE_RALT},
    };

    using namespace LuaBindingMetadata;

    void RegisterInputMetadata() {
        std::vector<EnumValueDoc> keys;
        for (const LuaKey& key : LUA_KEYS) keys.push_back({key.name, key.scancode, {}});

        RegisterType(Enum("Key", "Keyboard keys for the Input functions, e.g. Input.GetKeyDown(Key.Space).", std::move(keys)));

        RegisterType(GlobalTable("Input", "Global keyboard/mouse input table. Keys come from the Key table.", {
            Prop("MouseLeft", "integer", true),
            Prop("MouseMiddle", "integer", true),
            Prop("MouseRight", "integer", true),
        }, {
            Method("GetKeyDown", {Param("key", "Key")}, "boolean", "True on the frame the key was pressed."),
            Method("GetKey", {Param("key", "Key")}, "boolean", "True while the key is held."),
            Method("GetKeyUp", {Param("key", "Key")}, "boolean", "True on the frame the key was released."),
            Method("GetAnyKey", {}, "Key?", "A held key, or nil."),
            Method("GetAnyKeyDown", {}, "Key?", "A key pressed this frame, or nil."),
            Method("GetAnyKeyUp", {}, "Key?", "A key released this frame, or nil."),
            Method("GetKeyName", {Param("key", "Key")}, "string", "The key's name in the Key table, e.g. \"Space\" for Key.Space."),
            Method("GetDoubleKeyDown", {Param("key", "Key"), Param("keyTwo", "Key")}, "boolean"),
            Method("GetDoubleKey", {Param("key", "Key"), Param("keyTwo", "Key")}, "boolean"),
            Method("GetMouseButtonDown", {Param("button", "integer")}, "boolean"),
            Method("GetMouseButton", {Param("button", "integer")}, "boolean"),
            Method("GetMouseButtonUp", {Param("button", "integer")}, "boolean"),
            Method("GetMousePosition", {}, "Vector2"),
        }));
    }

    // Raises a Lua error for anything that isn't a value of the Key table.
    // Takes a sol::object, not an int: sol would turn nil (a misspelled
    // Key.X) or a string into 0 and hide the mistake.
    SDL_Scancode ToScancode(const sol::object& value) {
        if (value.get_type() != sol::type::number)
            throw sol::error(std::string("Expected a Key value, e.g. Key.Space, got ") +
                             sol::type_name(value.lua_state(), value.get_type()));

        const int key = value.as<int>();

        static const std::unordered_set<int> validKeys = [] {
            std::unordered_set<int> set;
            for (const LuaKey& luaKey : LUA_KEYS) set.insert(luaKey.scancode);
            return set;
        }();

        if (!validKeys.contains(key))
            throw sol::error("Unknown key " + std::to_string(key) + " - use a value from the Key table, e.g. Key.Space");

        return static_cast<SDL_Scancode>(key);
    }

    // The first key of LUA_KEYS that `matches`, or nil.
    template<typename Predicate>
    sol::object FirstKey(const sol::this_state state, Predicate matches) {
        for (const LuaKey& key : LUA_KEYS)
            if (matches(key.scancode)) return sol::make_object(state, static_cast<int>(key.scancode));

        return sol::make_object(state, sol::nil);
    }
}


std::vector<ScriptEnumOption> LuaScriptSystem::KeyEnumOptions() {
    std::vector<ScriptEnumOption> options;
    for (const LuaKey& key : LUA_KEYS) options.push_back({key.name, key.scancode});
    return options;
}

void LuaScriptSystem::RegisterInputBindings(sol::state& lua) {
    RegisterInputMetadata();

    sol::table key = lua.create_named_table("Key");
    for (const LuaKey& luaKey : LUA_KEYS) key[luaKey.name] = static_cast<int>(luaKey.scancode);

    sol::table inputManager = lua.create_table();

    inputManager.set_function("GetKeyDown", [](const sol::object& key) -> bool {
        return InputManager::GetKeyDown(ToScancode(key));
    });

    inputManager.set_function("GetKey", [](const sol::object& key) -> bool {
        return InputManager::GetKey(ToScancode(key));
    });

    inputManager.set_function("GetKeyUp", [](const sol::object& key) -> bool {
        return InputManager::GetKeyUp(ToScancode(key));
    });

    inputManager.set_function("GetAnyKey", [](const sol::this_state state) -> sol::object {
        return FirstKey(state, [](const SDL_Scancode scancode) { return InputManager::GetKey(scancode); });
    });

    inputManager.set_function("GetAnyKeyDown", [](const sol::this_state state) -> sol::object {
        return FirstKey(state, [](const SDL_Scancode scancode) { return InputManager::GetKeyDown(scancode); });
    });

    inputManager.set_function("GetAnyKeyUp", [](const sol::this_state state) -> sol::object {
        return FirstKey(state, [](const SDL_Scancode scancode) { return InputManager::GetKeyUp(scancode); });
    });

    inputManager.set_function("GetKeyName", [](const sol::object& key) -> std::string {
        const SDL_Scancode scancode = ToScancode(key);

        for (const LuaKey& luaKey : LUA_KEYS)
            if (luaKey.scancode == scancode) return luaKey.name;

        return {};
    });

    inputManager.set_function("GetDoubleKeyDown", [](const sol::object& key, const sol::object& keyTwo) -> bool {
        return InputManager::GetDoubleKeyDown(ToScancode(key), ToScancode(keyTwo));
    });

    inputManager.set_function("GetDoubleKey", [](const sol::object& key, const sol::object& keyTwo) -> bool {
        return InputManager::GetDoubleKey(ToScancode(key), ToScancode(keyTwo));
    });

    inputManager.set_function("GetMouseButtonDown", [](const int button) -> bool {
        return InputManager::GetMouseButtonDown(button);
    });

    inputManager.set_function("GetMouseButton", [](const int button) -> bool {
        return InputManager::GetMouseButton(button);
    });

    inputManager.set_function("GetMouseButtonUp", [](const int button) -> bool {
        return InputManager::GetMouseButtonUp(button);
    });

    inputManager.set_function("GetMousePosition", []() -> Vector2 {
        return InputManager::GetMousePosition();
    });

    inputManager["MouseLeft"] = SDL_BUTTON_LEFT;
    inputManager["MouseMiddle"] = SDL_BUTTON_MIDDLE;
    inputManager["MouseRight"] = SDL_BUTTON_RIGHT;

    lua["Input"] = inputManager;
}
