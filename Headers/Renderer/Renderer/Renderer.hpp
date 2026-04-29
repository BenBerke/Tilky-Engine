#ifndef WOLFY_ENGINE_RENDERER_H
#define WOLFY_ENGINE_RENDERER_H

#include <string>

#include <glad/glad.h>
#include <SDL3/SDL_video.h>

#include "../../Math/Vector/Vector2.hpp"
#include "../../Math/Vector/Vector3.hpp"

namespace Renderer {
    extern SDL_Window* window;
    extern SDL_GLContext glContext;

    extern GLuint VAO;
    extern GLuint wallSSBO;

    extern GLuint debugVAO;
    extern GLuint debugVBO;
    extern GLint debugColorUniform;

    extern GLuint textVAO;
    extern GLuint textVBO;

    bool Initialize();
    void Update(const Vector2& playerPos, float angle);
    bool CreateMap();
    void Destroy();

    void RenderTextRaw(
        const std::string& text,
        float x,
        float y,
        float scale,
        Vector3 color
    );

    inline constexpr int SECTOR_FLOOR_COUNT = 3;
    inline constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;
}

#endif // WOLFY_ENGINE_RENDERER_H