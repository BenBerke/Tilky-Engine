#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <unordered_map>
#include <spdlog/spdlog.h>

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/WallPieces.hpp"

namespace {
    using OpenGLRendererInternal::GpuWall;
    using OpenGLRendererInternal::GpuFlatTriangle;

    // Everything that decides how many flat triangle instances exist and which
    // (sector, floor) each one points at. Cheap enough to compare every frame.
    size_t ComputeFlatLayoutSignature(const Level& level) {
        constexpr size_t PRIME = 1000003u;

        size_t signature = level.sectors.size();

        for (const Sector& sector : level.sectors) {
            signature = signature * PRIME + sector.floors.size();
            signature = signature * PRIME + sector.triangles.size();
        }

        return signature;
    }

    // The piece is lit by the sector it is seen from, with the same
    // subtractive rule as floors and ceilings (light 255 = unlit color).
    // Fake contrast nudges that light by the wall's direction: +contrast for
    // a wall along Z, -contrast along X, smoothly in between (dz^2 - dx^2 of
    // the unit direction), clamped so it never goes past full or zero light.
    Vector4 LitWallColor(const Wall& wall, const WallPieces::WallPiece& piece, const float contrast, const bool fullbright) {
        if (piece.facingSector == nullptr || fullbright) return wall.color;

        Vector3 light = piece.facingSector->light;

        const float dx = wall.end.x - wall.start.x;
        const float dz = wall.end.y - wall.start.y;
        const float lengthSq = dx * dx + dz * dz;

        if (contrast != 0.0f && lengthSq > 0.0f) {
            const float adjustment = contrast * (dz * dz - dx * dx) / lengthSq;

            light = {
                std::clamp(light.x + adjustment, 0.0f, 255.0f),
                std::clamp(light.y + adjustment, 0.0f, 255.0f),
                std::clamp(light.z + adjustment, 0.0f, 255.0f)
            };
        }

        return {
            wall.color.x - (255.0f - light.x) / 255.0f,
            wall.color.y - (255.0f - light.y) / 255.0f,
            wall.color.z - (255.0f - light.z) / 255.0f,
            wall.color.w
        };
    }

