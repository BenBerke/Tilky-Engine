#ifndef WOLFY_ENGINE_RENDERER_H
#define WOLFY_ENGINE_RENDERER_H

#include <string>

#include <glad/glad.h>
#include <SDL3/SDL_video.h>

#include "Headers/Math/Vector/Vector4.hpp"

struct Vector2;
struct Vector3;

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

    extern GLuint uiVAO;
    extern GLuint uiVBO;
    extern GLuint uiEBO;

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

    void DrawUIRectangle(
    const Vector2& position,
    const Vector2& size,
    const Vector4& color = (Vector4){255.0f, 255.0f, 255.0f, 255.0f},
    int textureIndex = -1
    );

    inline constexpr int SECTOR_FLOOR_COUNT = 3;
    inline constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;
}

#endif // WOLFY_ENGINE_RENDERER_H