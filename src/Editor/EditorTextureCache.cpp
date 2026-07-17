#include "Headers/Editor/EditorTextureCache.hpp"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace {
    constexpr const char* TEXTURE_INDEX_MANIFEST = ".tilky_texture_indices";

    std::vector<SDL_Texture*> textures;

    bool IsSupportedTextureExtension(std::string extension) {
        std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
    }

    std::string NormalizeFileName(std::string fileName) {
        std::ranges::transform(fileName, fileName.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return fileName;
    }

    bool LoadTextureIndexManifest(const std::filesystem::path& manifestPath, std::vector<std::string>& indexedFileNames) {
        std::ifstream file(manifestPath);

        if (!file.is_open()) return false;

        std::string line;

        while (std::getline(file, line)) {
            // Remove the carriage return produced by Windows line endings.
            if (!line.empty() && line.back() == '\r') line.pop_back();

            indexedFileNames.push_back(line);
        }

        return true;
    }

    bool SaveTextureIndexManifest(const std::filesystem::path& manifestPath, const std::vector<std::string>& indexedFileNames) {
        const std::filesystem::path temporaryPath = manifestPath.string() + ".tmp";

        {
            std::ofstream file(temporaryPath, std::ios::trunc);

            if (!file.is_open()) {
                spdlog::error( "Failed to create texture index manifest: {}",temporaryPath.string());

                return false;
            }

            for (const std::string& fileName : indexedFileNames) file << fileName << '\n';

            if (!file.good()) {
                spdlog::error("Failed while writing texture index manifest: {}", temporaryPath.string());

                return false;
            }
        }

        std::error_code error;

        std::filesystem::copy_file(
            temporaryPath,
            manifestPath,
            std::filesystem::copy_options::overwrite_existing,
            error
        );

        std::filesystem::remove(temporaryPath);

        if (error) {
            spdlog::error("Failed to save texture index manifest {}: {}", manifestPath.string(), error.message());

            return false;
        }

        return true;
    }
}

namespace EditorTextureCache {
    void RefreshLevelTexturesFromFolder() {
        Level& level = LevelManager::CurrentLevel();

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

        const std::filesystem::path manifestPath =
            texturesPath / TEXTURE_INDEX_MANIFEST;

        /*
         * Each element represents one permanent texture index.
         *
         * The filename remains in this manifest even when the corresponding
         * file is deleted. This prevents later indices from shifting.
         */
        std::vector<std::string> indexedFileNames;

        const bool manifestLoaded =
            LoadTextureIndexManifest(manifestPath, indexedFileNames);

        if (!manifestLoaded) {
            /*
             * Preserve the current level's ordering when creating the
             * manifest for the first time.
             */
            indexedFileNames.reserve(level.textures.size());

            for (const Texture& texture : level.textures)
                indexedFileNames.push_back(texture.fileName);
        }

        std::vector<std::string> folderFileNames;
        std::unordered_map<std::string, std::string> folderFilesByName;

        for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
            if (!entry.is_regular_file())
                continue;

            const std::filesystem::path& path = entry.path();

            if (!IsSupportedTextureExtension(path.extension().string()))
                continue;

            const std::string fileName = path.filename().string();
            const std::string normalizedName = NormalizeFileName(fileName);

            folderFileNames.push_back(fileName);
            folderFilesByName[normalizedName] = fileName;
        }

        /*
         * Sorting only affects the order in which entirely new textures are
         * appended. Existing texture indices are never reordered.
         */
        std::ranges::sort(folderFileNames);

        std::unordered_map<std::string, std::size_t> existingIndices;

        for (std::size_t i = 0; i < indexedFileNames.size(); ++i) {
            if (indexedFileNames[i].empty())
                continue;

            existingIndices.try_emplace(
                NormalizeFileName(indexedFileNames[i]),
                i
            );
        }

        for (const std::string& fileName : folderFileNames) {
            const std::string normalizedName = NormalizeFileName(fileName);
            const auto existing = existingIndices.find(normalizedName);

            if (existing != existingIndices.end()) {
                // Preserve the index while updating case-only filename changes.
                indexedFileNames[existing->second] = fileName;
                continue;
            }

            const std::size_t newIndex = indexedFileNames.size();

            indexedFileNames.push_back(fileName);
            existingIndices.emplace(normalizedName, newIndex);

            spdlog::info(
                "Assigned permanent texture index {} to {}",
                newIndex,
                fileName
            );
        }

        level.textures.clear();
        level.textures.resize(indexedFileNames.size());

        int availableTextureCount = 0;

        for (std::size_t i = 0; i < indexedFileNames.size(); ++i) {
            if (indexedFileNames[i].empty())
                continue;

            const std::string normalizedName =
                NormalizeFileName(indexedFileNames[i]);

            const auto currentFile = folderFilesByName.find(normalizedName);

            if (currentFile == folderFilesByName.end()) {
                /*
                 * Leave an empty Texture at this index. The index remains
                 * reserved but Load() will skip it.
                 */
                continue;
            }

            level.textures[i].fileName = currentFile->second;
            ++availableTextureCount;
        }

        SaveTextureIndexManifest(manifestPath, indexedFileNames);

        spdlog::info(
            "Refreshed {} available texture(s) across {} permanent index slot(s)",
            availableTextureCount,
            level.textures.size()
        );
    }

    bool Load(SDL_Renderer* renderer, const Level& level) {
        Destroy();

        textures.resize(level.textures.size(), nullptr);

        const std::filesystem::path texturesPath =
            ProjectManager::GetTexturesPath();

        for (int i = 0; i < static_cast<int>(level.textures.size()); ++i) {
            const Texture& texture = level.textures[i];

            // Empty entries are permanently reserved deleted texture slots.
            if (texture.fileName.empty())
                continue;

            const std::filesystem::path path =
                texturesPath / texture.fileName;

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