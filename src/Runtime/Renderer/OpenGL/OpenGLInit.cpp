#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <filesystem>

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>

#include <spdlog/spdlog.h>

#ifndef TILKY_STANDALONE
#include "Headers/Editor/EditorStyle.hpp"
#endif
#include "Headers/Engine/FolderWatcher.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Components.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>

#ifndef TILKY_STANDALONE
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#endif
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/TextureTiling.hpp"

namespace fs = std::filesystem;

namespace OpenGLRendererInternal {
    void ApplyTextureSampling(const RendererTextureSettings setting, const GLenum target) {
        switch (setting) {
            case PIXEL_ART_SHIMMERY:
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                break;
            case PIXEL_ART_LESS_MOIRE:
                glGenerateMipmap(target);
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                break;
            case PIXEL_ART_SMOOTH_DISTANCE:
                glGenerateMipmap(target);
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                break;
            case REALISTIC_NORMAL:
                glGenerateMipmap(target);
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                break;
            case RETRO:
                glGenerateMipmap(target);
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                break;
            case LOW_RES:
                glGenerateMipmap(target);
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                break;
            default:
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                break;
        }
    }
}

bool OpenGL::InitializeOpenGL() {
    using namespace OpenGLRendererInternal;

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while setting OpenGL major version: {}",
            SDL_GetError()
        );
        return false;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5)) {
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

    // Windows (WGL) takes the closest pixel format to 64, so it gets the
    // deepest buffer the driver has. GLX/EGL on Linux treat the size as a
    // minimum, and no driver offers 64 bits, so ask for 24 there.
#ifdef _WIN32
    constexpr int depthBits = 64;
#else
    constexpr int depthBits = 24;
#endif
    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthBits)) {
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

    // Back to what the window has (its pixel format was picked when it was
    // created, see InitSDL): the attributes stay set for every window made
    // later, and no pixel format has a 64-bit depth buffer - the editor's
    // panels pulled out into windows of their own couldn't be created.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        spdlog::critical("Failed to initialize GLAD");
        return false;
    }
    if (!GLAD_GL_VERSION_4_5) {
        spdlog::critical("Reverse-Z with glClipControl requires OpenGL 4.5");
        return false;
    }

    {
        GLint stencilBits = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencilBits);
        hasStencil = stencilBits > 0;

        if (!hasStencil) spdlog::warn("The window has no stencil buffer: gateways will show black");
    }

    glViewport(0, 0, screenWidth, screenHeight);

    // Default fallback incase something happens to user defined settings
    SetVSync(-1);

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glClearDepth(0.0);
    glDepthRange(0.0, 1.0);

    spdlog::info("OpenGL initialized successfully");

    return true;
}

bool OpenGL::InitSDL(const std::string& windowName, const bool startHidden) {
    using namespace OpenGLRendererInternal;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::critical(
            "SDL_Init failed while initializing video subsystem: {}",
            SDL_GetError()
        );
        return false;
    }

    // The window's pixel format is picked when it is created, so these go
    // first. Depth 24 is what the window always got; the stencil is where
    // gateways mark the screen their views draw to.
    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) || !SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)) {
        spdlog::critical(
            "SDL_GL_SetAttribute failed while setting the depth/stencil buffer size: {}",
            SDL_GetError()
        );
        return false;
    }

    // A hidden window is maximized the first time SetWindowVisible shows it.
    window = SDL_CreateWindow(
        windowName.c_str(),
        screenWidth,
        screenHeight,
        startHidden ? SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN : WINDOW_FLAGS
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
    }
    else {
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

// Only for Play in the engine (the runtime editor draws in the editor's own
// context); the standalone game has no ImGui.
#ifndef TILKY_STANDALONE
bool OpenGL::InitImGui() {
    IMGUI_CHECKVERSION();
    // In the engine the editor's context already exists, and CreateContext
    // only makes the new one current when none is.
    imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(imguiContext);

    ImGuiIO& io = ImGui::GetIO();
    // No layout file: Play's overlays have no layout worth keeping, and
    // writing the editor's imgui.ini would let this context overwrite its
    // window positions and dock ids. (imgui_runtime.ini, which the runtime
    // editor's own context used to keep, is no longer read.)
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // The same text font the editor loads (Editor.cpp), so Play's overlays
    // look the same. Each context has its own font atlas, so it's loaded
    // again here rather than shared. A missing file just falls back.
    std::error_code ec;

    if (const std::filesystem::path textFont = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");
        std::filesystem::exists(textFont, ec)) {
        if (ImFont* uiFont = io.Fonts->AddFontFromFileTTF(textFont.string().c_str(), 18.0f)) io.FontDefault = uiFont;
    }

    // Whatever design the editor had, so Play's overlays match it.
    EditorStyle::Apply(EditorStyle::ActiveDesign());

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 430");

    spdlog::info("Renderer ImGui initialized");

    return true;
}
#endif

bool OpenGL::InitProjection() {
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

    glUniform1i(glGetUniformLocation(backgroundShader->ID, "backgroundTexture"), 0);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);
    glBindVertexArray(0);

    spdlog::info("Projection and background shaders initialized");

    return true;
}

bool OpenGL::InitUI() {
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

bool OpenGL::InitText() {
    using namespace OpenGLRendererInternal;

    const fs::path glyphVsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Glyph" / "glyph.vs.glsl");
    const fs::path glyphFsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Glyph" / "glyph.fs.glsl");

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

    const Matrix4 projection = Matrix4::Orthographic(
        0.0f, static_cast<float>(screenWidth),
        static_cast<float>(screenHeight), 0.0f,
        -1.0f, 1.0f
    );

    glUniformMatrix4fv(
        glGetUniformLocation(textShader->ID, "projection"),
        1,
        GL_TRUE,
        &projection.m[0][0]
    );

    glUniform1i(glGetUniformLocation(textShader->ID, "text"),0);

    if (!InitializeFont()) {
        spdlog::critical("Failed to initialize renderer font system");
        return false;
    }

    spdlog::info("Renderer text system initialized");

    return true;
}

bool OpenGL::Initialize(const std::string windowName, const bool startHidden) {
    if (!InitSDL(windowName, startHidden)) {
        spdlog::critical("Renderer initialization stopped at InitSDL");
        return false;
    }

    RefreshTexturesFromLevel();

#ifndef TILKY_STANDALONE
    if (!InitImGui()) {
        spdlog::critical("Renderer initialization stopped at InitImGui");
        return false;
    }
#endif

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

    if (!startHidden) SDL_SetWindowRelativeMouseMode(window, true);

    spdlog::info("OpenGL renderer initialized successfully");

    return true;
}