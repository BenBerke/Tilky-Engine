//
// Created by berke on 6/18/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include "Headers/Map/LevelManager.hpp"

void OpenGL::BuildGpuColliders() {
    const Level& level = LevelManager::CurrentLevel();

    gpuColliders.clear();

    for (const ComponentCollider& collider : level.colliders.ActiveColliders()) {
        const ComponentTransform* transform = level.transforms.Get(collider.ownerID);

        if (!transform) [[unlikely]] continue;

        GpuCollider gpuCollider;

        gpuCollider.positionType.x = transform->position.x;
        gpuCollider.positionType.y = transform->position.y;
        gpuCollider.positionType.z = transform->position.z + transform->scale.z * .5f;

        gpuCollider.positionType.w = collider.type == COLLIDERTYPE_SPHERE ? 0.0f : 1.0f;

        gpuCollider.scale.x = transform->scale.x;
        gpuCollider.scale.y = transform->scale.y;
        gpuCollider.scale.z = transform->scale.z;
        gpuCollider.scale.w = transform->scale.w;

        gpuColliders.push_back(gpuCollider);
    }

    colliderCount = static_cast<GLsizei>(gpuColliders.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, colliderSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuColliders.size() * sizeof(GpuCollider),
        gpuColliders.empty() ? nullptr : gpuColliders.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, colliderSSBO);

}