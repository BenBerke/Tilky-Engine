// The texture atlas and standalone textures, shared by every backend. The
// backends only make, fill and destroy the GPU objects (RendererCore hooks).

#include "Headers/Runtime/Renderer/RendererCore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/Engine/FolderWatcher.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/TextureTiling.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace {
    using namespace RendererGpu;

    std::vector<std::string> CollectReferencedTextureFileNames(const Level& level) {
        std::set<std::string> uniqueNames;

        // Every frame, so an animation never waits on the atlas mid-Play.
        const auto addSurface = [&uniqueNames](const SurfaceTexture& texture) {
            for (const TextureFrame& frame : texture.frames)
                if (!frame.texture.empty()) uniqueNames.insert(frame.texture);
        };

        for (const Wall& wall : level.walls) {
            addSurface(wall.top.texture);
            addSurface(wall.bottom.texture);
            addSurface(wall.middle.texture);
        }

        for (const Sector& sector : level.sectors) {
            for (const SectorFloor& floor : sector.floors) {
                addSurface(floor.floor.texture);
                addSurface(floor.ceiling.texture);
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

bool RendererCore::BuildTextureAtlasFromLevel() {
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
        for (const ComponentUIButton& button : level.ui_buttons.components)
            for (const std::string* texture : {&button.normalTexture, &button.hoverTexture, &button.pressedTexture, &button.disabledTexture})
                addIfMissing(*texture);
        addIfMissing(level.worldSettings.messageStyle.backgroundTexture);
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

    if (AtlasExists() && signature == atlasSignature) {
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

    const int maxLayers = MaxAtlasPages();

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
    CreateAtlasGpu(pageCount);

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

        UploadAtlasPage(currentPage, atlasPixels.data());
    }

    FinishAtlasGpu(level.rendererSettings.textureSetting);

    atlasSignature = signature;

    spdlog::info("Created texture atlas with {} texture region(s) on {} page(s)", placements.size(), pageCount);

    return true;
}

// Decodes an image to RGBA8 once and keeps it until the file changes on disk,
// so rebuilding the atlas doesn't re-read every image in the project.
// Returns nullptr if the file can't be loaded.
const DecodedImage* RendererCore::GetDecodedImage(const fs::path& path) {
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
void RendererCore::RefreshTexturesIfLevelNeedsNew() {
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
int RendererCore::GetTextureRegionIndex(const std::string& fileName) const {
    if (fileName.empty()) return -1;

    const auto found = textureRegionIndexByName.find(fileName);

    if (found == textureRegionIndexByName.end()) return -1;

    return found->second;
}

// Same tile size as GetTextureTileSize() in Rendering.vs.glsl.
Vector2 RendererCore::SurfaceScroll(const SurfaceTexture& texture, const int regionIndex) const {
    if (texture.scrollX == 0.0 && texture.scrollY == 0.0) return {0.0f, 0.0f};

    Vector2 tile = {TextureTiling::FALLBACK_TILE_SIZE, TextureTiling::FALLBACK_TILE_SIZE};

    if (regionIndex >= 0 && regionIndex < static_cast<int>(textureRegions.size())) {
        const Vector4& data = textureRegions[static_cast<size_t>(regionIndex)].data;
        const float texelsPerUnit = std::max(ProjectManager::GetTexelsPerUnit(), 0.01f);
        if (data.z >= 0.5f && data.w >= 0.5f) tile = {data.z / texelsPerUnit, data.w / texelsPerUnit};
    }

    return texture.WrappedScroll(tile);
}

// ============================================================================
// Standalone textures
// ============================================================================

bool RendererCore::LoadTextureFile(const fs::path& path, GPUTexture& out) {
    SDL_Surface* loadedSurface = IMG_Load(path.string().c_str());

    if (loadedSurface == nullptr) {
        spdlog::error("IMG_Load failed for {}: {}", path.string(), SDL_GetError());
        return false;
    }

    SDL_Surface* convertedSurface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);

    SDL_DestroySurface(loadedSurface);

    if (convertedSurface == nullptr) {
        spdlog::error("SDL_ConvertSurface failed for {}: {}", path.string(), SDL_GetError());
        return false;
    }

    // Tightly packed rows for the backends.
    std::vector<unsigned char> pixels;
    const void* rows = convertedSurface->pixels;

    if (convertedSurface->pitch != convertedSurface->w * 4) {
        pixels.resize(static_cast<size_t>(convertedSurface->w) * convertedSurface->h * 4);
        for (int row = 0; row < convertedSurface->h; ++row)
            std::memcpy(pixels.data() + static_cast<size_t>(row) * convertedSurface->w * 4,
                        static_cast<const unsigned char*>(convertedSurface->pixels) + row * convertedSurface->pitch,
                        static_cast<size_t>(convertedSurface->w) * 4);
        rows = pixels.data();
    }

    UploadTexturePixels(path, rows, convertedSurface->w, convertedSurface->h, out);

    SDL_DestroySurface(convertedSurface);

    return true;
}

void RendererCore::UploadTexturePixels(const fs::path& path, const void* pixels, const int width, const int height, GPUTexture& out) {
    std::error_code ec;

    out.id = UploadTextureGpu(pixels, width, height);
    out.width = width;
    out.height = height;
    out.path = path;
    out.writeTime = fs::last_write_time(path, ec);
}

bool RendererCore::FindLoadedTexture(const std::string& fileName, LoadedTexture* out) const {
    const auto found = textureIndexByName.find(fileName);
    if (found == textureIndexByName.end() || found->second < 0 || found->second >= GetTextureCount()) return false;

    const GPUTexture& texture = textures[found->second];
    if (texture.id == 0) return false;

    *out = {texture.id, texture.width, texture.height, TextureImGuiID(texture.id)};
    return true;
}

bool RendererCore::AddDecodedTexture(const std::string& fileName, const fs::path& path, const void* pixels, const int width,
                                     const int height) {
    if (fileName.empty() || textureIndexByName.contains(fileName)) return false;

    // A failed decode is cached as a failed load is, so it isn't retried every frame.
    if (pixels == nullptr || width <= 0 || height <= 0) {
        textureIndexByName.emplace(fileName, -1);
        return false;
    }

    GPUTexture texture;
    UploadTexturePixels(path, pixels, width, height, texture);
    textures.push_back(std::move(texture));
    textureIndexByName.emplace(fileName, static_cast<int>(textures.size()) - 1);
    return true;
}

int RendererCore::CreateTexture(const std::string& fileName) {
    fs::path path(fileName);

    if (!path.is_absolute()) path = ProjectManager::GetAssetsPath() / fs::path(fileName).lexically_normal();

    GPUTexture texture;
    if (!LoadTextureFile(path, texture)) return -1;

    textures.push_back(std::move(texture));

    return static_cast<int>(textures.size()) - 1;
}

void RendererCore::ReloadChangedTextures() {
    for (GPUTexture& texture : textures) {
        if (texture.id == 0 || texture.path.empty()) continue;

        std::error_code ec;
        const fs::file_time_type writeTime = fs::last_write_time(texture.path, ec);

        // A deleted file keeps its last good texture rather than going blank.
        if (ec || writeTime == texture.writeTime) continue;

        GPUTexture reloaded;
        if (!LoadTextureFile(texture.path, reloaded)) continue;

        DestroyTextureGpu(texture.id);
        texture = std::move(reloaded);
    }
}

// Lazy, filename-keyed resolution on top of the plain textures[] list
// above (NOT the atlas - see GetTextureRegionIndex for that). Used by
// anything that binds a single standalone texture directly, such as
// the background. Loads and caches on first request; returns -1 (never
// crashes) if the file is missing or fails to decode - a failed load is
// cached too, so a broken reference isn't retried every frame.
int RendererCore::GetOrCreateTextureIndex(const std::string& fileName) {
    if (fileName.empty()) return -1;

    const auto found = textureIndexByName.find(fileName);
    if (found != textureIndexByName.end()) return found->second;

    const int index = CreateTexture(fileName);
    textureIndexByName.emplace(fileName, index); // cache -1 too, on failure

    return index;
}

void RendererCore::RefreshTexturesFromLevel() {
    // A model that failed to load may have been imported or fixed since.
    failedModelFiles.clear();
    unresolvedLevelTextures.clear();
    // Same for fonts; ones that loaded are reread in case the file changed.
    ReloadFonts();

    if (!BuildTextureAtlasFromLevel()) {
        spdlog::error("Failed to build texture atlas from level");
        return;
    }

    spdlog::info("Built texture atlas with {} texture region(s)", textureRegions.size());
}

const GPUTexture& RendererCore::GetTexture(const int index) const {
    return textures[index];
}

int RendererCore::GetTextureCount() const {
    return static_cast<int>(textures.size());
}

void RendererCore::DestroyAllTextures() {
    for (GPUTexture& texture : textures) {
        if (texture.id != 0) {
            DestroyTextureGpu(texture.id);
            texture.id = 0;
        }

        texture.width = 0;
        texture.height = 0;
    }

    textures.clear();
    textureIndexByName.clear();

    DestroyAtlas();
}

void RendererCore::DestroyAtlas() {
    DestroyAtlasGpu();
    textureRegions.clear();
}

ImTextureID RendererCore::GetImGuiTextureID(const std::string& fileName) {
    const int textureIndex = GetOrCreateTextureIndex(fileName);

    if (textureIndex < 0 || textureIndex >= GetTextureCount()) return ImTextureID{};

    return TextureImGuiID(GetTexture(textureIndex).id);
}
