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
                    const EntityID id = level.CreateEntity(isUIEntity);
                    selectedEntity = *level.GetEntity(id); // This gives the entity ComponentUITransform

                    auto* t = selectedEntity.GetComponent<ComponentUITransform>();

                    if (t != nullptr) {
                        t->anchorMin = {0.0f, 0.0f};
                        t->anchorMax = {0.0f, 0.0f};

                        t->pivot = {0.0f, 0.0f};

                        t->position = mouseScreen;
                        t->scale = {15.0f, 15.0f};
                    }

                    editingEntity = true;
                }
            }
            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT)) {
                if (holdingEntity) {
                    if (auto* t = selectedEntity.GetComponent<ComponentUITransform>()) {
                        t->position += InputManager::GetMouseDelta();
                    }
                }
            }
            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
                holdingEntity = false;
            }
        }
    }

}
