#include "Headers/Map/TextureTiling.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

#include <SDL3_image/SDL_image.h>

#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace TextureTiling {
    namespace {
        struct ImageSize {
            fs::file_time_type writeTime{};
            int width = 0;
            int height = 0;
        };

        std::unordered_map<std::string, ImageSize> imageSizeCache;

        // Pixel size of the image at `path`, 0x0 when it can't be read.
        ImageSize ReadImageSize(const fs::path& path) {
            std::error_code ec;
            const fs::file_time_type writeTime = fs::last_write_time(path, ec);
            if (ec) return {};

            const std::string key = path.string();
            const auto it = imageSizeCache.find(key);
            if (it != imageSizeCache.end() && it->second.writeTime == writeTime) return it->second;

            ImageSize size;
            size.writeTime = writeTime;

            if (SDL_Surface* surface = IMG_Load(key.c_str())) {
                size.width = surface->w;
                size.height = surface->h;
                SDL_DestroySurface(surface);
            }

            imageSizeCache[key] = size;
            return size;
        }
    }

    Vector2 GetTileSize(const std::string& texture) {
        if (texture.empty()) return {FALLBACK_TILE_SIZE, FALLBACK_TILE_SIZE};

        const ImageSize size = ReadImageSize(ProjectManager::GetAssetsPath() / fs::path(texture).lexically_normal());
        if (size.width <= 0 || size.height <= 0) return {FALLBACK_TILE_SIZE, FALLBACK_TILE_SIZE};

        const float texelsPerUnit = (std::max)(ProjectManager::MIN_TEXELS_PER_UNIT, ProjectManager::GetTexelsPerUnit());
        return {static_cast<float>(size.width) / texelsPerUnit, static_cast<float>(size.height) / texelsPerUnit};
    }
}
