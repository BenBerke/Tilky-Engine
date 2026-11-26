// The Vulkan device and what every draw goes through: the window's
// swapchain, frames in flight, memory (VMA), the bindless descriptor set,
// samplers, images, the per-frame arena, render targets and the frame's
// begin/end.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include <VkBootstrap.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
// Older vcpkg puts VMA's header under vma/, newer vcpkg (and CI, which uses
// the latest) at the include root.
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif

#include "Headers/Project/ProjectManager.hpp"

using namespace VulkanInternal;

namespace {
    constexpr VkDeviceSize ARENA_CHUNK_SIZE = 8ull * 1024 * 1024;
    constexpr VkDeviceSize ZERO_BUFFER_SIZE = 64 * 1024;
    constexpr uint32_t IMGUI_TEXTURE_SETS = 4096;

    bool ValidationWanted() {
        const char* value = std::getenv("TILKY_VULKAN_VALIDATION");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

#ifdef __APPLE__
    // macOS has no Vulkan driver: the build ships MoltenVK (Vulkan on Metal)
    // as libMoltenVK.dylib beside the executable, and SDL loads that instead
    // of searching for a system Vulkan loader. Validation runs need the
    // Vulkan SDK's loader (the layers live there), so it's used first when
    // TILKY_VULKAN_VALIDATION is set and one is installed. A user-set
    // SDL_VULKAN_LIBRARY always wins. Call before creating the window.
    void UseBundledMoltenVK() {
        if (SDL_GetHint(SDL_HINT_VULKAN_LIBRARY) != nullptr) return;

        if (ValidationWanted() && SDL_Vulkan_LoadLibrary(nullptr)) {
            SDL_Vulkan_UnloadLibrary();
            spdlog::info("Vulkan: validation wanted, using the system Vulkan loader instead of the bundled MoltenVK");
            return;
        }

        const char* basePath = SDL_GetBasePath();
        if (basePath == nullptr) return;

        // Beside the executable, or in an app bundle's Frameworks folder
        // (SDL_GetBasePath is Contents/Resources/ there).
        const std::filesystem::path base(basePath);
        for (const std::filesystem::path& candidate : {base / "libMoltenVK.dylib", base / ".." / "Frameworks" / "libMoltenVK.dylib"}) {
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec)) continue;

            SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, candidate.string().c_str());
            spdlog::info("Vulkan: using MoltenVK at {}", candidate.string());
            return;
        }

        spdlog::warn("Vulkan: libMoltenVK.dylib is not beside the executable; looking for a system Vulkan loader");
    }
#endif

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) spdlog::error("Vulkan validation: {}", data->pMessage);
        else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) spdlog::warn("Vulkan validation: {}", data->pMessage);
        return VK_FALSE;
    }

    VkSampler MakeSampler(const VkDevice device, const VkFilter mag, const VkFilter min, const VkSamplerMipmapMode mipmap,
                          const float maxLod, const VkSamplerAddressMode u, const VkSamplerAddressMode v, const float bias = 0.0f) {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = mag;
        info.minFilter = min;
        info.mipmapMode = mipmap;
        info.addressModeU = u;
        info.addressModeV = v;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
#ifdef __APPLE__
        // MoltenVK's portability subset has no samplerMipLodBias (Metal
        // samplers have no LOD bias), so mips there are a touch softer than
        // on Windows.
        (void)bias;
        info.mipLodBias = 0.0f;
#else
        info.mipLodBias = bias;
#endif
        info.minLod = 0.0f;
        info.maxLod = maxLod;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) return VK_NULL_HANDLE;
        return sampler;
    }

    // GL's filtering for each level texture setting (ApplyTextureSampling).
    VkSampler MakeSettingSampler(const VkDevice device, const RendererTextureSettings setting, const VkSamplerAddressMode address) {
        switch (setting) {
            case PIXEL_ART_LESS_MOIRE:
                return MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE, address, address);
            case PIXEL_ART_SMOOTH_DISTANCE:
            case RETRO:
                return MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE, address, address);
            case REALISTIC_NORMAL:
            case LOW_RES:
                return MakeSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE, address, address);
            case PIXEL_ART_SHIMMERY:
            default:
                return MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, address, address);
        }
    }

    VkAccessFlags2 AccessFor(const VkImageLayout layout) {
        switch (layout) {
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return VK_ACCESS_2_TRANSFER_WRITE_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return VK_ACCESS_2_TRANSFER_READ_BIT;
            default:
                return VK_ACCESS_2_NONE;
        }
    }

    VkPipelineStageFlags2 StageFor(const VkImageLayout layout) {
        switch (layout) {
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                return VK_PIPELINE_STAGE_2_NONE;
            default:
                return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
    }
}

