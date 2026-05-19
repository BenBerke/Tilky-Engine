#include "Headers/Runtime/Renderer/OpenGL/OpenGLRenderer.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Loadables.hpp"

int OpenGLRenderer::CreateTexture(const std::string& fileName) {
    const std::string path =
        ProjectManager::GetTexturesPath().string() + "/" + fileName + ".png";

    SDL_Surface* loadedSurface = IMG_Load(path.c_str());

    if (loadedSurface == nullptr) {
        spdlog::error("IMG_Load failed for {}: {}", path, SDL_GetError());
        return -1;
    }

    SDL_Surface* convertedSurface = SDL_ConvertSurface(
        loadedSurface,
        SDL_PIXELFORMAT_RGBA32
    );

    SDL_DestroySurface(loadedSurface);

    if (convertedSurface == nullptr) {
        spdlog::error("SDL_ConvertSurface failed for {}: {}", path, SDL_GetError());
        return -1;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
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

    GPUTexture texture;
    texture.id = textureID;
    texture.width = convertedSurface->w;
    texture.height = convertedSurface->h;

    SDL_DestroySurface(convertedSurface);

    textures.push_back(texture);

    return static_cast<int>(textures.size()) - 1;
}

void OpenGLRenderer::RefreshTexturesFromLevel() {
    DestroyAllTextures();

    const Level& level = LevelManager::CurrentLevel();

    for (const Texture& texture : level.textures) {
        if (texture.fileName.empty()) {
            continue;
        }

        const int textureIndex = CreateTexture(texture.fileName);

        if (textureIndex == -1) {
            spdlog::error("Failed to create renderer texture: {}", texture.fileName);
            continue;
        }

        spdlog::info(
            "Created renderer texture '{}' at index {}",
            texture.fileName,
            textureIndex
        );
    }

    spdlog::info(
        "Created {} renderer texture(s) from level texture list",
        textures.size()
    );
}

const OpenGLRenderer::GPUTexture& OpenGLRenderer::GetTexture(const int index) const {
    return textures[index];
}

int OpenGLRenderer::GetTextureCount() const {
    return static_cast<int>(textures.size());
}

void OpenGLRenderer::BindAllTextures(const int firstTextureUnit) const {
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        glActiveTexture(GL_TEXTURE0 + firstTextureUnit + i);
        glBindTexture(GL_TEXTURE_2D, textures[i].id);
    }
}

void OpenGLRenderer::DestroyAllTextures() {
    for (GPUTexture& texture : textures) {
        if (texture.id != 0) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }

        texture.width = 0;
        texture.height = 0;
    }

    textures.clear();
}