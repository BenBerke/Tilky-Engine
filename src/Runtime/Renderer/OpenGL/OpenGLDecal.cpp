#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <cmath>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"

void OpenGL::BuildGpuDecals() {
    gpuDecals.clear();

    Level& level = LevelManager::CurrentLevel();

    for (ComponentDecal& decalComponent : level.decals.components) {
        ComponentTransform* transform = level.transforms.Get(decalComponent.ownerID);

        if (transform == nullptr) continue;

        ComponentSprite* sprite = level.sprites.Get(decalComponent.ownerID);

        //todo add rotational sprite
        //const int textureIndex = sprite != nullptr ? sprite->textureIndex : -1;

        const int textureIndex = sprite != nullptr ? 0 : -1;

        if (decalComponent.wallIndex < 0 ||decalComponent.wallIndex >= static_cast<int>(level.walls.size()))
            continue;

        const Wall& wall = level.walls[decalComponent.wallIndex];

        const Vector2 wallVector = wall.vector;

        const float wallLength = std::sqrt(
            wallVector.x * wallVector.x +
            wallVector.y * wallVector.y
        );

        if (wallLength <= 0.0001f) continue;

        const Vector2 wallDir = {
            wallVector.x / wallLength,
            wallVector.y / wallLength
        };

        if (decalComponent.horizontalPos < 0.0f) {
            const Vector2 toObject = {
                transform->position.x - wall.start.x,
                transform->position.y - wall.start.y
            };

            float t =
                (toObject.x * wallVector.x + toObject.y * wallVector.y) /
                (wallLength * wallLength);

            t = std::clamp(t, 0.0f, 1.0f);

            decalComponent.horizontalPos = wallLength * t;
        }

        decalComponent.horizontalPos =
            std::clamp(decalComponent.horizontalPos, 0.0f, wallLength);

        const Vector2 wallNormal = {
            -wallDir.y,
            wallDir.x
        };

        const Vector2 decalCentre = {
            wall.start.x +
                wallDir.x * decalComponent.horizontalPos +
                wallNormal.x * decalComponent.wallNormalOffset,

            wall.start.y +
                wallDir.y * decalComponent.horizontalPos +
                wallNormal.y * decalComponent.wallNormalOffset
        };

        const float halfWidth = transform->scale.x * 0.5f;

        const Vector2 decalStart = {
            decalCentre.x - wallDir.x * halfWidth,
            decalCentre.y - wallDir.y * halfWidth
        };

        const Vector2 decalEnd = {
            decalCentre.x + wallDir.x * halfWidth,
            decalCentre.y + wallDir.y * halfWidth
        };

        int sectorIndex = transform->sectorIndex;

        if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) {
            sectorIndex = wall.frontSector;

            if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) {
                sectorIndex = wall.backSector;
            }
        }

        if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) {
            continue;
        }

        const Sector& sector = level.sectors[sectorIndex];

        float decalBottom = 0.0f;

        if (decalComponent.absHeight) {
            decalBottom = decalComponent.baseHeight + decalComponent.verticalPos;
        }
        else {
            decalBottom = sector.floorHeight + decalComponent.verticalPos;
        }

        const float decalTop = decalBottom + transform->scale.z;

        GpuDecal gpuDecal;

        gpuDecal.startEnd = {
            decalStart.x,
            decalStart.y,
            decalEnd.x,
            decalEnd.y
        };

        gpuDecal.color = {
            255.0f,
            255.0f,
            255.0f,
            255.0f
        };

        gpuDecal.heights = {
            decalBottom,
            decalTop,
            0.0f,
            0.0f
        };

        gpuDecal.data = {
            static_cast<float>(textureIndex),
            0.0f,
            0.0f,
            0.0f
        };

        gpuDecals.push_back(gpuDecal);
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