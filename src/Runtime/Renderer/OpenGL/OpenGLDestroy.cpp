#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <SDL3/SDL_init.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

void OpenGL::Shutdown() {
    // Textures must be deleted while the OpenGL context still exists.
    DestroyAllTextures();

    for (auto& [character, glyph] : Characters) {
        if (glyph.textureID != 0) {
            glDeleteTextures(1, &glyph.textureID);
            glyph.textureID = 0;
        }
    }

    Characters.clear();

    if (sectorSSBO != 0) {
        glDeleteBuffers(1, &sectorSSBO);
        sectorSSBO = 0;
    }

    if (decalSSBO != 0) {
        glDeleteBuffers(1, &decalSSBO);
        decalSSBO = 0;
    }

    if (spriteSSBO != 0) {
        glDeleteBuffers(1, &spriteSSBO);
        spriteSSBO = 0;
    }

    if (flatSSBO != 0) {
        glDeleteBuffers(1, &flatSSBO);
        flatSSBO = 0;
    }

    if (wallSSBO != 0) {
        glDeleteBuffers(1, &wallSSBO);
        wallSSBO = 0;
    }

    if (uiEBO != 0) {
        glDeleteBuffers(1, &uiEBO);
        uiEBO = 0;
    }

    if (uiVBO != 0) {
        glDeleteBuffers(1, &uiVBO);
        uiVBO = 0;
    }

    if (uiVAO != 0) {
        glDeleteVertexArrays(1, &uiVAO);
        uiVAO = 0;
    }

    if (textVBO != 0) {
        glDeleteBuffers(1, &textVBO);
        textVBO = 0;
    }

    if (textVAO != 0) {
        glDeleteVertexArrays(1, &textVAO);
        textVAO = 0;
    }

    if (VAO != 0) {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }

    projectionShader.reset();
    backgroundShader.reset();
    textShader.reset();
    uiShader.reset();

    gpuSectors.clear();
    gpuWalls.clear();
    flatTriangles.clear();
    visibleFlatTriangles.clear();
    gpuSprites.clear();
    gpuDecals.clear();

    gpuWallCount = 0;
    flatTriangleCount = 0;
    spriteCount = 0;
    decalCount = 0;

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    if (glContext != nullptr) {
        SDL_GL_DestroyContext(glContext);
        glContext = nullptr;
    }

    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}