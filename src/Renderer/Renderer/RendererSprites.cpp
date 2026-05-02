#include "RendererInternal.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"

namespace RendererInternal {
    void BuildGpuSprites() {
        gpuSprites.clear();

        Level& level = LevelManager::CurrentLevel();

        for (ComponentSprite& spriteComponent : level.sprites.components) {
            ComponentTransform* transform =
                level.transforms.Get(spriteComponent.ownerID);

            if (transform == nullptr) continue;
            if (level.decals.Has(spriteComponent.ownerID)) continue;

            transform->UpdateObjectSector(level.sectors);

            if (transform->sectorIndex < 0 ||
                transform->sectorIndex >= static_cast<int>(level.sectors.size())) {
                continue;
            }

            const float bottomHeight =
                transform->GetObjectBottomHeight(level.sectors);

            GpuSprite gpuSprite;

            gpuSprite.positionSize = {
                transform->position.x,
                transform->position.y,
                bottomHeight,
                transform->scale.y
            };

            gpuSprite.color = {
                255.0f,
                255.0f,
                255.0f,
                255.0f
            };

            gpuSprite.data = {
                transform->scale.x,
                static_cast<float>(spriteComponent.textureIndex),
                0.0f,
                0.0f
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
}