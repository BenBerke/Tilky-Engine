#include "../../Headers/Engine/InputManager.hpp"
#include <algorithm>

#include "imgui_impl_sdl3.h"

#if WOLFY_USE_IMGUI
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
}

namespace InputManager {

    void BeginFrame() {
        mouseWheelScrollAmount = 0.0f;
        // First time setup
        if (!keyboardState) {
            SDL_PumpEvents();

            keyboardState = SDL_GetKeyboardState(nullptr);
            std::copy_n(keyboardState, SDL_SCANCODE_COUNT, prevKeyboardState);

            mouseState = SDL_GetMouseState(&mousePosition.x, &mousePosition.y);
            prevMouseState = mouseState;

            mouseDelta = {0.0f, 0.0f};
        }

        // Save previous frame input
        std::copy_n(keyboardState, SDL_SCANCODE_COUNT, prevKeyboardState);
        prevMouseState = mouseState;

        const Vector2 previousMousePosition = mousePosition;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (ImGui::GetCurrentContext() != nullptr) {
                ImGui_ImplSDL3_ProcessEvent(&event);
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                mouseWheelScrollAmount += event.wheel.y;
            }

            if (event.type == SDL_EVENT_QUIT) {
                quitRequested = true;
            }
        }

        keyboardState = SDL_GetKeyboardState(nullptr);
        mouseState = SDL_GetMouseState(&mousePosition.x, &mousePosition.y);

        if (relativeMouseMode) {
            float dx = 0.0f;
            float dy = 0.0f;

            mouseState = SDL_GetRelativeMouseState(&dx, &dy);

            mouseDelta = {
                dx,
                dy
            };
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

    bool GetMouseButtonDown(const Uint32 button) {
        return (mouseState & SDL_BUTTON_MASK(button)) &&
               !(prevMouseState & SDL_BUTTON_MASK(button));
    }

    bool GetMouseButton(const Uint32 button) {
        return (mouseState & SDL_BUTTON_MASK(button)) != 0;
    }

    bool GetMouseButtonUp(Uint32 button) {
        return !(mouseState & SDL_BUTTON_MASK(button)) &&
                (prevMouseState & SDL_BUTTON_MASK(button));
    }

    bool GetMouseWheelScrollUp() {
         return mouseWheelScrollAmount > 0;
    }

    bool GetMouseWheelScrollDown() {
        return mouseWheelScrollAmount < 0;
    }

    float GetMouseWheelScroll() {
        return mouseWheelScrollAmount;
    }

    Vector2 GetMousePosition() {
        return mousePosition;
    }

    Vector2 GetMouseDelta() {
        return mouseDelta;
    }

    void SetRelativeMouseMode(SDL_Window* window, const bool enabled) {
        relativeMouseMode = enabled;

        SDL_SetWindowRelativeMouseMode(window, enabled);

        float dx = 0.0f;
        float dy = 0.0f;
        SDL_GetRelativeMouseState(&dx, &dy);

        mouseDelta = {0.0f, 0.0f};
    }
}