#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "../../Headers/Renderer/Renderer/RendererInternal.hpp"

#include "Headers/Math/Matrix/Matrix4.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

namespace RendererInternal {
    bool InitializeOpenGL() {
        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4)) {
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

        Renderer::glContext = SDL_GL_CreateContext(Renderer::window);
        if (!Renderer::glContext) {
            SDL_Log("SDL_GL_CreateContext Error: %s", SDL_GetError());
            SDL_DestroyWindow(Renderer::window);
            Renderer::window = nullptr;
            return false;
        }

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            SDL_Log("Failed to initialize GLAD");
            return false;
        }

        glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        if (!SDL_GL_SetSwapInterval(1)) {
            SDL_Log("SDL_GL_SetSwapInterval Warning: %s", SDL_GetError());
        }

        return true;
    }
}

namespace Renderer {
    bool Initialize() {
        using namespace RendererInternal;

        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init Error: %s\n", SDL_GetError());
            return false;
        }

        window = SDL_CreateWindow("Wolfy Engine", SCREEN_WIDTH, SCREEN_HEIGHT, WINDOW_FLAGS);
        if (!window) {
            SDL_Log("SDL_CreateWindow Error: %s\n", SDL_GetError());
            return false;
        }

        if (!InitializeOpenGL()) {
            SDL_Log("Initialize OpenGL Error: %s\n", SDL_GetError());
            return false;
        }

        projectionShader = std::make_unique<Shader>(
            "../Shaders/Wall/wallProjection.vs.glsl",
            "../Shaders/Wall/wallColoring.fs.glsl"
        );

        if (projectionShader->ID == 0) {
            SDL_Log("Projection Shader creation failed");
            return false;
        }

        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);
        glBindVertexArray(0);

        if (DEBUG_ENABLED) {
            debugShader = std::make_unique<Shader>(
                "../Shaders/Debug/debug.vs.glsl",
                "../Shaders/Debug/debug.fs.glsl"
            );

            if (debugShader->ID == 0) {
                SDL_Log("Debug Shader creation failed");
                return false;
            }

            glGenVertexArrays(1, &debugVAO);
            glGenBuffers(1, &debugVBO);

            glBindVertexArray(debugVAO);
            glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            debugColorUniform = glGetUniformLocation(debugShader->ID, "uColor");
            if (debugColorUniform == -1) {
                SDL_Log("Failed to get debug shader uniform location uColor");
                return false;
            }
        }

        textShader = std::make_unique<Shader>(
            "../Shaders/Glyph/glyph.vs.glsl",
            "../Shaders/Glyph/glyph.fs.glsl"
        );

        if (textShader->ID == 0) {
            SDL_Log("Text Shader creation failed");
            return false;
        }

        textShader->use();

        Matrix4 projection = Matrix4::Orthographic(
            0.0f, static_cast<float>(SCREEN_WIDTH),
            0.0f, static_cast<float>(SCREEN_HEIGHT),
            -1.0f, 1.0f
        );

        glUniformMatrix4fv(
            glGetUniformLocation(textShader->ID, "projection"),
            1,
            GL_TRUE,
            &projection.m[0][0]
        );

        glUniform1i(glGetUniformLocation(textShader->ID, "text"), 0);

        if (!InitializeFont()) {
            SDL_Log("Failed to initialize font");
            return false;
        }

        SDL_SetWindowRelativeMouseMode(window, true);

        return true;
    }
}