#include "Headers/Editor/Editor.hpp"
#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <spdlog/spdlog.h>

void Vulkan::DrawBackground(
    const float pitch, const float yaw, const float horizontalFov, const float parallaxStrength, const float backgroundScroll) {
    using namespace VulkanRendererInternal;

    // Reads Editor::backgroundTextureFileName, NOT this class's own
    // (structurally-present-but-unused-by-any-uploaded-.cpp)
    // backgroundTextureFileName member - matches the original exactly,
    // odd as that split ownership looks. Kept the member on Vulkan too,
    // for the same reason it presumably still exists on OpenGL: something
    // outside these files may read/write it.
    const std::string& fileName = Editor::backgroundTextureFileName;

    if (activeCommandBuffer == nullptr || fileName.empty()) return;

    const int textureIndex = GetOrCreateTextureIndex(fileName);
    if (textureIndex < 0 || textureIndex >= GetTextureCount()) {
        spdlog::error("Failed to resolve background texture '{}'", fileName);
        return;
    }

    const GPUTexture& texture = GetTexture(textureIndex);

    const vk::DescriptorImageInfo imageInfo{
        .sampler = texture.sampler,
        .imageView = texture.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    const vk::WriteDescriptorSet write{
        .dstSet = backgroundSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageInfo
    };

    // Re-pointed every call, same simplicity trade-off GL made by just
    // rebinding whatever GL texture GetOrCreateTextureIndex resolved -
    // this only actually changes when the editor's background selection
    // changes, so the extra vkUpdateDescriptorSets call is cheap.
    device.updateDescriptorSets(write, {});

    const BackgroundPushConstants pushConstants{
        .playerPitch = pitch,
        .playerAngle = yaw,
        .horizontalFov = horizontalFov,
        .parallaxStrength = parallaxStrength,
        .backgroundScroll = backgroundScroll
    };

    vk::raii::CommandBuffer& cmd = *activeCommandBuffer;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, backgroundPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, backgroundPipelineLayout, 0, *backgroundSet, {});

    cmd.pushConstants<BackgroundPushConstants>(
        backgroundPipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        pushConstants
    );

    // No vertex buffer bound - same fullscreen-triangle-from-nowhere trick
    // as the GL version's empty VAO + glDrawArrays(GL_TRIANGLES, 0, 3);
    // the vertex shader is expected to synthesize the triangle from
    // gl_VertexIndex alone.
    cmd.draw(3, 1, 0, 0);
}