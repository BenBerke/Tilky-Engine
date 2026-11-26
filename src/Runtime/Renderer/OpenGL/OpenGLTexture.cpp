// The OpenGL side of textures: standalone GL_TEXTURE_2Ds and the atlas, a
// GL_TEXTURE_2D_ARRAY with its region SSBO. Which images go where is
// RendererCore's (Core/RendererCoreTextures.cpp).

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

unsigned OpenGL::UploadTextureGpu(const void* pixels, const int width, const int height) {
    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.25f);

    return textureID;
}

void OpenGL::DestroyTextureGpu(const unsigned id) {
    const GLuint texture = id;
    glDeleteTextures(1, &texture);
}

ImTextureID OpenGL::TextureImGuiID(const unsigned id) const {
    return static_cast<ImTextureID>(id);
}

bool OpenGL::AtlasExists() const {
    return atlasTexture != 0;
}

int OpenGL::MaxAtlasPages() const {
    GLint maxLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    return maxLayers;
}

void OpenGL::CreateAtlasGpu(const int pageCount) {
    using namespace RendererGpu;

    glGenTextures(1, &atlasTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTexture);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE, pageCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

void OpenGL::UploadAtlasPage(const int page, const unsigned char* pixels) {
    using namespace RendererGpu;
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, page, ATLAS_SIZE, ATLAS_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void OpenGL::FinishAtlasGpu(const RendererTextureSettings setting) {
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    OpenGLRendererInternal::ApplyTextureSampling(setting, GL_TEXTURE_2D_ARRAY);

    glGenBuffers(1, &textureRegionSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, textureRegionSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        textureRegions.size() * sizeof(RendererGpu::GPUTextureRegion),
        textureRegions.empty() ? nullptr : textureRegions.data(),
        GL_STATIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);
}

void OpenGL::UploadAtlasRegionGpu(const int page, const int x, const int y, const int width, const int height, const unsigned char* pixels) {
    if (atlasTexture == 0) return;
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTexture);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, page, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void OpenGL::RefreshAtlasGpu(const RendererTextureSettings setting) {
    if (atlasTexture == 0) return;

    // Mips again (where the setting uses them), then the longer region list.
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTexture);
    OpenGLRendererInternal::ApplyTextureSampling(setting, GL_TEXTURE_2D_ARRAY);

    if (textureRegionSSBO == 0) glGenBuffers(1, &textureRegionSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, textureRegionSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, textureRegions.size() * sizeof(RendererGpu::GPUTextureRegion),
                 textureRegions.empty() ? nullptr : textureRegions.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);
}

void OpenGL::DestroyAtlasGpu() {
    if (atlasTexture != 0) {
        glDeleteTextures(1, &atlasTexture);
        atlasTexture = 0;
    }

    if (textureRegionSSBO != 0) {
        glDeleteBuffers(1, &textureRegionSSBO);
        textureRegionSSBO = 0;
    }
}
