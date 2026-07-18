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

        // spriteComponent.textureFileNames[i] are filenames now, not
        // indices - sprites share the wall/sector/decal texture atlas, so
        // each slot resolves the same way (see GetTextureRegionIndex,
        // added alongside the atlas builder - not part of this file).
        gpuSprite.textureIndices0 = {
            (GetTextureRegionIndex(spriteComponent.textureFileNames[0])), // N
            (GetTextureRegionIndex(spriteComponent.textureFileNames[1])), // NE
            (GetTextureRegionIndex(spriteComponent.textureFileNames[2])), // E
            (GetTextureRegionIndex(spriteComponent.textureFileNames[3]))  // SE
        };

        gpuSprite.textureIndices1 = {
            (GetTextureRegionIndex(spriteComponent.textureFileNames[4])), // S
            (GetTextureRegionIndex(spriteComponent.textureFileNames[5])), // SW
            (GetTextureRegionIndex(spriteComponent.textureFileNames[6])), // W
            (GetTextureRegionIndex(spriteComponent.textureFileNames[7]))  // NW
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