#include "RendererInternal.hpp"

#include "Headers/Renderer/MapEditor.hpp"
#include "Headers/Objects/Object.h"
#include "Headers/Objects/Wall.hpp"

namespace RendererInternal {
    void BuildGpuDecals() {
        gpuDecals.clear();

        for (Object& object : MapEditor::objects) {
            if (object.type != OBJ_DECAL) {
                continue;
            }

            if (object.wallIndex < 0 ||
                object.wallIndex >= static_cast<int>(MapEditor::walls.size())) {
                continue;
            }

            const Wall& wall = MapEditor::walls[object.wallIndex];

            const Vector2 wallVector = wall.vector;

            const float wallLength = std::sqrt(
                wallVector.x * wallVector.x +
                wallVector.y * wallVector.y
            );

            if (wallLength <= 0.0001f) {
                continue;
            }

            const Vector2 wallDir = wall.dir;

            // Only calculate wallOffset from object.position if it has not been set yet.
            // Ideally, set wallOffset once when placing the decal in the editor.
            if (object.wallOffset < 0.0f) {
                const Vector2 toObject = object.position - wall.start;

                float t = (toObject.x * wallVector.x + toObject.y * wallVector.y) / (wallLength * wallLength);

                t = std::clamp(t, 0.0f, 1.0f);

                object.wallOffset = wallLength * t;
            }

            object.wallOffset = std::clamp(object.wallOffset, 0.0f, wallLength);

            const Vector2 decalCentre = {
                (wall.start.x + wallDir.x * object.wallOffset),
                wall.start.y + wallDir.y * object.wallOffset
            };

            const float halfWidth = object.width * 0.5f;

            const Vector2 decalStart = {
                decalCentre.x - wallDir.x * halfWidth,
                decalCentre.y - wallDir.y * halfWidth
            };

            const Vector2 decalEnd = {
                decalCentre.x + wallDir.x * halfWidth,
                decalCentre.y + wallDir.y * halfWidth
            };

            GpuDecal decal;

            decal.startEnd = {
                decalStart.x,
                decalStart.y,
                decalEnd.x,
                decalEnd.y
            };

            decal.color = {
                255.0f,
                255.0f,
                255.0f,
                255.0f
            };

            // decal.heights = {
            //     object.bottomHeight,
            //     object.bottomHeight + object.height,
            //     0.0f,
            //     0.0f
            // };

            int sectorIndex = object.sectorIndex;

            if (sectorIndex < 0 ||
                sectorIndex >= static_cast<int>(MapEditor::sectors.size())) {
                sectorIndex = wall.frontSector;

                if (sectorIndex < 0 ||
                    sectorIndex >= static_cast<int>(MapEditor::sectors.size())) {
                    sectorIndex = wall.backSector;
                    }
                }

            if (sectorIndex < 0 ||
                sectorIndex >= static_cast<int>(MapEditor::sectors.size())) {
                continue;
                }

            const Sector& sector = MapEditor::sectors[sectorIndex];

            const float sectorHeight =
                sector.ceilingHeight - sector.floorHeight;

            const int floor = std::clamp(
                wall.floor,
                0,
                std::max(1, sector.floorCount) - 1
            );

            const float decalBottom = object.decalBaseHeight + object.zOffset;
            const float decalTop = decalBottom + object.height;

            decal.heights = {
                decalBottom,
                decalTop,
                0.0f,
                0.0f
            };

            decal.data = {
                static_cast<float>(object.textureIndex),
                0.0f,
                0.0f,
                0.0f
            };

            gpuDecals.push_back(decal);
        }

        decalCount = static_cast<GLsizei>(gpuDecals.size());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, decalSSBO);

        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            gpuDecals.size() * sizeof(GpuDecal),
            gpuDecals.empty() ? nullptr : gpuDecals.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, decalSSBO);
    }
}