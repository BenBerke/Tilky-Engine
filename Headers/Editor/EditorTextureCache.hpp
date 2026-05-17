//
// Created by berke on 5/17/2026.
//

#ifndef TILKY_ENGINE_EDITORTEXTURECACHE_H
#define TILKY_ENGINE_EDITORTEXTURECACHE_H

#include <SDL3/SDL_render.h>
#include <vector>

#include "Headers/Objects/Level.hpp"

namespace EditorTextureCache {
    bool Load(SDL_Renderer* renderer, const Level& level);
    void Destroy();

    SDL_Texture* Get(int textureIndex);

    void RefreshLevelTexturesFromFolder();
}
#endif //TILKY_ENGINE_EDITORTEXTURECACHE_H