// ============================================================================
// Window and device
// ============================================================================

bool Vulkan::InitWindow(const std::string& windowName, const bool startHidden) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::critical("SDL_Init failed while initializing video subsystem: {}", SDL_GetError());
        return false;
    }

#ifdef __APPLE__
    UseBundledMoltenVK();
#endif

    const SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | (startHidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_MAXIMIZED);
    window = SDL_CreateWindow(windowName.c_str(), screenWidth, screenHeight, flags);

    if (window == nullptr) {
        spdlog::critical("SDL_CreateWindow (Vulkan) failed: {}", SDL_GetError());
        return false;
    }

    const std::filesystem::path iconPath = ProjectManager::FindAssetPath(std::filesystem::path("LauncherAssets") / "Fox.png");
    if (SDL_Surface* icon = IMG_Load(iconPath.string().c_str()); icon != nullptr) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
    else spdlog::warn("Renderer window icon failed to load. This does not break the renderer. Path: {}", iconPath.string());

    return true;
}

bool Vulkan::InitDevice() {
    const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (getInstanceProcAddr == nullptr) {
        spdlog::error("Vulkan: no Vulkan loader ({})", SDL_GetError());
        return false;
    }

    volkInitializeCustom(getInstanceProcAddr);

    Uint32 sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (sdlExtensions == nullptr) {
        spdlog::error("Vulkan: SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    const bool validation = ValidationWanted();

    vkb::InstanceBuilder instanceBuilder(getInstanceProcAddr);
    instanceBuilder.set_app_name("Tilky")
        .set_engine_name("Tilky")
        .require_api_version(1, 3, 0)
        .enable_extensions(sdlExtensionCount, sdlExtensions);

    if (validation) instanceBuilder.request_validation_layers(true).set_debug_callback(DebugCallback);

    const auto instanceResult = instanceBuilder.build();
    if (!instanceResult) {
        spdlog::error("Vulkan: creating the instance failed: {}", instanceResult.error().message());
        return false;
    }

    const vkb::Instance& vkbInstance = instanceResult.value();
    instance = vkbInstance.instance;
    debugMessenger = vkbInstance.debug_messenger;
    volkLoadInstance(instance);

    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        spdlog::error("Vulkan: SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceFeatures features{};
    features.fillModeNonSolid = VK_TRUE;
    features.shaderClipDistance = VK_TRUE;

    vkb::PhysicalDeviceSelector selector(vkbInstance);
    const auto physicalResult = selector.set_surface(surface)
        .set_minimum_version(1, 3)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .set_required_features(features)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .select();

    if (!physicalResult) {
        spdlog::error("Vulkan: no GPU with Vulkan 1.3 and the features the renderer needs: {}", physicalResult.error().message());
        return false;
    }

    const vkb::PhysicalDevice& vkbPhysical = physicalResult.value();
    physicalDevice = vkbPhysical.physical_device;

    vkb::DeviceBuilder deviceBuilder(vkbPhysical);
    const auto deviceResult = deviceBuilder.build();
    if (!deviceResult) {
        spdlog::error("Vulkan: creating the device failed: {}", deviceResult.error().message());
        return false;
    }

    const vkb::Device& vkbDevice = deviceResult.value();
    device = vkbDevice.device;
    volkLoadDevice(device);

    const auto queueIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    const auto graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!queueIndex || !graphicsQueue) {
        spdlog::error("Vulkan: the device has no graphics queue");
        return false;
    }
    queueFamily = queueIndex.value();
    queue = graphicsQueue.value();

    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamily, surface, &presentSupported);
    if (!presentSupported) {
        spdlog::error("Vulkan: the graphics queue can't present to the window");
        return false;
    }

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.pVulkanFunctions = &functions;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        spdlog::error("Vulkan: creating the memory allocator failed");
        return false;
    }

    // Depth 24 + stencil 8 like GL's window, else 32 + 8.
    for (const VkFormat format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat = format;
            break;
        }
    }
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        spdlog::error("Vulkan: no depth/stencil format");
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    spdlog::info("Vulkan: using {} (Vulkan {}.{}.{})", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
                 VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache);

    return true;
}

