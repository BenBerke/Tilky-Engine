#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <cmath>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Wall.hpp"

void OpenGL::BuildGpuDecals() {
    gpuDecals.clear();

    Level& level = LevelManager::CurrentLevel();

    constexpr float MIN_DECAL_SIZE = 0.0001f;
    constexpr float FLOOR_DECAL_OFFSET = 0.01f;

    for (ComponentDecal& decalComponent : level.decals.components) {
        ComponentTransform* transform = level.transforms.Get(decalComponent.ownerID);
        if (transform == nullptr) continue;

        const ComponentSprite* sprite = level.sprites.Get(decalComponent.ownerID);

        const int textureIndex =
            sprite != nullptr
                ? GetTextureRegionIndex(sprite->textureFileNames[0])
                : -1;

        /*
         * Floor decal
         */
        if (decalComponent.type == FLOOR) {
            const float halfWidth = std::abs(transform->scale.x) * 0.5f;
            const float halfDepth = std::abs(transform->scale.z) * 0.5f;

            if (halfWidth <= MIN_DECAL_SIZE || halfDepth <= MIN_DECAL_SIZE) continue;

            const float decalHeight =
                transform->position.y + FLOOR_DECAL_OFFSET;

            const Vector2 decalMin = {
                transform->position.x - halfWidth,
                transform->position.z - halfDepth
            };

            const Vector2 decalMax = {
                transform->position.x + halfWidth,
                transform->position.z + halfDepth
            };

            GpuDecal gpuDecal;

            gpuDecal.startEnd = {
                decalMin.x,
                decalMin.y,
                decalMax.x,
                decalMax.y
            };

            gpuDecal.color = {
                255.0f,
                255.0f,
                255.0f,
                255.0f
            };

            gpuDecal.heights = {
                decalHeight,
                decalHeight,
                0.0f,
                0.0f
            };

            gpuDecal.data = {
                static_cast<float>(textureIndex),
                static_cast<float>(decalComponent.type),
                0.0f,
                0.0f
            };

            gpuDecals.push_back(gpuDecal);
            continue;
        }

        /*
         * Wall decal
         */
        if (decalComponent.type != WALL) continue;

        if (decalComponent.wallIndex < 0 ||
            decalComponent.wallIndex >= static_cast<int>(level.walls.size())) {
            continue;
        }

        const Wall& wall = level.walls[decalComponent.wallIndex];
        const Vector2 wallVector = wall.vector;

        const float wallLength = std::hypot(
            wallVector.x,
            wallVector.y
        );

        if (wallLength <= MIN_DECAL_SIZE) continue;

        const float inverseWallLength = 1.0f / wallLength;

        const Vector2 wallDirection = {
            wallVector.x * inverseWallLength,
            wallVector.y * inverseWallLength
        };

        if (decalComponent.horizontalPos < 0.0f) {
            const Vector2 toObject = {
                transform->position.x - wall.start.x,
                transform->position.z - wall.start.y
            };

            const float projectedDistance = toObject.x * wallDirection.x + toObject.y * wallDirection.y;

            decalComponent.horizontalPos = std::clamp(
                projectedDistance,
                0.0f,
                wallLength
            );
        }
        else {
            decalComponent.horizontalPos = std::clamp(
                decalComponent.horizontalPos,
                0.0f,
                wallLength
            );
        }

        const Vector2 wallNormal = {-wallDirection.y, wallDirection.x};

        const Vector2 decalCentre = {
            wall.start.x +
                wallDirection.x * decalComponent.horizontalPos +
                wallNormal.x * decalComponent.wallNormalOffset,

            wall.start.y +
                wallDirection.y * decalComponent.horizontalPos +
                wallNormal.y * decalComponent.wallNormalOffset
        };

        const float halfWidth = std::abs(transform->scale.x) * 0.5f;
        const float decalHeight = std::abs(transform->scale.y);

        if (halfWidth <= MIN_DECAL_SIZE || decalHeight <= MIN_DECAL_SIZE) continue;

        const Vector2 decalStart = {
            decalCentre.x - wallDirection.x * halfWidth,
            decalCentre.y - wallDirection.y * halfWidth
        };

        const Vector2 decalEnd = {
            decalCentre.x + wallDirection.x * halfWidth,
            decalCentre.y + wallDirection.y * halfWidth
        };

        const float decalBottom = transform->position.y;
        const float decalTop = decalBottom + decalHeight;

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
            static_cast<float>(decalComponent.type),
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