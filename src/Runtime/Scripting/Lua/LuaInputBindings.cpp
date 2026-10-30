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

        // Punctuation, named after the US-layout key (the scancode is the
        // key's place, whatever the layout prints on it).
        {"Minus", SDL_SCANCODE_MINUS},
        {"Equals", SDL_SCANCODE_EQUALS},
        {"LeftBracket", SDL_SCANCODE_LEFTBRACKET},
        {"RightBracket", SDL_SCANCODE_RIGHTBRACKET},
        {"Backslash", SDL_SCANCODE_BACKSLASH},
        {"Semicolon", SDL_SCANCODE_SEMICOLON},
        {"Quote", SDL_SCANCODE_APOSTROPHE},
        {"BackQuote", SDL_SCANCODE_GRAVE},
        {"Comma", SDL_SCANCODE_COMMA},
        {"Period", SDL_SCANCODE_PERIOD},
        {"Slash", SDL_SCANCODE_SLASH},
        {"CapsLock", SDL_SCANCODE_CAPSLOCK},

        {"F1", SDL_SCANCODE_F1}, {"F2", SDL_SCANCODE_F2}, {"F3", SDL_SCANCODE_F3}, {"F4", SDL_SCANCODE_F4},
        {"F5", SDL_SCANCODE_F5}, {"F6", SDL_SCANCODE_F6}, {"F7", SDL_SCANCODE_F7}, {"F8", SDL_SCANCODE_F8},
        {"F9", SDL_SCANCODE_F9}, {"F10", SDL_SCANCODE_F10}, {"F11", SDL_SCANCODE_F11}, {"F12", SDL_SCANCODE_F12},

        {"PrintScreen", SDL_SCANCODE_PRINTSCREEN},
        {"ScrollLock", SDL_SCANCODE_SCROLLLOCK},
        {"Pause", SDL_SCANCODE_PAUSE},
        {"Insert", SDL_SCANCODE_INSERT},
        {"Home", SDL_SCANCODE_HOME},
        {"PageUp", SDL_SCANCODE_PAGEUP},
        {"Delete", SDL_SCANCODE_DELETE},
        {"End", SDL_SCANCODE_END},
        {"PageDown", SDL_SCANCODE_PAGEDOWN},

        {"Right", SDL_SCANCODE_RIGHT},
        {"Left", SDL_SCANCODE_LEFT},
        {"Down", SDL_SCANCODE_DOWN},
        {"Up", SDL_SCANCODE_UP},

        {"NumLock", SDL_SCANCODE_NUMLOCKCLEAR},
        {"KeypadDivide", SDL_SCANCODE_KP_DIVIDE},
        {"KeypadMultiply", SDL_SCANCODE_KP_MULTIPLY},
        {"KeypadMinus", SDL_SCANCODE_KP_MINUS},
        {"KeypadPlus", SDL_SCANCODE_KP_PLUS},
        {"KeypadEnter", SDL_SCANCODE_KP_ENTER},
        {"Keypad1", SDL_SCANCODE_KP_1}, {"Keypad2", SDL_SCANCODE_KP_2}, {"Keypad3", SDL_SCANCODE_KP_3},
        {"Keypad4", SDL_SCANCODE_KP_4}, {"Keypad5", SDL_SCANCODE_KP_5}, {"Keypad6", SDL_SCANCODE_KP_6},
        {"Keypad7", SDL_SCANCODE_KP_7}, {"Keypad8", SDL_SCANCODE_KP_8}, {"Keypad9", SDL_SCANCODE_KP_9},
        {"Keypad0", SDL_SCANCODE_KP_0},
        {"KeypadPeriod", SDL_SCANCODE_KP_PERIOD},

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
            Prop("MouseBack", "integer", true, "The side button nearer the user (mouse button 4)."),
            Prop("MouseForward", "integer", true, "The side button further from the user (mouse button 5)."),
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
            Method("GetMousePosition", {}, "Vector2", "The cursor position in window pixels, from the top-left corner."),
            Method("GetMouseDelta", {}, "Vector2",
                   "How far the mouse moved since the last frame, in pixels: +x right, +y down. Works whether the cursor is locked or not."),
            Method("GetMouseScroll", {}, "number",
                   "How far the scroll wheel turned this frame: positive away from you (up), negative towards you, 0 when still. One notch is 1 on most mice."),
        }));

        RegisterType(Enum("CursorLockMode", "Values for Cursor.lockState.", {
            {"None", static_cast<int>(InputManager::CursorLockMode::None), "Free, and can leave the window."},
            {"Locked", static_cast<int>(InputManager::CursorLockMode::Locked), "Hidden and held in place: the mouse only turns the view. Player Controller mouse look needs this."},
            {"Confined", static_cast<int>(InputManager::CursorLockMode::Confined), "Free, but kept inside the window."},
        }));

        RegisterType(GlobalTable("Cursor", "The mouse cursor, like Unity's Cursor. Each level starts it from World Settings > Cursor (locked and hidden by default).", {
            Prop("lockState", "CursorLockMode", false,
                 "CursorLockMode.Locked, None or Confined. Player Controller mouse look only runs while Locked."),
            Prop("visible", "boolean", false, "Whether the cursor is drawn while not Locked. A Locked cursor is always hidden."),
        }));
    }

    struct ScriptCursor {};

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
    inputManager["MouseBack"] = SDL_BUTTON_X1;
    inputManager["MouseForward"] = SDL_BUTTON_X2;

    inputManager.set_function("GetMouseDelta", []() -> Vector2 {
        return InputManager::GetMouseDelta();
    });

    inputManager.set_function("GetMouseScroll", []() -> float {
        return InputManager::GetMouseWheelScroll();
    });

    lua["Input"] = inputManager;

    sol::table lockModes = lua.create_named_table("CursorLockMode");
    lockModes["None"] = static_cast<int>(InputManager::CursorLockMode::None);
    lockModes["Locked"] = static_cast<int>(InputManager::CursorLockMode::Locked);
    lockModes["Confined"] = static_cast<int>(InputManager::CursorLockMode::Confined);

    lua.new_usertype<ScriptCursor>(
        "ScriptCursor",
        sol::no_constructor,

        "lockState",
        sol::property(
            [](const ScriptCursor&) { return static_cast<int>(InputManager::GetCursorLockMode()); },
            [](ScriptCursor&, const sol::object& value) {
                const int mode = value.get_type() == sol::type::number ? value.as<int>() : -1;
                if (mode < static_cast<int>(InputManager::CursorLockMode::None) ||
                    mode > static_cast<int>(InputManager::CursorLockMode::Confined))
                    throw sol::error("Cursor.lockState must be a CursorLockMode value, e.g. CursorLockMode.None");

                InputManager::SetCursorLockMode(static_cast<InputManager::CursorLockMode>(mode));
            }),

        "visible",
        sol::property(
            [](const ScriptCursor&) { return InputManager::IsCursorVisible(); },
            [](ScriptCursor&, const sol::object& value) {
                if (value.get_type() != sol::type::boolean) throw sol::error("Cursor.visible must be true or false");
                InputManager::SetCursorVisible(value.as<bool>());
            })
    );

    lua["Cursor"] = ScriptCursor{};
}
