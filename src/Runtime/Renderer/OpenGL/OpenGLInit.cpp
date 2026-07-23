#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <filesystem>

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Components.hpp"

#include <set>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "Headers/Map/LevelManager.hpp"

namespace fs = std::filesystem;

namespace {
    std::vector<std::string> CollectReferencedTextureFileNames(const Level& level) {
        std::set<std::string> uniqueNames;

        for (const Wall& wall : level.walls) {
            if (!wall.textureFileName.empty()) uniqueNames.insert(wall.textureFileName);
        }

        for (const Sector& sector : level.sectors) {
            for (const SectorFloor& floor : sector.floors) {
                if (!floor.floor.texture.empty()) uniqueNames.insert(floor.floor.texture);
                if (!floor.ceiling.texture.empty()) uniqueNames.insert(floor.ceiling.texture);
            }
        }

        for (const ComponentSprite& sprite : level.sprites.components) {
            for (const std::string& fileName : sprite.textureFileNames) {
                if (!fileName.empty()) uniqueNames.insert(fileName);
            }
        }

        return {uniqueNames.begin(), uniqueNames.end()};
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

    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 64)) {
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

bool OpenGL::InitSDL(const std::string& windowName) {
    using namespace OpenGLRendererInternal;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::critical(
            "SDL_Init failed while initializing video subsystem: {}",
            SDL_GetError()
        );
        return false;
    }

    window = SDL_CreateWindow(
        windowName.c_str(),
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

    const std::vector<std::string> referencedFileNames = CollectReferencedTextureFileNames(level);

    std::vector<LoadedTextureSurface> loadedSurfaces;
    textureRegions.clear();
    textureRegions.resize(referencedFileNames.size());
    textureRegionIndexByName.clear();

    int cursorX = ATLAS_PADDING;
    int cursorY = ATLAS_PADDING;
    int shelfHeight = 0;

    std::vector<unsigned char> atlasPixels(
        ATLAS_SIZE * ATLAS_SIZE * 4,
        0
    );

    auto copyPixel = [&](int dstX, int dstY, int srcX, int srcY) {
        if (dstX < 0 || dstX >= ATLAS_SIZE || dstY < 0 || dstY >= ATLAS_SIZE) return;
        if (srcX < 0 || srcX >= ATLAS_SIZE || srcY < 0 || srcY >= ATLAS_SIZE) return;

        unsigned char* dst = atlasPixels.data() + (dstY * ATLAS_SIZE + dstX) * 4;

        const unsigned char* src = atlasPixels.data() + (srcY * ATLAS_SIZE + srcX) * 4;

        std::memcpy(dst, src, 4);
    };

    for (int i = 0; i < static_cast<int>(referencedFileNames.size()); ++i) {
        const std::string& fileName = referencedFileNames[i];

        const std::filesystem::path path = ProjectManager::GetTexturesPath() / fileName;

        SDL_Surface* loadedSurface = IMG_Load(path.string().c_str());

        if (loadedSurface == nullptr) {
            spdlog::error("IMG_Load failed for {}: {}", path.string(), SDL_GetError());
            continue;
        }

        SDL_Surface* surface = SDL_ConvertSurface(
            loadedSurface,
            SDL_PIXELFORMAT_RGBA32
        );

        SDL_DestroySurface(loadedSurface);

        if (surface == nullptr) {
            spdlog::error("SDL_ConvertSurface failed for {}: {}", path.string(), SDL_GetError());
            continue;
        }

        const int textureWidth = surface->w;
        const int textureHeight = surface->h;

        if (textureWidth + ATLAS_PADDING * 2 > ATLAS_SIZE ||
            textureHeight + ATLAS_PADDING * 2 > ATLAS_SIZE) {
            spdlog::error(
                "Texture '{}' is too large for atlas: {}x{}",
                fileName,
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
            spdlog::error("Texture atlas is full. Could not add '{}'", fileName);
            SDL_DestroySurface(surface);
            continue;
        }

        for (int row = 0; row < textureHeight; ++row) {
            const unsigned char* srcRow =
                static_cast<unsigned char*>(surface->pixels) + row * surface->pitch;

            unsigned char* dstRow = atlasPixels.data() +((cursorY + row) * ATLAS_SIZE + cursorX) * 4;

            std::memcpy(dstRow, srcRow,textureWidth * 4);
        }

        constexpr float halfTexel = .5f;

        const float uMin = (cursorX + halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float vMin = (cursorY + halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float uMax = (cursorX + textureWidth - halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float vMax = (cursorY + textureHeight - halfTexel) / static_cast<float>(ATLAS_SIZE);

        textureRegions[i] = {
            {uMin, vMin, uMax, vMax},
            {1.0f, 0.0f, 0.0f, 0.0f}
        };

        // Only recorded once packing has actually succeeded, so a texture
        // that failed to load/pack correctly resolves through
        // GetTextureRegionIndex() to -1 ("no texture") instead of pointing
        // at an unused, zero-initialized region slot.
        textureRegionIndexByName[fileName] = i;

        for (int pad = 1; pad <= ATLAS_PADDING; ++pad) {
            // Left and right padding
            for (int y = 0; y < textureHeight; ++y) {
                copyPixel(cursorX - pad, y + cursorY, cursorX, y + cursorY);
                copyPixel(cursorX + textureWidth - 1 + pad, y + cursorY, cursorX + textureWidth - 1, y + cursorY);
            }

            // Top and bottom padding
            for (int x = 0; x < textureWidth; ++x) {
                copyPixel(x + cursorX, cursorY - pad, x + cursorX, cursorY);
                copyPixel(x + cursorX, cursorY + textureHeight - 1 + pad, x + cursorX, cursorY + textureHeight - 1);
            }

            // Corners
            for (int yPad = 1; yPad <= ATLAS_PADDING; ++yPad) {
                copyPixel(cursorX - pad, cursorY - yPad, cursorX, cursorY);
                copyPixel(cursorX + textureWidth - 1 + pad, cursorY - yPad, cursorX + textureWidth - 1, cursorY);
                copyPixel(cursorX - pad, cursorY + textureHeight - 1 + yPad, cursorX, cursorY + textureHeight - 1);
                copyPixel(cursorX + textureWidth - 1 + pad, cursorY + textureHeight - 1 + yPad, cursorX + textureWidth - 1, cursorY + textureHeight - 1);
            }
        }

        spdlog::info(
            "Packed texture '{}' at atlas position {}, {} size {}x{}",
            fileName,
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

    switch (level.rendererSettings.textureSetting) {
        case PIXEL_ART_SHIMMERY:
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            break;
        case PIXEL_ART_LESS_MOIRE:
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            break;
        case PIXEL_ART_SMOOTH_DISTANCE:
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            break;
        case REALISTIC_NORMAL:
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            break;
        case RETRO:
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            break;
        case LOW_RES:
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            break;
        default:
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            break;
    }

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

// Resolves a texture filename to its slot in the atlas built above.
// Returns -1 (the same "no texture" sentinel used throughout the wall/
// sector/sprite/decal GPU builders) if the name is empty, was never
// referenced by the level, or failed to pack. Safe to call every frame -
// it's a plain hash lookup, no loading happens here.
//
// NOTE: if a temporary stub of this function (always returning -1) was
// added elsewhere (e.g. OpenGLTexture.cpp) to get a linkable build, delete
// it now - this definition replaces it, and having both will fail to link
// with a duplicate symbol error.
int OpenGL::GetTextureRegionIndex(const std::string& fileName) const {
    if (fileName.empty()) return -1;

    const auto found = textureRegionIndexByName.find(fileName);

    if (found == textureRegionIndexByName.end()) return -1;

    return found->second;
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

bool OpenGL::Initialize(const std::string windowName) {
    if (!InitSDL(windowName)) {
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