bool Vulkan::CreateSwapchain() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (width <= 0 || height <= 0) return false;

    // Supported present modes, for SetVSync's choice.
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());
    const auto has = [&modes](const VkPresentModeKHR mode) { return std::ranges::find(modes, mode) != modes.end(); };

    presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (vsyncMode == -1 && has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    else if (vsyncMode == 0) {
        if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (has(VK_PRESENT_MODE_MAILBOX_KHR)) presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    const VkSwapchainKHR old = swapchain;

    vkb::SwapchainBuilder builder(physicalDevice, device, surface, queueFamily, queueFamily);
    const auto result = builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(presentMode)
        .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .set_old_swapchain(old)
        .build();

    if (!result) {
        spdlog::error("Vulkan: creating the swapchain failed: {}", result.error().message());
        return false;
    }

    // The old one's images may still be in use by a frame in flight.
    vkDeviceWaitIdle(device);
    for (const VkImageView view : swapchainViews) vkDestroyImageView(device, view, nullptr);
    for (const VkSemaphore semaphore : renderFinished) vkDestroySemaphore(device, semaphore, nullptr);
    swapchainViews.clear();
    renderFinished.clear();
    if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, old, nullptr);
    DestroyImage(windowDepth);

    vkb::Swapchain vkbSwapchain = result.value();
    swapchain = vkbSwapchain.swapchain;
    swapchainFormat = vkbSwapchain.image_format;
    swapchainExtent = vkbSwapchain.extent;
    swapchainImages = vkbSwapchain.get_images().value();
    swapchainViews = vkbSwapchain.get_image_views().value();
    swapchainLayouts.assign(swapchainImages.size(), VK_IMAGE_LAYOUT_UNDEFINED);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished.resize(swapchainImages.size());
    for (VkSemaphore& semaphore : renderFinished) vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);

    if (!CreateImage(static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height), 1, 1, depthFormat,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, false, windowDepth))
        return false;

    swapchainDirty = false;
    return true;
}

void Vulkan::DestroySwapchain() {
    for (const VkImageView view : swapchainViews) vkDestroyImageView(device, view, nullptr);
    for (const VkSemaphore semaphore : renderFinished) vkDestroySemaphore(device, semaphore, nullptr);
    swapchainViews.clear();
    swapchainImages.clear();
    swapchainLayouts.clear();
    renderFinished.clear();
    DestroyImage(windowDepth);

    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool Vulkan::InitFrames() {
    for (Frame& frame : frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = queueFamily;
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool) != VK_SUCCESS) return false;

        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = frame.pool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &allocateInfo, &frame.commands) != VK_SUCCESS) return false;

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(device, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS) return false;
    }

    VkCommandPoolCreateInfo uploadInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    uploadInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    uploadInfo.queueFamilyIndex = queueFamily;
    return vkCreateCommandPool(device, &uploadInfo, nullptr, &uploadPool) == VK_SUCCESS;
}

