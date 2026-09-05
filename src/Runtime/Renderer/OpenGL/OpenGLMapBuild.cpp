#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <unordered_map>
#include <spdlog/spdlog.h>

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"

namespace {
    using OpenGLRendererInternal::GpuWall;
    using OpenGLRendererInternal::GpuFlatTriangle;

    constexpr float MIN_WALL_HEIGHT = 0.0001f;

    enum class WallSpanSide {
        Front,
        Back
    };

    // Axis aligned bounds of a sector's triangulated area, in map XY.
    // This must match GetSectorBounds() in Rendering_vs.glsl exactly -
    // the shader derives slope offsets from the same rectangle, so any
    // difference here shows up as a seam between a sloped flat and the
    // wall that is supposed to close it.
    struct SectorBounds {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool valid = false;
    };

    SectorBounds ComputeSectorBounds(const Sector& sector) {
        SectorBounds bounds;

        for (const Triangle& triangle : sector.triangles) {
            const Vector2 points[3] = {triangle.a, triangle.b, triangle.c};

            for (const Vector2& point : points) {
                if (!bounds.valid) {
                    bounds.minX = point.x;
                    bounds.maxX = point.x;
                    bounds.minY = point.y;
                    bounds.maxY = point.y;
                    bounds.valid = true;

                    continue;
                }

                bounds.minX = std::min(bounds.minX, point.x);
                bounds.maxX = std::max(bounds.maxX, point.x);
                bounds.minY = std::min(bounds.minY, point.y);
                bounds.maxY = std::max(bounds.maxY, point.y);
            }
        }

        return bounds;
    }

    // Walls are visited once per side, so most sectors get looked up
    // several times per rebuild. Bounds are pure triangle data, so
    // computing them once per sector is enough.
    class SectorBoundsCache {
    public:
        const SectorBounds& Get(const Sector* sector) {
            if (sector == nullptr) return emptyBounds;

            const auto existing = cache.find(sector);

            if (existing != cache.end()) return existing->second;

            return cache.emplace(sector, ComputeSectorBounds(*sector)).first->second;
        }

    private:
        std::unordered_map<const Sector*, SectorBounds> cache;
        SectorBounds emptyBounds;
    };

    // Mirrors GetSlopeOffset() in Rendering_vs.glsl. slopeStrength is used
    // as a direct height-per-unit gradient here because that is what the
    // shader does; if it should be read as an angle instead, wrap it in
    // std::tan() here and tan() there in the same commit.
    float GetSlopeOffset(
        const Vector2& point,
        const SectorBounds& bounds,
        const SlopeDirection slopeDirection,
        const float slopeStrength
    ) {
        if (!bounds.valid || slopeStrength == 0.0f) return 0.0f;

        const float gradient = slopeStrength * Constants::DegToRad;

        switch (slopeDirection) {
            case PLUS_X: return (point.x - bounds.minX) * gradient;
            case MINUS_X: return (bounds.maxX - point.x) * gradient;
            case PLUS_Z: return (point.y - bounds.minY) * gradient;
            case MINUS_Z: return (bounds.maxY - point.y) * gradient;
        }

        return 0.0f;
    }

    float GetSurfaceHeight(
        const SectorSurface& surface,
        const SectorBounds& bounds,
        const Vector2& point
    ) {
        return surface.height +
               GetSlopeOffset(point, bounds, surface.slopeDirection, surface.slopeStrength);
    }

    struct SectorSample {
        const Sector* sector = nullptr;
        SectorBounds bounds;
    };

    // The three points along the wall we evaluate slopes at. Start and end
    // give the geometry, middle decides the topology (which spans exist).
    struct WallSamplePoints {
        Vector2 start;
        Vector2 middle;
        Vector2 end;
    };

    // One floor or ceiling plane, sampled at each of those three points.
    struct HeightSample {
        float start = 0.0f;
        float middle = 0.0f;
        float end = 0.0f;
    };

    struct WallSpan {
        HeightSample bottom;
        HeightSample top;
        WallSpanSide side;
    };

    bool IsSectorOpenAtHeight(
        const SectorSample& sample,
        const Vector2& point,
        const float height
    ) {
        if (sample.sector == nullptr) return false;

        for (const SectorFloor& floor: sample.sector->floors) {
            const float floorHeight = GetSurfaceHeight(floor.floor, sample.bounds, point);
            const float ceilingHeight = GetSurfaceHeight(floor.ceiling, sample.bounds, point);

            if (height > floorHeight + MIN_WALL_HEIGHT &&
                height < ceilingHeight - MIN_WALL_HEIGHT) {
                return true;
            }
        }

        return false;
    }

