//
// Created by berke on 5/16/2026.
//
#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Editor/EditorInternal.hpp"

#include <cmath>

namespace MapEditorInternal {

void HandleUIEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
    Level& level = LevelManager::CurrentLevel();
}

} // namespace MapEditorInternal