bool Vulkan::InitBindless() {
    const VkDescriptorSetLayoutBinding bindings[3] = {
        {BINDING_TEXTURES_2D, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURES_2D, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {BINDING_TEXTURE_ARRAYS, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURE_ARRAYS, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {BINDING_SAMPLERS, VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };

    const VkDescriptorBindingFlags bindingFlags[3] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagsInfo.bindingCount = 3;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &bindlessLayout) != VK_SUCCESS) return false;

    const VkDescriptorPoolSize sizes[2] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURES_2D + MAX_TEXTURE_ARRAYS},
        {VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &bindlessPool) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = bindlessPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &bindlessLayout;
    if (vkAllocateDescriptorSets(device, &allocateInfo, &bindlessSet) != VK_SUCCESS) return false;

    // Every draw pushes the address of its uniform block.
    const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkDeviceAddress)};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &bindlessLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) return false;

    // ImGui texture ids: one sampled image each, as imgui_impl_vulkan's own.
    const VkDescriptorSetLayoutBinding imguiBinding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo imguiLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    imguiLayoutInfo.bindingCount = 1;
    imguiLayoutInfo.pBindings = &imguiBinding;
    if (vkCreateDescriptorSetLayout(device, &imguiLayoutInfo, nullptr, &imguiTextureLayout) != VK_SUCCESS) return false;

    const VkDescriptorPoolSize imguiSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_TEXTURE_SETS};
    VkDescriptorPoolCreateInfo imguiPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    imguiPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    imguiPoolInfo.maxSets = IMGUI_TEXTURE_SETS;
    imguiPoolInfo.poolSizeCount = 1;
    imguiPoolInfo.pPoolSizes = &imguiSize;
    if (vkCreateDescriptorPool(device, &imguiPoolInfo, nullptr, &imguiTexturePool) != VK_SUCCESS) return false;

    // Slot 0 of each kind reads black, as GL's texture 0 does.
    const uint8_t black[4] = {0, 0, 0, 255};
    if (!UploadImage(black, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1, placeholder2D)) return false;
    placeholder2D.slot = AllocateTextureSlot(false);
    WriteTextureSlot(placeholder2D, false);

    if (!CreateImage(1, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, true, placeholderArray))
        return false;
    {
        Buffer staging;
        if (!CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, staging)) return false;
        std::memcpy(staging.mapped, black, 4);
        VkCommandBuffer commands = BeginOneShot();
        Transition(commands, placeholderArray, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(commands, staging.buffer, placeholderArray.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Transition(commands, placeholderArray, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        EndOneShot(commands);
        DestroyBuffer(staging);
    }
    placeholderArray.slot = AllocateTextureSlot(true);
    WriteTextureSlot(placeholderArray, true);

    return CreateBuffer(ZERO_BUFFER_SIZE,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        true, zeroBuffer) &&
           (std::memset(zeroBuffer.mapped, 0, ZERO_BUFFER_SIZE), true);
}

bool Vulkan::InitSamplers() {
    const auto repeat = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    const auto clamp = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    samplers[SAMPLER_STANDALONE] = MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE,
                                               repeat, repeat, -0.25f);
    samplers[SAMPLER_STANDALONE_CLAMP] = MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                                     VK_LOD_CLAMP_NONE, clamp, clamp, -0.25f);
    samplers[SAMPLER_STANDALONE_REPEAT_CLAMP] = MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                                            VK_LOD_CLAMP_NONE, repeat, clamp, -0.25f);
    samplers[SAMPLER_LINEAR_REPEAT] = MakeSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, repeat, repeat);
    samplers[SAMPLER_LINEAR_CLAMP] = MakeSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, clamp, clamp);
    samplers[SAMPLER_NEAREST_CLAMP] = MakeSampler(device, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, clamp, clamp);

    for (int setting = 0; setting < 8; ++setting) {
        samplers[SAMPLER_ATLAS_FIRST + setting] = MakeSettingSampler(device, static_cast<RendererTextureSettings>(setting), clamp);
        samplers[SAMPLER_MODEL_FIRST + setting] = MakeSettingSampler(device, static_cast<RendererTextureSettings>(setting), repeat);
    }

    std::vector<VkDescriptorImageInfo> infos;
    for (const VkSampler sampler : samplers) {
        if (sampler == VK_NULL_HANDLE) return false;
        infos.push_back({sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
    }

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = bindlessSet;
    write.dstBinding = BINDING_SAMPLERS;
    write.descriptorCount = static_cast<uint32_t>(infos.size());
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = infos.data();
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return true;
}

void Vulkan::DestroyDevice() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        for (Frame& frame : frames) {
            for (auto& deletion : frame.deletions) deletion();
            frame.deletions.clear();
        }
        for (auto& deletion : pendingDeletions) deletion();
        pendingDeletions.clear();

        for (Image& image : retiredImages) DestroyImage(image);
        retiredImages.clear();

        for (Frame& frame : frames) {
            for (Buffer& chunk : frame.arena.chunks) DestroyBuffer(chunk);
            frame.arena = {};
            if (frame.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.imageAvailable, nullptr);
            if (frame.fence != VK_NULL_HANDLE) vkDestroyFence(device, frame.fence, nullptr);
            if (frame.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, frame.pool, nullptr);
            frame = {};
        }
        if (uploadPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, uploadPool, nullptr);
        uploadPool = VK_NULL_HANDLE;

        DestroyBuffer(zeroBuffer);
        DestroyImage(placeholder2D);
        DestroyImage(placeholderArray);

        for (VkSampler& sampler : samplers) {
            if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }

        if (imguiTexturePool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, imguiTexturePool, nullptr);
        if (imguiTextureLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, imguiTextureLayout, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (bindlessPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, bindlessPool, nullptr);
        if (bindlessLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, bindlessLayout, nullptr);
        imguiTexturePool = VK_NULL_HANDLE;
        imguiTextureLayout = VK_NULL_HANDLE;
        pipelineLayout = VK_NULL_HANDLE;
        bindlessPool = VK_NULL_HANDLE;
        bindlessLayout = VK_NULL_HANDLE;
        bindlessSet = VK_NULL_HANDLE;

        DestroySwapchain();

        if (pipelineCache != VK_NULL_HANDLE) vkDestroyPipelineCache(device, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;

        if (allocator != nullptr) vmaDestroyAllocator(allocator);
        allocator = nullptr;

        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debugMessenger != VK_NULL_HANDLE) vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    surface = VK_NULL_HANDLE;
    debugMessenger = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    freeTextureSlots.clear();
    freeArraySlots.clear();
    nextTextureSlot = 0;
    nextArraySlot = 0;
    frameOpen = false;
    imageAcquired = false;
}

// ============================================================================
// Memory
// ============================================================================

bool Vulkan::CreateBuffer(const VkDeviceSize size, const VkBufferUsageFlags usage, const bool hostVisible, Buffer& out) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(size, 16);
    info.usage = usage;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo result{};
    if (vmaCreateBuffer(allocator, &info, &allocationInfo, &out.buffer, &out.allocation, &result) != VK_SUCCESS) {
        spdlog::error("Vulkan: allocating a buffer of {} bytes failed", size);
        out = {};
        return false;
    }

    out.size = info.size;
    out.mapped = result.pMappedData;

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = out.buffer;
        out.address = vkGetBufferDeviceAddress(device, &addressInfo);
    }
    return true;
}

