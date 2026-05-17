#include "Headers/Editor/EditorTextureCache.hpp"

#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace {
    std::vector<SDL_Texture*> textures;
}

namespace EditorTextureCache {
    void RefreshLevelTexturesFromFolder() {
        Level& level = LevelManager::CurrentLevel();
        level.textures.clear();

        const std::filesystem::path texturesPath = ProjectManager::GetTexturesPath();

        if (!std::filesystem::exists(texturesPath)) {
            std::filesystem::create_directories(texturesPath);
            spdlog::warn("Created missing Textures folder: {}", texturesPath.string());
            return;
        }

        if (!std::filesystem::is_directory(texturesPath)) {
            spdlog::error("Textures path is not a directory: {}", texturesPath.string());
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path& path = entry.path();

            std::string extension = path.extension().string();

            std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (extension != ".png") {
                continue;
            }

            Texture texture;
            texture.fileName = path.stem().string(); // "Brick.png" -> "Brick"

            level.textures.push_back(texture);
        }

        std::ranges::sort(level.textures, [](const Texture& a, const Texture& b) {
            return a.fileName < b.fileName;
        });

        spdlog::info("Refreshed {} level texture(s)", level.textures.size());
    }

    bool Load(SDL_Renderer* renderer, const Level& level) {
        Destroy();

        textures.resize(level.textures.size(), nullptr);

        const std::filesystem::path texturesPath =
            ProjectManager::GetTexturesPath();

        for (int i = 0; i < static_cast<int>(level.textures.size()); ++i) {
            const Texture& texture = level.textures[i];

            if (texture.fileName.empty()) {
                continue;
            }

            std::filesystem::path path = texturesPath / texture.fileName;

            if (!path.has_extension()) {
                path += ".png";
            }

            SDL_Texture* sdlTexture =
                IMG_LoadTexture(renderer, path.string().c_str());

            if (sdlTexture == nullptr) {
                spdlog::error(
                    "Editor failed to load texture index {} from {}: {}",
                    i,
                    path.string(),
                    SDL_GetError()
                );

                continue;
            }

            textures[i] = sdlTexture;

            spdlog::info(
                "Editor loaded texture index {} from {}",
                i,
                path.string()
            );
        }

        return true;
    }

    void Destroy() {
        for (SDL_Texture*& texture : textures) {
            if (texture != nullptr) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
            }
        }

        textures.clear();
    }

    SDL_Texture* Get(const int textureIndex) {
        if (textureIndex < 0 ||
            textureIndex >= static_cast<int>(textures.size())) {
            return nullptr;
        }

        return textures[textureIndex];
    }
}