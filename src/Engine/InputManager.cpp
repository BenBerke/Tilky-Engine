#include "../../Headers/Engine/InputManager.hpp"
#include <algorithm>
#include <string>
#include <unordered_map>

#if TILKY_USE_IMGUI
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#endif

namespace {
    const bool* keyboardState = nullptr;
    bool prevKeyboardState[SDL_SCANCODE_COUNT] = {};

    SDL_MouseButtonFlags mouseState = 0;
    SDL_MouseButtonFlags prevMouseState = 0;

    Vector2 mousePosition{};
    Vector2 mouseDelta{};

    float mouseWheelScrollAmount = 0.0f;

    bool relativeMouseMode = false;

    bool quitRequested = false;

    std::vector<InputManager::DroppedFile> droppedFiles;

    bool externalDragActive = false;
    Vector2 externalDragPosition{};

    const std::unordered_map<SDL_Scancode, std::string> scancodeToString = {
        {SDL_SCANCODE_UNKNOWN, "Unknown"},

        // Usage page 0x07 (USB keyboard page)
        {SDL_SCANCODE_A, "A"},
        {SDL_SCANCODE_B, "B"},
        {SDL_SCANCODE_C, "C"},
        {SDL_SCANCODE_D, "D"},
        {SDL_SCANCODE_E, "E"},
        {SDL_SCANCODE_F, "F"},
        {SDL_SCANCODE_G, "G"},
        {SDL_SCANCODE_H, "H"},
        {SDL_SCANCODE_I, "I"},
        {SDL_SCANCODE_J, "J"},
        {SDL_SCANCODE_K, "K"},
        {SDL_SCANCODE_L, "L"},
        {SDL_SCANCODE_M, "M"},
        {SDL_SCANCODE_N, "N"},
        {SDL_SCANCODE_O, "O"},
        {SDL_SCANCODE_P, "P"},
        {SDL_SCANCODE_Q, "Q"},
        {SDL_SCANCODE_R, "R"},
        {SDL_SCANCODE_S, "S"},
        {SDL_SCANCODE_T, "T"},
        {SDL_SCANCODE_U, "U"},
        {SDL_SCANCODE_V, "V"},
        {SDL_SCANCODE_W, "W"},
        {SDL_SCANCODE_X, "X"},
        {SDL_SCANCODE_Y, "Y"},
        {SDL_SCANCODE_Z, "Z"},

        {SDL_SCANCODE_1, "1"},
        {SDL_SCANCODE_2, "2"},
        {SDL_SCANCODE_3, "3"},
        {SDL_SCANCODE_4, "4"},
        {SDL_SCANCODE_5, "5"},
        {SDL_SCANCODE_6, "6"},
        {SDL_SCANCODE_7, "7"},
        {SDL_SCANCODE_8, "8"},
        {SDL_SCANCODE_9, "9"},
        {SDL_SCANCODE_0, "0"},

        {SDL_SCANCODE_RETURN, "Return"},
        {SDL_SCANCODE_ESCAPE, "Escape"},
        {SDL_SCANCODE_BACKSPACE, "Backspace"},
        {SDL_SCANCODE_TAB, "Tab"},
        {SDL_SCANCODE_SPACE, "Space"},

        {SDL_SCANCODE_MINUS, "-"},
        {SDL_SCANCODE_EQUALS, "="},
        {SDL_SCANCODE_LEFTBRACKET, "["},
        {SDL_SCANCODE_RIGHTBRACKET, "]"},
        {SDL_SCANCODE_BACKSLASH, "\\"},
        {SDL_SCANCODE_NONUSHASH, "Non-US #"},
        {SDL_SCANCODE_SEMICOLON, ";"},
        {SDL_SCANCODE_APOSTROPHE, "'"},
        {SDL_SCANCODE_GRAVE, "`"},
        {SDL_SCANCODE_COMMA, ","},
        {SDL_SCANCODE_PERIOD, "."},
        {SDL_SCANCODE_SLASH, "/"},

        {SDL_SCANCODE_CAPSLOCK, "Caps Lock"},

        {SDL_SCANCODE_F1, "F1"},
        {SDL_SCANCODE_F2, "F2"},
        {SDL_SCANCODE_F3, "F3"},
        {SDL_SCANCODE_F4, "F4"},
        {SDL_SCANCODE_F5, "F5"},
        {SDL_SCANCODE_F6, "F6"},
        {SDL_SCANCODE_F7, "F7"},
        {SDL_SCANCODE_F8, "F8"},
        {SDL_SCANCODE_F9, "F9"},
        {SDL_SCANCODE_F10, "F10"},
        {SDL_SCANCODE_F11, "F11"},
        {SDL_SCANCODE_F12, "F12"},

        {SDL_SCANCODE_PRINTSCREEN, "Print Screen"},
        {SDL_SCANCODE_SCROLLLOCK, "Scroll Lock"},
        {SDL_SCANCODE_PAUSE, "Pause"},
        {SDL_SCANCODE_INSERT, "Insert"},
        {SDL_SCANCODE_HOME, "Home"},
        {SDL_SCANCODE_PAGEUP, "Page Up"},
        {SDL_SCANCODE_DELETE, "Delete"},
        {SDL_SCANCODE_END, "End"},
        {SDL_SCANCODE_PAGEDOWN, "Page Down"},
        {SDL_SCANCODE_RIGHT, "Right"},
        {SDL_SCANCODE_LEFT, "Left"},
        {SDL_SCANCODE_DOWN, "Down"},
        {SDL_SCANCODE_UP, "Up"},

        {SDL_SCANCODE_NUMLOCKCLEAR, "Num Lock"},
        {SDL_SCANCODE_KP_DIVIDE, "Keypad /"},
        {SDL_SCANCODE_KP_MULTIPLY, "Keypad *"},
        {SDL_SCANCODE_KP_MINUS, "Keypad -"},
        {SDL_SCANCODE_KP_PLUS, "Keypad +"},
        {SDL_SCANCODE_KP_ENTER, "Keypad Enter"},
        {SDL_SCANCODE_KP_1, "Keypad 1"},
        {SDL_SCANCODE_KP_2, "Keypad 2"},
        {SDL_SCANCODE_KP_3, "Keypad 3"},
        {SDL_SCANCODE_KP_4, "Keypad 4"},
        {SDL_SCANCODE_KP_5, "Keypad 5"},
        {SDL_SCANCODE_KP_6, "Keypad 6"},
        {SDL_SCANCODE_KP_7, "Keypad 7"},
        {SDL_SCANCODE_KP_8, "Keypad 8"},
        {SDL_SCANCODE_KP_9, "Keypad 9"},
        {SDL_SCANCODE_KP_0, "Keypad 0"},
        {SDL_SCANCODE_KP_PERIOD, "Keypad ."},

        {SDL_SCANCODE_NONUSBACKSLASH, "Non-US \\"},
        {SDL_SCANCODE_APPLICATION, "Application"},
        {SDL_SCANCODE_POWER, "Power"},
        {SDL_SCANCODE_KP_EQUALS, "Keypad ="},
        {SDL_SCANCODE_F13, "F13"},
        {SDL_SCANCODE_F14, "F14"},
        {SDL_SCANCODE_F15, "F15"},
        {SDL_SCANCODE_F16, "F16"},
        {SDL_SCANCODE_F17, "F17"},
        {SDL_SCANCODE_F18, "F18"},
        {SDL_SCANCODE_F19, "F19"},
        {SDL_SCANCODE_F20, "F20"},
        {SDL_SCANCODE_F21, "F21"},
        {SDL_SCANCODE_F22, "F22"},
        {SDL_SCANCODE_F23, "F23"},
        {SDL_SCANCODE_F24, "F24"},
        {SDL_SCANCODE_EXECUTE, "Execute"},
        {SDL_SCANCODE_HELP, "Help"},
        {SDL_SCANCODE_MENU, "Menu"},
        {SDL_SCANCODE_SELECT, "Select"},
        {SDL_SCANCODE_STOP, "Stop"},
        {SDL_SCANCODE_AGAIN, "Again"},
        {SDL_SCANCODE_UNDO, "Undo"},
        {SDL_SCANCODE_CUT, "Cut"},
        {SDL_SCANCODE_COPY, "Copy"},
        {SDL_SCANCODE_PASTE, "Paste"},
        {SDL_SCANCODE_FIND, "Find"},
        {SDL_SCANCODE_MUTE, "Mute"},
        {SDL_SCANCODE_VOLUMEUP, "Volume Up"},
        {SDL_SCANCODE_VOLUMEDOWN, "Volume Down"},
        {SDL_SCANCODE_KP_COMMA, "Keypad ,"},
        {SDL_SCANCODE_KP_EQUALSAS400, "Keypad = (AS400)"},

        {SDL_SCANCODE_INTERNATIONAL1, "International 1"},
        {SDL_SCANCODE_INTERNATIONAL2, "International 2"},
        {SDL_SCANCODE_INTERNATIONAL3, "International 3 (Yen)"},
        {SDL_SCANCODE_INTERNATIONAL4, "International 4"},
        {SDL_SCANCODE_INTERNATIONAL5, "International 5"},
        {SDL_SCANCODE_INTERNATIONAL6, "International 6"},
        {SDL_SCANCODE_INTERNATIONAL7, "International 7"},
        {SDL_SCANCODE_INTERNATIONAL8, "International 8"},
        {SDL_SCANCODE_INTERNATIONAL9, "International 9"},
        {SDL_SCANCODE_LANG1, "Lang 1 (Hangul/English)"},
        {SDL_SCANCODE_LANG2, "Lang 2 (Hanja)"},
        {SDL_SCANCODE_LANG3, "Lang 3 (Katakana)"},
        {SDL_SCANCODE_LANG4, "Lang 4 (Hiragana)"},
        {SDL_SCANCODE_LANG5, "Lang 5 (Zenkaku/Hankaku)"},
        {SDL_SCANCODE_LANG6, "Lang 6"},
        {SDL_SCANCODE_LANG7, "Lang 7"},
        {SDL_SCANCODE_LANG8, "Lang 8"},
        {SDL_SCANCODE_LANG9, "Lang 9"},

        {SDL_SCANCODE_ALTERASE, "Alt Erase"},
        {SDL_SCANCODE_SYSREQ, "SysReq"},
        {SDL_SCANCODE_CANCEL, "Cancel"},
        {SDL_SCANCODE_CLEAR, "Clear"},
        {SDL_SCANCODE_PRIOR, "Prior"},
        {SDL_SCANCODE_RETURN2, "Return 2"},
        {SDL_SCANCODE_SEPARATOR, "Separator"},
        {SDL_SCANCODE_OUT, "Out"},
        {SDL_SCANCODE_OPER, "Oper"},
        {SDL_SCANCODE_CLEARAGAIN, "Clear/Again"},
        {SDL_SCANCODE_CRSEL, "CrSel"},
        {SDL_SCANCODE_EXSEL, "ExSel"},

        {SDL_SCANCODE_KP_00, "Keypad 00"},
        {SDL_SCANCODE_KP_000, "Keypad 000"},
        {SDL_SCANCODE_THOUSANDSSEPARATOR, "Thousands Separator"},
        {SDL_SCANCODE_DECIMALSEPARATOR, "Decimal Separator"},
        {SDL_SCANCODE_CURRENCYUNIT, "Currency Unit"},
        {SDL_SCANCODE_CURRENCYSUBUNIT, "Currency Sub-unit"},
        {SDL_SCANCODE_KP_LEFTPAREN, "Keypad ("},
        {SDL_SCANCODE_KP_RIGHTPAREN, "Keypad )"},
        {SDL_SCANCODE_KP_LEFTBRACE, "Keypad {"},
        {SDL_SCANCODE_KP_RIGHTBRACE, "Keypad }"},
        {SDL_SCANCODE_KP_TAB, "Keypad Tab"},
        {SDL_SCANCODE_KP_BACKSPACE, "Keypad Backspace"},
        {SDL_SCANCODE_KP_A, "Keypad A"},
        {SDL_SCANCODE_KP_B, "Keypad B"},
        {SDL_SCANCODE_KP_C, "Keypad C"},
        {SDL_SCANCODE_KP_D, "Keypad D"},
        {SDL_SCANCODE_KP_E, "Keypad E"},
        {SDL_SCANCODE_KP_F, "Keypad F"},
        {SDL_SCANCODE_KP_XOR, "Keypad XOR"},
        {SDL_SCANCODE_KP_POWER, "Keypad ^"},
        {SDL_SCANCODE_KP_PERCENT, "Keypad %"},
        {SDL_SCANCODE_KP_LESS, "Keypad <"},
        {SDL_SCANCODE_KP_GREATER, "Keypad >"},
        {SDL_SCANCODE_KP_AMPERSAND, "Keypad &"},
        {SDL_SCANCODE_KP_DBLAMPERSAND, "Keypad &&"},
        {SDL_SCANCODE_KP_VERTICALBAR, "Keypad |"},
        {SDL_SCANCODE_KP_DBLVERTICALBAR, "Keypad ||"},
        {SDL_SCANCODE_KP_COLON, "Keypad :"},
        {SDL_SCANCODE_KP_HASH, "Keypad #"},
        {SDL_SCANCODE_KP_SPACE, "Keypad Space"},
        {SDL_SCANCODE_KP_AT, "Keypad @"},
        {SDL_SCANCODE_KP_EXCLAM, "Keypad !"},
        {SDL_SCANCODE_KP_MEMSTORE, "Keypad MemStore"},
        {SDL_SCANCODE_KP_MEMRECALL, "Keypad MemRecall"},
        {SDL_SCANCODE_KP_MEMCLEAR, "Keypad MemClear"},
        {SDL_SCANCODE_KP_MEMADD, "Keypad MemAdd"},
        {SDL_SCANCODE_KP_MEMSUBTRACT, "Keypad MemSubtract"},
        {SDL_SCANCODE_KP_MEMMULTIPLY, "Keypad MemMultiply"},
        {SDL_SCANCODE_KP_MEMDIVIDE, "Keypad MemDivide"},
        {SDL_SCANCODE_KP_PLUSMINUS, "Keypad +/-"},
        {SDL_SCANCODE_KP_CLEAR, "Keypad Clear"},
        {SDL_SCANCODE_KP_CLEARENTRY, "Keypad ClearEntry"},
        {SDL_SCANCODE_KP_BINARY, "Keypad Binary"},
        {SDL_SCANCODE_KP_OCTAL, "Keypad Octal"},
        {SDL_SCANCODE_KP_DECIMAL, "Keypad Decimal"},
        {SDL_SCANCODE_KP_HEXADECIMAL, "Keypad Hexadecimal"},

        {SDL_SCANCODE_LCTRL, "Left Ctrl"},
        {SDL_SCANCODE_LSHIFT, "Left Shift"},
        {SDL_SCANCODE_LALT, "Left Alt"},
        {SDL_SCANCODE_LGUI, "Left GUI"},
        {SDL_SCANCODE_RCTRL, "Right Ctrl"},
        {SDL_SCANCODE_RSHIFT, "Right Shift"},
        {SDL_SCANCODE_RALT, "Right Alt"},
        {SDL_SCANCODE_RGUI, "Right GUI"},

        {SDL_SCANCODE_MODE, "Mode"},

        // Usage page 0x0C (USB consumer page)
        {SDL_SCANCODE_SLEEP, "Sleep"},
        {SDL_SCANCODE_WAKE, "Wake"},

        {SDL_SCANCODE_CHANNEL_INCREMENT, "Channel Up"},
        {SDL_SCANCODE_CHANNEL_DECREMENT, "Channel Down"},

        {SDL_SCANCODE_MEDIA_PLAY, "Media Play"},
        {SDL_SCANCODE_MEDIA_PAUSE, "Media Pause"},
        {SDL_SCANCODE_MEDIA_RECORD, "Media Record"},
        {SDL_SCANCODE_MEDIA_FAST_FORWARD, "Media Fast Forward"},
        {SDL_SCANCODE_MEDIA_REWIND, "Media Rewind"},
        {SDL_SCANCODE_MEDIA_NEXT_TRACK, "Media Next Track"},
        {SDL_SCANCODE_MEDIA_PREVIOUS_TRACK, "Media Previous Track"},
        {SDL_SCANCODE_MEDIA_STOP, "Media Stop"},
        {SDL_SCANCODE_MEDIA_EJECT, "Media Eject"},
        {SDL_SCANCODE_MEDIA_PLAY_PAUSE, "Media Play/Pause"},
        {SDL_SCANCODE_MEDIA_SELECT, "Media Select"},

        {SDL_SCANCODE_AC_NEW, "AC New"},
        {SDL_SCANCODE_AC_OPEN, "AC Open"},
        {SDL_SCANCODE_AC_CLOSE, "AC Close"},
        {SDL_SCANCODE_AC_EXIT, "AC Exit"},
        {SDL_SCANCODE_AC_SAVE, "AC Save"},
        {SDL_SCANCODE_AC_PRINT, "AC Print"},
        {SDL_SCANCODE_AC_PROPERTIES, "AC Properties"},

        {SDL_SCANCODE_AC_SEARCH, "AC Search"},
        {SDL_SCANCODE_AC_HOME, "AC Home"},
        {SDL_SCANCODE_AC_BACK, "AC Back"},
        {SDL_SCANCODE_AC_FORWARD, "AC Forward"},
        {SDL_SCANCODE_AC_STOP, "AC Stop"},
        {SDL_SCANCODE_AC_REFRESH, "AC Refresh"},
        {SDL_SCANCODE_AC_BOOKMARKS, "AC Bookmarks"},

        // Mobile keys
        {SDL_SCANCODE_SOFTLEFT, "Soft Left"},
        {SDL_SCANCODE_SOFTRIGHT, "Soft Right"},
        {SDL_SCANCODE_CALL, "Call"},
        {SDL_SCANCODE_ENDCALL, "End Call"},
    };
}

