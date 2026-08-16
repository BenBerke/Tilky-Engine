#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"
#include "Headers/Map/LevelManager.hpp"

/// Collider debug view

void Vulkan::BuildGpuColliders() {
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

        gpuCollider.scale.x = collider.scale.x;
        gpuCollider.scale.y = collider.scale.y;
        gpuCollider.scale.z = collider.scale.z;
        gpuCollider.scale.w = .0f;

        gpuColliders.push_back(gpuCollider);
    }

    colliderCount = static_cast<int>(gpuColliders.size());

    if (gpuColliders.empty()) return;

    UploadToDynamicBuffer(
        colliderBuffers[currentFrame], gpuColliders.data(),
        gpuColliders.size() * sizeof(GpuCollider),
        vk::BufferUsageFlagBits::eStorageBuffer
    );

    const vk::DescriptorBufferInfo colliderInfo{.buffer = colliderBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize};
    const vk::WriteDescriptorSet colliderWrite{
        .dstSet = sceneSets[currentFrame], .dstBinding = 6, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &colliderInfo
    };
    device.updateDescriptorSets(colliderWrite, {});
}