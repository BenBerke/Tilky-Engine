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

    bool quitRequested = false;
}

namespace InputManager {

    void BeginFrame() {
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

            if (event.type == SDL_EVENT_QUIT) {
                quitRequested = true;
            }
        }

        keyboardState = SDL_GetKeyboardState(nullptr);
        mouseState = SDL_GetMouseState(&mousePosition.x, &mousePosition.y);

        mouseDelta = {
            mousePosition.x - previousMousePosition.x,
            mousePosition.y - previousMousePosition.y
        };
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

    Vector2 GetMousePosition() {
        return mousePosition;
    }

    Vector2 GetMouseDelta() {
        return mouseDelta;
    }
}