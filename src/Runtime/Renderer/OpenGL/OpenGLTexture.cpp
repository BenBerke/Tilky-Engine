#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Map/LevelManager.hpp"

bool OpenGL::LoadTextureFile(const std::filesystem::path& path, GPUTexture& out) {
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

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        convertedSurface->w,
        convertedSurface->h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        convertedSurface->pixels
    );

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.25f);

    std::error_code ec;

    out.id = textureID;
    out.width = convertedSurface->w;
    out.height = convertedSurface->h;
    out.path = path;
    out.writeTime = std::filesystem::last_write_time(path, ec);

    SDL_DestroySurface(convertedSurface);

    return true;
}

int OpenGL::CreateTexture(const std::string& fileName) {
    std::filesystem::path path(fileName);

    if (!path.is_absolute()) path = ProjectManager::GetAssetsPath() / std::filesystem::path(fileName).lexically_normal();

    GPUTexture texture;
    if (!LoadTextureFile(path, texture)) return -1;

    textures.push_back(std::move(texture));

    return static_cast<int>(textures.size()) - 1;
}

void OpenGL::ReloadChangedTextures() {
    for (GPUTexture& texture : textures) {
        if (texture.id == 0 || texture.path.empty()) continue;

        std::error_code ec;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(texture.path, ec);

        // A deleted file keeps its last good texture rather than going blank.
        if (ec || writeTime == texture.writeTime) continue;

        GPUTexture reloaded;
        if (!LoadTextureFile(texture.path, reloaded)) continue;

        glDeleteTextures(1, &texture.id);
        texture = std::move(reloaded);
    }
}

// Lazy, filename-keyed resolution on top of the plain textures[] list
// above (NOT the atlas - see GetTextureRegionIndex for that). Used by
// anything that binds a single standalone GL texture directly, such as
// the background. Loads and caches on first request; returns -1 (never
// crashes) if the file is missing or fails to decode - a failed load is
// cached too, so a broken reference isn't retried every frame.
int OpenGL::GetOrCreateTextureIndex(const std::string& fileName) {
    if (fileName.empty()) return -1;

    const auto found = textureIndexByName.find(fileName);
    if (found != textureIndexByName.end()) return found->second;

    const int index = CreateTexture(fileName);
    textureIndexByName.emplace(fileName, index); // cache -1 too, on failure

    return index;
}

void OpenGL::RefreshTexturesFromLevel() {
    // A model that failed to load may have been imported or fixed since.
    failedModelFiles.clear();
    unresolvedLevelTextures.clear();
    // Same for fonts; ones that loaded are reread in case the file changed.
    ReloadFonts();

    if (!BuildTextureAtlasFromLevel()) {
        spdlog::error("Failed to build texture atlas from level");
        return;
    }

    spdlog::info("Built texture atlas with {} texture region(s)",textureRegions.size());
}

const OpenGL::GPUTexture& OpenGL::GetTexture(const int index) const {
    return textures[index];
}

int OpenGL::GetTextureCount() const {
    return static_cast<int>(textures.size());
}

// void OpenGL::BindAllTextures(const int firstTextureUnit) const {
//     for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
//         glActiveTexture(GL_TEXTURE0 + firstTextureUnit + i);
//         glBindTexture(GL_TEXTURE_2D, textures[i].id);
//     }
// }

void OpenGL::DestroyAllTextures() {
    for (GPUTexture& texture : textures) {
        if (texture.id != 0) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }

        texture.width = 0;
        texture.height = 0;
    }

    textures.clear();
    textureIndexByName.clear();

    DestroyAtlas();
}

void OpenGL::DestroyAtlas() {
    if (atlasTexture != 0) {
        glDeleteTextures(1, &atlasTexture);
        atlasTexture = 0;
    }

    if (textureRegionSSBO != 0) {
        glDeleteBuffers(1, &textureRegionSSBO);
        textureRegionSSBO = 0;
    }

    textureRegions.clear();
}

#ifndef TILKY_STANDALONE
ImTextureID OpenGL::GetImGuiTextureID(const std::string& fileName) {
    const int textureIndex = GetOrCreateTextureIndex(fileName);

    if (textureIndex < 0 || textureIndex >= GetTextureCount()) return ImTextureID{};

    return static_cast<ImTextureID>(GetTexture(textureIndex).id);
}
#endif
