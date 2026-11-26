// The texture atlas and standalone textures, shared by every backend. The
// backends only make, fill and destroy the GPU objects (RendererCore hooks).

#include "Headers/Runtime/Renderer/RendererCore.hpp"
#include "Headers/Engine/AssetFS.hpp"
#include "Headers/Engine/AssetFSImage.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/Engine/FolderWatcher.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/TextureTiling.hpp"
#include "Headers/Project/AssetSets.hpp"
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
    // Safe on any thread: the background atlas load runs it on a worker.
    std::vector<std::string> CollectProjectImageFileNames(const fs::path& assetsPath) {
        static const std::set<std::string> kImageExtensions = {".png", ".jpg", ".jpeg"};

        std::vector<std::string> imageNames;

        if (AssetFS::IsPacked()) {
            for (const fs::path& file : AssetFS::ListPackedFiles(assetsPath, true)) {
                const fs::path relative = file.lexically_relative(assetsPath);

                // Hidden entries aren't shown in the Asset Browser either.
                bool hidden = false;
                for (const fs::path& part : relative) {
                    const std::string partName = part.string();
                    if (!partName.empty() && partName.front() == '.') hidden = true;
                }
                if (hidden) continue;

                std::string extension = file.extension().string();
                std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (kImageExtensions.contains(extension)) imageNames.push_back(relative.generic_string());
            }

            std::ranges::sort(imageNames);
            return imageNames;
        }

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

    std::vector<std::string> CollectProjectImageFileNames() {
        return CollectProjectImageFileNames(ProjectManager::GetAssetsPath());
    }

    size_t WriteTimeHash(const fs::path& assetsPath, const std::string& fileName) {
        fs::file_time_type writeTime;
        const bool found = AssetFS::WriteTime(assetsPath / fs::path(fileName).lexically_normal(), writeTime);
        return !found ? 0 : std::hash<long long>{}(static_cast<long long>(writeTime.time_since_epoch().count()));
    }

    size_t WriteTimeHash(const std::string& fileName) {
        return WriteTimeHash(ProjectManager::GetAssetsPath(), fileName);
    }

    // Reads an image to tightly packed RGBA8. Safe on any thread. The
    // parallel decodes below are quiet: a file they can't read is tried
    // again by GetDecodedImage, which reports it once.
    bool DecodeImageFile(const fs::path& path, DecodedImage& out, const bool logErrors = true) {
        fs::file_time_type writeTime;
        if (!AssetFS::WriteTime(path, writeTime)) {
            if (logErrors) spdlog::error("Texture file does not exist: {}", path.string());
            return false;
        }

        const std::string key = path.string();
        SDL_Surface* loadedSurface = AssetFS::IsPacked() ? AssetFS::LoadImage(path) : IMG_Load(key.c_str());

        if (loadedSurface == nullptr) {
            if (logErrors) spdlog::error("IMG_Load failed for {}: {}", key, SDL_GetError());
            return false;
        }

        SDL_Surface* surface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loadedSurface);

        if (surface == nullptr) {
            if (logErrors) spdlog::error("SDL_ConvertSurface failed for {}: {}", key, SDL_GetError());
            return false;
        }

        out.width = surface->w;
        out.height = surface->h;
        out.writeTime = writeTime;
        out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 4);

        for (int row = 0; row < out.height; ++row) {
            const unsigned char* srcRow = static_cast<unsigned char*>(surface->pixels) + row * surface->pitch;
            std::memcpy(out.pixels.data() + static_cast<std::size_t>(row) * out.width * 4, srcRow, out.width * 4);
        }

        SDL_DestroySurface(surface);
        return true;
    }

    // Decodes `paths` on several threads (this one among them); a file that
    // fails stays empty. Stops early once `cancelled` is set, and counts
    // each decoded file in `done`.
    std::vector<std::optional<DecodedImage>> DecodeImagesInParallel(const std::vector<fs::path>& paths,
                                                                     const std::atomic<bool>* cancelled = nullptr,
                                                                     std::atomic<size_t>* done = nullptr) {
        std::vector<std::optional<DecodedImage>> images(paths.size());
        std::atomic<size_t> next{0};

        const auto work = [&] {
            while (cancelled == nullptr || !*cancelled) {
                const size_t index = next.fetch_add(1);
                if (index >= paths.size()) return;

                if (DecodedImage image; DecodeImageFile(paths[index], image, false)) images[index] = std::move(image);
                if (done != nullptr) done->fetch_add(1);
            }
        };

        // Up to six threads, leaving cores for the editor and the sound load.
        const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
        const size_t helperCount = std::min<size_t>({cores - 1, 5, paths.empty() ? 0 : paths.size() - 1});

        std::vector<std::thread> helpers;
        helpers.reserve(helperCount);
        for (size_t i = 0; i < helperCount; ++i) helpers.emplace_back(work);

        work();
        for (std::thread& helper : helpers) helper.join();

        return images;
    }
}

