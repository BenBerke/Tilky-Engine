#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <filesystem>

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "Headers/Map/LevelManager.hpp"

namespace fs = std::filesystem;

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

    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 32)) {
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
    if (!GLAD_GL_VERSION_4_5) {
        spdlog::critical("Reverse-Z with glClipControl requires OpenGL 4.5");
        return false;
    }

    glViewport(0, 0, screenWidth, screenHeight);

    if (!SDL_GL_SetSwapInterval(0)) {
        spdlog::warn(
            "SDL_GL_SetSwapInterval failed. VSync setting may not apply: {}",
            SDL_GetError()
        );
    }

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glClearDepth(0.0);
    glDepthRange(0.0, 1.0);

    spdlog::info("OpenGL initialized successfully");

    return true;
}

bool OpenGL::InitSDL() {
    using namespace OpenGLRendererInternal;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::critical(
            "SDL_Init failed while initializing video subsystem: {}",
            SDL_GetError()
        );
        return false;
    }

    window = SDL_CreateWindow(
        "screen.title.engine",
        screenWidth,
        screenHeight,
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

bool OpenGL::InitImGui() const {
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

bool OpenGL::BuildTextureAtlasFromLevel() {
    using namespace OpenGLRendererInternal;
    DestroyAllTextures();

    const Level& level = LevelManager::CurrentLevel();

    std::vector<LoadedTextureSurface> loadedSurfaces;
    textureRegions.clear();
    textureRegions.resize(level.textures.size());

    int cursorX = ATLAS_PADDING;
    int cursorY = ATLAS_PADDING;
    int shelfHeight = 0;

    std::vector<unsigned char> atlasPixels(
        ATLAS_SIZE * ATLAS_SIZE * 4,
        0
    );

    for (int i = 0; i < static_cast<int>(level.textures.size()); ++i) {
        const Texture& texture = level.textures[i];

        if (texture.fileName.empty()) {
            textureRegions[i] = {
                {0.0f, 0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.0f, 0.0f}
            };

            continue;
        }

        const std::string path =
            ProjectManager::GetTexturesPath().string() + "/" + texture.fileName + ".png";

        SDL_Surface* loadedSurface = IMG_Load(path.c_str());

        if (loadedSurface == nullptr) {
            spdlog::error("IMG_Load failed for {}: {}", path, SDL_GetError());
            continue;
        }

        SDL_Surface* surface = SDL_ConvertSurface(
            loadedSurface,
            SDL_PIXELFORMAT_RGBA32
        );

        SDL_DestroySurface(loadedSurface);

        if (surface == nullptr) {
            spdlog::error("SDL_ConvertSurface failed for {}: {}", path, SDL_GetError());
            continue;
        }

        const int textureWidth = surface->w;
        const int textureHeight = surface->h;

        if (textureWidth + ATLAS_PADDING * 2 > ATLAS_SIZE ||
            textureHeight + ATLAS_PADDING * 2 > ATLAS_SIZE) {
            spdlog::error(
                "Texture '{}' is too large for atlas: {}x{}",
                texture.fileName,
                textureWidth,
                textureHeight
            );

            SDL_DestroySurface(surface);
            continue;
        }

        if (cursorX + textureWidth + ATLAS_PADDING > ATLAS_SIZE) {
            cursorX = ATLAS_PADDING;
            cursorY += shelfHeight + ATLAS_PADDING;
            shelfHeight = 0;
        }

        if (cursorY + textureHeight + ATLAS_PADDING > ATLAS_SIZE) {
            spdlog::error("Texture atlas is full. Could not add '{}'", texture.fileName);
            SDL_DestroySurface(surface);
            continue;
        }

        for (int row = 0; row < textureHeight; ++row) {
            const unsigned char* srcRow =
                static_cast<unsigned char*>(surface->pixels) + row * surface->pitch;

            unsigned char* dstRow =
                atlasPixels.data() +
                ((cursorY + row) * ATLAS_SIZE + cursorX) * 4;

            std::memcpy(
                dstRow,
                srcRow,
                textureWidth * 4
            );
        }

        const float uMin = static_cast<float>(cursorX) / static_cast<float>(ATLAS_SIZE);
        const float vMin = static_cast<float>(cursorY) / static_cast<float>(ATLAS_SIZE);
        const float uMax = static_cast<float>(cursorX + textureWidth) / static_cast<float>(ATLAS_SIZE);
        const float vMax = static_cast<float>(cursorY + textureHeight) / static_cast<float>(ATLAS_SIZE);

        textureRegions[i] = {
            {uMin, vMin, uMax, vMax},
            {1.0f, 0.0f, 0.0f, 0.0f}
        };

        spdlog::info(
            "Packed texture '{}' at atlas position {}, {} size {}x{}",
            texture.fileName,
            cursorX,
            cursorY,
            textureWidth,
            textureHeight
        );

        cursorX += textureWidth + ATLAS_PADDING;
        shelfHeight = std::max(shelfHeight, textureHeight);

        SDL_DestroySurface(surface);
    }

    glGenTextures(1, &atlasTexture);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        ATLAS_SIZE,
        ATLAS_SIZE,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        atlasPixels.data()
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Use NEAREST first. LINEAR needs edge padding/extrusion to avoid bleeding.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenBuffers(1, &textureRegionSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, textureRegionSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        textureRegions.size() * sizeof(GPUTextureRegion),
        textureRegions.empty() ? nullptr : textureRegions.data(),
        GL_STATIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);

    spdlog::info("Created texture atlas with {} texture region(s)", textureRegions.size());

    return true;
}

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

bool OpenGL::Initialize() {
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