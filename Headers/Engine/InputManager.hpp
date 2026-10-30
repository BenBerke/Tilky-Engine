#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <filesystem>
#include <string>
#include <vector>
#include "../Math/Vector/Vector2.hpp"

namespace InputManager {
    // Cursor.lockState in Lua, the same three modes as Unity.
    enum class CursorLockMode : uint8_t {
        None,     // free, and can leave the window
        Locked,   // hidden and held in place; the mouse only moves the view
        Confined  // free, but kept inside the window
    };
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
    float GetMouseWheelScroll(); // This frame's scrolling, positive away from the user

    // Cursor.lockState / Cursor.visible: what the game wants. RuntimeSession
    // applies it to the window while the game has the mouse. A Locked
    // cursor is always hidden.
    void SetCursorLockMode(CursorLockMode mode);
    [[nodiscard]] CursorLockMode GetCursorLockMode();
    void SetCursorVisible(bool visible);
    [[nodiscard]] bool IsCursorVisible();
    // Locked and hidden, where Stop leaves it. Each level sets its own on
    // start (World Settings > Cursor).
    void ResetCursor();

    Vector2 GetMousePosition(); // Screen Position
    Vector2 GetMouseDelta();
    void SetRelativeMouseMode(SDL_Window* window, bool enabled);

    [[nodiscard]] const std::vector<DroppedFile>& GetDroppedFiles();
    [[nodiscard]] bool IsExternalDragActive();
    [[nodiscard]] Vector2 GetExternalDragPosition();

    // A display name for the key ("A", "Left Ctrl", "Page Up"), "Unknown" if it has none.
    [[nodiscard]] const std::string& ScancodeToString(SDL_Scancode code);
}