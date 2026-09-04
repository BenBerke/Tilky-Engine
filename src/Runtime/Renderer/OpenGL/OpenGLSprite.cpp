#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"

void OpenGL::BuildGpuSprites() {
    gpuSprites.clear();

    Level& level = LevelManager::CurrentLevel();

    for (ComponentSprite& spriteComponent : level.sprites.components) {
        ComponentTransform* transform = level.transforms.Get(spriteComponent.ownerID);

        if (transform == nullptr) [[unlikely]] continue;


        GpuSprite gpuSprite{};

        gpuSprite.positionSize = {
            transform->position.x,
            transform->position.y,
            transform->position.z,
            transform->scale.z
        };


        gpuSprite.color = spriteComponent.color;

        if (transform->sectorIndex >= 0 &&transform->sectorIndex < static_cast<int>(level.sectors.size())) {
            const Sector& sector = level.sectors[transform->sectorIndex];

            gpuSprite.color = {
                spriteComponent.color.x - (255.0f - sector.light.x),
                spriteComponent.color.y - (255.0f - sector.light.y),
                spriteComponent.color.z - (255.0f - sector.light.z),
                spriteComponent.color.w
            };
        }

        gpuSprite.textureIndices0 = {
            GetTextureRegionIndex(spriteComponent.textureFileNames[0]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[1]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[2]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[3])
        };

        gpuSprite.textureIndices1 = {
            GetTextureRegionIndex(spriteComponent.textureFileNames[4]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[5]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[6]),
            GetTextureRegionIndex(spriteComponent.textureFileNames[7])
        };

        gpuSprite.data = {
            transform->scale.x,
            static_cast<float>(spriteComponent.sideCount),
            transform->forward.x,
            transform->forward.y
        };

        Quaternion rotation = Quaternion::Identity();

        if (spriteComponent.isStatic) rotation = transform->rotation.Normalized();

        gpuSprite.rotation = {
            rotation.x,
            rotation.y,
            rotation.z,
            rotation.w
        };

        gpuSprite.flags = {
            spriteComponent.isStatic ? 1 : 0,
            0,
            0,
            0
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

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,spriteSSBO);
}