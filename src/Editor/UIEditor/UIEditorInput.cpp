//
// Created by berke on 5/16/2026.
//

#include "Headers/Editor/Editor.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "src/Editor/EditorInternal.hpp"

namespace {
    bool holdingEntity = false;
}

namespace MapEditorInternal {
    void HandleUIEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui) {
        Level& level = LevelManager::CurrentLevel();

        if (!mouseBlockedByImGui) {
            const Vector2 mouseScreen = InputManager::GetMousePosition();

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {

                Entity *en = EntityAt(mouseScreen);

                if (en != nullptr) {
                    selectedEntity = *en;
                    editingEntity = true;
                    holdingEntity = true;
                } else {
                    static constexpr bool isUIEntity = true;
                    selectedEntity = level.CreateEntity(isUIEntity); // This gives the entity ComponentUITransform

                    auto *t = selectedEntity.GetComponent<ComponentUITransform>();
                    if (t != nullptr) {
                        t->position = mouseScreen;
                        t->scale = {15.0f, 15.0f};
                    }

                    editingEntity = true;
                }
            }
            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) {
                if (holdingEntity && currentMode == MODE_ENTITY)
                    selectedEntity.GetComponent<ComponentTransform>()->position = mouseScreen;
            }
            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
                holdingEntity = false;
            }
        }
    }

}
