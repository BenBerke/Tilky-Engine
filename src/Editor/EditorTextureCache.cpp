#include "Headers/Editor/EditorTextureCache.hpp"

#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include "Headers/Project/ProjectManager.hpp"

namespace {
    std::unordered_map<std::string, SDL_Texture*> cache;
}

namespace EditorTextureCache {
    SDL_Texture* Get(SDL_Renderer* renderer, const std::string& relativeFileName) {
        if (relativeFileName.empty() || renderer == nullptr)
            return nullptr;

        const auto found = cache.find(relativeFileName);
        if (found != cache.end())
            return found->second; // may be nullptr, from a previously failed load

        const std::filesystem::path fullPath =
            ProjectManager::GetTexturesPath() / relativeFileName;

        SDL_Texture* texture = IMG_LoadTexture(renderer, fullPath.string().c_str());

        if (texture == nullptr) {
            spdlog::warn(
                "EditorTextureCache: failed to load {}: {}",
                fullPath.string(),
                SDL_GetError()
            );
        } else {
            spdlog::info("EditorTextureCache: loaded {}", fullPath.string());
        }

        // Cache the result either way so a broken reference doesn't retry
        // a disk read on every single frame.
        cache.emplace(relativeFileName, texture);
        return texture;
    }

    void Invalidate(const std::string& relativeFileName) {
        const auto found = cache.find(relativeFileName);
        if (found == cache.end())
            return;

        if (found->second != nullptr)
            SDL_DestroyTexture(found->second);

        cache.erase(found);
    }

    void Destroy() {
        for (auto& [name, texture] : cache) {
            if (texture != nullptr)
                SDL_DestroyTexture(texture);
        }

        cache.clear();
    }
}