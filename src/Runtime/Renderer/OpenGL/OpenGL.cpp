#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "Headers/Editor/EditorStyle.hpp"

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

void OpenGL::MakeCurrent() {
    if (window != nullptr && glContext != nullptr && SDL_GL_GetCurrentContext() != glContext)
        SDL_GL_MakeCurrent(window, glContext);

    if (imguiContext != nullptr) ImGui::SetCurrentContext(imguiContext);
}

void OpenGL::SetVSync(const int mode) {
    if (SDL_GL_SetSwapInterval(mode)) return;

    spdlog::warn("SDL_GL_SetSwapInterval({}) failed: {}", mode, SDL_GetError());

    // Not every driver has adaptive VSync; plain VSync is the closest.
    if (mode == -1 && !SDL_GL_SetSwapInterval(1))
        spdlog::warn("SDL_GL_SetSwapInterval(1) failed too: {}", SDL_GetError());
}

void OpenGL::SetWindowVisible(const bool visible) {
    if (window == nullptr) return;

    if (!visible) {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_HideWindow(window);
        return;
    }

    MakeCurrent();

    // The editor's design may have changed since the last session.
    if (imguiContext != nullptr) EditorStyle::Apply(EditorStyle::ActiveDesign());

    SDL_ShowWindow(window);

    if (!windowShownOnce) {
        // Later sessions reopen at whatever size and position it was left.
        SDL_MaximizeWindow(window);
        windowShownOnce = true;
    }

    SDL_RaiseWindow(window);
}

void OpenGL::OnResize(const int width, const int height) {
    if (width <= 0 || height <= 0) return;

    screenWidth = width;
    screenHeight = height;

    glViewport(0, 0, screenWidth, screenHeight);
}