void Vulkan::DestroyBuffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool Vulkan::CreateImage(const int width, const int height, const int layers, const int mips, const VkFormat format,
                         const VkImageUsageFlags usage, const VkImageAspectFlags aspect, const bool arrayView, Image& out) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {static_cast<uint32_t>(std::max(width, 1)), static_cast<uint32_t>(std::max(height, 1)), 1};
    info.mipLevels = static_cast<uint32_t>(std::max(mips, 1));
    info.arrayLayers = static_cast<uint32_t>(std::max(layers, 1));
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(allocator, &info, &allocationInfo, &out.image, &out.allocation, nullptr) != VK_SUCCESS) {
        spdlog::error("Vulkan: allocating a {}x{} image failed", width, height);
        out = {};
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = out.image;
    viewInfo.viewType = arrayView ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    // A view of a depth/stencil image may only name one aspect when sampled; these are attachments only.
    viewInfo.subresourceRange = {aspect, 0, info.mipLevels, 0, info.arrayLayers};

    if (vkCreateImageView(device, &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
        vmaDestroyImage(allocator, out.image, out.allocation);
        out = {};
        return false;
    }

    out.format = format;
    out.aspect = aspect;
    out.width = width;
    out.height = height;
    out.layers = static_cast<int>(info.arrayLayers);
    out.mips = static_cast<int>(info.mipLevels);
    out.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    out.arrayView = arrayView;
    return true;
}

void Vulkan::DestroyImage(Image& image) {
    if (image.imguiSet != VK_NULL_HANDLE && imguiTexturePool != VK_NULL_HANDLE)
        vkFreeDescriptorSets(device, imguiTexturePool, 1, &image.imguiSet);
    if (image.view != VK_NULL_HANDLE) vkDestroyImageView(device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE) vmaDestroyImage(allocator, image.image, image.allocation);
    image = {};
}

void Vulkan::DeferDelete(std::function<void()> deletion) {
    if (frameOpen) frames[frameIndex].deletions.push_back(std::move(deletion));
    else pendingDeletions.push_back(std::move(deletion));
}

void Vulkan::DeferDestroyImage(Image& image) {
    if (image.image == VK_NULL_HANDLE) {
        image = {};
        return;
    }

    if (image.slot != UINT32_MAX) FreeTextureSlot(image.slot, image.layers > 1 || image.arrayView);
    image.slot = UINT32_MAX;

    Image doomed = image;
    image = {};
    DeferDelete([this, doomed]() mutable { DestroyImage(doomed); });
}

void Vulkan::DeferDestroyBuffer(Buffer& buffer) {
    if (buffer.buffer == VK_NULL_HANDLE) {
        buffer = {};
        return;
    }

    Buffer doomed = buffer;
    buffer = {};
    DeferDelete([this, doomed]() mutable { DestroyBuffer(doomed); });
}

// ============================================================================
// Descriptors
// ============================================================================

uint32_t Vulkan::AllocateTextureSlot(const bool array) {
    std::vector<uint32_t>& freeSlots = array ? freeArraySlots : freeTextureSlots;
    if (!freeSlots.empty()) {
        const uint32_t slot = freeSlots.back();
        freeSlots.pop_back();
        return slot;
    }

    uint32_t& next = array ? nextArraySlot : nextTextureSlot;
    const uint32_t limit = array ? MAX_TEXTURE_ARRAYS : MAX_TEXTURES_2D;
    if (next >= limit) {
        spdlog::error("Vulkan: out of bindless {} slots ({})", array ? "texture array" : "texture", limit);
        return 0; // the placeholder
    }
    return next++;
}

void Vulkan::FreeTextureSlot(const uint32_t slot, const bool array) {
    if (slot == 0 || slot == UINT32_MAX) return;
    // Draws already recorded may still read it.
    DeferDelete([this, slot, array] { (array ? freeArraySlots : freeTextureSlots).push_back(slot); });
}

void Vulkan::WriteTextureSlot(const Image& image, const bool array) {
    if (image.slot == UINT32_MAX) return;

    const VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = bindlessSet;
    write.dstBinding = array ? BINDING_TEXTURE_ARRAYS : BINDING_TEXTURES_2D;
    write.dstArrayElement = image.slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

VkDescriptorSet Vulkan::MakeImGuiSet(const VkImageView view) {
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = imguiTexturePool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &imguiTextureLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &allocateInfo, &set) != VK_SUCCESS) {
        spdlog::error("Vulkan: out of ImGui texture descriptor sets");
        return VK_NULL_HANDLE;
    }

    const VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return set;
}

