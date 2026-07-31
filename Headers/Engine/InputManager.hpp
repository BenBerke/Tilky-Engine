#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <filesystem>
#include <vector>
#include "../Math/Vector/Vector2.hpp"

namespace InputManager {
    struct DroppedFile {
        std::filesystem::path path;
        Vector2 position;
    };

    void Initialize(SDL_Window* window); // Helps reduce the branch count, must be called once before the loop
    void BeginFrame(); // Must be called at the start of every frame

    bool QuitRequested(); // Close button pressed

    bool GetKeyDown(SDL_Scancode key);
    bool GetKey(SDL_Scancode key);
    bool GetKeyUp(SDL_Scancode key);
    bool GetDoubleKeyDown(SDL_Scancode key, SDL_Scancode keyTwo);
    bool GetDoubleKey(SDL_Scancode key, SDL_Scancode keyTwo);

    bool GetMouseButtonDown(Uint32 button);
    bool GetMouseButton(Uint32 button);
    bool GetMouseButtonUp(Uint32 button);

    // Returns 0 (SDL_SCANCODE_UNKNOWN) if false
    SDL_Scancode GetAnyKey();
    SDL_Scancode GetAnyKeyDown();
    SDL_Scancode GetAnyKeyUp();

    bool GetMouseWheelScrollUp(); // if wheelScroll > 0
    bool GetMouseWheelScrollDown(); // if wheelScroll < 0
    float GetMouseWheelScroll(); // Returns -1, 0 or +1

    Vector2 GetMousePosition(); // Screen Position
    Vector2 GetMouseDelta();
    void SetRelativeMouseMode(SDL_Window* window, bool enabled);

    [[nodiscard]] const std::vector<DroppedFile>& GetDroppedFiles();
    [[nodiscard]] bool IsExternalDragActive();
    [[nodiscard]] Vector2 GetExternalDragPosition();
}