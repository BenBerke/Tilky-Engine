#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"

void Vulkan::BuildGpuSprites() {
    gpuSprites.clear();

    Level& level = LevelManager::CurrentLevel();

    const auto UnpackColor = [](const uint_fast32_t packedColor) -> Vector4 {
        const uint32_t color = static_cast<uint32_t>(packedColor);

        return {
            static_cast<float>(color & 0xFFu),
            static_cast<float>((color >> 8u) & 0xFFu),
            static_cast<float>((color >> 16u) & 0xFFu),
            static_cast<float>((color >> 24u) & 0xFFu)
        };
    };

    for (ComponentSprite& spriteComponent : level.sprites.components) {
        ComponentTransform* transform =
            level.transforms.Get(spriteComponent.ownerID);

        if (transform == nullptr) [[unlikely]] {
            continue;
        }

        GpuSprite gpuSprite{};

        gpuSprite.positionSize = {
            transform->position.x,
            transform->position.y,
            transform->position.z,
            transform->scale.z
        };

        const Vector4 spriteColor = UnpackColor(spriteComponent.color);

        gpuSprite.color = spriteColor;

        if (
            transform->sectorIndex >= 0 &&
            transform->sectorIndex < static_cast<int>(level.sectors.size())
        ) {
            const Sector& sector =
                level.sectors[transform->sectorIndex];

            gpuSprite.color = {
                spriteColor.x - (255.0f - sector.light.x),
                spriteColor.y - (255.0f - sector.light.y),
                spriteColor.z - (255.0f - sector.light.z),
                spriteColor.w
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

        if (spriteComponent.isStatic) {
            rotation = transform->rotation.Normalized();
        }

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

    spriteCount = static_cast<int>(gpuSprites.size());

    if (gpuSprites.empty()) return;

    UploadToDynamicBuffer(
        spriteBuffers[currentFrame], gpuSprites.data(),
        gpuSprites.size() * sizeof(GpuSprite),
        vk::BufferUsageFlagBits::eStorageBuffer
    );

    const vk::DescriptorBufferInfo spriteInfo{.buffer = spriteBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize};
    const vk::WriteDescriptorSet spriteWrite{
        .dstSet = sceneSets[currentFrame], .dstBinding = 2, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &spriteInfo
    };
    device.updateDescriptorSets(spriteWrite, {});
}