// ============================================================================
// Commands outside frames, layouts, uploads
// ============================================================================

VkCommandBuffer Vulkan::BeginOneShot() {
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = uploadPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commands = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocateInfo, &commands);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &beginInfo);
    return commands;
}

void Vulkan::EndOneShot(VkCommandBuffer commands) {
    vkEndCommandBuffer(commands);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device, &fenceInfo, nullptr, &fence);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    vkQueueSubmit(queue, 1, &submit, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, uploadPool, 1, &commands);
}

void Vulkan::Transition(VkCommandBuffer commands, Image& image, const VkImageLayout to) {
    if (image.layout == to) return;
    Transition(commands, image.image, image.aspect, image.layout, to);
    image.layout = to;
}

void Vulkan::Transition(VkCommandBuffer commands, const VkImage image, const VkImageAspectFlags aspect, const VkImageLayout from,
                        const VkImageLayout to, const uint32_t mips, const uint32_t layers) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : StageFor(from);
    barrier.srcAccessMask = AccessFor(from);
    barrier.dstStageMask = to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT : StageFor(to);
    barrier.dstAccessMask = AccessFor(to);
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, mips, 0, layers};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void Vulkan::GenerateMips(VkCommandBuffer commands, Image& image) {
    // Mip 0 is in TRANSFER_DST; each level is filled from the one above.
    int width = image.width;
    int height = image.height;
    const uint32_t layers = static_cast<uint32_t>(image.layers);

    for (int level = 1; level < image.mips; ++level) {
        // Level - 1 from TRANSFER_DST to TRANSFER_SRC.
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level - 1), 1, 0, layers};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);

        const int nextWidth = std::max(width / 2, 1);
        const int nextHeight = std::max(height / 2, 1);

        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level - 1), 0, layers};
        blit.srcOffsets[1] = {width, height, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(level), 0, layers};
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        vkCmdBlitImage(commands, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);

        width = nextWidth;
        height = nextHeight;
    }

    // Every level but the last is TRANSFER_SRC now; the last TRANSFER_DST.
    VkImageMemoryBarrier2 barriers[2]{};
    for (VkImageMemoryBarrier2& barrier : barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.image;
    }
    uint32_t barrierCount = 0;
    if (image.mips > 1) {
        barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[barrierCount].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(image.mips - 1), 0, layers};
        ++barrierCount;
    }
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[barrierCount].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(image.mips - 1), 1, 0, layers};
    ++barrierCount;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = barrierCount;
    dependency.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(commands, &dependency);

    image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool Vulkan::UploadImage(const void* pixels, const int width, const int height, const VkFormat format, const int mips, Image& out) {
    const VkDeviceSize pixelBytes = format == VK_FORMAT_R8_UNORM ? 1 : 4;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * pixelBytes;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mips > 1) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!CreateImage(width, height, 1, mips, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, false, out)) return false;

    Buffer staging;
    if (!CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, staging)) {
        DestroyImage(out);
        return false;
    }
    if (pixels != nullptr) std::memcpy(staging.mapped, pixels, bytes);
    else std::memset(staging.mapped, 0, bytes);

    VkCommandBuffer commands = BeginOneShot();
    Transition(commands, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    out.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(commands, staging.buffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if (out.mips > 1) GenerateMips(commands, out);
    else Transition(commands, out, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    EndOneShot(commands);
    DestroyBuffer(staging);
    return true;
}

// ============================================================================
// Frames
// ============================================================================

VkCommandBuffer Vulkan::Commands() {
    Frame& frame = frames[frameIndex];
    if (frameOpen) return frame.commands;

    vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

    for (auto& deletion : frame.deletions) deletion();
    frame.deletions.clear();

    // Queued between frames: after this frame's fence, everything before it is done too.
    frame.deletions = std::move(pendingDeletions);
    pendingDeletions.clear();

    frame.arena.chunk = 0;
    frame.arena.used = 0;

    vkResetCommandPool(device, frame.pool, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commands, &beginInfo);

    frameOpen = true;
    rendering = false;
    boundPipeline = VK_NULL_HANDLE;
    descriptorsBound = false;
    return frame.commands;
}

ArenaAllocation Vulkan::ArenaAllocate(const VkDeviceSize size, const VkDeviceSize alignment) {
    Commands();
    Arena& arena = frames[frameIndex].arena;

    const auto fits = [&](const Buffer& chunk, VkDeviceSize& offset) {
        offset = (arena.used + alignment - 1) / alignment * alignment;
        return offset + size <= chunk.size;
    };

    VkDeviceSize offset = 0;
    while (arena.chunk < arena.chunks.size() && !fits(arena.chunks[arena.chunk], offset)) {
        ++arena.chunk;
        arena.used = 0;
    }

    if (arena.chunk >= arena.chunks.size()) {
        Buffer chunk;
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (!CreateBuffer(std::max(ARENA_CHUNK_SIZE, size), usage, true, chunk)) {
            // Nothing sensible to draw with; the zero buffer keeps the GPU reading valid memory.
            return {zeroBuffer.buffer, 0, zeroBuffer.address, zeroBuffer.mapped};
        }
        arena.chunks.push_back(chunk);
        arena.chunk = arena.chunks.size() - 1;
        arena.used = 0;
        offset = 0;
    }

    const Buffer& chunk = arena.chunks[arena.chunk];
    arena.used = offset + size;
    return {chunk.buffer, offset, chunk.address + offset, static_cast<uint8_t*>(chunk.mapped) + offset};
}

VkDeviceAddress Vulkan::UploadFrame(const void* data, const size_t bytes) {
    if (bytes == 0 || data == nullptr) return zeroBuffer.address;
    const ArenaAllocation allocation = ArenaAllocate(bytes, 16);
    std::memcpy(allocation.pointer, data, bytes);
    return allocation.address;
}

bool Vulkan::AcquireWindowImage() {
    if (imageAcquired) return true;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;

    if (swapchain == VK_NULL_HANDLE || swapchainDirty || swapchainExtent.width != static_cast<uint32_t>(width) ||
        swapchainExtent.height != static_cast<uint32_t>(height)) {
        if (!CreateSwapchain()) return false;
    }

    Commands();
    Frame& frame = frames[frameIndex];

    for (int attempt = 0; attempt < 2; ++attempt) {
        const VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &swapchainImageIndex);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            if (result == VK_SUBOPTIMAL_KHR) swapchainDirty = true;
            imageAcquired = true;
            return true;
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR || !CreateSwapchain()) break;
    }

    return false;
}

