//
// Created by berke on 5/16/2026.
//
#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Editor/EditorInternal.hpp"

#include <cmath>

/// This script is responsible for handling keyboard/mouse input inside the UI Editor
namespace MapEditorInternal {
    void HandleUIEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level &level = LevelManager::CurrentLevel();
    }
} // namespace MapEditorInternal
