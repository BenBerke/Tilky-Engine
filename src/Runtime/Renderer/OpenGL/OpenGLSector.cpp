#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Map/LevelManager.hpp"

void OpenGL::BuildGpuSectors() {
    const Level& level = LevelManager::CurrentLevel();

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

            gpuFloor.heights = {
                floor.floor.height,
                floor.ceiling.height,
                0.0f,
                0.0f
            };

            gpuFloor.floorColor = {
                floor.floor.color.x,
                floor.floor.color.y,
                floor.floor.color.z,
                255.0f
            };

            gpuFloor.ceilingColor = {
                floor.ceiling.color.x,
                floor.ceiling.color.y,
                floor.ceiling.color.z,
                255.0f
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