    void AddSectorHeights(
        const SectorSample& sample,
        const WallSamplePoints& points,
        std::vector<HeightSample>& heights
    ) {
        if (sample.sector == nullptr) return;

        for (const SectorFloor& floor: sample.sector->floors) {
            for (const SectorSurface* surface: {&floor.floor, &floor.ceiling}) {
                heights.push_back({
                    GetSurfaceHeight(*surface, sample.bounds, points.start),
                    GetSurfaceHeight(*surface, sample.bounds, points.middle),
                    GetSurfaceHeight(*surface, sample.bounds, points.end)
                });
            }
        }
    }

    void SortAndRemoveDuplicateHeights(std::vector<HeightSample>& heights) {
        std::ranges::sort(
            heights,
            [](const HeightSample& a, const HeightSample& b) {
                return a.middle < b.middle;
            }
        );

        // Two planes only count as the same plane if they agree along the
        // whole wall - equal in the middle but diverging at the ends is a
        // real gap that still needs covering.
        heights.erase(
            std::unique(
                heights.begin(),
                heights.end(),
                [](const HeightSample& a, const HeightSample& b) {
                    return std::abs(a.start - b.start) <= MIN_WALL_HEIGHT &&
                           std::abs(a.middle - b.middle) <= MIN_WALL_HEIGHT &&
                           std::abs(a.end - b.end) <= MIN_WALL_HEIGHT;
                }
            ),
            heights.end()
        );
    }

    void PushOrMergeWallSpan(
        std::vector<WallSpan>& spans,
        const HeightSample& bottom,
        const HeightSample& top,
        const WallSpanSide side
    ) {
        if (!spans.empty()) {
            WallSpan& previous = spans.back();

            if (previous.side == side &&
                std::abs(previous.top.start - bottom.start) <= MIN_WALL_HEIGHT &&
                std::abs(previous.top.middle - bottom.middle) <= MIN_WALL_HEIGHT &&
                std::abs(previous.top.end - bottom.end) <= MIN_WALL_HEIGHT) {
                previous.top = top;

                return;
            }
        }

        spans.push_back({bottom, top, side});
    }

    struct SpanProbe {
        Vector2 point;
        float bottom = 0.0f;
        float top = 0.0f;
    };

    // Slopes can make a slab pinch to nothing at one end while still being
    // open at the other, so the openness test runs where the slab is
    // thickest rather than always at the wall's middle.
    SpanProbe PickThickestProbe(
        const HeightSample& bottom,
        const HeightSample& top,
        const WallSamplePoints& points
    ) {
        const SpanProbe probes[3] = {
            {points.start, bottom.start, top.start},
            {points.middle, bottom.middle, top.middle},
            {points.end, bottom.end, top.end}
        };

        SpanProbe best = probes[0];

        for (int i = 1; i < 3; ++i) {
            if (probes[i].top - probes[i].bottom > best.top - best.bottom) {
                best = probes[i];
            }
        }

        return best;
    }

    std::vector<WallSpan> BuildWallSpans(
        const SectorSample& frontSector,
        const SectorSample& backSector,
        const WallSamplePoints& points
    ) {
        std::vector<HeightSample> heights;

        AddSectorHeights(frontSector, points, heights);
        AddSectorHeights(backSector, points, heights);

        SortAndRemoveDuplicateHeights(heights);

        std::vector<WallSpan> spans;

        for (size_t i = 0; i + 1 < heights.size(); ++i) {
            const HeightSample& bottom = heights[i];
            const HeightSample& top = heights[i + 1];

            const SpanProbe probe = PickThickestProbe(bottom, top, points);

            if (probe.top - probe.bottom <= MIN_WALL_HEIGHT) continue;

            const float sampleHeight = (probe.bottom + probe.top) * 0.5f;

            const bool frontOpen = IsSectorOpenAtHeight(frontSector, probe.point, sampleHeight);

            const bool backOpen = IsSectorOpenAtHeight(backSector, probe.point, sampleHeight);

            if (frontOpen == backOpen) continue;

            PushOrMergeWallSpan(spans, bottom, top,
            frontOpen ? WallSpanSide::Front : WallSpanSide::Back
            );
        }

        return spans;
    }

