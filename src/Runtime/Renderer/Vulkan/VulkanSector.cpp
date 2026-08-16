#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include "Headers/Map/LevelManager.hpp"

void Vulkan::BuildGpuSectors() {
    const Level& level = LevelManager::CurrentLevel();

    const auto UnpackColor = [](const uint_fast32_t packedColor) -> Vector4 {
        const uint32_t color = static_cast<uint32_t>(packedColor);

        return {
            static_cast<float>(color & 0xFFu),
            static_cast<float>((color >> 8u) & 0xFFu),
            static_cast<float>((color >> 16u) & 0xFFu),
            static_cast<float>((color >> 24u) & 0xFFu)
        };
    };

    gpuSectors.clear();
    gpuSectorFloors.clear();

    gpuSectors.reserve(level.sectors.size());

    size_t totalFloorCount = 0;
    for (const Sector& sector : level.sectors) totalFloorCount += sector.floors.size();

    gpuSectorFloors.reserve(totalFloorCount);

    for (const Sector& sector : level.sectors) {
        GpuSector gpuSector;

        gpuSector.floorData = {
            static_cast<float>(gpuSectorFloors.size()),
            static_cast<float>(sector.floors.size()),
            0.0f,
            0.0f
        };

        for (const SectorFloor& floor : sector.floors) {
            GpuSectorFloor gpuFloor;

            const Vector4 floorColor = UnpackColor(floor.floor.color);
            const Vector4 ceilingColor = UnpackColor(floor.ceiling.color);

            gpuFloor.heights = {
                floor.floor.height,
                floor.ceiling.height,
                0.0f,
                0.0f
            };

            gpuFloor.slopeData = {
                static_cast<float>(floor.floor.slopeDirection),
                floor.floor.slopeStrength * Constants::DegToRad,
                static_cast<float>(floor.ceiling.slopeDirection),
                floor.ceiling.slopeStrength * Constants::DegToRad,
            };

            gpuFloor.floorColor = {
                floorColor.x - (255.0f - sector.light.x),
                floorColor.y - (255.0f - sector.light.y),
                floorColor.z - (255.0f - sector.light.z),
                floorColor.w
            };

            gpuFloor.ceilingColor = {
                ceilingColor.x - (255.0f - sector.light.x),
                ceilingColor.y - (255.0f - sector.light.y),
                ceilingColor.z - (255.0f - sector.light.z),
                ceilingColor.w
            };

            gpuFloor.textureData = {
                static_cast<float>(GetTextureRegionIndex(floor.floor.texture)),
                static_cast<float>(GetTextureRegionIndex(floor.ceiling.texture)),
                0.0f,
                0.0f
            };

            gpuSectorFloors.push_back(gpuFloor);
        }

        gpuSectors.push_back(gpuSector);
    }

    if (!gpuSectors.empty()) {
        UploadToDynamicBuffer(
            sectorBuffers[currentFrame], gpuSectors.data(),
            gpuSectors.size() * sizeof(GpuSector),
            vk::BufferUsageFlagBits::eStorageBuffer
        );

        const vk::DescriptorBufferInfo sectorInfo{.buffer = sectorBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize};
        const vk::WriteDescriptorSet sectorWrite{
            .dstSet = sceneSets[currentFrame], .dstBinding = 4, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &sectorInfo
        };
        device.updateDescriptorSets(sectorWrite, {});
    }

    if (!gpuSectorFloors.empty()) {
        UploadToDynamicBuffer(
            sectorFloorBuffers[currentFrame], gpuSectorFloors.data(),
            gpuSectorFloors.size() * sizeof(GpuSectorFloor),
            vk::BufferUsageFlagBits::eStorageBuffer
        );

        const vk::DescriptorBufferInfo floorInfo{.buffer = sectorFloorBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize};
        const vk::WriteDescriptorSet floorWrite{
            .dstSet = sceneSets[currentFrame], .dstBinding = 7, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &floorInfo
        };
        device.updateDescriptorSets(floorWrite, {});
    }
}