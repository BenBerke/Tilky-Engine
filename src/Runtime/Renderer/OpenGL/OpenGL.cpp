#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

void OpenGL::BeginFrame() {
    glEnable(GL_DEPTH_TEST);

    glDepthFunc(GL_GREATER);
    glDepthMask(GL_TRUE);

    glDisable(GL_CULL_FACE);

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    glClearDepth(0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGL::EndFrame() {
    if (window == nullptr) {
        spdlog::error("OpenGL::EndFrame called with null window");
        return;
    }

    SDL_GL_SwapWindow(window);
}

void OpenGL::OnResize(const int width, const int height) {
    if (width <= 0 || height <= 0) return;

    screenWidth = width;
    screenHeight = height;

    glViewport(0, 0, screenWidth, screenHeight);
}