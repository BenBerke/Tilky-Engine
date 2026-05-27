#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"

void OpenGL::BuildGpuSprites() {
    gpuSprites.clear();

    Level& level = LevelManager::CurrentLevel();

    for (ComponentSprite& spriteComponent : level.sprites.components) {
        ComponentTransform* transform =
            level.transforms.Get(spriteComponent.ownerID);

        if (transform == nullptr) [[unlikely]] continue;

        if (level.decals.Has(spriteComponent.ownerID)) continue;

        //todo do proper update loop
        transform->UpdateObjectSector(level.sectors);

        if (transform->sectorIndex < 0 || transform->sectorIndex >= static_cast<int>(level.sectors.size())) [[unlikely]] continue;

        /*
            New Vector3 convention:

            position.x = map/world x
            position.y = vertical world height
            position.z = map/world z

            So an object in a sector with floorHeight = 10
            and position.y = 15 is 5 units above the floor.
        */

        GpuSprite gpuSprite;

        gpuSprite.positionSize = {
            transform->position.x,
            transform->position.z,
            transform->position.y, // Intentionally swapped
            transform->scale.y
        };


        //todo add texture coloring
        gpuSprite.color = {
            255.0f,
            255.0f,
            255.0f,
            255.0f
        };

        gpuSprite.data = {
            transform->scale.x,
            static_cast<float>(spriteComponent.textureIndex),
            0.0f,
            0.0f
        };

        gpuSprites.push_back(gpuSprite);
    }

    spriteCount = static_cast<GLsizei>(gpuSprites.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, spriteSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuSprites.size() * sizeof(GpuSprite),
        gpuSprites.empty() ? nullptr : gpuSprites.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
}