#include "../../Headers/Renderer/Renderer/RendererInternal.hpp"

namespace RendererInternal {
    void BuildGpuSprites() {
        gpuSprites.clear();

        GpuSprite testSprite;

        testSprite.positionSize = {
            testSpritePosition.x,
            testSpritePosition.y,
            0.0f,
            64.0f
        };

        testSprite.color = {
            255.0f,
            255.0f,
            255.0f,
            255.0f
        };

        testSprite.data = {
            32.0f,
            4.0f,
            0.0f,
            0.0f
        };

        gpuSprites.push_back(testSprite);

        spriteCount = static_cast<GLsizei>(gpuSprites.size());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, spriteSSBO);

        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            gpuSprites.size() * sizeof(GpuSprite),
            gpuSprites.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
    }
}