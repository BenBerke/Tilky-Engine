//
// Created by berke on 5/17/2026.
//

#include "imgui.h"

#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Editor/EditorInternal.hpp"


namespace MapEditorInternal {
    void DrawEntities_UI() {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& entity : level.entities) {
            const ComponentUITransform* transform = level.ui_transforms.Get(entity.id);

            if (transform == nullptr) {
                continue;
            }

            const Vector2 screenEntitySize = transform->scale;

            SDL_FRect rect = {
                transform->position.x - screenEntitySize.x * 0.5f,
                transform->position.y - screenEntitySize.y * 0.5f,
                screenEntitySize.x,
                screenEntitySize.y
            };

            // if (level.playerSpawns.Get(entity.id) != nullptr) {
            //     SDL_SetRenderDrawColor(renderer, 80, 180, 255, 255);
            // }
            // else {
            //     SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            // }

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

            SDL_RenderFillRect(renderer, &rect);

            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderRect(renderer, &rect);
        }
    }
    void DrawUIImages() {
        Level& level = LevelManager::CurrentLevel();
        for (Entity& entity : level.entities) {
            const ComponentUITransform* transform = entity.GetComponent<ComponentUITransform>();
            const ComponentUISprite* sprite = entity.GetComponent<ComponentUISprite>();

            if (transform == nullptr || sprite == nullptr) continue;


            SDL_Texture *texture = EditorTextureCache::Get(sprite->textureIndex);
            if (texture == nullptr) {
                spdlog::error("Texture does not exist");
                continue;
            }

            SDL_FRect dst {
                transform->position.x, transform->position.y,
                transform->scale.x, transform->scale.y
            };

            SDL_RenderTexture(renderer, texture, nullptr, &dst);
        }
    }
}
