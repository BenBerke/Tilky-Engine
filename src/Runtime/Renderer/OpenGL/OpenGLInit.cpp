#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <filesystem>

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>

#include <spdlog/spdlog.h>

#include "Headers/Editor/EditorStyle.hpp"
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

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "Headers/Map/LevelManager.hpp"

namespace fs = std::filesystem;

namespace {
    std::vector<std::string> CollectReferencedTextureFileNames(const Level& level) {
        std::set<std::string> uniqueNames;

        for (const Wall& wall : level.walls) {
            if (!wall.top.texture.empty()) uniqueNames.insert(wall.top.texture);
            if (!wall.bottom.texture.empty()) uniqueNames.insert(wall.bottom.texture);
            if (!wall.middle.texture.empty()) uniqueNames.insert(wall.middle.texture);
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

    // Every image the Asset Browser shows as a texture, anywhere under
    // Assets, as the same Assets-relative reference the browser hands out.
    std::vector<std::string> CollectProjectImageFileNames() {
        static const std::set<std::string> kImageExtensions = {".png", ".jpg", ".jpeg"};

        const fs::path assetsPath = ProjectManager::GetAssetsPath();
        std::vector<std::string> imageNames;

        std::error_code ec;
        for (fs::recursive_directory_iterator it(assetsPath, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();

            // Hidden entries aren't shown in the Asset Browser either.
            if (!name.empty() && name.front() == '.') {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }

            if (!it->is_regular_file()) continue;

            std::string extension = it->path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (kImageExtensions.contains(extension))
                imageNames.push_back(it->path().lexically_relative(assetsPath).generic_string());
        }

        std::ranges::sort(imageNames);
        return imageNames;
    }

#ifndef TILKY_STANDALONE
    // CollectProjectImageFileNames and those images' write times, kept until
    // an image under Assets changes. Walking Assets and reading every
    // image's time took ~200 ms on a 1700-image project, on every switch
    // into the runtime editor or Play.
    FolderWatcher imageWatcher({".png", ".jpg", ".jpeg"});
    std::uint64_t cachedImageGeneration = 0;
    std::vector<std::string> cachedProjectImages;
    std::unordered_map<std::string, size_t> cachedWriteTimes; // project images only
#endif

    size_t WriteTimeHash(const std::string& fileName) {
        std::error_code ec;
        const fs::file_time_type writeTime =
            fs::last_write_time(ProjectManager::GetAssetsPath() / fs::path(fileName).lexically_normal(), ec);
        return ec ? 0 : std::hash<long long>{}(static_cast<long long>(writeTime.time_since_epoch().count()));
    }
}

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

bool OpenGL::InitImGui() {
    IMGUI_CHECKVERSION();
    // In the engine the editor's context already exists, and CreateContext
    // only makes the new one current when none is.
    imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(imguiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // the runtime editor's windows dock into each other

    // The same text and icon fonts the editor loads (Editor.cpp), so the
    // runtime editor's panels - the asset browser's icons especially - look
    // the same. Each context has its own font atlas, so they're loaded again
    // here rather than shared. Missing files just fall back (the standalone
    // game may not ship them).
    std::error_code ec;

    if (const std::filesystem::path textFont = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");
        std::filesystem::exists(textFont, ec)) {
        if (ImFont* uiFont = io.Fonts->AddFontFromFileTTF(textFont.string().c_str(), 18.0f)) io.FontDefault = uiFont;
    }

    imguiIconFont = nullptr;

    if (const std::filesystem::path iconFont = ProjectManager::FindAssetPath("EngineAssets/Fonts/FontAwesome6-Solid.otf");
        std::filesystem::exists(iconFont, ec)) {
        imguiIconFont = io.Fonts->AddFontFromFileTTF(iconFont.string().c_str(), 16.0f);
    }

    // Whatever design the editor had, so the runtime editor matches it.
    EditorStyle::Apply(EditorStyle::ActiveDesign());

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 430");

    spdlog::info("Renderer ImGui initialized");

    return true;
}

bool OpenGL::BuildTextureAtlasFromLevel() {
    using namespace OpenGLRendererInternal;

    const Level& level = LevelManager::CurrentLevel();

    // Every image in Assets, in name order, so a texture first named at
    // runtime (assigned in the runtime editor, or set from Lua) is already in
    // the atlas. Then any level texture that isn't one of them (a path from
    // outside Assets), at the end.
    //
    // The order must not depend on which images the level uses: it is part of
    // the signature below, so assigning a texture for the first time would
    // otherwise rebuild the whole atlas.
#ifndef TILKY_STANDALONE
    // Read before scanning: a change made during the scan moves the
    // generation on, so the next call scans again.
    const std::uint64_t imageGeneration = imageWatcher.Generation(ProjectManager::GetAssetsPath());

    if (imageGeneration != cachedImageGeneration) {
        cachedProjectImages = CollectProjectImageFileNames();
        cachedWriteTimes.clear();
        cachedImageGeneration = imageGeneration;
    }

    std::vector<std::string> referencedFileNames = cachedProjectImages;
    const size_t projectImageCount = cachedProjectImages.size();
#else
    std::vector<std::string> referencedFileNames = CollectProjectImageFileNames();
#endif

    {
        std::set<std::string> alreadyListed(referencedFileNames.begin(), referencedFileNames.end());

        const auto addIfMissing = [&](const std::string& fileName) {
            if (!fileName.empty() && alreadyListed.insert(fileName).second) referencedFileNames.push_back(fileName);
        };

        for (const std::string& fileName : CollectReferencedTextureFileNames(level)) addIfMissing(fileName);
        for (const ComponentUISprite& uiSprite : level.ui_sprites.components) addIfMissing(uiSprite.texture);
    }

    // The engine keeps the atlas between Play sessions; it only has to be
    // rebuilt when an image, the image order or the sampling changed.
    size_t signature = std::hash<int>{}(static_cast<int>(level.rendererSettings.textureSetting));

    const auto combine = [&signature](const size_t value) {
        signature ^= value + 0x9e3779b97f4a7c15ULL + (signature << 6) + (signature >> 2);
    };

    for (size_t index = 0; index < referencedFileNames.size(); ++index) {
        const std::string& fileName = referencedFileNames[index];
        combine(std::hash<std::string>{}(fileName));

#ifndef TILKY_STANDALONE
        // The level's own references after them may lie outside Assets,
        // where the watcher can't see changes, so those are read every time.
        if (index < projectImageCount) {
            const auto [cached, inserted] = cachedWriteTimes.try_emplace(fileName, 0);
            if (inserted) cached->second = WriteTimeHash(fileName);

            combine(cached->second);
            continue;
        }
#endif

        combine(WriteTimeHash(fileName));
    }

    if (atlasTexture != 0 && signature == atlasSignature) {
        spdlog::info("Texture atlas unchanged, reusing it");
        return true;
    }

    // Only the atlas itself. The standalone textures - the asset browser's
    // and inspector's previews, the sky - stay; just the ones whose file
    // changed are reloaded. Deleting them all made every preview reload at
    // once, so the browser went blank for a moment after each rebuild.
    DestroyAtlas();
    ReloadChangedTextures();

    textureRegions.clear();
    textureRegions.resize(referencedFileNames.size());
    textureRegionIndexByName.clear();

    // Pass 1: decode (or reuse) each image and give it a spot. Images are
    // packed in shelves; when a page is full the next one starts.
    struct Placement {
        const DecodedImage* image;
        int page;
        int x;
        int y;
    };

    std::vector<Placement> placements;
    placements.reserve(referencedFileNames.size());

    std::unordered_set<std::string> stillOnDisk;

    int page = 0;
    int cursorX = ATLAS_PADDING;
    int cursorY = ATLAS_PADDING;
    int shelfHeight = 0;

    GLint maxLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);

    for (int i = 0; i < static_cast<int>(referencedFileNames.size()); ++i) {
        const std::string& fileName = referencedFileNames[i];
        const fs::path path = ProjectManager::GetAssetsPath() / fs::path(fileName).lexically_normal();

        const DecodedImage* image = GetDecodedImage(path);
        if (image == nullptr) continue;

        stillOnDisk.insert(path.string());

        const int textureWidth = image->width;
        const int textureHeight = image->height;

        if (textureWidth + ATLAS_PADDING * 2 > ATLAS_SIZE ||
            textureHeight + ATLAS_PADDING * 2 > ATLAS_SIZE) {
            spdlog::error("Texture '{}' is too large for atlas: {}x{}", fileName, textureWidth, textureHeight);
            continue;
        }

        if (cursorX + textureWidth + ATLAS_PADDING > ATLAS_SIZE) {
            cursorX = ATLAS_PADDING;
            cursorY += shelfHeight + ATLAS_PADDING;
            shelfHeight = 0;
        }

        if (cursorY + textureHeight + ATLAS_PADDING > ATLAS_SIZE) {
            if (page + 1 >= maxLayers) {
                spdlog::error("Texture atlas is full ({} pages). Could not add '{}'", maxLayers, fileName);
                continue;
            }

            ++page;
            cursorX = ATLAS_PADDING;
            cursorY = ATLAS_PADDING;
            shelfHeight = 0;
        }

        constexpr float halfTexel = .5f;

        const float uMin = (cursorX + halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float vMin = (cursorY + halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float uMax = (cursorX + textureWidth - halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float vMax = (cursorY + textureHeight - halfTexel) / static_cast<float>(ATLAS_SIZE);

        textureRegions[i] = {
            {uMin, vMin, uMax, vMax},
            {1.0f, static_cast<float>(page), static_cast<float>(textureWidth), static_cast<float>(textureHeight)}
        };

        // Only recorded once packing has actually succeeded, so a texture
        // that failed to load/pack correctly resolves through
        // GetTextureRegionIndex() to -1 ("no texture") instead of pointing
        // at an unused, zero-initialized region slot.
        textureRegionIndexByName[fileName] = i;

        placements.push_back({image, page, cursorX, cursorY});

        cursorX += textureWidth + ATLAS_PADDING;
        shelfHeight = std::max(shelfHeight, textureHeight);
    }

    // Images deleted from disk since the last build don't need to stay decoded.
    std::erase_if(decodedImageCache, [&](const auto& entry) { return !stillOnDisk.contains(entry.first); });

    const int pageCount = page + 1;

    // Pass 2: fill each page on the CPU and upload it as one layer.
    glGenTextures(1, &atlasTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTexture);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE, pageCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    std::vector<unsigned char> atlasPixels(ATLAS_SIZE * ATLAS_SIZE * 4);

    auto copyPixel = [&](int dstX, int dstY, int srcX, int srcY) {
        if (dstX < 0 || dstX >= ATLAS_SIZE || dstY < 0 || dstY >= ATLAS_SIZE) return;
        if (srcX < 0 || srcX >= ATLAS_SIZE || srcY < 0 || srcY >= ATLAS_SIZE) return;

        unsigned char* dst = atlasPixels.data() + (dstY * ATLAS_SIZE + dstX) * 4;

        const unsigned char* src = atlasPixels.data() + (srcY * ATLAS_SIZE + srcX) * 4;

        std::memcpy(dst, src, 4);
    };

    for (int currentPage = 0; currentPage < pageCount; ++currentPage) {
        std::ranges::fill(atlasPixels, 0);

        for (const Placement& placement : placements) {
            if (placement.page != currentPage) continue;

            const int textureWidth = placement.image->width;
            const int textureHeight = placement.image->height;
            const int x0 = placement.x;
            const int y0 = placement.y;

            for (int row = 0; row < textureHeight; ++row) {
                const unsigned char* srcRow = placement.image->pixels.data() + row * textureWidth * 4;
                unsigned char* dstRow = atlasPixels.data() + ((y0 + row) * ATLAS_SIZE + x0) * 4;

                std::memcpy(dstRow, srcRow, textureWidth * 4);
            }

            for (int pad = 1; pad <= ATLAS_PADDING; ++pad) {
                // Left and right padding
                for (int y = 0; y < textureHeight; ++y) {
                    copyPixel(x0 - pad, y + y0, x0, y + y0);
                    copyPixel(x0 + textureWidth - 1 + pad, y + y0, x0 + textureWidth - 1, y + y0);
                }

                // Top and bottom padding
                for (int x = 0; x < textureWidth; ++x) {
                    copyPixel(x + x0, y0 - pad, x + x0, y0);
                    copyPixel(x + x0, y0 + textureHeight - 1 + pad, x + x0, y0 + textureHeight - 1);
                }

                // Corners
                for (int yPad = 1; yPad <= ATLAS_PADDING; ++yPad) {
                    copyPixel(x0 - pad, y0 - yPad, x0, y0);
                    copyPixel(x0 + textureWidth - 1 + pad, y0 - yPad, x0 + textureWidth - 1, y0);
                    copyPixel(x0 - pad, y0 + textureHeight - 1 + yPad, x0, y0 + textureHeight - 1);
                    copyPixel(x0 + textureWidth - 1 + pad, y0 + textureHeight - 1 + yPad, x0 + textureWidth - 1, y0 + textureHeight - 1);
                }
            }
        }

        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, currentPage, ATLAS_SIZE, ATLAS_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    ApplyTextureSampling(level.rendererSettings.textureSetting, GL_TEXTURE_2D_ARRAY);

    glGenBuffers(1, &textureRegionSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, textureRegionSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        textureRegions.size() * sizeof(GPUTextureRegion),
        textureRegions.empty() ? nullptr : textureRegions.data(),
        GL_STATIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);

    atlasSignature = signature;

    spdlog::info("Created texture atlas with {} texture region(s) on {} page(s)", placements.size(), pageCount);

    return true;
}

// Decodes an image to RGBA8 once and keeps it until the file changes on disk,
// so rebuilding the atlas doesn't re-read every image in the project.
// Returns nullptr if the file can't be loaded.
const OpenGLRendererInternal::DecodedImage* OpenGL::GetDecodedImage(const fs::path& path) {
    using namespace OpenGLRendererInternal;

    std::error_code ec;
    const fs::file_time_type writeTime = fs::last_write_time(path, ec);
    if (ec) {
        spdlog::error("Texture file does not exist: {}", path.string());
        return nullptr;
    }

    const std::string key = path.string();

    if (const auto found = decodedImageCache.find(key); found != decodedImageCache.end() && found->second.writeTime == writeTime)
        return &found->second;

    SDL_Surface* loadedSurface = IMG_Load(key.c_str());

    if (loadedSurface == nullptr) {
        spdlog::error("IMG_Load failed for {}: {}", key, SDL_GetError());
        return nullptr;
    }

    SDL_Surface* surface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loadedSurface);

    if (surface == nullptr) {
        spdlog::error("SDL_ConvertSurface failed for {}: {}", key, SDL_GetError());
        return nullptr;
    }

    DecodedImage image;
    image.width = surface->w;
    image.height = surface->h;
    image.writeTime = writeTime;
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);

    for (int row = 0; row < image.height; ++row) {
        const unsigned char* srcRow = static_cast<unsigned char*>(surface->pixels) + row * surface->pitch;
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(row) * image.width * 4, srcRow, image.width * 4);
    }

    SDL_DestroySurface(surface);

    return &(decodedImageCache[key] = std::move(image));
}

// The full refresh rescans every image under Assets and reloads the fonts,
// which stalls a frame. A texture already in the atlas needs none of that:
// walls and flats look their region up by name every frame. So only a name
// the atlas has never seen - an image added since it was built - pays.
void OpenGL::RefreshTexturesIfLevelNeedsNew() {
    const std::vector<std::string> referenced = CollectReferencedTextureFileNames(LevelManager::CurrentLevel());

    const auto isNew = [&](const std::string& name) {
        return !textureRegionIndexByName.contains(name) && !unresolvedLevelTextures.contains(name);
    };

    if (std::ranges::none_of(referenced, isNew)) return;

    RefreshTexturesFromLevel();

    // Whatever is still missing doesn't exist (yet); don't rescan for it
    // after every edit.
    for (const std::string& name : referenced)
        if (!textureRegionIndexByName.contains(name)) unresolvedLevelTextures.insert(name);
}

// Resolves a texture filename to its slot in the atlas built above.
// Returns -1 (the same "no texture" sentinel used throughout the wall/
// sector/sprite GPU builders) if the name is empty, was never
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

bool OpenGL::Initialize(const std::string windowName, const bool startHidden) {
    if (!InitSDL(windowName, startHidden)) {
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

    if (!startHidden) SDL_SetWindowRelativeMouseMode(window, true);

    spdlog::info("OpenGL renderer initialized successfully");

    return true;
}