    void PushGpuWallPiece(
        std::vector<GpuWall>& gpuWalls,
        const Wall& wall,
        const HeightSample& bottom,
        const HeightSample& top,
        const Vector4& color,
        const float textureRegionIndex,
        const WallSpanSide side
    ) {
        const float bottomStart = bottom.start;
        const float bottomEnd = bottom.end;

        // Crossing slopes could invert a quad at one end; clamping keeps
        // the piece degenerate there instead of flipping it inside out.
        const float topStart = std::max(top.start, bottomStart);
        const float topEnd = std::max(top.end, bottomEnd);

        if (topStart - bottomStart <= MIN_WALL_HEIGHT &&
            topEnd - bottomEnd <= MIN_WALL_HEIGHT) {
            return;
        }

        const bool frontSide = side == WallSpanSide::Front;

        // A single world height so the texture keeps a constant vertical
        // alignment; the sloped edges cut it instead of skewing it.
        const float textureAnchorHeight = frontSide ? std::max(topStart, topEnd) : std::min(bottomStart, bottomEnd);

        const float textureDirection = frontSide ? -1.0f : 1.0f;

        GpuWall gpuWall{};

        gpuWall.data = {
            textureRegionIndex,
            frontSide ? 0.0f : 1.0f,
            textureAnchorHeight,
            textureDirection
        };

        gpuWall.startEnd = {
            wall.start.x,
            wall.start.y,
            wall.end.x,
            wall.end.y
        };

        gpuWall.color = color;

        // heights.xy = bottom/top at the start point
        // heights.zw = bottom/top at the end point
        gpuWall.heights = {
            bottomStart,
            topStart,
            bottomEnd,
            topEnd
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

//todo TILKYTODO put this function to a seperate script because it might also be used by the vulkan renderer
void OpenGL::BuildGpuWallsFromMap() {
    Level& level = LevelManager::CurrentLevel();

    gpuWalls.clear();

    SectorBoundsCache boundsCache;

    for (const Wall& wall : level.walls) {
        const float textureRegionIndex =
            static_cast<float>(GetTextureRegionIndex(wall.textureFileName));

        const Sector* frontSectorPtr = MapQueries::GetSectorByID(level, wall.frontSector);

        const Sector* backSectorPtr = MapQueries::GetSectorByID(level, wall.backSector);

        if (frontSectorPtr == backSectorPtr) backSectorPtr = nullptr;

        const SectorSample frontSector{frontSectorPtr, boundsCache.Get(frontSectorPtr)};

        const SectorSample backSector{backSectorPtr, boundsCache.Get(backSectorPtr)};

        const WallSamplePoints points{
            wall.start,
            Vector2{
                (wall.start.x + wall.end.x) * 0.5f,
                (wall.start.y + wall.end.y) * 0.5f
            },
            wall.end
        };

        const std::vector<WallSpan> spans =
            BuildWallSpans(frontSector, backSector, points);

        if (spans.empty() && frontSectorPtr == nullptr && backSectorPtr == nullptr) {
            PushGpuWallPiece(
                gpuWalls,
                wall,
                HeightSample{0.0f, 0.0f, 0.0f},
                HeightSample{32.0f, 32.0f, 32.0f},
                wall.color,
                textureRegionIndex,
                WallSpanSide::Front
            );

            continue;
        }

        for (const WallSpan& span : spans) {
            PushGpuWallPiece(
                gpuWalls,
                wall,
                span.bottom,
                span.top,
                wall.color,
                textureRegionIndex,
                span.side
            );
        }
    }

    gpuWallCount = static_cast<GLsizei>(gpuWalls.size());
}

void OpenGL::UploadGpuWallsFromMap() {
    BuildGpuWallsFromMap();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuWalls.size() * sizeof(GpuWall),
        gpuWalls.empty() ? nullptr : gpuWalls.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
}

void OpenGL::BuildFlatTrianglesFromSectors() {
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

    flatTriangleCount = static_cast<GLsizei>(flatTriangles.size());
}

bool OpenGL::CreateMap() {
    using namespace OpenGLRendererInternal;

    spdlog::info("Creating OpenGL renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    BuildFlatTrianglesFromSectors();
    BuildGpuWallsFromMap();

    spdlog::info("Built OpenGL map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());

    glGenBuffers(1, &wallSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuWalls.size() * sizeof(GpuWall),
        gpuWalls.empty() ? nullptr : gpuWalls.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);

    glGenBuffers(1, &flatSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        flatTriangles.size() * sizeof(GpuFlatTriangle),
        flatTriangles.empty() ? nullptr : flatTriangles.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);

    glGenBuffers(1, &spriteSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, spriteSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);

    glEnable(GL_PROGRAM_POINT_SIZE);

    // Sector
    glGenBuffers(1, &sectorSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);

    // Sector floor
    glGenBuffers(1, &sectorFloorSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorFloorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sectorFloorSSBO);

    glGenBuffers(1, &colliderSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, colliderSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, colliderSSBO);

    projectionShader->use();

    renderModeUniform = glGetUniformLocation(projectionShader->ID, "renderMode");

    if (renderModeUniform == -1) {
        spdlog::critical("Failed to get projection shader uniform location: renderMode");
        return false;
    }

    viewUniform = glGetUniformLocation(projectionShader->ID, "uView");

    if (viewUniform == -1) {
        spdlog::critical("Failed to get projection shader uniform location: uView");
        return false;
    }

    projectionUniform = glGetUniformLocation(projectionShader->ID, "uProjection");

    if (projectionUniform == -1) {
        spdlog::critical("Failed to get projection shader uniform location: uProjection");
        return false;
    }

    spdlog::info("OpenGL renderer map creation completed successfully");

    return true;
}