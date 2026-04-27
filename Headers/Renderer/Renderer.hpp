//
// Created by berke on 4/10/2026.
//
#ifndef WOLFY_ENGINE_RENDERER_H
#define WOLFY_ENGINE_RENDERER_H

#include <SDL3/SDL_render.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "Shader.hpp"
#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector3.hpp"


#endif //WOLFY_ENGINE_RENDERER_H

namespace Renderer {
    inline SDL_Window* window;
    inline SDL_GLContext glContext;

    inline GLuint VAO, VBO, EBO, wallSSBO;
    inline GLuint debugVAO, debugVBO, debugColorUniform;
    inline GLuint textVAO, textVBO;

    bool Initialize();
    void Update(const Vector2& playerPos, const float angle);
    bool CreateMap();
    void Destroy();
    void RenderTextRaw(const std::string& text, const float x, const float y, const float scale, const Vector3 color);
}