#include "Headers/Editor/EditorTextureCache.hpp"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Renderer/IRenderer.hpp"

namespace {
    constexpr unsigned MAX_WORKERS = 4;
    constexpr std::chrono::milliseconds UPLOAD_BUDGET{4};

    struct DecodeJob {
        std::string name;
        std::filesystem::path fullPath;
        std::uint64_t generation;
    };

    struct DecodedImage {
        std::string name;
        std::filesystem::path fullPath;
        std::uint64_t generation;
        SDL_Surface* surface; // RGBA32; nullptr when decoding failed
        std::string error;    // SDL_GetError() is per thread, so it is copied here on the worker
    };

    IRenderer* textureRenderer = nullptr;

    // Main thread only. Every name asked for that finished loading; a failed
    // one has nullopt.
    std::unordered_map<std::string, std::optional<EditorTextureCache::Texture>> cache;

    // Main thread only. Every name with a load in flight, mapped to that
    // load's generation. A finished image whose generation no longer matches
    // (or whose name is gone, after Invalidate()) is stale and thrown away.
    std::unordered_map<std::string, std::uint64_t> pending;
    std::uint64_t nextGeneration = 1;

    // Main thread only. Decoded images waiting for their texture, carried
    // over between frames when the upload budget runs out.
    std::deque<DecodedImage> readyToUpload;

    std::mutex jobMutex;
    std::condition_variable jobAvailable;
    std::deque<DecodeJob> jobs;
    bool stopping = false;

    std::mutex resultMutex;
    std::vector<DecodedImage> results;

    std::vector<std::thread> workers;

    void WorkerLoop() {
        while (true) {
            DecodeJob job;
            {
                std::unique_lock lock(jobMutex);
                jobAvailable.wait(lock, [] { return stopping || !jobs.empty(); });

                if (stopping) return;

                job = std::move(jobs.front());
                jobs.pop_front();
            }

            SDL_Surface* surface = IMG_Load(job.fullPath.string().c_str());
            std::string error = surface == nullptr ? SDL_GetError() : std::string();

            // The renderer takes RGBA8 rows.
            if (surface != nullptr && surface->format != SDL_PIXELFORMAT_RGBA32) {
                SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
                if (converted == nullptr) error = SDL_GetError();
                SDL_DestroySurface(surface);
                surface = converted;
            }

            std::lock_guard lock(resultMutex);
            results.push_back({std::move(job.name), std::move(job.fullPath), job.generation, surface, std::move(error)});
        }
    }

    void StartWorkers() {
        if (!workers.empty()) return;

        stopping = false;

        // One core is left for the main thread. hardware_concurrency() may
        // report 0 when it can't tell.
        const unsigned hardwareThreads = std::thread::hardware_concurrency();
        const unsigned workerCount = std::clamp(hardwareThreads > 1 ? hardwareThreads - 1 : 1u, 1u, MAX_WORKERS);

        workers.reserve(workerCount);
        for (unsigned i = 0; i < workerCount; ++i) workers.emplace_back(WorkerLoop);
    }
}

namespace EditorTextureCache {
    void SetRenderer(IRenderer* renderer) {
        textureRenderer = renderer;
    }

    const Texture* Get(const std::string& relativeFileName) {
        if (relativeFileName.empty() || textureRenderer == nullptr) return nullptr;

        if (const auto found = cache.find(relativeFileName); found != cache.end())
            return found->second.has_value() ? &*found->second : nullptr; // nullopt: a previously failed load

        if (pending.contains(relativeFileName)) return nullptr;

        // Already loaded by the game or the runtime editor.
        if (IRenderer::LoadedTexture loaded; textureRenderer->FindLoadedTexture(relativeFileName, &loaded)) {
            const auto [it, inserted] = cache.emplace(relativeFileName, Texture{static_cast<ImTextureID>(loaded.id), loaded.id, loaded.width, loaded.height});
            return &*it->second;
        }

        StartWorkers();

        const std::uint64_t generation = nextGeneration++;
        pending.emplace(relativeFileName, generation);

        {
            std::lock_guard lock(jobMutex);
            jobs.push_back({
                relativeFileName,
                ProjectManager::GetAssetsPath() / std::filesystem::path(relativeFileName).lexically_normal(),
                generation
            });
        }

        jobAvailable.notify_one();
        return nullptr;
    }

    void Update() {
        if (textureRenderer == nullptr) return;

        {
            std::lock_guard lock(resultMutex);
            for (DecodedImage& image : results) readyToUpload.push_back(std::move(image));
            results.clear();
        }

        const auto start = std::chrono::steady_clock::now();
        bool uploadedAny = false;

        while (!readyToUpload.empty()) {
            if (uploadedAny && std::chrono::steady_clock::now() - start >= UPLOAD_BUDGET) break;

            DecodedImage image = std::move(readyToUpload.front());
            readyToUpload.pop_front();

            const auto pendingIt = pending.find(image.name);

            if (pendingIt == pending.end() || pendingIt->second != image.generation) {
                SDL_DestroySurface(image.surface); // safe on nullptr
                continue;
            }

            pending.erase(pendingIt);

            std::optional<Texture> texture;
            IRenderer::LoadedTexture loaded;

            if (image.surface != nullptr) {
                // The game may have loaded it meanwhile; then its texture is used.
                if (!textureRenderer->FindLoadedTexture(image.name, &loaded) &&
                    !textureRenderer->AddDecodedTexture(image.name, image.fullPath, image.surface->pixels, image.surface->w, image.surface->h))
                    image.error = "the renderer could not make a texture";

                SDL_DestroySurface(image.surface);
                uploadedAny = true;
            }

            if (textureRenderer->FindLoadedTexture(image.name, &loaded))
                texture = Texture{static_cast<ImTextureID>(loaded.id), loaded.id, loaded.width, loaded.height};

            if (!texture.has_value()) spdlog::warn("EditorTextureCache: failed to load {}: {}", image.fullPath.string(), image.error);
            else spdlog::info("EditorTextureCache: loaded {}", image.fullPath.string());

            // Cache the result either way so a broken reference doesn't retry
            // a disk read on every single frame.
            cache.insert_or_assign(std::move(image.name), texture);
        }
    }

    void Invalidate(const std::string& relativeFileName) {
        pending.erase(relativeFileName);
        cache.erase(relativeFileName);
    }

    void Destroy() {
        {
            std::lock_guard lock(jobMutex);
            stopping = true;
            jobs.clear();
        }

        jobAvailable.notify_all();

        for (std::thread& worker : workers) worker.join();
        workers.clear();

        for (DecodedImage& image : results) SDL_DestroySurface(image.surface);
        results.clear();

        for (DecodedImage& image : readyToUpload) SDL_DestroySurface(image.surface);
        readyToUpload.clear();

        pending.clear();
        cache.clear();
        textureRenderer = nullptr;
    }
}