    void PushGpuWallPiece(
        std::vector<GpuWall>& gpuWalls,
        const Wall& wall,
        const WallPieces::WallPiece& piece,
        const float textureRegionIndex,
        const float wallContrast,
        const bool fullbright,
        const uint8_t metaData
    ) {
        const WallSurface& surface = wall.Surface(piece.slot);

        GpuWall gpuWall{};

        gpuWall.data = {
            textureRegionIndex,
            piece.frontFacing ? 0.0f : 1.0f,
            piece.anchorHeight,
            static_cast<float>(metaData)
        };

        gpuWall.startEnd = {
            wall.start.x,
            wall.start.y,
            wall.end.x,
            wall.end.y
        };

        gpuWall.color = LitWallColor(wall, piece, wallContrast, fullbright);

        // heights.xy = bottom/top at the start point
        // heights.zw = bottom/top at the end point
        gpuWall.heights = {
            piece.bottomStart,
            piece.topStart,
            piece.bottomEnd,
            piece.topEnd
        };

        // data2.xy = texture offset; data2.zw = independent X/Y scale.
        gpuWall.data2 = {
            surface.textureOffset.x,
            surface.textureOffset.y,
            surface.textureScale.x,
            surface.textureScale.y
        };

        // Store flip flags as floats to match the GPU vec4 layout.
        gpuWall.data3 = {
            surface.flipTextureX ? 1.0f : 0.0f,
            surface.flipTextureY ? 1.0f : 0.0f,
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

    WallPieces::SectorBoundsCache boundsCache;
    std::vector<WallPieces::WallPiece> pieces;

    const float wallContrast = level.rendererSettings.wallContrast;

    for (int wallIndex = 0; wallIndex < static_cast<int>(level.walls.size()); ++wallIndex) {
        const Wall& wall = level.walls[wallIndex];
        if (IsWallCulled(level, wall)) continue;

        const float topTextureIndex = static_cast<float>(GetTextureRegionIndex(wall.top.texture));
        const float bottomTextureIndex = static_cast<float>(GetTextureRegionIndex(wall.bottom.texture));
        const float middleTextureIndex = wall.HasMiddle() ? static_cast<float>(GetTextureRegionIndex(wall.middle.texture)) : 0.0f;

        pieces.clear();
        WallPieces::Build(level, wall, boundsCache, pieces);

        for (const WallPieces::WallPiece& piece : pieces) {
            PushGpuWallPiece(
                gpuWalls,
                wall,
                piece,
                piece.slot == WallSurfaceSlot::Bottom ? bottomTextureIndex
                    : piece.slot == WallSurfaceSlot::Middle ? middleTextureIndex
                    : topTextureIndex,
                wallContrast,
                fullbright,
                WallPieceMetaData(wallIndex, piece.slot)
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
    flatSectorRanges.assign(level.sectors.size(), {});
    flatSectorBounds.assign(level.sectors.size(), {});

    for (int sectorIndex = 0; sectorIndex < static_cast<int>(level.sectors.size()); ++sectorIndex) {
        const Sector& sector = level.sectors[sectorIndex];

        flatSectorRanges[sectorIndex].first = static_cast<GLint>(flatTriangles.size());

        if (const WallPieces::SectorBounds bounds = WallPieces::ComputeSectorBounds(sector); bounds.valid)
            flatSectorBounds[sectorIndex] = {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};

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

        flatSectorRanges[sectorIndex].count = static_cast<GLsizei>(flatTriangles.size()) - flatSectorRanges[sectorIndex].first;
    }

    flatTriangleCount = static_cast<GLsizei>(flatTriangles.size());
    flatLayoutSignature = ComputeFlatLayoutSignature(level);
}

// BuildGpuSectors() re-uploads floor data every frame, but the triangle
// instances that draw each floor and ceiling are only baked here. Without this
// a floor added at runtime has GPU data and no triangles, so it never renders.
void OpenGL::RefreshFlatTrianglesIfLayoutChanged() {
    if (flatSSBO == 0) return;

    if (ComputeFlatLayoutSignature(LevelManager::CurrentLevel()) == flatLayoutSignature) return;

    BuildFlatTrianglesFromSectors();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        flatTriangles.size() * sizeof(GpuFlatTriangle),
        flatTriangles.empty() ? nullptr : flatTriangles.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
}

// Reuses the buffers CreateMap() made. Sectors, sprites, colliders and
// models are rebuilt from the level every frame, so only the baked data -
// texture atlas, walls, floor/ceiling triangles - needs redoing here.
void OpenGL::ReloadMap() {
    spdlog::info("Reloading OpenGL renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    // Walls store atlas region indices, so the atlas comes first.
    RefreshTexturesFromLevel();
    UploadGpuWallsFromMap();

    BuildFlatTrianglesFromSectors();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        flatTriangles.size() * sizeof(GpuFlatTriangle),
        flatTriangles.empty() ? nullptr : flatTriangles.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);

    spdlog::info("Reloaded OpenGL map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());
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

    glGenBuffers(1, &entityMarkerSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, entityMarkerSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, entityMarkerSSBO);

    glGenBuffers(1, &editorGizmoSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, editorGizmoSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, editorGizmoSSBO);

    // Per-entity model transforms. Grown on demand by BuildGpuModels().
    glGenBuffers(1, &modelSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, modelSSBO);
    modelSSBOCapacity = 0;

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

    // Not fatal when missing: the driver drops uniforms a shader never reads.
    cameraWorldPosUniform = glGetUniformLocation(projectionShader->ID, "uCameraWorldPos");
    fullbrightUniform = glGetUniformLocation(projectionShader->ID, "uFullbright");
    viewportSizeUniform = glGetUniformLocation(projectionShader->ID, "uViewportSize");
    modelInstanceOffsetUniform = glGetUniformLocation(projectionShader->ID, "uModelInstanceOffset");
    flatInstanceOffsetUniform = glGetUniformLocation(projectionShader->ID, "uFlatInstanceOffset");
    modelLocalUniform = glGetUniformLocation(projectionShader->ID, "uModelLocal");
    modelLocalNormalUniform = glGetUniformLocation(projectionShader->ID, "uModelLocalNormal");
    modelBaseColorUniform = glGetUniformLocation(projectionShader->ID, "uModelBaseColor");
    modelHasTextureUniform = glGetUniformLocation(projectionShader->ID, "uModelHasTexture");
    modelTextureUniform = glGetUniformLocation(projectionShader->ID, "uModelTexture");

    spdlog::info("OpenGL renderer map creation completed successfully");

    return true;
}