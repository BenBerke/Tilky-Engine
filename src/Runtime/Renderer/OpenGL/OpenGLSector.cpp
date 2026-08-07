#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Map/LevelManager.hpp"

void OpenGL::BuildGpuSectors() {
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

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuSectors.size() * sizeof(GpuSector),
        gpuSectors.empty() ? nullptr : gpuSectors.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorFloorSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuSectorFloors.size() * sizeof(GpuSectorFloor),
        gpuSectorFloors.empty() ? nullptr : gpuSectorFloors.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sectorFloorSSBO);
}