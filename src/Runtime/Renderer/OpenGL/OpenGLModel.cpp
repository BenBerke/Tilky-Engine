//
// Created by berke on 9/26/2026.
//

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include "Headers/Objects/Components.hpp"

void OpenGL::BuildGpuModels() {
    gpuSprites.clear();

    Level& level = LevelManager::CurrentLevel();

    for (ComponentModel& modelComponent : level.models.components) {
        ComponentTransform* transform = level.transforms.Get(modelComponent.ownerID);

        if (transform == nullptr) [[unlikely]] continue;

        // Upaate teh GpuSprites and upload the SSBO

        modelCount = static_cast<GLsizei>(gpuModels.size());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelSSBO);

        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            gpuModels.size() * sizeof(GpuSprite),
            gpuModels.empty() ? nullptr : gpuModels.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,modelSSBO);
    }
}