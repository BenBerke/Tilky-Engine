#include "../../Headers/Renderer/TextureManager.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

namespace {
    std::vector<Texture> textures;
}

namespace TextureManager {

    int CreateTexture(const std::string& _path) {
        const std::string path = ProjectManager::GetTexturesPath().string() + "/" + _path + ".png";
        SDL_Surface* loadedSurface = IMG_Load(path.c_str());

        if (loadedSurface == nullptr) {
            SDL_Log("IMG_Load failed for %s: %s", path.c_str(), SDL_GetError());
            return -1;
        }

        SDL_Surface* convertedSurface = SDL_ConvertSurface(
            loadedSurface,
            SDL_PIXELFORMAT_RGBA32
        );

        SDL_DestroySurface(loadedSurface);

        if (convertedSurface == nullptr) {
            SDL_Log("SDL_ConvertSurface failed for %s: %s", path.c_str(), SDL_GetError());
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

        // glGenerateMipmap(GL_TEXTURE_2D);
        //
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        //
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        //----
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        //
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        //----
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // Smooth between mipmap levels.
        // This reduces far-away shimmering.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        // Keep close-up texture sharp-ish.
        // Use GL_LINEAR if you want smoother close-up textures.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // Negative bias means: use higher-detail mip levels for longer.
        // This stops mipmaps becoming blurry too quickly.
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.25f);
        //---
        // glGenerateMipmap(GL_TEXTURE_2D);
        //
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        //
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        //
        // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.25f);

        Texture texture;
        texture.id = textureID;
        texture.width = convertedSurface->w;
        texture.height = convertedSurface->h;

        SDL_DestroySurface(convertedSurface);

        textures.push_back(texture);

        return static_cast<int>(textures.size()) - 1;
    }

    const Texture& GetTexture(const int index) {
        return textures[index];
    }

    int GetTextureCount() {
        return static_cast<int>(textures.size());
    }

    void BindAllTextures(const int firstTextureUnit) {
        for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
            glActiveTexture(GL_TEXTURE0 + firstTextureUnit + i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
    }

    void DestroyAll() {
        for (Texture& texture : textures) {
            if (texture.id != 0) {
                glDeleteTextures(1, &texture.id);
                texture.id = 0;
            }

            texture.width = 0;
            texture.height = 0;
        }

        textures.clear();
    }
}