namespace InputManager {
    void Initialize(SDL_Window* window) {
        SDL_PumpEvents();

        keyboardState = SDL_GetKeyboardState(nullptr);
        std::copy_n(keyboardState, SDL_SCANCODE_COUNT, prevKeyboardState);

        mouseState = SDL_GetMouseState(&mousePosition.x, &mousePosition.y);
        prevMouseState = mouseState;

        mouseDelta = {0.0f, 0.0f};

#if TILKY_USE_IMGUI
        if (!SDL_TextInputActive(window))
            if (!SDL_StartTextInput(window))
                SDL_Log("SDL_StartTextInput failed: %s", SDL_GetError());
#endif
    }

    void BeginFrame() {
        mouseWheelScrollAmount = 0.0f;
        quitRequested = false;

        if (!droppedFiles.empty()) [[unlikely]] droppedFiles.clear();

        // Save previous frame input
        std::copy_n(keyboardState, SDL_SCANCODE_COUNT, prevKeyboardState);
        prevMouseState = mouseState;

        const Vector2 previousMousePosition = mousePosition;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#if TILKY_USE_IMGUI
            if (ImGui::GetCurrentContext() != nullptr) ImGui_ImplSDL3_ProcessEvent(&event);

#endif

            switch (event.type) {
                case SDL_EVENT_MOUSE_WHEEL: mouseWheelScrollAmount += event.wheel.y; break;
                case SDL_EVENT_QUIT: quitRequested = true; break;
                // The engine keeps a hidden window next to the visible one,
                // so closing a window doesn't end SDL; treat it as a quit.
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: quitRequested = true; break;
                case SDL_EVENT_DROP_BEGIN: externalDragActive = true; break;

                case SDL_EVENT_DROP_POSITION:
                    externalDragActive = true;
                    externalDragPosition = {event.drop.x,event.drop.y};
                    break;

                case SDL_EVENT_DROP_FILE:
                    externalDragPosition = {
                        event.drop.x,
                        event.drop.y
                    };

                    if (event.drop.data != nullptr) {
                        droppedFiles.push_back({
                            std::filesystem::path(event.drop.data),
                            externalDragPosition
                        });
                    }
                    break;

                case SDL_EVENT_DROP_COMPLETE: externalDragActive = false; break;
                default: break;
            }
        }

