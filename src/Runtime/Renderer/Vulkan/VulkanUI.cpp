#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"
#include <spdlog/spdlog.h>

void Vulkan::DrawUIRectangle(
    const Vector2& position,
    const Vector2& size,
    const Vector4& color,
    const float rotation,
    const std::string& texture
) const {
    using namespace VulkanRendererInternal;

    if (activeCommandBuffer == nullptr) {
        spdlog::critical("DrawUIRectangle failed: called outside BeginFrame/EndFrame");
        return;
    }

    if (size.x <= 0.0f || size.y <= 0.0f) {
        spdlog::critical(
            "DrawUIRectangle failed: invalid size ({}, {}) for texture '{}'",
            size.x,
            size.y,
            texture
        );
        return;
    }

    if (texture.empty()) {
        spdlog::critical(
            "DrawUIRectangle: texture name is empty at position ({}, {})",
            position.x,
            position.y
        );
    }

    int textureIndex = -1;

    if (!texture.empty()) textureIndex = GetTextureRegionIndex(texture);

    const bool useTexture = textureIndex >= 0 && textureIndex < static_cast<int>(textureRegions.size());

    if (!texture.empty() && textureRegions.empty())
        spdlog::critical("DrawUIRectangle failed: textureRegions is empty while drawing '{}'", texture);

    if (!texture.empty() && textureIndex < 0)
        spdlog::critical("DrawUIRectangle failed: texture '{}' was not found in the atlas", texture);

    if (textureIndex >= static_cast<int>(textureRegions.size()) && textureIndex >= 0) {
        spdlog::critical(
            "DrawUIRectangle failed: texture '{}' resolved to invalid index {} "
            "with only {} atlas regions",
            texture,
            textureIndex,
            textureRegions.size()
        );
    }

    if (useTexture && (atlasImage == nullptr || atlasSampler == nullptr)) {
        spdlog::critical("DrawUIRectangle failed: atlas image/sampler not ready");
        return;
    }

    UIPushConstants pushConstants{
        .screenSize = {static_cast<float>(screenWidth), static_cast<float>(screenHeight)},
        .position = {position.x, position.y},
        .size = {size.x, size.y},
        .color = {color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, color.w / 255.0f},
        .rotation = static_cast<float>(rotation * Constants::DegToRad),
        .useTexture = useTexture ? 1 : 0,
        .textureIndex = textureIndex,
        .textureCount = static_cast<int>(textureRegions.size())
    };

    vk::raii::CommandBuffer& cmd = *activeCommandBuffer;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, uiPipeline);

    // sceneSets carries bindings 5 (texture region SSBO) and 8 (atlas
    // sampler) that the UI shader needs to resolve `texture` into a
    // sampled color, same underlying atlas/region data the wall/flat/
    // sprite/collider pipelines use.
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, uiPipelineLayout, 0, *sceneSets[currentFrame], {});

    cmd.pushConstants<UIPushConstants>(
        uiPipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        pushConstants
    );

    cmd.bindVertexBuffers(0, {*uiVertexBuffer.buffer}, {vk::DeviceSize{0}});
    cmd.bindIndexBuffer(uiIndexBuffer.buffer, 0, vk::IndexType::eUint32);

    cmd.drawIndexed(6, 1, 0, 0, 0);

    // GL's DrawUIRectangle ended with glDisable(GL_BLEND) and checked
    // glGetError(). Neither has a per-draw-call Vulkan equivalent: blend
    // state is baked into uiPipeline rather than being global toggled
    // state, and Vulkan validation (in debug builds, via the
    // VK_LAYER_KHRONOS_validation layer enabled in InitVulkanInstance)
    // reports errors through the DebugCallback in VulkanInit.cpp instead
    // of a per-call glGetError() poll.
}