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

    for (size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex) {
        const Sector& sector = level.sectors[sectorIndex];
        GpuSector gpuSector{};

        gpuSector.floorData = {
            static_cast<float>(gpuSectorFloors.size()),
            static_cast<float>(sector.floors.size()),
            0.0f,
            0.0f
        };

        // Baked with the flat triangles; a sector added since then has no
        // triangles drawn yet either.
        if (sectorIndex < flatSectorBounds.size()) gpuSector.bounds = flatSectorBounds[sectorIndex];

        for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
            const SectorFloor& floor = sector.floors[floorIndex];
            GpuSectorFloor gpuFloor{};

            const Vector4 floorColor = floor.floor.color;
            const Vector4 ceilingColor = floor.ceiling.color;

            gpuFloor.heights = {
                floor.floor.height,
                floor.ceiling.height,
                0.0f,
                0.0f
            };

            gpuFloor.slopeData = {
                static_cast<float>(floor.floor.slopeDirection),
                floor.floor.SlopeGradient(),
                static_cast<float>(floor.ceiling.slopeDirection),
                floor.ceiling.SlopeGradient(),
            };

            const Vector3 light = SectorLight(sector);

            gpuFloor.floorColor = {
                floorColor.x - (255.0f - light.x) / 255.0f,
                floorColor.y - (255.0f - light.y) / 255.0f,
                floorColor.z - (255.0f - light.z) / 255.0f,
                floorColor.w
            };

            gpuFloor.ceilingColor = {
                ceilingColor.x - (255.0f - light.x) / 255.0f,
                ceilingColor.y - (255.0f - light.y) / 255.0f,
                ceilingColor.z - (255.0f - light.z) / 255.0f,
                ceilingColor.w
            };

            // The frame showing right now, its placement, and the scroll so far.
            const TextureFrame& floorFrame = floor.floor.texture.Shown();
            const TextureFrame& ceilingFrame = floor.ceiling.texture.Shown();
            const int floorTextureIndex = GetTextureRegionIndex(floor.floor.texture.Current());
            const int ceilingTextureIndex = GetTextureRegionIndex(floor.ceiling.texture.Current());
            const Vector2 floorScroll = SurfaceScroll(floor.floor.texture, floorTextureIndex);
            const Vector2 ceilingScroll = SurfaceScroll(floor.ceiling.texture, ceilingTextureIndex);

            gpuFloor.textureData = {
                static_cast<float>(floorTextureIndex),
                static_cast<float>(ceilingTextureIndex),
                static_cast<float>(FlatMetaData(static_cast<int>(sectorIndex), floorIndex, MarkedSurfaceKind::Floor)),
                static_cast<float>(FlatMetaData(static_cast<int>(sectorIndex), floorIndex, MarkedSurfaceKind::Ceiling))
            };

            gpuFloor.textureOffsets = {
                floorFrame.textureOffset.x + floorScroll.x,
                floorFrame.textureOffset.y + floorScroll.y,
                ceilingFrame.textureOffset.x + ceilingScroll.x,
                ceilingFrame.textureOffset.y + ceilingScroll.y
            };

            gpuFloor.textureFlip = {
                floorFrame.flipTextureX ? 1.0f : 0.0f,
                floorFrame.flipTextureY ? 1.0f : 0.0f,
                ceilingFrame.flipTextureX ? 1.0f : 0.0f,
                ceilingFrame.flipTextureY ? 1.0f : 0.0f
            };

            gpuFloor.textureScales = {
                floorFrame.textureScale.x,
                floorFrame.textureScale.y,
                ceilingFrame.textureScale.x,
                ceilingFrame.textureScale.y
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