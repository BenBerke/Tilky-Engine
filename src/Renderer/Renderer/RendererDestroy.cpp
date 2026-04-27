#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "../../Headers/Renderer/Renderer/RendererInternal.hpp"

#include "Headers/Renderer/TextureManager.hpp"

#include <SDL3/SDL_init.h>

namespace Renderer {
    void Destroy() {
        using namespace RendererInternal;

        TextureManager::DestroyAll();

        glDeleteBuffers(1, &wallSSBO);
        glDeleteBuffers(1, &flatSSBO);
        glDeleteBuffers(1, &spriteSSBO);

        glDeleteVertexArrays(1, &VAO);

        if (glContext) {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }

        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();
    }
}