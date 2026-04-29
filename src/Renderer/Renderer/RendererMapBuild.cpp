#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "RendererInternal.hpp"

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Renderer/MapEditor.hpp"

#include <algorithm>
#include <SDL3/SDL_log.h>

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
        return index >= 0 && index < static_cast<int>(MapEditor::sectors.size());
    }

    static bool IsPortalWall(const Wall &wall) {
        //return IsValidSectorIndex(wall.frontSector) &&
        //  IsValidSectorIndex(wall.backSector);
        return wall.frontSector != -1 && wall.backSector != -1 && wall.frontSector != wall.backSector;
    }

    static void PushGpuWallPiece(
        const Wall &wall,
        const float bottomHeight,
        const float topHeight,
        const Vector4 &color,
        const int floor
    ) {
        if (topHeight <= bottomHeight + 0.0001f) {
            return;
        }

        GpuWall gpuWall;

        gpuWall.data = {
            static_cast<float>(wall.textureIndex),
            static_cast<float>(floor),
            0.0f,
            0.0f
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

    static float GetSectorStoreyHeight(const Sector& sector) {
        return sector.ceilingHeight - sector.floorHeight;
    }

    static float GetSectorBoundaryHeight(const Sector& sector, const int boundaryIndex) {
        const float storeyHeight = GetSectorStoreyHeight(sector);

        if (boundaryIndex == 0) {
            return sector.floorHeight;
        }

        if (boundaryIndex == 1) {
            return sector.ceilingHeight;
        }

        if (boundaryIndex == 2) {
            return sector.ceilingHeight + storeyHeight;
        }

        return sector.floorHeight;
    }

    static void PushPortalBoundaryDifference(
        const Wall& wall,
        const Sector& front,
        const Sector& back,
        const int boundaryIndex
    ) {
        const float frontHeight = GetSectorBoundaryHeight(front, boundaryIndex);
        const float backHeight = GetSectorBoundaryHeight(back, boundaryIndex);

        PushGpuWallPiece(
            wall,
            std::min(frontHeight, backHeight),
            std::max(frontHeight, backHeight),
            wall.color,
            boundaryIndex
        );
    }

    static float GetWallBandBottomHeight(const Sector& sector, const int floorIndex) {
        return GetSectorBoundaryHeight(sector, floorIndex);
    }

    static float GetWallBandTopHeight(const Sector& sector, const int floorIndex) {
        return GetSectorBoundaryHeight(sector, floorIndex + 1);
    }

    void BuildGpuWallsFromMap() {
        gpuWalls.clear();

        for (const Wall &wall: MapEditor::walls) {
            if (IsPortalWall(wall)) {
                const Sector& front = MapEditor::sectors[wall.frontSector];
                const Sector& back = MapEditor::sectors[wall.backSector];

                // floor difference
                PushPortalBoundaryDifference(wall, front, back, 0);

                // first ceiling / second floor difference
                //PushPortalBoundaryDifference(wall, front, back, 1);

                // second ceiling difference
                PushPortalBoundaryDifference(wall, front, back, 2);

                continue;
            }

            int sectorIndex = -1;

            if (IsValidSectorIndex(wall.frontSector)) {
                sectorIndex = wall.frontSector;
            } else if (IsValidSectorIndex(wall.backSector)) {
                sectorIndex = wall.backSector;
            }

            if (sectorIndex == -1) {
                PushGpuWallPiece(wall, 0.0f, 32.0f, wall.color, wall.floor);
                continue;
            }

            const Sector& sector = MapEditor::sectors[sectorIndex];

            PushGpuWallPiece(
                wall,
                GetWallBandBottomHeight(sector, wall.floor),
                GetWallBandTopHeight(sector, wall.floor),
                wall.color,
                wall.floor
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
        flatTriangles.clear();
        visibleFlatTriangles.clear();

        for (int sectorIndex = 0; sectorIndex < static_cast<int>(MapEditor::sectors.size()); ++sectorIndex) {
            const Sector &sector = MapEditor::sectors[sectorIndex];

            for (const Triangle &triangle: sector.triangles) {
                GpuFlatTriangle floorTriangle;

                floorTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                floorTriangle.b = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};
                floorTriangle.c = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};

                floorTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                floorTriangle.data = {
                    static_cast<float>(sectorIndex),
                    0.0f, // floor
                    0.0f,
                    0.0f
                };

                flatTriangles.push_back(floorTriangle);

                GpuFlatTriangle ceilingTriangle;

                ceilingTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                ceilingTriangle.b = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};
                ceilingTriangle.c = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};

                ceilingTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                ceilingTriangle.data = {
                    static_cast<float>(sectorIndex),
                    1.0f, // ceiling
                    0.0f,
                    0.0f
                };

                GpuFlatTriangle higherCeilingTriangle;

                higherCeilingTriangle.a = {triangle.a.x, triangle.a.y, 0.0f, 0.0f};
                higherCeilingTriangle.b = {triangle.b.x, triangle.b.y, 0.0f, 0.0f};
                higherCeilingTriangle.c = {triangle.c.x, triangle.c.y, 0.0f, 0.0f};

                higherCeilingTriangle.color = {255.0f, 255.0f, 255.0f, 255.0f};

                higherCeilingTriangle.data = {
                    static_cast<float>(sectorIndex),
                    2.0f, // higher ceiling
                    0.0f,
                    0.0f
                };

                flatTriangles.push_back(higherCeilingTriangle);

                flatTriangles.push_back(ceilingTriangle);
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
