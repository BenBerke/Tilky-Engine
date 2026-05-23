//
// Created by berke on 5/17/2026.
//
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "src/Editor/EditorInternal.hpp"
#include "Headers/UISystem.hpp"

namespace MapEditorInternal {
    void DrawEntities_UI() {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& entity : level.entities) {
            const ComponentUITransform* transform = level.ui_transforms.Get(entity.id);
            if (auto* i = level.ui_sprites.Get(entity.id); i != nullptr) continue;

            if (transform == nullptr) {
                spdlog::error("UI Entity has no UI Transform {}: {}", entity.name, entity.id);
                continue;
            }

            SDL_FRect rect = {
                transform->resolvedPosition.x,
                transform->resolvedPosition.y,
                transform->resolvedSize.x,
                transform->resolvedSize.y
            };

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(renderer, &rect);
        }
    }
    void DrawUIImages() {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& entity : level.entities) {
            const ComponentUITransform* transform = level.ui_transforms.Get(entity.id);
            const ComponentUISprite* sprite = level.ui_sprites.Get(entity.id);

            if (transform == nullptr || sprite == nullptr) {
                continue;
            }

            SDL_Texture* texture = EditorTextureCache::Get(sprite->textureIndex);

            if (texture == nullptr) {
                spdlog::error("Texture does not exist");
                continue;
            }

            SDL_FRect dst = {
                transform->resolvedPosition.x,
                transform->resolvedPosition.y,
                transform->resolvedSize.x,
                transform->resolvedSize.y
            };

            SDL_FPoint centre = {
                transform->resolvedSize.x * transform->pivot.x,
                transform->resolvedSize.y * transform->pivot.y
            };

            SDL_RenderTextureRotated(
                renderer,
                texture,
                nullptr,
                &dst,
                transform->rotation,
                &centre,
                SDL_FLIP_NONE
            );
        }
    }
}
