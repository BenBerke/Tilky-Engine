#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "Headers/Runtime/Renderer/RendererMath.hpp"

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"

namespace {
    using OpenGLRendererInternal::GpuWall;
    using OpenGLRendererInternal::GpuFlatTriangle;

    constexpr float MIN_WALL_HEIGHT = 0.0001f;

    void PushGpuWallPiece(
        std::vector<GpuWall>& gpuWalls,
        Wall& wall,
        const float bottomHeight,
        const float topHeight,
        const Vector4& color,
        const float textureAnchorHeight,
        const float textureDirection
    ) {
        if (!MapQueries::PushWallQuad3D(wall, bottomHeight, topHeight,MIN_WALL_HEIGHT)) return;

        GpuWall gpuWall;

        gpuWall.data = {
            static_cast<float>(wall.textureIndex),
            0.0f,
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

    void ClipFlatTriangleAgainstNearPlane(
        std::vector<GpuFlatTriangle>& visibleFlatTriangles,
        const GpuFlatTriangle& triangle,
        const Vector2& playerPos,
        const float playerAngle
    ) {
        using namespace OpenGLRendererInternal;

        std::vector<Vector4> input = {
            triangle.a,
            triangle.b,
            triangle.c
        };

        std::vector<Vector4> output;

        for (int i = 0; i < static_cast<int>(input.size()); ++i) {
            const Vector4 current = input[i];
            const Vector4 next = input[(i + 1) % input.size()];

            const float currentDepth =
                RendererMath::GetViewDepth(current, playerPos, playerAngle);

            const float nextDepth =
                RendererMath::GetViewDepth(next, playerPos, playerAngle);

            const bool currentInside = currentDepth >= FLAT_NEAR_PLANE;
            const bool nextInside = nextDepth >= FLAT_NEAR_PLANE;

            if (currentInside && nextInside) {
                output.push_back(next);
            }
            else if (currentInside && !nextInside) {
                const float t =
                    (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);

                output.push_back(RendererMath::LerpVector4(current, next, t));
            }
            else if (!currentInside && nextInside) {
                const float t =
                    (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);

                output.push_back(RendererMath::LerpVector4(current, next, t));
                output.push_back(next);
            }
        }

        if (output.size() < 3) {
            return;
        }

        if (output.size() == 3) {
            visibleFlatTriangles.push_back({
                output[0],
                output[1],
                output[2],
                triangle.color,
                triangle.data
            });
        }
        else if (output.size() == 4) {
            visibleFlatTriangles.push_back({
                output[0],
                output[1],
                output[2],
                triangle.color,
                triangle.data
            });

            visibleFlatTriangles.push_back({
                output[0],
                output[2],
                output[3],
                triangle.color,
                triangle.data
            });
        }
    }
}

//todo put this function to a seperate script because it might also be used by the vulkan renderer
void OpenGL::BuildGpuWallsFromMap() {
    Level& level = LevelManager::CurrentLevel();

    gpuWalls.clear();

    for (Wall& wall : level.walls) {
        wall.quads3D = {};
        wall.quad3DCount = 0;

        const Sector* frontSector = MapQueries::GetSectorByID(level, wall.frontSector);
        const Sector* backSector  = MapQueries::GetSectorByID(level, wall.backSector);

        if (frontSector != nullptr && backSector != nullptr && frontSector != backSector) {
            const float frontFloor = frontSector->floorHeight;
            const float backFloor = backSector->floorHeight;

            const float frontCeiling = frontSector->ceilingHeight;
            const float backCeiling = backSector->ceilingHeight;

            const float lowFloor = std::min(frontFloor, backFloor);
            const float highFloor = std::max(frontFloor, backFloor);

            const float lowCeiling = std::min(frontCeiling, backCeiling);
            const float highCeiling = std::max(frontCeiling, backCeiling);

            // Lower solid portal piece: floor height difference.
            PushGpuWallPiece(
                gpuWalls,
                wall,
                lowFloor,
                highFloor,
                wall.color,
                highFloor,
                -1.0f
            );

            // Upper solid portal piece: ceiling height difference.
            PushGpuWallPiece(
                gpuWalls,
                wall,
                lowCeiling,
                highCeiling,
                wall.color,
                lowCeiling,
                1.0f
            );

            continue;
        }

        const Sector* sector = frontSector != nullptr ? frontSector : backSector;

        if (sector == nullptr) {
            PushGpuWallPiece(
                gpuWalls,
                wall,
                0.0f,
                32.0f,
                wall.color,
                32.0f,
                -1.0f
            );

            continue;
        }

        PushGpuWallPiece(
            gpuWalls,
            wall,
            sector->floorHeight,
            sector->ceilingHeight,
            wall.color,
            sector->ceilingHeight,
            -1.0f
        );
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

void OpenGL::BuildVisibleFlatTriangles(const Vector2 &playerPos, const float playerAngle) {
    visibleFlatTriangles.clear();

    for (const GpuFlatTriangle &triangle: flatTriangles) {
        ClipFlatTriangleAgainstNearPlane(
            visibleFlatTriangles,
            triangle,
            playerPos,
            playerAngle
        );
    }

    flatTriangleCount = static_cast<GLsizei>(visibleFlatTriangles.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        visibleFlatTriangles.size() * sizeof(GpuFlatTriangle),
        visibleFlatTriangles.empty() ? nullptr : visibleFlatTriangles.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
}

void OpenGL::BuildFlatTrianglesFromSectors() {
    Level& level = LevelManager::CurrentLevel();

    flatTriangles.clear();
    visibleFlatTriangles.clear();

    for (
        int sectorIndex = 0;
        sectorIndex < static_cast<int>(level.sectors.size());
        ++sectorIndex
    ) {
        const Sector& sector = level.sectors[sectorIndex];

        for (const Triangle& triangle : sector.triangles) {
            // Floor triangle.
            {
                GpuFlatTriangle flatTriangle;

                flatTriangle.a = {
                    triangle.a.x,
                    triangle.a.y,
                    0.0f,
                    0.0f
                };

                // Reversed winding for floor.
                flatTriangle.b = {
                    triangle.c.x,
                    triangle.c.y,
                    0.0f,
                    0.0f
                };

                flatTriangle.c = {
                    triangle.b.x,
                    triangle.b.y,
                    0.0f,
                    0.0f
                };

                flatTriangle.color = {
                    255.0f,
                    255.0f,
                    255.0f,
                    255.0f
                };

                flatTriangle.data = {
                    static_cast<float>(sectorIndex),
                    0.0f, // boundary index: floor
                    static_cast<float>(sector.floorTextureIndex),
                    0.0f
                };

                flatTriangles.push_back(flatTriangle);
            }

            // Ceiling triangle.
            {
                GpuFlatTriangle flatTriangle;

                flatTriangle.a = {
                    triangle.a.x,
                    triangle.a.y,
                    0.0f,
                    0.0f
                };

                flatTriangle.b = {
                    triangle.b.x,
                    triangle.b.y,
                    0.0f,
                    0.0f
                };

                flatTriangle.c = {
                    triangle.c.x,
                    triangle.c.y,
                    0.0f,
                    0.0f
                };

                flatTriangle.color = {
                    255.0f,
                    255.0f,
                    255.0f,
                    255.0f
                };

                flatTriangle.data = {
                    static_cast<float>(sectorIndex),
                    1.0f, // boundary index: ceiling
                    static_cast<float>(sector.ceilingTextureIndex),
                    0.0f
                };

                flatTriangles.push_back(flatTriangle);
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

    spdlog::info(
        "Built OpenGL map GPU data. Walls: {}, flat triangles: {}",
        gpuWalls.size(),
        flatTriangles.size()
    );

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

    glGenBuffers(1, &decalSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, decalSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, decalSSBO);

    glGenBuffers(1, &sectorSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);

    glGenBuffers(1, &colliderSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, colliderSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, colliderSSBO);

    projectionShader->use();

    renderModeUniform = glGetUniformLocation(
        projectionShader->ID,
        "renderMode"
    );

    if (renderModeUniform == -1) {
        spdlog::critical(
            "Failed to get projection shader uniform location: renderMode"
        );
        return false;
    }

    viewUniform = glGetUniformLocation(
        projectionShader->ID,
        "uView"
    );

    if (viewUniform == -1) {
        spdlog::critical(
            "Failed to get projection shader uniform location: uView"
        );
        return false;
    }

    projectionUniform = glGetUniformLocation(
        projectionShader->ID,
        "uProjection"
    );

    if (projectionUniform == -1) {
        spdlog::critical(
            "Failed to get projection shader uniform location: uProjection"
        );
        return false;
    }

    spdlog::info("OpenGL renderer map creation completed successfully");

    return true;
}