        keyboardState = SDL_GetKeyboardState(nullptr);
        mouseState = SDL_GetMouseState(&mousePosition.x, &mousePosition.y);

        if (relativeMouseMode) {
            float dx = 0.0f;
            float dy = 0.0f;

            mouseState = SDL_GetRelativeMouseState(&dx, &dy);

            mouseDelta = {dx, dy};
        }
        else {
            mouseDelta = {
                mousePosition.x - previousMousePosition.x,
                mousePosition.y - previousMousePosition.y
            };
        }
    }

    bool QuitRequested() {
        return quitRequested;
    }

    bool GetKeyDown(const SDL_Scancode key) {
        return keyboardState[key] && !prevKeyboardState[key];
    }

    bool GetKey(const SDL_Scancode key) {
        return keyboardState[key];
    }

    bool GetKeyUp(const SDL_Scancode key) {
        return !keyboardState[key] && prevKeyboardState[key];
    }

    bool GetDoubleKeyDown(const SDL_Scancode key, const SDL_Scancode keyTwo) {
        const bool bothHeld = GetKey(key) && GetKey(keyTwo);
        const bool eitherFirstFrame = GetKeyDown(key) || GetKeyDown(keyTwo);
        return bothHeld && eitherFirstFrame;
    }

    bool GetDoubleKey(const SDL_Scancode key, const SDL_Scancode keyTwo) {
        return GetKey(key) && GetKey(keyTwo);
    }

    bool GetMouseButtonDown(const Uint32 button) {
        return (mouseState & SDL_BUTTON_MASK(button)) && !(prevMouseState & SDL_BUTTON_MASK(button));
    }

    bool GetMouseButton(const Uint32 button) {
        return (mouseState & SDL_BUTTON_MASK(button)) != 0;
    }

    bool GetMouseButtonUp(Uint32 button) {
        return !(mouseState & SDL_BUTTON_MASK(button)) && (prevMouseState & SDL_BUTTON_MASK(button));
    }

    SDL_Scancode GetAnyKey() {
        if (!keyboardState) return SDL_SCANCODE_UNKNOWN;

        for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) if (keyboardState[i]) return static_cast<SDL_Scancode>(i);
        return SDL_SCANCODE_UNKNOWN;
    }

    SDL_Scancode GetAnyKeyDown() {
        for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) if (keyboardState[i] && !prevKeyboardState[i])
            return static_cast<SDL_Scancode>(i);
        return SDL_SCANCODE_UNKNOWN;
    }

    SDL_Scancode GetAnyKeyUp() {
        for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) if (!keyboardState[i] && prevKeyboardState[i])
            return static_cast<SDL_Scancode>(i);
        return SDL_SCANCODE_UNKNOWN;
    }

    bool GetMouseWheelScrollUp() {return mouseWheelScrollAmount > 0; }

    bool GetMouseWheelScrollDown() {return mouseWheelScrollAmount < 0;}

    float GetMouseWheelScroll() {return mouseWheelScrollAmount;}

    Vector2 GetMousePosition() {return mousePosition;}

    Vector2 GetMouseDelta() {return mouseDelta;}

    namespace {
        CursorLockMode cursorLockMode = CursorLockMode::Locked;
        bool cursorVisible = false;
    }

    void SetCursorLockMode(const CursorLockMode mode) {cursorLockMode = mode;}

    CursorLockMode GetCursorLockMode() {return cursorLockMode;}

    void SetCursorVisible(const bool visible) {cursorVisible = visible;}

    bool IsCursorVisible() {return cursorVisible;}

    void ResetCursor() {
        cursorLockMode = CursorLockMode::Locked;
        cursorVisible = false;
    }

    void SetRelativeMouseMode(SDL_Window* window, const bool enabled) {
        relativeMouseMode = enabled;

        SDL_SetWindowRelativeMouseMode(window, enabled);

        float dx = .0f;
        float dy = .0f;
        SDL_GetRelativeMouseState(&dx, &dy);

        mouseDelta = {.0f, .0f};
    }

    const std::vector<DroppedFile>& GetDroppedFiles() {
        return droppedFiles;
    }

    bool IsExternalDragActive() {
        return externalDragActive;
    }

    Vector2 GetExternalDragPosition() {
        return externalDragPosition;
    }

    const std::string& ScancodeToString(const SDL_Scancode code)
    {
        static const std::string unknown = "Unknown";
        auto it = scancodeToString.find(code);
        return it != scancodeToString.end() ? it->second : unknown;
    }
}