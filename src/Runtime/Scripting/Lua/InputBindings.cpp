//
// Created by berke on 6/20/2026.
//

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Runtime/Scripting/ScriptSystem.hpp"
#include "sol/sol.hpp"

namespace {
    const std::unordered_map<std::string, SDL_Scancode> keys = {
        {"A", SDL_SCANCODE_A},
        {"B", SDL_SCANCODE_B},
        {"C", SDL_SCANCODE_C},
        {"D", SDL_SCANCODE_D},
        {"E", SDL_SCANCODE_E},
        {"F", SDL_SCANCODE_F},
        {"G", SDL_SCANCODE_G},
        {"H", SDL_SCANCODE_H},
        {"I", SDL_SCANCODE_I},
        {"J", SDL_SCANCODE_J},
        {"K", SDL_SCANCODE_K},
        {"L", SDL_SCANCODE_L},
        {"M", SDL_SCANCODE_M},
        {"N", SDL_SCANCODE_N},
        {"O", SDL_SCANCODE_O},
        {"P", SDL_SCANCODE_P},
        {"Q", SDL_SCANCODE_Q},
        {"R", SDL_SCANCODE_R},
        {"S", SDL_SCANCODE_S},
        {"T", SDL_SCANCODE_T},
        {"U", SDL_SCANCODE_U},
        {"V", SDL_SCANCODE_V},
        {"W", SDL_SCANCODE_W},
        {"X", SDL_SCANCODE_X},
        {"Y", SDL_SCANCODE_Y},
        {"Z", SDL_SCANCODE_Z},

        {"Space", SDL_SCANCODE_SPACE},
        {"Escape", SDL_SCANCODE_ESCAPE},
        {"Enter", SDL_SCANCODE_RETURN},
        {"Tab", SDL_SCANCODE_TAB},
        {"Backspace", SDL_SCANCODE_BACKSPACE},

        {"Left", SDL_SCANCODE_LEFT},
        {"Right", SDL_SCANCODE_RIGHT},
        {"Up", SDL_SCANCODE_UP},
        {"Down", SDL_SCANCODE_DOWN},

        {"LShift", SDL_SCANCODE_LSHIFT},
        {"RShift", SDL_SCANCODE_RSHIFT},
        {"LCtrl", SDL_SCANCODE_LCTRL},
        {"RCtrl", SDL_SCANCODE_RCTRL},
        {"LAlt", SDL_SCANCODE_LALT},
        {"RAlt", SDL_SCANCODE_RALT},

        {"1", SDL_SCANCODE_1},
        {"2", SDL_SCANCODE_2},
        {"3", SDL_SCANCODE_3},
        {"4", SDL_SCANCODE_4},
        {"5", SDL_SCANCODE_5},
        {"6", SDL_SCANCODE_6},
        {"7", SDL_SCANCODE_7},
        {"8", SDL_SCANCODE_8},
        {"9", SDL_SCANCODE_9},
        {"0", SDL_SCANCODE_0},
    };

    SDL_Scancode GetScancodeFromString(const std::string& key) {
        const auto it = keys.find(key);

        if (it != keys.end()) return it->second;

        return SDL_SCANCODE_UNKNOWN;
    }
}


void ScriptSystem::RegisterInputBindings(sol::state& lua) {
    sol::table inputManager = lua.create_table();

    inputManager.set_function("GetKeyDown", [](const std::string& key) -> bool {
        const SDL_Scancode scancode = GetScancodeFromString(key);

        if (scancode == SDL_SCANCODE_UNKNOWN) return false;

        return InputManager::GetKeyDown(scancode);
    });

    inputManager.set_function("GetKey", [](const std::string& key) -> bool {
        const SDL_Scancode scancode = GetScancodeFromString(key);

        if (scancode == SDL_SCANCODE_UNKNOWN) return false;

        return InputManager::GetKey(scancode);
    });

    inputManager.set_function("GetKeyUp", [](const std::string& key) -> bool {
        const SDL_Scancode scancode = GetScancodeFromString(key);

        if (scancode == SDL_SCANCODE_UNKNOWN) return false;

        return InputManager::GetKeyUp(scancode);
    });

    inputManager.set_function("GetAnyKey", []() -> std::string {
        for (const auto& [key, val] : keys)
            if (val == InputManager::GetAnyKey()) return key;

        return "UNKNOWN";
   });

    inputManager.set_function("GetAnyKeyDown", []() -> std::string {
        for (const auto &[key, val]: keys)
            if (val == InputManager::GetAnyKeyDown()) return key;

        return "UNKNOWN";
    });

    inputManager.set_function("GetAnyKeyUp", []() -> std::string {
        for (const auto &[key, val]: keys)
            if (val == InputManager::GetAnyKeyUp()) return key;

        return "UNKNOWN";
    });

    inputManager.set_function("GetDoubleKeyDown", [](const std::string& key, const std::string& keyTwo) -> bool {
        const SDL_Scancode scancode = GetScancodeFromString(key);
        const SDL_Scancode scancodeTwo = GetScancodeFromString(keyTwo);

        if (scancode == SDL_SCANCODE_UNKNOWN) return false;

        return InputManager::GetDoubleKeyDown(scancodeTwo, scancode);
    });

    inputManager.set_function("GetDoubleKey", [](const std::string& key, const std::string& keyTwo) -> bool {
        const SDL_Scancode scancode = GetScancodeFromString(key);
        const SDL_Scancode scancodeTwo = GetScancodeFromString(keyTwo);

        if (scancode == SDL_SCANCODE_UNKNOWN) return false;

        for (const auto& [key, val] : keys) {

        }
        return InputManager::GetDoubleKey(scancodeTwo, scancode);
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