#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"

namespace {
    using VulkanRendererInternal::GpuWall;
    using VulkanRendererInternal::GpuFlatTriangle;

    constexpr float MIN_WALL_HEIGHT = 0.0001f;

    enum class WallSpanSide {
        Front,
        Back
    };

    struct WallSpan {
        float bottom;
        float top;
        WallSpanSide side;
    };

    bool IsSectorOpenAtHeight(const Sector &sector, const float height) {
        for (const SectorFloor &floor: sector.floors) {
            if (height > floor.floor.height + MIN_WALL_HEIGHT &&
                height < floor.ceiling.height - MIN_WALL_HEIGHT) {
                return true;
            }
        }

        return false;
    }

    void AddSectorHeights(const Sector &sector, std::vector<float> &heights) {
        for (const SectorFloor &floor: sector.floors) {
            heights.push_back(floor.floor.height);
            heights.push_back(floor.ceiling.height);
        }
    }

    void SortAndRemoveDuplicateHeights(std::vector<float> &heights) {
        std::ranges::sort(heights);

        heights.erase(
            std::unique(
                heights.begin(),
                heights.end(),
                [](const float a, const float b) {
                    return std::abs(a - b) <= MIN_WALL_HEIGHT;
                }
            ),
            heights.end()
        );
    }

    void PushOrMergeWallSpan(
        std::vector<WallSpan> &spans,
        const float bottom,
        const float top,
        const WallSpanSide side
    ) {
        if (top - bottom <= MIN_WALL_HEIGHT) return;

        if (!spans.empty()) {
            WallSpan &previous = spans.back();

            if (previous.side == side &&
                std::abs(previous.top - bottom) <= MIN_WALL_HEIGHT) {
                previous.top = top;
                return;
            }
        }

        spans.push_back({bottom, top, side});
    }

    std::vector<WallSpan> BuildWallSpans(
        const Sector *frontSector,
        const Sector *backSector
    ) {
        std::vector<float> heights;

        if (frontSector != nullptr) AddSectorHeights(*frontSector, heights);
        if (backSector != nullptr) AddSectorHeights(*backSector, heights);

        SortAndRemoveDuplicateHeights(heights);

        std::vector<WallSpan> spans;

        for (size_t i = 0; i + 1 < heights.size(); ++i) {
            const float bottom = heights[i];
            const float top = heights[i + 1];

            if (top - bottom <= MIN_WALL_HEIGHT) continue;

            const float sampleHeight = (bottom + top) * 0.5f;

            const bool frontOpen =
                    frontSector != nullptr &&
                    IsSectorOpenAtHeight(*frontSector, sampleHeight);

            const bool backOpen =
                    backSector != nullptr &&
                    IsSectorOpenAtHeight(*backSector, sampleHeight);

            if (frontOpen == backOpen) continue;

            PushOrMergeWallSpan(
                spans,
                bottom,
                top,
                frontOpen ? WallSpanSide::Front : WallSpanSide::Back
            );
        }

        return spans;
    }

    void PushGpuWallPiece(
    std::vector<GpuWall>& gpuWalls,
    const Wall& wall,
    const float bottomHeight,
    const float topHeight,
    const uint_fast32_t packedColor,
    const float textureAnchorHeight,
    const float textureDirection,
    const float textureRegionIndex,
    const WallSpanSide side
) {
        if (topHeight - bottomHeight <= MIN_WALL_HEIGHT) return;

        const uint32_t color = static_cast<uint32_t>(packedColor);

        GpuWall gpuWall{};

        gpuWall.data = {
            textureRegionIndex,
            side == WallSpanSide::Front ? 0.0f : 1.0f,
            textureAnchorHeight,
            textureDirection
        };

        gpuWall.startEnd = {
            wall.start.x,
            wall.start.y,
            wall.end.x,
            wall.end.y
        };

        gpuWall.color = {
            static_cast<float>(color & 0xFFu),
            static_cast<float>((color >> 8u) & 0xFFu),
            static_cast<float>((color >> 16u) & 0xFFu),
            static_cast<float>((color >> 24u) & 0xFFu)
        };

        gpuWall.heights = {
            bottomHeight,
            topHeight,
            0.0f,
            0.0f
        };

        gpuWall.textureOffset_padding = {
            wall.textureOffset.x,
            wall.textureOffset.y,
            0.0f,
            0.0f
        };

        gpuWalls.push_back(gpuWall);
    }
}

//todo put this function to a seperate script because it might also be used by the vulkan renderer
// (already true as of this file - kept the original TODO wording since the
// underlying suggestion, sharing this with a future third backend, still
// applies just as much to Vulkan as it did to GL)
void Vulkan::BuildGpuWallsFromMap() {
    Level& level = LevelManager::CurrentLevel();

    gpuWalls.clear();

    for (const Wall& wall : level.walls) {
        const float textureRegionIndex =
            static_cast<float>(GetTextureRegionIndex(wall.textureFileName));

        const Sector* frontSector = MapQueries::GetSectorByID(level, wall.frontSector);

        const Sector* backSector = MapQueries::GetSectorByID(level, wall.backSector);

        if (frontSector == backSector) backSector = nullptr;

        const std::vector<WallSpan> spans =
            BuildWallSpans(frontSector, backSector);

        if (spans.empty() && frontSector == nullptr && backSector == nullptr) {
            PushGpuWallPiece(
                gpuWalls,
                wall,
                0.0f,
                32.0f,
                wall.color,
                32.0f,
                -1.0f,
                textureRegionIndex,
                WallSpanSide::Front
            );

            continue;
        }

        for (const WallSpan& span : spans) {
            const bool frontSide = span.side == WallSpanSide::Front;

            PushGpuWallPiece(
                gpuWalls,
                wall,
                span.bottom,
                span.top,
                wall.color,
                frontSide ? span.top : span.bottom,
                frontSide ? -1.0f : 1.0f,
                textureRegionIndex,
                span.side
            );
        }
    }

    gpuWallCount = static_cast<int>(gpuWalls.size());
}