// The engine's first atlas: the image list, its write times and every image
// decoded on a worker, then built on the main thread by UpdateBackgroundLoad
// (or FinishBackgroundLoad, which Play calls) from what it left in the caches.
struct RendererCore::BackgroundAtlasLoad {
    fs::path assetsPath;
    std::uint64_t imageGeneration = 0; // the image watcher's, read before the scan

    // Written by the worker before `finished`.
    std::vector<std::string> projectImages;
    std::unordered_map<std::string, size_t> writeTimes;
    std::vector<std::pair<std::string, DecodedImage>> decoded; // keyed as decodedImageCache is

    std::atomic<bool> listed{false};
    std::atomic<size_t> total{0};
    std::atomic<size_t> done{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> finished{false};

    std::thread worker;

    ~BackgroundAtlasLoad() {
        cancelled = true;
        if (worker.joinable()) worker.join();
    }

    void Run() {
        projectImages = CollectProjectImageFileNames(assetsPath);

        std::vector<fs::path> paths;
        paths.reserve(projectImages.size());

        for (const std::string& fileName : projectImages) {
            if (cancelled) break;
            writeTimes.emplace(fileName, WriteTimeHash(assetsPath, fileName));
            paths.push_back(assetsPath / fs::path(fileName).lexically_normal());
        }

        total = paths.size();
        listed = true;

        std::vector<std::optional<DecodedImage>> images = DecodeImagesInParallel(paths, &cancelled, &done);

        decoded.reserve(images.size());
        for (size_t i = 0; i < images.size(); ++i)
            if (images[i]) decoded.emplace_back(paths[i].string(), std::move(*images[i]));

        finished = true;
    }
};

// A level change's images, decoding on worker threads (BeginTexturePreload).
struct RendererCore::TexturePreload {
    std::vector<fs::path> paths;
    std::vector<std::optional<DecodedImage>> images; // written by the worker before `finished`
    std::atomic<size_t> done{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> finished{false};
    std::thread worker;

    ~TexturePreload() {
        cancelled = true;
        if (worker.joinable()) worker.join();
    }
};

void RendererCore::BeginTexturePreload(const std::vector<std::string>& imageNames) {
    FinishTexturePreload();

    auto job = std::make_shared<TexturePreload>();
    for (const std::string& fileName : imageNames) {
        fs::path path = ProjectManager::GetAssetsPath() / fs::path(fileName).lexically_normal();
        if (!decodedImageCache.contains(path.string())) job->paths.push_back(std::move(path));
    }
    if (job->paths.empty()) return;

    job->worker = std::thread([raw = job.get()] {
        raw->images = DecodeImagesInParallel(raw->paths, &raw->cancelled, &raw->done);
        raw->finished = true;
    });
    texturePreload = std::move(job);
}

std::pair<size_t, size_t> RendererCore::TexturePreloadProgress() const {
    if (!texturePreload) return {0, 0};
    return {texturePreload->done.load(), texturePreload->paths.size()};
}

void RendererCore::FinishTexturePreload() {
    if (!texturePreload) return;

    const std::shared_ptr<TexturePreload> job = std::move(texturePreload);
    if (job->worker.joinable()) job->worker.join();

    for (size_t i = 0; i < job->images.size() && i < job->paths.size(); ++i)
        if (job->images[i]) decodedImageCache[job->paths[i].string()] = std::move(*job->images[i]);
}

bool RendererCore::BuildTextureAtlasFromLevel() {
#ifndef TILKY_STANDALONE
    // Still decoding: UpdateBackgroundLoad builds it, with whatever the
    // level uses by then. Until then the 3D view draws untextured.
    if (backgroundAtlas) return true;

    // The engine's first atlas decodes in the background so the editor
    // opens at once; the exported game builds its own before the first frame.
    if (!backgroundAtlasStarted && !AtlasExists()) {
        backgroundAtlasStarted = true;

        auto job = std::make_shared<BackgroundAtlasLoad>();
        job->assetsPath = ProjectManager::GetAssetsPath();
        // Read before the scan, like below: a change during it rescans.
        job->imageGeneration = imageWatcher.Generation(job->assetsPath);
        job->worker = std::thread([raw = job.get()] { raw->Run(); });
        backgroundAtlas = std::move(job);

        spdlog::info("Building the texture atlas in the background");
        return true;
    }
#endif

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
    // An exported game with per-level sets: only the level's (and the
    // Always Loaded ones), still in name order.
    lateTexturesEnabled = AssetFS::IsPacked() && AssetSets::HasPackedSets();

    std::vector<std::string> referencedFileNames;
    if (lateTexturesEnabled) {
        for (std::string& fileName : CollectProjectImageFileNames())
            if (AssetSets::InCurrentSet(fileName)) referencedFileNames.push_back(std::move(fileName));
    }
    else referencedFileNames = CollectProjectImageFileNames();

    // Late textures of an earlier level go with it.
    if (lateTexturesGeneration != AssetSets::SessionGeneration()) {
        lateTextures.clear();
        lateTexturesGeneration = AssetSets::SessionGeneration();
    }
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
        for (const std::string& fileName : lateTextures) addIfMissing(fileName);
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

    // Images never decoded before, decoded together on several threads.
    // Ones decoded already but changed since are few; GetDecodedImage
    // redoes those below.
    {
        std::vector<fs::path> notDecoded;

        for (const std::string& fileName : referencedFileNames) {
            fs::path path = ProjectManager::GetAssetsPath() / fs::path(fileName).lexically_normal();
            if (!decodedImageCache.contains(path.string())) notDecoded.push_back(std::move(path));
        }

        if (notDecoded.size() > 1) {
            std::vector<std::optional<DecodedImage>> images = DecodeImagesInParallel(notDecoded);
            for (size_t i = 0; i < images.size(); ++i)
                if (images[i]) decodedImageCache[notDecoded[i].string()] = std::move(*images[i]);
        }
    }

    AtlasCursor cursor;

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

        int page = 0;
        int cursorX = 0;
        int cursorY = 0;
        if (!PlaceInAtlas(textureWidth, textureHeight, maxLayers, cursor, page, cursorX, cursorY)) {
            spdlog::error("Texture atlas is full ({} pages). Could not add '{}'", maxLayers, fileName);
            continue;
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
    }

    // Images deleted from disk since the last build don't need to stay decoded.
    std::erase_if(decodedImageCache, [&](const auto& entry) { return !stillOnDisk.contains(entry.first); });

    // Room for textures used outside the set: the rest of the last page and a spare one.
    const int pageCount = std::min(cursor.page + 1 + (lateTexturesEnabled ? 1 : 0), std::max(maxLayers, 1));
    atlasPageCount = pageCount;
    atlasCursor = cursor;

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

bool RendererCore::PlaceInAtlas(const int width, const int height, const int pageLimit, AtlasCursor& cursor, int& outPage, int& outX,
                                int& outY) {
    if (cursor.x + width + ATLAS_PADDING > ATLAS_SIZE) {
        cursor.x = ATLAS_PADDING;
        cursor.y += cursor.shelfHeight + ATLAS_PADDING;
        cursor.shelfHeight = 0;
    }

    if (cursor.y + height + ATLAS_PADDING > ATLAS_SIZE) {
        if (cursor.page + 1 >= pageLimit) return false;

        ++cursor.page;
        cursor.x = ATLAS_PADDING;
        cursor.y = ATLAS_PADDING;
        cursor.shelfHeight = 0;
    }

    outPage = cursor.page;
    outX = cursor.x;
    outY = cursor.y;

    cursor.x += width + ATLAS_PADDING;
    cursor.shelfHeight = std::max(cursor.shelfHeight, height);
    return true;
}

// Textures first used last frame that the atlas lacks (exported games with
// per-level sets only): into the spare room, or a rebuild once it's full.
void RendererCore::ServiceLateTextures() {
    if (missedTextures.empty()) return;

    std::vector<std::string> names(missedTextures.begin(), missedTextures.end());
    missedTextures.clear();
    std::ranges::sort(names);

    if (!lateTexturesEnabled || !AtlasExists()) return;

    const Level& level = LevelManager::CurrentLevel();
    bool added = false;
    bool rebuild = false;

    for (const std::string& fileName : names) {
        if (textureRegionIndexByName.contains(fileName) || unresolvedLevelTextures.contains(fileName)) continue;

        const fs::path path = ProjectManager::GetAssetsPath() / fs::path(fileName).lexically_normal();
        const DecodedImage* image = AssetFS::IsFile(path) ? GetDecodedImage(path) : nullptr;

        if (image == nullptr || image->width + ATLAS_PADDING * 2 > ATLAS_SIZE || image->height + ATLAS_PADDING * 2 > ATLAS_SIZE) {
            unresolvedLevelTextures.insert(fileName);
            continue;
        }

        AssetSets::NoteOnDemandUse(fileName, "Texture");
        lateTextures.push_back(fileName);

        int page = 0;
        int x = 0;
        int y = 0;
        if (rebuild || !PlaceInAtlas(image->width, image->height, atlasPageCount, atlasCursor, page, x, y)) {
            rebuild = true;
            continue;
        }

        // The image with its edges repeated ATLAS_PADDING out, as the full build pads.
        const int paddedWidth = image->width + ATLAS_PADDING * 2;
        const int paddedHeight = image->height + ATLAS_PADDING * 2;
        std::vector<unsigned char> padded(static_cast<size_t>(paddedWidth) * paddedHeight * 4);
        for (int row = 0; row < paddedHeight; ++row) {
            const int sourceRow = std::clamp(row - ATLAS_PADDING, 0, image->height - 1);
            for (int column = 0; column < paddedWidth; ++column) {
                const int sourceColumn = std::clamp(column - ATLAS_PADDING, 0, image->width - 1);
                std::memcpy(padded.data() + (static_cast<size_t>(row) * paddedWidth + column) * 4,
                            image->pixels.data() + (static_cast<size_t>(sourceRow) * image->width + sourceColumn) * 4, 4);
            }
        }
        UploadAtlasRegionGpu(page, x - ATLAS_PADDING, y - ATLAS_PADDING, paddedWidth, paddedHeight, padded.data());

        constexpr float halfTexel = .5f;
        textureRegions.push_back({
            {(x + halfTexel) / static_cast<float>(ATLAS_SIZE), (y + halfTexel) / static_cast<float>(ATLAS_SIZE),
             (x + image->width - halfTexel) / static_cast<float>(ATLAS_SIZE), (y + image->height - halfTexel) / static_cast<float>(ATLAS_SIZE)},
            {1.0f, static_cast<float>(page), static_cast<float>(image->width), static_cast<float>(image->height)}
        });
        textureRegionIndexByName[fileName] = static_cast<int>(textureRegions.size()) - 1;
        added = true;

        spdlog::info("Added texture '{}' to the atlas on demand", fileName);
    }

    if (rebuild) {
        spdlog::info("The atlas's spare room is used up: rebuilding it with the late textures");
        atlasSignature = 0;
        if (!BuildTextureAtlasFromLevel()) spdlog::error("Failed to rebuild the texture atlas");
        return;
    }

    if (added) RefreshAtlasGpu(level.rendererSettings.textureSetting);
}

// Decodes an image to RGBA8 once and keeps it until the file changes on disk,
// so rebuilding the atlas doesn't re-read every image in the project.
// Returns nullptr if the file can't be loaded.
const DecodedImage* RendererCore::GetDecodedImage(const fs::path& path) {
    fs::file_time_type writeTime;
    if (!AssetFS::WriteTime(path, writeTime)) {
        spdlog::error("Texture file does not exist: {}", path.string());
        return nullptr;
    }

    const std::string key = path.string();

    if (const auto found = decodedImageCache.find(key); found != decodedImageCache.end() && found->second.writeTime == writeTime)
        return &found->second;

    DecodedImage image;
    if (!DecodeImageFile(path, image)) return nullptr;

    return &(decodedImageCache[key] = std::move(image));
}

void RendererCore::UpdateBackgroundLoad() {
    if (backgroundAtlas && backgroundAtlas->finished) FinishBackgroundLoad();
}

void RendererCore::FinishBackgroundLoad() {
    if (!backgroundAtlas) return;

    if (!backgroundAtlas->finished) spdlog::info("Waiting for the background texture atlas to finish");

    const std::shared_ptr<BackgroundAtlasLoad> job = std::move(backgroundAtlas);
    if (job->worker.joinable()) job->worker.join();

#ifndef TILKY_STANDALONE
    // What the worker found is what a scan here would: the build below
    // reuses it and only decodes what the level adds from outside Assets.
    cachedProjectImages = std::move(job->projectImages);
    cachedWriteTimes = std::move(job->writeTimes);
    cachedImageGeneration = job->imageGeneration;
#endif

    for (auto& [key, image] : job->decoded) decodedImageCache[key] = std::move(image);

    // Names that went unresolved while there was no atlas may be in it now.
    unresolvedLevelTextures.clear();

    if (!BuildTextureAtlasFromLevel()) {
        spdlog::error("Failed to build texture atlas from level");
        return;
    }

    spdlog::info("Built texture atlas with {} texture region(s) in the background", textureRegions.size());
}

bool RendererCore::IsBackgroundLoading() const {
    return backgroundAtlas != nullptr;
}

IRenderer::BackgroundLoadProgress RendererCore::GetBackgroundLoadProgress() const {
    if (!backgroundAtlas) return {};

    BackgroundLoadProgress progress;
    progress.listed = backgroundAtlas->listed;
    progress.total = backgroundAtlas->total;
    progress.done = backgroundAtlas->done;
    return progress;
}

void RendererCore::CancelBackgroundAtlas() {
    // Its destructor stops the worker and waits for it.
    backgroundAtlas.reset();
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

    if (found == textureRegionIndexByName.end()) {
        // Loaded at the start of the next frame (exported games with per-level sets).
        if (lateTexturesEnabled) missedTextures.insert(fileName);
        return -1;
    }

    // Editor Play: a texture outside the level's set is reported once.
    if (AssetSets::HasCurrentSet()) {
        if (regionSetCheckedGeneration != AssetSets::SessionGeneration() || regionSetChecked.size() != textureRegions.size()) {
            regionSetChecked.assign(textureRegions.size(), 0);
            regionSetCheckedGeneration = AssetSets::SessionGeneration();
        }

        const auto index = static_cast<size_t>(found->second);
        if (index < regionSetChecked.size() && regionSetChecked[index] == 0) {
            regionSetChecked[index] = 1;
            if (!AssetSets::InCurrentSet(fileName)) AssetSets::NoteOnDemandUse(fileName, "Texture");
        }
    }

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
    SDL_Surface* loadedSurface = AssetFS::IsPacked() ? AssetFS::LoadImage(path) : IMG_Load(path.string().c_str());

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
    out.id = UploadTextureGpu(pixels, width, height);
    out.width = width;
    out.height = height;
    out.path = path;
    // As fs::last_write_time with an error code: min() for a missing file.
    if (!AssetFS::WriteTime(path, out.writeTime)) out.writeTime = fs::file_time_type::min();
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

        fs::file_time_type writeTime;

        // A deleted file keeps its last good texture rather than going blank.
        if (!AssetFS::WriteTime(texture.path, writeTime) || writeTime == texture.writeTime) continue;

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

    // Still in the background: FinishBackgroundLoad reports it.
    if (backgroundAtlas) return;

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

    CancelBackgroundAtlas();
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
