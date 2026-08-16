#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <spdlog/spdlog.h>

// NOTE on vk::raii::SwapchainKHR::acquireNextImage / vk::raii::Queue::presentKHR:
// both throw a typed exception (vk::OutOfDateKHRError) on VK_ERROR_OUT_OF_DATE_KHR
// but return VK_SUBOPTIMAL_KHR normally (it's in their accepted-success-codes
// list, same as the classic C API's swapchain calls). The exact return
// *type* for acquireNextImage (std::pair<vk::Result,uint32_t> on older
// Vulkan-Hpp, vk::ResultValue<uint32_t> since PR #2226) differs by version,
// which is why this uses a structured binding (`auto [result, index] = ...`)
// rather than naming the type directly - both shapes support it.

void Vulkan::BeginFrame() {
    if (window == nullptr) {
        spdlog::error("Vulkan::BeginFrame called with null window");
        return;
    }

    [[maybe_unused]] const vk::Result waitResult =
        device.waitForFences(*inFlightFences[currentFrame], vk::True, std::numeric_limits<uint64_t>::max());

    if (swapchainDirty) {
        RecreateSwapchain();
        if (swapchainDirty) return; // still zero-sized (minimized) - skip this frame entirely
    }

    uint32_t imageIndex = 0;

    try {
        const auto [result, index] = swapchain.acquireNextImage(
            std::numeric_limits<uint64_t>::max(), *imageAvailableSemaphores[currentFrame], nullptr
        );

        imageIndex = index;
        if (result == vk::Result::eSuboptimalKHR) swapchainDirty = true; // finish this frame, recreate before the next
    } catch (const vk::OutOfDateKHRError&) {
        swapchainDirty = true;
        RecreateSwapchain();
        return;
    }

    currentImageIndex = imageIndex;

    device.resetFences(*inFlightFences[currentFrame]);

    vk::raii::CommandBuffer& cmd = commandBuffers[currentFrame];
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo{});

    activeCommandBuffer = &cmd;

    TransitionImageLayout(
        cmd, swapchainImages[currentImageIndex],
        vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor
    );

    // GL: glClearColor(1,1,1,1); glClearDepth(0.0); glClear(COLOR|DEPTH).
    // The depth clear of 0.0 (not the usual 1.0) matches the reversed-Z
    // setup from glClipControl/glDepthFunc(GL_GREATER) in the original
    // InitializeOpenGL() - see VulkanUpdate.cpp for the full trace.
    vk::ClearValue colorClear;
    colorClear.color = vk::ClearColorValue(std::array{1.0f, 1.0f, 1.0f, 1.0f});

    vk::ClearValue depthClear;
    depthClear.depthStencil = vk::ClearDepthStencilValue{0.0f, 0};

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchainImageViews[currentImageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = colorClear
    };

    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = depthClear
    };

    const vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0}, .extent = swapchainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment
    };

    cmd.beginRendering(renderingInfo);

    // Negative-height viewport: this is the standard trick to make
    // Vulkan's Y-down NDC behave like GL's Y-up NDC, so the engine's
    // existing view/projection matrices AND its GL_CCW front-face
    // convention both carry over unchanged instead of needing every
    // camera/winding assumption re-derived for Vulkan. See the longer
    // note in VulkanUpdate.cpp where the flat/sector pipeline's
    // GL_CCW-equivalent cull state is set up.
    const vk::Viewport viewport{
        .x = 0.0f,
        .y = static_cast<float>(swapchainExtent.height),
        .width = static_cast<float>(swapchainExtent.width),
        .height = -static_cast<float>(swapchainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    cmd.setViewport(0, viewport);

    const vk::Rect2D scissor{.offset = {0, 0}, .extent = swapchainExtent};
    cmd.setScissor(0, scissor);

    // Everything BeginFrame() set here (depth test/write/func, cull,
    // blend) is baked per-pipeline in Vulkan rather than being runtime
    // state, so unlike glEnable(GL_DEPTH_TEST)/glDepthFunc(GL_GREATER)/
    // glDisable(GL_CULL_FACE) here, there's nothing further to set - which
    // pipeline gets bound in Update()/DrawUIRectangle/RenderText decides it.
}

void Vulkan::EndFrame() {
    if (activeCommandBuffer == nullptr) {
        return; // BeginFrame bailed this frame (minimized window, out-of-date swapchain)
    }

    vk::raii::CommandBuffer& cmd = *activeCommandBuffer;

    cmd.endRendering();

    TransitionImageLayout(
        cmd, swapchainImages[currentImageIndex],
        vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor
    );

    cmd.end();

    constexpr vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    const vk::CommandBuffer rawCmd = cmd;

    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*imageAvailableSemaphores[currentFrame],
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &rawCmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*renderFinishedSemaphores[currentFrame]
    };

    graphicsQueue.submit(submitInfo, *inFlightFences[currentFrame]);

    const vk::SwapchainKHR rawSwapchain = *swapchain;

    const vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderFinishedSemaphores[currentFrame],
        .swapchainCount = 1,
        .pSwapchains = &rawSwapchain,
        .pImageIndices = &currentImageIndex
    };

    try {
        const vk::Result presentResult = graphicsQueue.presentKHR(presentInfo);
        if (presentResult == vk::Result::eSuboptimalKHR) swapchainDirty = true;
    } catch (const vk::OutOfDateKHRError&) {
        swapchainDirty = true;
    }

    activeCommandBuffer = nullptr;
    currentFrame = (currentFrame + 1) % FRAMES_IN_FLIGHT;
}

void Vulkan::OnResize(const int width, const int height) {
    if (width <= 0 || height <= 0) return;

    screenWidth = width;
    screenHeight = height;

    // Unlike GL's immediate glViewport call, actually rebuilding the
    // swapchain here would mean doing GPU work (device.waitIdle(), image
    // recreation) inside whatever event-handling context calls OnResize -
    // deferred instead to the top of the next BeginFrame, which also
    // collapses a burst of resize events during an active drag into a
    // single recreation instead of one per event.
    swapchainDirty = true;
}