#ifndef TILKY_ENGINE_ASSETFS_IMAGE_HPP
#define TILKY_ENGINE_ASSETFS_IMAGE_HPP

// SDL_image loading through AssetFS: from disk as before (IMG_Load), or from
// an exported game's Data.pak.

#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "Headers/Engine/AssetFS.hpp"

namespace AssetFS {
    inline SDL_Surface* LoadImage(const std::filesystem::path& path) {
        const std::u8string utf8 = path.u8string();
        const std::string file(utf8.begin(), utf8.end());
        if (!CoversPath(path)) return IMG_Load(file.c_str());

        std::vector<unsigned char> bytes;
        if (!ReadFile(path, bytes)) {
            SDL_SetError("%s isn't in the game's data", file.c_str());
            return nullptr;
        }

        SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (stream == nullptr) return nullptr;

        // The extension, as IMG_Load passes it: formats like TGA have no magic bytes.
        std::string type = path.extension().string();
        if (!type.empty() && type.front() == '.') type.erase(0, 1);
        return IMG_LoadTyped_IO(stream, true, type.empty() ? nullptr : type.c_str());
    }
}

#endif // TILKY_ENGINE_ASSETFS_IMAGE_HPP
