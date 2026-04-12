//
// Created by berke on 4/10/2026.
//

#include <glad/glad.h>
#include "../../Headers/Renderer/Renderer.h"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

constexpr Uint64 windowFlags = SDL_WINDOW_OPENGL;

namespace Renderer {
    bool InitializeOpenGL() {
        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3)) {
            SDL_Log("SDL_GL_SetAttribute Major Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)) {
            SDL_Log("SDL_GL_SetAttribute Minor Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)) {
            SDL_Log("SDL_GL_SetAttribute Core Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)) {
            SDL_Log("SDL_GL_SetAttribute Double Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)) {
            SDL_Log("SDL_GL_SetAttribute Depth Error: %s\n", SDL_GetError());
            return false;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            SDL_Log("SDL_GL_CreateContext Error: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            window = nullptr;
            return false;
        }

        if (!SDL_GL_SetSwapInterval(1)) {
            SDL_Log("SDL_GL_SetSwapInterval Warning: %s", SDL_GetError());
        }
        return true;
    }
    bool Initialize() {
        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init Error: %s\n", SDL_GetError());
            return false;
        }
        window = SDL_CreateWindow("Wolfy Engine", SCREEN_WIDTH, SCREEN_HEIGHT, windowFlags);
        if (!window) {
            SDL_Log("SDL_CreateWindow Error: %s\n", SDL_GetError());
            return false;
        }
        if (!InitializeOpenGL()) {
            SDL_Log("Initialize OpenGL Error: %s\n", SDL_GetError());
            return false;
        }

        //SDL_SetWindowRelativeMouseMode(window, true);
        return true;
    }

    void Update() {
    }
}