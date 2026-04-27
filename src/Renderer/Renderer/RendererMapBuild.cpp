#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "../../Headers/Renderer/Renderer/RendererInternal.hpp"

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Renderer/MapEditor.hpp"

#include <algorithm>
#include <SDL3/SDL_log.h>

namespace RendererInternal {
    static bool IsValidSectorIndex(const int index) {
        return index >= 0 && index < static_cast<int>(MapEditor::sectors.size());
    }

    static bool IsPortalWall(const Wall& wall) {
        return IsValidSectorIndex(wall.frontSector) &&
               IsValidSectorIndex(wall.backSector);
    }

    static void PushGpuWallPiece(
        const Wall& wall,
        const float bottomHeight,
        const float topHeight,
        const Vector4& color
    ) {
        if (topHeight <= bottomHeight + 0.0001f) {
            return;
        }

        GpuWall gpuWall;

        gpuWall.data = {
            static_cast<float>(wall.textureIndex),
            0.0f,
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

    void BuildGpuWallsFromMap() {
        gpuWalls.clear();

        for (const Wall& wall : MapEditor::walls) {
            if (IsPortalWall(wall)) {
                const Sector& front = MapEditor::sectors[wall.frontSector];
                const Sector& back = MapEditor::sectors[wall.backSector];

                const float lowerBottom = std::min(front.floorHeight, back.floorHeight);
                const float lowerTop = std::max(front.floorHeight, back.floorHeight);

                PushGpuWallPiece(wall, lowerBottom, lowerTop, wall.color);

                const float upperBottom = std::min(front.ceilingHeight, back.ceilingHeight);
                const float upperTop = std::max(front.ceilingHeight, back.ceilingHeight);

                PushGpuWallPiece(wall, upperBottom, upperTop, wall.color);

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
                PushGpuWallPiece(wall, 0.0f, 32.0f, wall.color);
                continue;
            }

            const Sector& sector = MapEditor::sectors[sectorIndex];

            PushGpuWallPiece(
                wall,
                sector.floorHeight,
                sector.ceilingHeight,
                wall.color
            );
        }

        gpuWallCount = static_cast<GLsizei>(gpuWalls.size());
    }

    static void ClipFlatTriangleAgainstNearPlane(
        const GpuFlatTriangle& triangle,
        const Vector2& playerPos,
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
            }
            else if (currentInside && !nextInside) {
                const float t = (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);
                output.push_back(LerpVector4(current, next, t));
            }
            else if (!currentInside && nextInside) {
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

    void BuildVisibleFlatTriangles(const Vector2& playerPos, const float playerAngle) {
        visibleFlatTriangles.clear();

        for (const GpuFlatTriangle& triangle : flatTriangles) {
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

        for (const Sector& sector : MapEditor::sectors) {
            const Vector4 floorColor = {
                sector.floorColor.x,
                sector.floorColor.y,
                sector.floorColor.z,
                255.0f
            };

            const Vector4 ceilingColor = {
                sector.ceilingColor.x,
                sector.ceilingColor.y,
                sector.ceilingColor.z,
                255.0f
            };

            for (const Triangle& triangle : sector.triangles) {
                GpuFlatTriangle floorTriangle;

                floorTriangle.a = {triangle.a.x, triangle.a.y, sector.floorHeight, 0.0f};
                floorTriangle.b = {triangle.c.x, triangle.c.y, sector.floorHeight, 0.0f};
                floorTriangle.c = {triangle.b.x, triangle.b.y, sector.floorHeight, 0.0f};
                floorTriangle.color = floorColor;
                floorTriangle.data = {
                    static_cast<float>(sector.floorTextureIndex),
                    0.0f,
                    0.0f,
                    0.0f
                };

                flatTriangles.push_back(floorTriangle);

                GpuFlatTriangle ceilingTriangle;

                ceilingTriangle.a = {triangle.a.x, triangle.a.y, sector.ceilingHeight, 0.0f};
                ceilingTriangle.b = {triangle.b.x, triangle.b.y, sector.ceilingHeight, 0.0f};
                ceilingTriangle.c = {triangle.c.x, triangle.c.y, sector.ceilingHeight, 0.0f};
                ceilingTriangle.color = ceilingColor;
                ceilingTriangle.data = {
                    static_cast<float>(sector.ceilingTextureIndex),
                    0.0f,
                    0.0f,
                    0.0f
                };

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

        renderModeUniform = glGetUniformLocation(projectionShader->ID, "renderMode");
        if (renderModeUniform == -1) {
            SDL_Log("Failed to get shader uniform location renderMode");
            return false;
        }

        return true;
    }
}