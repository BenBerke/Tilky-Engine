#include "Headers/Runtime/Renderer/OpenGL/OpenGLRenderer.hpp"

#include <filesystem>

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

namespace fs = std::filesystem;

bool OpenGLRenderer::InitializeOpenGL() {
    using namespace OpenGLRendererInternal;

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while setting OpenGL major version: {}",
            SDL_GetError()
        );
        return false;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while setting OpenGL minor version: {}",
            SDL_GetError()
        );
        return false;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while setting OpenGL core profile: {}",
            SDL_GetError()
        );
        return false;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while enabling double buffering: {}",
            SDL_GetError()
        );
        return false;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while setting depth buffer size: {}",
            SDL_GetError()
        );
        return false;
    }

    glContext = SDL_GL_CreateContext(window);

    if (glContext == nullptr) {
        spdlog::critical(
            "SDL_GL_CreateContext failed: {}",
            SDL_GetError()
        );

        SDL_DestroyWindow(window);
        window = nullptr;

        return false;
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        spdlog::critical("Failed to initialize GLAD");
        return false;
    }

    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (!SDL_GL_SetSwapInterval(0)) {
        spdlog::warn(
            "SDL_GL_SetSwapInterval failed. VSync setting may not apply: {}",
            SDL_GetError()
        );
    }

    spdlog::info("OpenGL initialized successfully");

    return true;
}

bool OpenGLRenderer::InitSDL() {
    using namespace OpenGLRendererInternal;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::critical(
            "SDL_Init failed while initializing video subsystem: {}",
            SDL_GetError()
        );
        return false;
    }

    window = SDL_CreateWindow(
        "Tilky Engine",
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        WINDOW_FLAGS
    );

    if (window == nullptr) {
        spdlog::critical(
            "SDL_CreateWindow failed: {}",
            SDL_GetError()
        );
        return false;
    }

    const fs::path iconPath =
        ProjectManager::FindAssetPath(fs::path("LauncherAssets") / "Fox.png");

    SDL_Surface* windowIcon = IMG_Load(iconPath.string().c_str());

    if (windowIcon == nullptr) {
        spdlog::warn(
            "Renderer window icon failed to load. This does not break the renderer. Path: {} Error: {}",
            iconPath.string(),
            SDL_GetError()
        );
    } else {
        if (!SDL_SetWindowIcon(window, windowIcon)) {
            spdlog::warn(
                "Failed to set renderer window icon. This does not break the renderer. Error: {}",
                SDL_GetError()
            );
        }

        SDL_DestroySurface(windowIcon);
    }

    if (!InitializeOpenGL()) {
        spdlog::critical("OpenGL initialization failed");
        return false;
    }

    spdlog::info("Renderer SDL initialization completed");

    return true;
}

bool OpenGLRenderer::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 430");

    spdlog::info("Renderer ImGui initialized");

    return true;
}

bool OpenGLRenderer::InitProjection() {
    using namespace OpenGLRendererInternal;

    const fs::path renderingVsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "Rendering" / "Rendering.vs.glsl");

    const fs::path renderingFsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "Rendering" / "Rendering.fs.glsl");

    projectionShader = std::make_unique<Shader>(
        renderingVsPath.string().c_str(),
        renderingFsPath.string().c_str()
    );

    if (projectionShader->ID == 0) {
        spdlog::critical(
            "Projection shader creation failed. VS: {} FS: {}",
            renderingVsPath.string(),
            renderingFsPath.string()
        );
        return false;
    }

    const fs::path backgroundVsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "Background" / "Background.vs.glsl");

    const fs::path backgroundFsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "Background" / "Background.fs.glsl");

    backgroundShader = std::make_unique<Shader>(
        backgroundVsPath.string().c_str(),
        backgroundFsPath.string().c_str()
    );

    if (backgroundShader->ID == 0) {
        spdlog::critical(
            "Background shader creation failed. VS: {} FS: {}",
            backgroundVsPath.string(),
            backgroundFsPath.string()
        );
        return false;
    }

    backgroundShader->use();

    glUniform1i(
        glGetUniformLocation(backgroundShader->ID, "backgroundTexture"),
        0
    );

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    glBindVertexArray(0);

    spdlog::info("Projection and background shaders initialized");

    return true;
}

bool OpenGLRenderer::InitUI() {
    constexpr float vertices[] = {
        // x, y,      u, v
         0.5f,  0.5f, 1.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 0.0f
    };

    const unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    const fs::path uiVsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "UI" / "UI.vs.glsl");

    const fs::path uiFsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "UI" / "UI.fs.glsl");

    uiShader = std::make_unique<Shader>(
        uiVsPath.string().c_str(),
        uiFsPath.string().c_str()
    );

    if (uiShader->ID == 0) {
        spdlog::critical(
            "UI shader creation failed. VS: {} FS: {}",
            uiVsPath.string(),
            uiFsPath.string()
        );
        return false;
    }

    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);
    glGenBuffers(1, &uiEBO);

    glBindVertexArray(uiVAO);

    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, uiEBO);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(indices),
        indices,
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    spdlog::info("Renderer UI buffers and shader initialized");

    return true;
}

bool OpenGLRenderer::InitText() {
    using namespace OpenGLRendererInternal;

    const fs::path glyphVsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "Glyph" / "glyph.vs.glsl");

    const fs::path glyphFsPath =
        ProjectManager::FindAssetPath(fs::path("Shaders") / "Glyph" / "glyph.fs.glsl");

    textShader = std::make_unique<Shader>(
        glyphVsPath.string().c_str(),
        glyphFsPath.string().c_str()
    );

    if (textShader->ID == 0) {
        spdlog::critical(
            "Text shader creation failed. VS: {} FS: {}",
            glyphVsPath.string(),
            glyphFsPath.string()
        );
        return false;
    }

    textShader->use();

    //todo support window resizing
    const Matrix4 projection = Matrix4::Orthographic(
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

    glUniform1i(
        glGetUniformLocation(textShader->ID, "text"),
        0
    );

    if (!InitializeFont()) {
        spdlog::critical("Failed to initialize renderer font system");
        return false;
    }

    spdlog::info("Renderer text system initialized");

    return true;
}

bool OpenGLRenderer::Initialize() {
    if (!InitSDL()) {
        spdlog::critical("Renderer initialization stopped at InitSDL");
        return false;
    }

    RefreshTexturesFromLevel();

    if (!InitImGui()) {
        spdlog::critical("Renderer initialization stopped at InitImGui");
        return false;
    }

    if (!InitProjection()) {
        spdlog::critical("Renderer initialization stopped at InitProjection");
        return false;
    }

    if (!InitUI()) {
        spdlog::critical("Renderer initialization stopped at InitUI");
        return false;
    }

    if (!InitText()) {
        spdlog::critical("Renderer initialization stopped at InitText");
        return false;
    }

    SDL_SetWindowRelativeMouseMode(window, true);

    spdlog::info("OpenGL renderer initialized successfully");

    return true;
}