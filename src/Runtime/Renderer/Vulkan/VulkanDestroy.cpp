#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <SDL3/SDL_init.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

// The OpenGL version of this file was ~120 lines of individually
// null-checked glDelete*/SDL_Destroy* calls, in careful dependency order.
// vk::raii makes nearly all of that automatic: every Vulkan handle here
// destroys itself (in the correct dependency order - device-children
// before the device, instance-children before the instance) the moment
// its wrapper is reset or goes out of scope, the same way a
// std::unique_ptr would. What's left below is explicitly ordered anyway
// rather than just left to `~Vulkan() = default`, so that resources are
// actually released the moment Shutdown() runs (matching the GL version's
// immediate, deterministic teardown) instead of whenever the owning
// Vulkan object itself eventually gets destroyed - which may be much
// later, e.g. if the engine keeps a moment-in-time snapshot of the
// renderer pointer alive after calling Shutdown().
//
// The one hard rule that matters here: everything that was created FROM
// `device` (or `instance`) must be reset before `device` (or `instance`)
// itself is. Relative order *among* device's children doesn't matter -
// a vk::raii::Pipeline and a vk::raii::Buffer don't depend on each other,
// only on the device that created them both.
void Vulkan::Shutdown() {
    if (device == nullptr) return; // Initialize() never got far enough to need teardown

    device.waitIdle();

    // editorCamera/editorCameraTransform are `inline static` members on
    // IRenderer itself, not per-backend state - freed here the same as
    // the GL Shutdown() did, via the inherited protected
    // DestroyEditorCamera().
    DestroyEditorCamera();

    // ImGui's Vulkan backend holds references into `device` (and the
    // descriptor pool/render target formats it was initialized with), so
    // it has to be torn down before those go away, same as the GL version
    // shutting down ImGui's OpenGL backend before destroying the GL context.
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    // Must happen while `device` is still valid, same comment as the GL
    // version had for why this runs first among the "real" cleanup.
    DestroyAllTextures();

    Characters.clear();
    fontAtlasView = nullptr;
    fontAtlasSampler = nullptr;
    fontAtlasImage = nullptr;
    fontAtlasMemory = nullptr;
    for (auto& buffer : glyphVertexBuffers) buffer = DynamicBuffer{};

    for (auto& buffer : wallBuffers) buffer = DynamicBuffer{};
    for (auto& buffer : flatBuffers) buffer = DynamicBuffer{};
    for (auto& buffer : spriteBuffers) buffer = DynamicBuffer{};
    for (auto& buffer : sectorBuffers) buffer = DynamicBuffer{};
    for (auto& buffer : sectorFloorBuffers) buffer = DynamicBuffer{};
    for (auto& buffer : colliderBuffers) buffer = DynamicBuffer{};
    for (auto& buffer : sceneUboBuffers) buffer = DynamicBuffer{};
    textureRegionBuffer = DynamicBuffer{};
    uiVertexBuffer = DynamicBuffer{};
    uiIndexBuffer = DynamicBuffer{};

    gpuWalls.clear();
    flatTriangles.clear();
    gpuSprites.clear();
    gpuColliders.clear();
    gpuSectors.clear();
    gpuSectorFloors.clear();
    gpuWallCount = 0;
    flatTriangleCount = 0;
    spriteCount = 0;
    colliderCount = 0;

    wallPipeline = nullptr;
    flatPipeline = nullptr;
    spritePipeline = nullptr;
    colliderPipeline = nullptr;
    scenePipelineLayout = nullptr;

    uiPipeline = nullptr;
    uiPipelineLayout = nullptr;

    backgroundPipeline = nullptr;
    backgroundPipelineLayout = nullptr;

    textPipeline = nullptr;
    textPipelineLayout = nullptr;

    sceneSets.clear();
    backgroundSet = nullptr;
    textSet = nullptr;
    sceneSetLayout = nullptr;
    backgroundSetLayout = nullptr;
    textSetLayout = nullptr;
    descriptorPool = nullptr;
    imguiDescriptorPool = nullptr;

    inFlightFences.clear();
    renderFinishedSemaphores.clear();
    imageAvailableSemaphores.clear();
    commandBuffers.clear();
    commandPool = nullptr;

    DestroySwapchainResources(); // depth image/view + swapchain image views (waits idle again internally - harmless)
    swapchain = nullptr;

    device = nullptr;
    physicalDevice = nullptr;
    surface = nullptr;
    debugMessenger = nullptr;
    instance = nullptr;

    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}