#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"

void OpenGL::BuildGpuSprites() {
    gpuSprites.clear();

    Level& level = LevelManager::CurrentLevel();

    for (ComponentSprite& spriteComponent : level.sprites.components) {
        ComponentTransform* transform =
            level.transforms.Get(spriteComponent.ownerID);

        if (transform == nullptr) [[unlikely]] continue;

        if (level.decals.Has(spriteComponent.ownerID)) continue;

        if (transform->sectorIndex < 0 ||
            transform->sectorIndex >= static_cast<int>(level.sectors.size())) [[unlikely]] {
            continue;
        }

        GpuSprite gpuSprite;

        gpuSprite.positionSize = {
            transform->position.x,
            transform->position.y,
            transform->position.z,
            transform->scale.z
        };

        const Sector& sector = level.sectors[transform->sectorIndex];

        gpuSprite.color = {
            sector.lightValue,
            sector.lightValue,
            sector.lightValue,
            255.0f
        };

        gpuSprite.textureIndices0 = {
            spriteComponent.textureIndices[0], // N
            spriteComponent.textureIndices[1], // NE
            spriteComponent.textureIndices[2], // E
            spriteComponent.textureIndices[3]  // SE
        };

        gpuSprite.textureIndices1 = {
            spriteComponent.textureIndices[4], // S
            spriteComponent.textureIndices[5], // SW
            spriteComponent.textureIndices[6], // W
            spriteComponent.textureIndices[7]  // NW
        };

        gpuSprite.data = {
            transform->scale.x,
            static_cast<float>(spriteComponent.sideCount),
            transform->forward.x,
            transform->forward.y
        };

        gpuSprites.push_back(gpuSprite);
    }

    spriteCount = static_cast<GLsizei>(gpuSprites.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, spriteSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuSprites.size() * sizeof(GpuSprite),
        gpuSprites.empty() ? nullptr : gpuSprites.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
}