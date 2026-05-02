#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "RendererInternal.hpp"

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Renderer/MapEditor.hpp"

#include <algorithm>
#include <SDL3/SDL_log.h>

#include "config.h"

namespace RendererInternal {
    static float GetSectorFloorHeight(const Sector &sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    static float GetWallFloorBottomHeight(const Sector &sector, const int floorIndex) {
        const float floorHeight = GetSectorFloorHeight(sector);

        return sector.floorHeight + floorHeight * static_cast<float>(floorIndex);
    }

    static float GetWallFloorTopHeight(const Sector &sector, const int floorIndex) {
        const float floorHeight = GetSectorFloorHeight(sector);

        return sector.floorHeight + floorHeight * static_cast<float>(floorIndex + 1);
    }

    static bool IsValidSectorIndex(const int index) {
        Level& level = LevelManager::CurrentLevel();

        return index >= 0 && index < static_cast<int>(level.sectors.size());
    }

    static bool IsPortalWall(const Wall& wall) {
        return IsValidSectorIndex(wall.frontSector) &&
               IsValidSectorIndex(wall.backSector) &&
               wall.frontSector != wall.backSector;
    }

    static void PushGpuWallPiece(
        const Wall& wall,
        const float bottomHeight,
        const float topHeight,
        const Vector4& color,
        const int floor,
        const float textureAnchorHeight,
        const float textureDirection
    ) {
        if (topHeight <= bottomHeight + 0.0001f) {
            return;
        }

        GpuWall gpuWall;

        gpuWall.data = {
            static_cast<float>(wall.textureIndex),
            static_cast<float>(floor),
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

        gpuWalls.push_back(gpuWall);
    }

    static float GetSectorStoreyHeight(const Sector &sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    static float GetSectorBoundaryHeight(const Sector &sector, const int boundaryIndex) {
        const float storeyHeight = GetSectorStoreyHeight(sector);

        return sector.floorHeight + storeyHeight * static_cast<float>(boundaryIndex);
    }

    static float GetWallBandBottomHeight(const Sector &sector, const int floorIndex) {
        return GetSectorBoundaryHeight(sector, floorIndex);
    }

    static float GetWallBandTopHeight(const Sector &sector, const int floorIndex) {
        return GetSectorBoundaryHeight(sector, floorIndex + 1);
    }

    static void PushPortalBoundaryDifference(
        const Wall& wall,
        const Sector& front,
        const int frontBoundaryIndex,
        const Sector& back,
        const int backBoundaryIndex,
        const int label
    ) {
        const float frontHeight = GetSectorBoundaryHeight(front, frontBoundaryIndex);
        const float backHeight = GetSectorBoundaryHeight(back, backBoundaryIndex);

        const float bottom = std::min(frontHeight, backHeight);
        const float top = std::max(frontHeight, backHeight);

        PushGpuWallPiece(
            wall,
            bottom,
            top,
            wall.color,
            label,
            top,
            -1.0f
        );
    }

    static int GetSafeSectorFloorCount(const Sector& sector) {
        return std::clamp(sector.floorCount, 1, MAX_FLOOR_COUNT);
    }

    void BuildGpuWallsFromMap() {
        Level& level = LevelManager::CurrentLevel();

        gpuWalls.clear();

        for (const Wall& wall : level.walls) {
            if (IsPortalWall(wall)) {
                const Sector& front = level.sectors[wall.frontSector];
                const Sector& back = level.sectors[wall.backSector];

                const int frontFloorCount = GetSafeSectorFloorCount(front);
                const int backFloorCount = GetSafeSectorFloorCount(back);

                const int frontFloorBoundary = std::clamp(
                    wall.floor,
                    0,
                    frontFloorCount
                );

                const int backFloorBoundary = std::clamp(
                    wall.floor,
                    0,
                    backFloorCount
                );

                const float frontFloor = GetSectorBoundaryHeight(front, frontFloorBoundary);
                const float backFloor = GetSectorBoundaryHeight(back, backFloorBoundary);

                const float frontTopCeiling = GetSectorBoundaryHeight(front, frontFloorCount);
                const float backTopCeiling = GetSectorBoundaryHeight(back, backFloorCount);

                const float lowFloor = std::min(frontFloor, backFloor);
                const float highFloor = std::max(frontFloor, backFloor);

                const float lowCeiling = std::min(frontTopCeiling, backTopCeiling);
                const float highCeiling = std::max(frontTopCeiling, backTopCeiling);

                // Lower portal wall: floor difference for this wall's floor
                PushGpuWallPiece(
                    wall,
                    lowFloor,
                    highFloor,
                    wall.color,
                    wall.floor,
                    highFloor,
                    -1.0f
                );

                // Upper portal wall: ceiling difference
                PushGpuWallPiece(
                    wall,
                    lowCeiling,
                    highCeiling,
                    wall.color,
                    std::max(frontFloorCount, backFloorCount),
                    lowCeiling,
                    1.0f
                );

                continue;
            }

            int sectorIndex = -1;

            if (IsValidSectorIndex(wall.frontSector)) {
                sectorIndex = wall.frontSector;
            }
            else if (IsValidSectorIndex(wall.backSector)) {
                sectorIndex = wall.backSector;
            }

            if (sectorIndex == -1) {
                PushGpuWallPiece(
                    wall,
                    0.0f,
                    32.0f,
                    wall.color,
                    wall.floor,
                    32.0f,
                    -1.0f
                );
                continue;
            }

            const Sector& sector = level.sectors[sectorIndex];

            const int sectorFloorCount = GetSafeSectorFloorCount(sector);

            if (wall.floor < 0 || wall.floor >= sectorFloorCount) {
                continue;
            }

            const float wallBottom = GetWallBandBottomHeight(sector, wall.floor);
            const float wallTop = GetWallBandTopHeight(sector, wall.floor);

            PushGpuWallPiece(
                wall,
                wallBottom,
                wallTop,
                wall.color,
                wall.floor,
                wallTop,
                -1.0f
            );
        }

        gpuWallCount = static_cast<GLsizei>(gpuWalls.size());
    }

    void UploadGpuWallsFromMap() {
        BuildGpuWallsFromMap();

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, Renderer::wallSSBO);

        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            gpuWalls.size() * sizeof(GpuWall),
            gpuWalls.empty() ? nullptr : gpuWalls.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, Renderer::wallSSBO);
    }

    static void ClipFlatTriangleAgainstNearPlane(
        const GpuFlatTriangle &triangle,
        const Vector2 &playerPos,
        const float playerAngle
    ) {
        std::vector<Vector4> input = {
            triangle.a,
            triangle.b,
            triangle.c
        };

        std::vector<Vector4> output;

        for (int i = 0; i < static_cast<int>(input.size()); ++i) {
            const Vector4 current = input[i];
            const Vector4 next = input[(i + 1) % input.size()];

            const float currentDepth = GetViewDepth(current, playerPos, playerAngle);
            const float nextDepth = GetViewDepth(next, playerPos, playerAngle);

            const bool currentInside = currentDepth >= FLAT_NEAR_PLANE;
            const bool nextInside = nextDepth >= FLAT_NEAR_PLANE;

            if (currentInside && nextInside) {
                output.push_back(next);
            } else if (currentInside && !nextInside) {
                const float t = (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);
                output.push_back(LerpVector4(current, next, t));
            } else if (!currentInside && nextInside) {
                const float t = (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);
                output.push_back(LerpVector4(current, next, t));
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
        } else if (output.size() == 4) {
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

    void BuildVisibleFlatTriangles(const Vector2 &playerPos, const float playerAngle) {
        visibleFlatTriangles.clear();

        for (const GpuFlatTriangle &triangle: flatTriangles) {
            ClipFlatTriangleAgainstNearPlane(triangle, playerPos, playerAngle);
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

    void BuildFlatTrianglesFromSectors() {
        Level& level = LevelManager::CurrentLevel();

        flatTriangles.clear();
        visibleFlatTriangles.clear();

        for (int sectorIndex = 0; sectorIndex < static_cast<int>(level.sectors.size()); ++sectorIndex) {
            const Sector& sector = level.sectors[sectorIndex];

            const int sectorFloorCount = GetSafeSectorFloorCount(sector);
            const int sectorBoundaryCount = sectorFloorCount + 1;

            for (const Triangle& triangle : sector.triangles) {
                for (int boundaryIndex = 0; boundaryIndex < sectorBoundaryCount; ++boundaryIndex) {
                    GpuFlatTriangle flatTriangle;

                    int textureIndex = sector.floorTextureIndex;

                    if (boundaryIndex > 0) {
                        const int ceilingIndex = boundaryIndex - 1;

                        if (ceilingIndex >= 0 && ceilingIndex < MAX_FLOOR_COUNT) {
                            textureIndex = sector.ceilingTextureIndices[ceilingIndex];
                        }
                    }

                    flatTriangle.a = {
                        triangle.a.x,
                        triangle.a.y,
                        0.0f,
                        0.0f
                    };

                    if (boundaryIndex == 0) {
                        // Bottom floor uses reversed winding.
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
                    }
                    else {
                        // Ceilings / upper floors.
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
                    }

                    flatTriangle.color = {
                        255.0f,
                        255.0f,
                        255.0f,
                        255.0f
                    };

                    flatTriangle.data = {
                        static_cast<float>(sectorIndex),
                        static_cast<float>(boundaryIndex),
                        static_cast<float>(textureIndex),
                        0.0f
                    };

                    flatTriangles.push_back(flatTriangle);
                }
            }
        }

        flatTriangleCount = static_cast<GLsizei>(flatTriangles.size());
    }
}

namespace Renderer {
    bool CreateMap() {
        using namespace RendererInternal;

        MapEditor::TriangulateSectors();

        BuildFlatTrianglesFromSectors();
        BuildGpuWallsFromMap();

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

        projectionShader->use();

        constexpr int MAX_WALL_TEXTURES = 8;

        for (int i = 0; i < MAX_WALL_TEXTURES; ++i) {
            std::string uniformName = "wallTextures[" + std::to_string(i) + "]";

            GLint location = glGetUniformLocation(
                projectionShader->ID,
                uniformName.c_str()
            );

            if (location != -1) {
                glUniform1i(location, i);
            }
        }

        playerPosUniform = glGetUniformLocation(projectionShader->ID, "playerPos");
        if (playerPosUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerPos");
            return false;
        }

        playerAngleUniform = glGetUniformLocation(projectionShader->ID, "playerAngle");
        if (playerAngleUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerAngle");
            return false;
        }

        playerHeightUniform = glGetUniformLocation(projectionShader->ID, "playerHeight");
        if (playerHeightUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerHeight");
            return false;
        }

        playerCamZUniform = glGetUniformLocation(projectionShader->ID, "playerCamZ");
        if (playerCamZUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerCamZ");
            return false;
        }

        renderModeUniform = glGetUniformLocation(projectionShader->ID, "renderMode");
        if (renderModeUniform == -1) {
            SDL_Log("Failed to get shader uniform location renderMode");
            return false;
        }

        return true;
    }
}