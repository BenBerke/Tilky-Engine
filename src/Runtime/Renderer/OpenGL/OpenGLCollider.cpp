//
// Created by berke on 6/18/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include "Headers/Map/LevelManager.hpp"

/// Collider debug view

void OpenGL::BuildGpuColliders() {
    const Level& level = LevelManager::CurrentLevel();

    gpuColliders.clear();

    for (const ComponentCollider& collider : level.colliders.ActiveColliders()) {
        const ComponentTransform* transform = level.transforms.Get(collider.ownerID);

        if (!transform) [[unlikely]] continue;

        GpuCollider gpuCollider;

        // The collider's bottom, as physics and Raycast place it: a sphere
        // rests on it, a box stands on it. The shader draws both around a
        // centre, so lift that by the radius or half the height.
        const Vector3 position = transform->LocalToWorld(collider.offset);
        const float lift = collider.type == COLLIDERTYPE_SPHERE ? collider.scale.x : collider.scale.y * 0.5f;

        gpuCollider.positionType.x = position.x;
        gpuCollider.positionType.y = position.y + lift;
        gpuCollider.positionType.z = position.z;

        gpuCollider.positionType.w = collider.type == COLLIDERTYPE_SPHERE ? 0.0f : 1.0f;

        gpuCollider.scale.x = collider.scale.x;
        gpuCollider.scale.y = collider.scale.y;
        gpuCollider.scale.z = collider.scale.z;
        gpuCollider.scale.w = .0f;

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

/// Runtime editor entity markers

void OpenGL::BuildGpuEntityMarkers() {
    const Level& level = LevelManager::CurrentLevel();

    gpuEntityMarkers.clear();

    for (const ComponentTransform& transform : level.transforms.components)
        gpuEntityMarkers.push_back({transform.position.x, transform.position.y, transform.position.z, 0.0f});

    entityMarkerCount = static_cast<GLsizei>(gpuEntityMarkers.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, entityMarkerSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuEntityMarkers.size() * sizeof(Vector4),
        gpuEntityMarkers.empty() ? nullptr : gpuEntityMarkers.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, entityMarkerSSBO);
}
