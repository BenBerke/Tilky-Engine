//
// Created by berke on 4/10/2026.
//
#ifndef WOLFY_ENGINE_RENDERER_H
#define WOLFY_ENGINE_RENDERER_H

#include <SDL3/SDL_render.h>
#include "../../Headers/Renderer/Shader.h"

#endif //WOLFY_ENGINE_RENDERER_H

namespace Renderer {
    inline SDL_Window* window;
    inline SDL_Renderer* renderer;
    inline SDL_GLContext glContext;

    inline unsigned int VAO, VBO, EBO, wallSSBO;

    bool Initialize();
    void Update();
    void Destroy();
}