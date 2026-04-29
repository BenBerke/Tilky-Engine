#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include "../Math/Vector/Vector2.hpp"

namespace InputManager {
    void BeginFrame();

    bool QuitRequested();

    bool GetKeyDown(SDL_Scancode key);
    bool GetKey(SDL_Scancode key);
    bool GetKeyUp(SDL_Scancode key);
    bool GetDoubleKeyDown(SDL_Scancode key, SDL_Scancode keyTwo);

    bool GetMouseButtonDown(Uint32 button);
    bool GetMouseButton(Uint32 button);
    bool GetMouseButtonUp(Uint32 button);

    bool GetMouseWheelScrollUp();
    bool GetMouseWheelScrollDown();
    float GetMouseWheelScroll();

    Vector2 GetMousePosition();
    Vector2 GetMouseDelta();
    void SetRelativeMouseMode(SDL_Window* window, bool enabled);
}