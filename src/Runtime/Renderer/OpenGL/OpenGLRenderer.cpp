#include "Headers/Runtime/Renderer/OpenGL/OpenGLRenderer.hpp"

#include <spdlog/spdlog.h>

void OpenGLRenderer::BeginFrame() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glDisable(GL_CULL_FACE);

    glDepthMask(GL_TRUE);

    glClearColor(
        1.0f,
        1.0f,
        1.0f,
        1.0f
    );

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );
}

void OpenGLRenderer::EndFrame() {
    if (window == nullptr) {
        spdlog::error("OpenGLRenderer::EndFrame called with null window");
        return;
    }

    SDL_GL_SwapWindow(window);
}

void OpenGLRenderer::OnResize(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    glViewport(
        0,
        0,
        width,
        height
    );
}