Vulkan::Target Vulkan::CurrentTarget() {
    if (activePanelTarget != nullptr)
        return {&activePanelTarget->color, &activePanelTarget->depth, activePanelTarget->width, activePanelTarget->height};

    if (!AcquireWindowImage()) return {};
    return {nullptr, &windowDepth, static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height)};
}

void Vulkan::BeginRendering(const bool clearColor, const Vector4 color, const bool clearDepthStencil) {
    VkCommandBuffer commands = Commands();
    if (rendering) EndRendering();

    const Target target = CurrentTarget();
    if (target.width <= 0 || target.height <= 0 || target.depth == nullptr) return;

    VkImageView colorView = VK_NULL_HANDLE;
    if (target.color != nullptr) {
        Transition(commands, *target.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        colorView = target.color->view;
    }
    else {
        VkImageLayout& layout = swapchainLayouts[swapchainImageIndex];
        if (layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            // Presented or new: its contents are not kept.
            Transition(commands, swapchainImages[swapchainImageIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        colorView = swapchainViews[swapchainImageIndex];
    }
    Transition(commands, *target.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{color.x, color.y, color.z, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = target.depth->view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = clearDepthStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {0.0f, 0};

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colorAttachment;
    info.pDepthAttachment = &depthAttachment;
    info.pStencilAttachment = &depthAttachment;

    vkCmdBeginRendering(commands, &info);
    rendering = true;
    boundPipeline = VK_NULL_HANDLE;
    descriptorsBound = false;
}

void Vulkan::EndRendering() {
    if (!rendering) return;
    vkCmdEndRendering(frames[frameIndex].commands);
    rendering = false;
}

void Vulkan::ClearColor(const Vector4 color) {
    Commands();
    if (!rendering) BeginRendering(false, {}, false);
    if (!rendering) return;

    const Target target = CurrentTarget();

    VkClearAttachment clear{};
    clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clear.colorAttachment = 0;
    clear.clearValue.color = {{color.x, color.y, color.z, 1.0f}};

    VkClearRect rect{};
    rect.rect = {{0, 0}, {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)}};
    rect.layerCount = 1;
    vkCmdClearAttachments(frames[frameIndex].commands, 1, &clear, 1, &rect);
}

void Vulkan::BeginFrame() {
    state.depthTest = true;
    state.depthCompare = VK_COMPARE_OP_GREATER;
    state.depthWrite = true;
    state.cull = false;
    state.stencilWriteMask = 0xFF;

    BeginRendering(true, {1.0f, 1.0f, 1.0f, 1.0f}, true);
}

void Vulkan::EndFrame() {
    if (!frameOpen) return;

    Frame& frame = frames[frameIndex];
    VkCommandBuffer commands = frame.commands;
    EndRendering();

    const bool present = imageAcquired;
    if (present) {
        VkImageLayout& layout = swapchainLayouts[swapchainImageIndex];
        Transition(commands, swapchainImages[swapchainImageIndex], VK_IMAGE_ASPECT_COLOR_BIT, layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    vkEndCommandBuffer(commands);
    vkResetFences(device, 1, &frame.fence);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    if (present) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished[swapchainImageIndex];
    }

    if (vkQueueSubmit(queue, 1, &submit, frame.fence) != VK_SUCCESS) spdlog::error("Vulkan: vkQueueSubmit failed");

    if (present) {
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished[swapchainImageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;

        const VkResult result = vkQueuePresentKHR(queue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) swapchainDirty = true;
    }

    frameOpen = false;
    imageAcquired = false;
    frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;
}

// ============================================================================
// Window state
// ============================================================================

bool Vulkan::UpdateScreenSize() {
    if (activePanelTarget != nullptr) {
        screenWidth = activePanelTarget->width;
        screenHeight = activePanelTarget->height;
        return true;
    }

    if (!SDL_GetWindowSizeInPixels(window, &screenWidth, &screenHeight)) {
        spdlog::error("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

void Vulkan::SetViewport(const int width, const int height) {
    state.viewport[0] = 0;
    state.viewport[1] = 0;
    state.viewport[2] = width;
    state.viewport[3] = height;
}

void Vulkan::BeginPanelTarget(const PanelTarget panel, int width, int height) {
    const int index = static_cast<int>(panel);
    if (index < 0 || index >= static_cast<int>(PanelTarget::Count)) return;

    if (width <= 0 || height <= 0) {
        if (!SDL_GetWindowSizeInPixels(window, &width, &height) || width <= 0 || height <= 0) return;
    }

    EndRendering();
    PanelTargetImages& target = panelTargets[index];

    if (target.color.image == VK_NULL_HANDLE || width != target.width || height != target.height) {
        DeferDestroyImage(target.color);
        DeferDestroyImage(target.depth);
        target = {};

        if (!CreateImage(width, height, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, false, target.color) ||
            !CreateImage(width, height, 1, 1, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, false, target.depth)) {
            spdlog::error("An editor panel's target could not be made; drawing to the window instead");
            DestroyImage(target.color);
            DestroyImage(target.depth);
            target = {};
            activePanelTarget = nullptr;
            return;
        }

        target.color.imguiSet = MakeImGuiSet(target.color.view);
        target.width = width;
        target.height = height;
    }

    activePanelTarget = &target;
    screenWidth = target.width;
    screenHeight = target.height;
    SetViewport(screenWidth, screenHeight);
}

void Vulkan::EndPanelTarget() {
    if (activePanelTarget != nullptr) {
        VkCommandBuffer commands = Commands();
        EndRendering();
        // ImGui shows it next.
        Transition(commands, activePanelTarget->color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    activePanelTarget = nullptr;
    state.scissorTest = false;
    state.stencilTest = false;

    if (!UpdateScreenSize()) return;
    SetViewport(screenWidth, screenHeight);
}

IRenderer::PanelImage Vulkan::GetPanelImage(const PanelTarget panel) const {
    const int index = static_cast<int>(panel);
    if (index < 0 || index >= static_cast<int>(PanelTarget::Count)) return {};

    const VulkanInternal::PanelTargetImages& target = panelTargets[index];
    if (target.color.imguiSet == VK_NULL_HANDLE || target.color.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return {};

    // Vulkan keeps the top row first: no flip.
    return {reinterpret_cast<ImTextureID>(target.color.imguiSet), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f)};
}

void Vulkan::DestroyPanelTargets() {
    for (PanelTargetImages& target : panelTargets) {
        DestroyImage(target.color);
        DestroyImage(target.depth);
        target = {};
    }
    activePanelTarget = nullptr;
}

void Vulkan::ClearWindow(const Vector4 color, const int width, const int height) {
    if (activePanelTarget != nullptr) EndPanelTarget();
    SetViewport(width, height);
    BeginRendering(true, color, false);
}

void Vulkan::SetVSync(const int mode) {
    if (mode == vsyncMode) return;
    vsyncMode = mode;
    swapchainDirty = true;
}

void Vulkan::SetDrawMode(const DrawMode mode) {
    if (mode == DrawMode::WIREFRAME) wireframe = true;
    else if (mode == DrawMode::SOLID) wireframe = false;
}

void Vulkan::OnResize(const int width, const int height) {
    if (width <= 0 || height <= 0) return;

    screenWidth = width;
    screenHeight = height;
    SetViewport(width, height);
    swapchainDirty = true;
}