void Vulkan::UploadGpuWallsFromMap() {
    BuildGpuWallsFromMap();

    if (gpuWalls.empty()) return;

    UploadToDynamicBuffer(
        wallBuffers[currentFrame],
        gpuWalls.data(),
        gpuWalls.size() * sizeof(GpuWall),
        vk::BufferUsageFlagBits::eStorageBuffer
    );

    const vk::DescriptorBufferInfo bufferInfo{
        .buffer = wallBuffers[currentFrame].buffer,
        .offset = 0,
        .range = vk::WholeSize
    };

    const vk::WriteDescriptorSet write{
        .dstSet = sceneSets[currentFrame],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &bufferInfo
    };

    // Descriptor sets need re-pointing at the buffer any time
    // UploadToDynamicBuffer had to grow (reallocate) it - which,
    // unlike GL's glBufferData, can't just silently keep writing into the
    // "same" binding, since the underlying vk::Buffer handle actually
    // changed. Updating unconditionally here is simpler than tracking
    // "did it grow this frame" and costs one more vkUpdateDescriptorSets
    // call than strictly necessary on frames where capacity didn't change.
    device.updateDescriptorSets(write, {});
}

void Vulkan::BuildFlatTrianglesFromSectors() {
    Level& level = LevelManager::CurrentLevel();

    flatTriangles.clear();

    for (int sectorIndex = 0; sectorIndex < static_cast<int>(level.sectors.size()); ++sectorIndex) {
        const Sector& sector = level.sectors[sectorIndex];

        for (int floorIndex = 0; floorIndex < static_cast<int>(sector.floors.size()); ++floorIndex) {
            for (const Triangle& triangle : sector.triangles) {
                {
                    GpuFlatTriangle flatTriangle;

                    flatTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                    flatTriangle.b = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};
                    flatTriangle.c = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};

                    flatTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                    flatTriangle.data = {
                        static_cast<float>(sectorIndex),
                        static_cast<float>(floorIndex),
                        0.0f, // floor surface
                        0.0f
                    };

                    flatTriangles.push_back(flatTriangle);
                }

                {
                    GpuFlatTriangle flatTriangle;

                    flatTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                    flatTriangle.b = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};
                    flatTriangle.c = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};

                    flatTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                    flatTriangle.data = {
                        static_cast<float>(sectorIndex),
                        static_cast<float>(floorIndex),
                        1.0f, // ceiling surface
                        0.0f
                    };

                    flatTriangles.push_back(flatTriangle);
                }
            }
        }
    }

    flatTriangleCount = static_cast<int>(flatTriangles.size());
}

bool Vulkan::CreateMap() {
    spdlog::info("Creating Vulkan renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    BuildFlatTrianglesFromSectors();
    BuildGpuWallsFromMap();

    spdlog::info("Built Vulkan map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());

    // wallBuffers/flatBuffers/spriteBuffers/sectorBuffers/sectorFloorBuffers/
    // colliderBuffers already exist (created empty in InitDescriptors, same
    // spirit as GL's glBufferData(SSBO, 0, nullptr, ...) placeholders) and
    // are already bound into every frame's descriptor set. What's left here
    // is just getting the initial wall/flat contents uploaded before the
    // first real Update() call - Update() rebuilds both from scratch every
    // frame regardless, same as the GL renderer did, so this is populating
    // "frame 0" a little early rather than something that has to be kept
    // in sync going forward.
    if (!flatTriangles.empty()) {
        UploadToDynamicBuffer(
            flatBuffers[currentFrame], flatTriangles.data(),
            flatTriangles.size() * sizeof(VulkanRendererInternal::GpuFlatTriangle),
            vk::BufferUsageFlagBits::eStorageBuffer
        );

        const vk::DescriptorBufferInfo flatInfo{.buffer = flatBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize};
        const vk::WriteDescriptorSet flatWrite{
            .dstSet = sceneSets[currentFrame], .dstBinding = 1, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &flatInfo
        };
        device.updateDescriptorSets(flatWrite, {});
    }

    if (!gpuWalls.empty()) {
        UploadToDynamicBuffer(
            wallBuffers[currentFrame], gpuWalls.data(),
            gpuWalls.size() * sizeof(VulkanRendererInternal::GpuWall),
            vk::BufferUsageFlagBits::eStorageBuffer
        );

        const vk::DescriptorBufferInfo wallInfo{.buffer = wallBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize};
        const vk::WriteDescriptorSet wallWrite{
            .dstSet = sceneSets[currentFrame], .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &wallInfo
        };
        device.updateDescriptorSets(wallWrite, {});
    }

    spdlog::info("Vulkan renderer map creation completed successfully");

    return true;
}