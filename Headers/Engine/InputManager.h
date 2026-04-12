#pragma once
#include <SDL3/SDL.h>
#include "../Math/Vector2.h"

namespace InputManager {
    void BeginFrame();

    bool GetKeyDown(SDL_Scancode key);
    bool GetKey(SDL_Scancode key);
    bool GetKeyUp(SDL_Scancode key);

    bool GetMouseButtonDown(Uint32 button);
    bool GetMouseButton(Uint32 button);
    bool GetMouseButtonUp(Uint32 button);

    Vector2 GetMousePosition();
    Vector2 GetMouseDelta();
}