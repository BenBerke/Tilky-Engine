#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#ifndef TILKY_STANDALONE
#include "imgui.h"
#include "Headers/Editor/EditorStyle.hpp"
#endif

void OpenGL::BeginFrame() {
    glEnable(GL_DEPTH_TEST);

    glDepthFunc(GL_GREATER);
    glDepthMask(GL_TRUE);

    glDisable(GL_CULL_FACE);

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    glClearDepth(0.0);
    glClearStencil(0);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGL::EndFrame() {
    if (window == nullptr) {
        spdlog::error("OpenGL::EndFrame called with null window");
        return;
    }

    SDL_GL_SwapWindow(window);
}

bool OpenGL::UpdateScreenSize() {
    if (activePanelTarget != nullptr) {
        screenWidth = activePanelTarget->width;
        screenHeight = activePanelTarget->height;
        return true;
    }

    if (!SDL_GetWindowSizeInPixels(window, &screenWidth, &screenHeight)) {
        spdlog::error("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError());
        return false;
    }

    return true;
}

void OpenGL::BeginPanelTarget(const PanelTarget panel, int width, int height) {
    const int index = static_cast<int>(panel);
    if (index < 0 || index >= static_cast<int>(PanelTarget::Count)) return;

    if (width <= 0 || height <= 0) {
        if (!SDL_GetWindowSizeInPixels(window, &width, &height) || width <= 0 || height <= 0) return;
    }

    PanelTargetBuffers& target = panelTargets[index];

    if (target.framebuffer == 0 || width != target.width || height != target.height) {
        DestroyPanelTarget(target);

        glGenTextures(1, &target.colorTexture);
        glBindTexture(GL_TEXTURE_2D, target.colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // Shown at its own size, pixel for pixel.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Depth 24 + stencil 8, like the window: gateways need the stencil.
        glGenRenderbuffers(1, &target.depthStencil);
        glBindRenderbuffer(GL_RENDERBUFFER, target.depthStencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

        glGenFramebuffers(1, &target.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.colorTexture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depthStencil);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            spdlog::error("An editor panel's framebuffer is incomplete; drawing to the window instead");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            DestroyPanelTarget(target);
            activePanelTarget = nullptr;
            return;
        }

        target.width = width;
        target.height = height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    activePanelTarget = &target;
    screenWidth = target.width;
    screenHeight = target.height;
    glViewport(0, 0, screenWidth, screenHeight);
}

void OpenGL::EndPanelTarget() {
    activePanelTarget = nullptr;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Scissor and stencil may still be on from gateways or the 2D canvas.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    if (!UpdateScreenSize()) return;
    glViewport(0, 0, screenWidth, screenHeight);
}

unsigned OpenGL::GetPanelTexture(const PanelTarget panel) const {
    const int index = static_cast<int>(panel);
    if (index < 0 || index >= static_cast<int>(PanelTarget::Count)) return 0;
    return panelTargets[index].colorTexture;
}

void OpenGL::DestroyPanelTarget(PanelTargetBuffers& target) {
    if (target.framebuffer != 0) glDeleteFramebuffers(1, &target.framebuffer);
    if (target.colorTexture != 0) glDeleteTextures(1, &target.colorTexture);
    if (target.depthStencil != 0) glDeleteRenderbuffers(1, &target.depthStencil);

    target = {};
}

void OpenGL::DestroyPanelTargets() {
    for (PanelTargetBuffers& target : panelTargets) DestroyPanelTarget(target);
    activePanelTarget = nullptr;
}

void OpenGL::MakeCurrent() {
    if (window != nullptr && glContext != nullptr && SDL_GL_GetCurrentContext() != glContext)
        SDL_GL_MakeCurrent(window, glContext);

#ifndef TILKY_STANDALONE
    if (imguiContext != nullptr) ImGui::SetCurrentContext(imguiContext);
#endif
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

#ifndef TILKY_STANDALONE
    // The editor's design may have changed since the last session.
    if (imguiContext != nullptr) EditorStyle::Apply(EditorStyle::ActiveDesign());
#endif

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

void OpenGL::SetDrawMode(DrawMode mode) {
    if (mode == DrawMode::WIREFRAME) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else if (mode == DrawMode::SOLID) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}