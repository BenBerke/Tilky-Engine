#ifndef TILKY_ENGINE_TEXTURE_MANAGER_H
#define TILKY_ENGINE_TEXTURE_MANAGER_H

#include <string>
#include <vector>

#include <glad/glad.h>

#include "Headers/Objects/Level.hpp"

struct GPUTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

namespace TextureManager {
    void RefreshTexturesFromLevel();

    // ONLY ACCEPTS PNGs todo: Add more fomat support
    // Simply write the name of the .png of the desired image that is in Assets/Textures
    // DO NOT add the file path or the format
    // Example path parameter: myPicture (for Assets/Textures/myPicture.png)
    int CreateTexture(const std::string& path);
    const GPUTexture& GetTexture(int index);
    int GetTextureCount();

    void BindAllTextures(int firstTextureUnit = 0);
    void DestroyAll();
}

#endif