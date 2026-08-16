#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Components.hpp"

namespace fs = std::filesystem;

namespace {
    using namespace VulkanRendererInternal;

#ifndef NDEBUG
    constexpr bool ENABLE_VALIDATION = true;
#else
    constexpr bool ENABLE_VALIDATION = false;
#endif

    // Same de-duplicated "every filename referenced anywhere in the level"
    // scan the GL atlas builder used, copied unchanged - it's pure Level
    // traversal, nothing GL/Vulkan-specific in it at all.
    std::vector<std::string> CollectReferencedTextureFileNames(const Level& level) {
        std::set<std::string> uniqueNames;

        for (const Wall& wall : level.walls) {
            if (!wall.textureFileName.empty()) uniqueNames.insert(wall.textureFileName);
        }

        for (const Sector& sector : level.sectors) {
            for (const SectorFloor& floor : sector.floors) {
                if (!floor.floor.texture.empty()) uniqueNames.insert(floor.floor.texture);
                if (!floor.ceiling.texture.empty()) uniqueNames.insert(floor.ceiling.texture);
            }
        }

        for (const ComponentSprite& sprite : level.sprites.components) {
            for (const std::string& fileName : sprite.textureFileNames) {
                if (!fileName.empty()) uniqueNames.insert(fileName);
            }
        }

        return {uniqueNames.begin(), uniqueNames.end()};
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
    const vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT /*type*/,
    const vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/
    ) {
        if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
            spdlog::error("[vulkan] {}", callbackData->pMessage);
        } else if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
            spdlog::warn("[vulkan] {}", callbackData->pMessage);
        } else {
            spdlog::info("[vulkan] {}", callbackData->pMessage);
        }

        return vk::False;
    }

    vk::SurfaceFormatKHR PickSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
        // GL never called glEnable(GL_FRAMEBUFFER_SRGB), so its output was
        // written/interpreted as linear UNORM, not sRGB-encoded. Matching
        // that (rather than defaulting to the usually-recommended sRGB
        // swapchain format) keeps color output identical to the GL build.
        for (const vk::SurfaceFormatKHR& format : formats) {
            if (format.format == vk::Format::eB8G8R8A8Unorm &&
                format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return format;
            }
        }

        return formats.front();
    }

    vk::PresentModeKHR PickPresentMode(const std::vector<vk::PresentModeKHR>& modes) {
        // InitializeOpenGL() called SDL_GL_SetSwapInterval(0) - vsync off.
        // eImmediate is the closest Vulkan equivalent; eFifo (always
        // supported) is the fallback since eImmediate support isn't
        // guaranteed.
        for (const vk::PresentModeKHR mode : modes) {
            if (mode == vk::PresentModeKHR::eImmediate) return mode;
        }

        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D PickExtent(const vk::SurfaceCapabilitiesKHR& capabilities, SDL_Window* window) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        vk::Extent2D extent{
            .width = std::clamp(
                static_cast<uint32_t>(width),
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width
            ),
            .height = std::clamp(
                static_cast<uint32_t>(height),
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height
            )
        };

        return extent;
    }
}

// ============================================================================
// Small shared helpers (buffer/image/memory plumbing used across this file
// and the other VulkanXxx.cpp translation units)
// ============================================================================

uint32_t Vulkan::FindMemoryType(const uint32_t typeBits, const vk::MemoryPropertyFlags properties) const {
    const vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    spdlog::critical("FindMemoryType failed: no memory type satisfies the requested properties");
    return 0;
}

vk::raii::CommandBuffer Vulkan::BeginSingleTimeCommands() const {
    const vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::raii::CommandBuffers buffers(device, allocInfo);
    vk::raii::CommandBuffer cmd = std::move(buffers.front());

    cmd.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    return cmd;
}

void Vulkan::EndSingleTimeCommands(vk::raii::CommandBuffer& cmd) const {
    cmd.end();

    const vk::CommandBuffer rawCmd = cmd;

    const vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &rawCmd
    };

    graphicsQueue.submit(submitInfo);
    graphicsQueue.waitIdle(); // one-off upload, simplicity over throughput
}

void Vulkan::TransitionImageLayout(
    const vk::raii::CommandBuffer& cmd,
    const vk::Image image,
    const vk::ImageLayout oldLayout,
    const vk::ImageLayout newLayout,
    const vk::ImageAspectFlags aspectMask,
    const uint32_t mipLevels
) const {
    vk::AccessFlags srcAccess;
    vk::AccessFlags dstAccess;
    vk::PipelineStageFlags srcStage;
    vk::PipelineStageFlags dstStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        srcAccess = {};
        dstAccess = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        srcAccess = vk::AccessFlagBits::eTransferWrite;
        dstAccess = vk::AccessFlagBits::eShaderRead;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
        srcAccess = {};
        dstAccess = vk::AccessFlagBits::eColorAttachmentWrite;
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    } else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::ePresentSrcKHR) {
        srcAccess = vk::AccessFlagBits::eColorAttachmentWrite;
        dstAccess = {};
        srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    } else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthAttachmentOptimal) {
        srcAccess = {};
        dstAccess = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    } else {
        // Uncommon transition for this renderer - safe (if not maximally
        // efficient) fallback that stalls the whole pipe rather than
        // silently getting the access/stage masks wrong.
        srcAccess = vk::AccessFlagBits::eMemoryWrite;
        dstAccess = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        srcStage = vk::PipelineStageFlagBits::eAllCommands;
        dstStage = vk::PipelineStageFlagBits::eAllCommands;
    }

    const vk::ImageMemoryBarrier barrier{
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);
}

Vulkan::GPUImageResources Vulkan::CreateGPUImage(
    const uint32_t width,
    const uint32_t height,
    const uint32_t mipLevels,
    const vk::Format format,
    const vk::ImageUsageFlags usage
) const {
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    GPUImageResources result;
    result.image = device.createImage(imageInfo);

    const vk::MemoryRequirements memReq = result.image.getMemoryRequirements();

    result.memory = device.allocateMemory(vk::MemoryAllocateInfo{
        .allocationSize = memReq.size,
        .memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
    });

    result.image.bindMemory(result.memory, 0);

    return result;
}

void Vulkan::UploadPixelsToImage(
    const vk::Image image,
    const void* pixels,
    const uint32_t width,
    const uint32_t height,
    const uint32_t bytesPerPixel,
    const uint32_t mipLevels
) const {
    const vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * bytesPerPixel;

    const vk::raii::Buffer staging = device.createBuffer(vk::BufferCreateInfo{
        .size = size,
        .usage = vk::BufferUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive
    });

    const vk::MemoryRequirements memReq = staging.getMemoryRequirements();

    const vk::raii::DeviceMemory stagingMemory = device.allocateMemory(vk::MemoryAllocateInfo{
        .allocationSize = memReq.size,
        .memoryTypeIndex = FindMemoryType(
            memReq.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        )
    });

    staging.bindMemory(stagingMemory, 0);

    void* mapped = stagingMemory.mapMemory(0, size);
    std::memcpy(mapped, pixels, static_cast<size_t>(size));
    stagingMemory.unmapMemory();

    vk::raii::CommandBuffer cmd = BeginSingleTimeCommands();

    TransitionImageLayout(
        cmd, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        vk::ImageAspectFlagBits::eColor, mipLevels
    );

    const vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };

    cmd.copyBufferToImage(staging, image, vk::ImageLayout::eTransferDstOptimal, region);

    if (mipLevels <= 1) {
        TransitionImageLayout(
            cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, mipLevels
        );
    }
    // else: GenerateMipmaps (called separately, after this returns) does
    // the transfer-dst -> shader-read-only transition itself, one mip
    // level at a time, as it blits.

    EndSingleTimeCommands(cmd);
}

void Vulkan::GenerateMipmaps(const vk::Image image, const int32_t width, const int32_t height, const uint32_t mipLevels) const {
    vk::raii::CommandBuffer cmd = BeginSingleTimeCommands();

    int32_t mipWidth = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; ++i) {
        vk::ImageMemoryBarrier toSrcBarrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, i - 1, 1, 0, 1}
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, toSrcBarrier
        );

        const int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        const int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        const vk::ImageBlit blit{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, i - 1, 0, 1},
            .srcOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth, mipHeight, 1}},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, i, 0, 1},
            .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{nextWidth, nextHeight, 1}}
        };

        cmd.blitImage(
            image, vk::ImageLayout::eTransferSrcOptimal,
            image, vk::ImageLayout::eTransferDstOptimal,
            blit, vk::Filter::eLinear
        );

        vk::ImageMemoryBarrier toReadBarrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferRead,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, i - 1, 1, 0, 1}
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, toReadBarrier
        );

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    const vk::ImageMemoryBarrier lastMipBarrier{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, mipLevels - 1, 1, 0, 1}
    };

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
        {}, {}, {}, lastMipBarrier
    );

    EndSingleTimeCommands(cmd);
}

Vulkan::DynamicBuffer Vulkan::CreateDynamicBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage) const {
    DynamicBuffer result;
    if (size == 0) return result;

    result.buffer = device.createBuffer(vk::BufferCreateInfo{
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    });

    const vk::MemoryRequirements memReq = result.buffer.getMemoryRequirements();

    result.memory = device.allocateMemory(vk::MemoryAllocateInfo{
        .allocationSize = memReq.size,
        .memoryTypeIndex = FindMemoryType(
            memReq.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        )
    });

    result.buffer.bindMemory(result.memory, 0);
    result.mapped = result.memory.mapMemory(0, size);
    result.capacity = size;

    return result;
}

// Re-creates the buffer if it's grown past its current capacity (mirrors
// glBufferData being safe to call every frame with a different size), then
// memcpys straight into the persistently-mapped, host-coherent range - no
// explicit flush needed. This is the every-frame equivalent of GL's
// glBufferData(SSBO, size, data, GL_DYNAMIC_DRAW) calls in
// BuildGpuSectors/BuildGpuSprites/BuildGpuColliders/UploadGpuWallsFromMap.
void Vulkan::UploadToDynamicBuffer(DynamicBuffer& target, const void* data, const vk::DeviceSize size, const vk::BufferUsageFlags usage) {
    if (size == 0) return;

    if (size > target.capacity) {
        // Growing: old mapped buffer/memory are simply replaced - their
        // vk::raii destructors free the GPU resources once no in-flight
        // command buffer references them (guaranteed here because each
        // DynamicBuffer belongs to one frame-in-flight slot, and this is
        // only ever called after that slot's fence has been waited on).
        target = CreateDynamicBuffer(size + size / 2, usage); // a little slack to reduce reallocation churn
    }

    std::memcpy(target.mapped, data, static_cast<size_t>(size));
}

vk::raii::ShaderModule Vulkan::LoadShaderModule(const std::string& spirvPath) const {
    std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        spdlog::critical("LoadShaderModule: could not open '{}'. Have you compiled the GLSL to SPIR-V and placed it there?", spirvPath);
        return device.createShaderModule(vk::ShaderModuleCreateInfo{});
    }

    const std::streamsize fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint32_t> code((static_cast<size_t>(fileSize) + 3) / 4);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);

    return device.createShaderModule(vk::ShaderModuleCreateInfo{
        .codeSize = static_cast<size_t>(fileSize),
        .pCode = code.data()
    });
}

// ============================================================================
// InitVulkanInstance - SDL window + VkInstance + VkSurfaceKHR.
// Combines what used to be InitSDL()'s non-GL-specific half with new
// instance/surface setup, since surface creation needs both the window and
// the instance to already exist.
// ============================================================================
bool Vulkan::InitVulkanInstance(const std::string& windowName) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        spdlog::critical("SDL_Init failed while initializing video subsystem: {}", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        windowName.c_str(),
        screenWidth,
        screenHeight,
        VulkanRendererInternal::WINDOW_FLAGS
    );

    if (window == nullptr) {
        spdlog::critical("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    const fs::path iconPath = ProjectManager::FindAssetPath(fs::path("LauncherAssets") / "Fox.png");
    SDL_Surface* windowIcon = IMG_Load(iconPath.string().c_str());

    if (windowIcon == nullptr) {
        spdlog::warn(
            "Renderer window icon failed to load. This does not break the renderer. Path: {} Error: {}",
            iconPath.string(), SDL_GetError()
        );
    } else {
        if (!SDL_SetWindowIcon(window, windowIcon)) {
            spdlog::warn("Failed to set renderer window icon. This does not break the renderer. Error: {}", SDL_GetError());
        }
        SDL_DestroySurface(windowIcon);
    }

    uint32_t sdlExtensionCount = 0;
    char const* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

    if (sdlExtensions == nullptr) {
        spdlog::critical("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

    std::vector<const char*> layers;

    if (ENABLE_VALIDATION) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);

        const std::vector<vk::LayerProperties> availableLayers = context.enumerateInstanceLayerProperties();

        const bool hasValidation = std::ranges::any_of(availableLayers, [](const vk::LayerProperties& layer) {
            return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });

        if (hasValidation) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            spdlog::warn("VK_LAYER_KHRONOS_validation not found - continuing without validation layers");
        }
    }

    const vk::ApplicationInfo appInfo{
        .pApplicationName = windowName.c_str(),
        .applicationVersion = vk::makeApiVersion(0, 1, 0, 0),
        .pEngineName = "Tilky Engine",
        .engineVersion = vk::makeApiVersion(0, 1, 0, 0),
        .apiVersion = vk::ApiVersion13
    };

    const vk::InstanceCreateInfo instanceInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    try {
        instance = context.createInstance(instanceInfo);
    } catch (const vk::SystemError& error) {
        spdlog::critical("vkCreateInstance failed: {}", error.what());
        return false;
    }

    if (ENABLE_VALIDATION) {
        const vk::DebugUtilsMessengerCreateInfoEXT debugInfo{
            .messageSeverity =
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType =
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            .pfnUserCallback = &DebugCallback
        };

        debugMessenger = instance.createDebugUtilsMessengerEXT(debugInfo);
    }

    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;

    if (!SDL_Vulkan_CreateSurface(window, *instance, nullptr, &rawSurface)) {
        spdlog::critical("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    surface = vk::raii::SurfaceKHR(instance, rawSurface);

    spdlog::info("Vulkan instance and surface created successfully");

    return true;
}

// ============================================================================
// InitDevice - physical device selection + logical device + queue.
// Assumes a single queue family serving both graphics and present, which
// covers effectively every desktop GPU/driver. Split queue families would
// need graphicsQueueFamily/presentQueueFamily tracked separately plus
// VK_SHARING_MODE_CONCURRENT (or ownership transfers) on the swapchain
// images - a reasonable follow-up if you ever target hardware where that
// assumption doesn't hold.
// ============================================================================
bool Vulkan::InitDevice() {
    const std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();

    if (physicalDevices.empty()) {
        spdlog::critical("No Vulkan-capable physical devices found");
        return false;
    }

    std::optional<vk::raii::PhysicalDevice> chosen;
    uint32_t chosenQueueFamily = 0;

    for (const vk::raii::PhysicalDevice& candidate : physicalDevices) {
        const vk::PhysicalDeviceProperties props = candidate.getProperties();

        if (props.apiVersion < vk::ApiVersion13) continue;

        const std::vector<vk::ExtensionProperties> extensions = candidate.enumerateDeviceExtensionProperties();
        const bool hasSwapchain = std::ranges::any_of(extensions, [](const vk::ExtensionProperties& ext) {
            return std::strcmp(ext.extensionName, vk::KHRSwapchainExtensionName) == 0;
        });

        if (!hasSwapchain) continue;

        const std::vector<vk::QueueFamilyProperties> queueFamilies = candidate.getQueueFamilyProperties();

        std::optional<uint32_t> suitableFamily;

        for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
            const bool hasGraphics = static_cast<bool>(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics);
            const bool hasPresent = candidate.getSurfaceSupportKHR(i, surface) != 0;

            if (hasGraphics && hasPresent) {
                suitableFamily = i;
                break;
            }
        }

        if (!suitableFamily.has_value()) continue;

        chosenQueueFamily = *suitableFamily;

        // Prefer a discrete GPU, but a suitable integrated/virtual one
        // still wins over finding nothing.
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu || !chosen.has_value()) {
            chosen = candidate;
            if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) break;
        }
    }

    if (!chosen.has_value()) {
        spdlog::critical("No physical device supports Vulkan 1.3 + swapchain + a combined graphics/present queue");
        return false;
    }

    physicalDevice = std::move(*chosen);
    graphicsQueueFamily = chosenQueueFamily;

    spdlog::info("Selected physical device: {}", physicalDevice.getProperties().deviceName.data());

    constexpr float queuePriority = 1.0f;

    const vk::DeviceQueueCreateInfo queueInfo{
        .queueFamilyIndex = graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    vk::PhysicalDeviceVulkan13Features features13{
        .dynamicRendering = vk::True
    };

    const vk::PhysicalDeviceFeatures2 features2{
        .pNext = &features13
    };

    const std::array deviceExtensions{vk::KHRSwapchainExtensionName};

    const vk::DeviceCreateInfo deviceInfo{
        .pNext = &features2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data()
    };

    device = physicalDevice.createDevice(deviceInfo);
    graphicsQueue = device.getQueue(graphicsQueueFamily, 0);

    spdlog::info("Vulkan logical device created successfully");

    return true;
}

// ============================================================================
// InitSwapchain
// ============================================================================
bool Vulkan::InitSwapchain() {
    const vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    const std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(surface);
    const std::vector<vk::PresentModeKHR> presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

    if (formats.empty() || presentModes.empty()) {
        spdlog::critical("Surface has no formats/present modes - cannot create swapchain");
        return false;
    }

    const vk::SurfaceFormatKHR surfaceFormat = PickSurfaceFormat(formats);
    presentMode = PickPresentMode(presentModes);
    swapchainExtent = PickExtent(capabilities, window);

    if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
        // Minimized window - nothing to do until OnResize sees a real size again.
        swapchainDirty = true;
        return true;
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    const vk::SwapchainCreateInfoKHR swapchainInfo{
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = vk::True,
        .oldSwapchain = swapchain != nullptr ? *swapchain : vk::SwapchainKHR{}
    };

    vk::raii::SwapchainKHR newSwapchain = device.createSwapchainKHR(swapchainInfo);

    swapchain = std::move(newSwapchain);
    swapchainFormat = surfaceFormat.format;
    swapchainImages = swapchain.getImages();

    swapchainImageViews.clear();
    swapchainImageViews.reserve(swapchainImages.size());

    for (const vk::Image image : swapchainImages) {
        swapchainImageViews.push_back(device.createImageView(vk::ImageViewCreateInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = swapchainFormat,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        }));
    }

    swapchainDirty = false;

    spdlog::info(
        "Swapchain created: {}x{}, {} images, present mode {}",
        swapchainExtent.width, swapchainExtent.height, swapchainImages.size(),
        vk::to_string(presentMode)
    );

    return true;
}

bool Vulkan::InitDepthResources() {
    if (swapchainExtent.width == 0 || swapchainExtent.height == 0) return true;

    GPUImageResources depth = CreateGPUImage(
        swapchainExtent.width, swapchainExtent.height, 1,
        depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment
    );

    depthImage = std::move(depth.image);
    depthImageMemory = std::move(depth.memory);

    depthImageView = device.createImageView(vk::ImageViewCreateInfo{
        .image = depthImage,
        .viewType = vk::ImageViewType::e2D,
        .format = depthFormat,
        .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}
    });

    // One-time layout transition: the depth image never leaves
    // eDepthAttachmentOptimal after this (nothing ever samples or presents
    // it, and eClear as the load op every frame means we don't need its
    // previous contents preserved either), so unlike the swapchain color
    // image this doesn't need re-transitioning per frame.
    vk::raii::CommandBuffer cmd = BeginSingleTimeCommands();
    TransitionImageLayout(cmd, depthImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageAspectFlagBits::eDepth);
    EndSingleTimeCommands(cmd);

    return true;
}

void Vulkan::DestroySwapchainResources() {
    device.waitIdle();
    depthImageView = nullptr;
    depthImage = nullptr;
    depthImageMemory = nullptr;
    swapchainImageViews.clear();
}

void Vulkan::RecreateSwapchain() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    if (width == 0 || height == 0) {
        swapchainDirty = true;
        return;
    }

    DestroySwapchainResources();
    InitSwapchain();
    InitDepthResources();
}

// ============================================================================
// InitCommands / InitSyncObjects
// ============================================================================
bool Vulkan::InitCommands() {
    commandPool = device.createCommandPool(vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphicsQueueFamily
    });

    vk::raii::CommandBuffers buffers(device, vk::CommandBufferAllocateInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = FRAMES_IN_FLIGHT
    });

    commandBuffers.clear();
    for (auto& buffer : buffers) commandBuffers.push_back(std::move(buffer));

    return true;
}

bool Vulkan::InitSyncObjects() {
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        imageAvailableSemaphores.push_back(device.createSemaphore(vk::SemaphoreCreateInfo{}));
        renderFinishedSemaphores.push_back(device.createSemaphore(vk::SemaphoreCreateInfo{}));
        inFlightFences.push_back(device.createFence(vk::FenceCreateInfo{
            .flags = vk::FenceCreateFlagBits::eSignaled
        }));
    }

    return true;
}

// ============================================================================
// InitDescriptors - see the binding-layout comment on sceneSetLayout in
// Vulkan.hpp for why the numbers are what they are.
// ============================================================================
bool Vulkan::InitDescriptors() {
    const std::array sceneBindings{
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{7, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{8, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{9, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment}
    };

    sceneSetLayout = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
        .bindingCount = static_cast<uint32_t>(sceneBindings.size()),
        .pBindings = sceneBindings.data()
    });

    const vk::DescriptorSetLayoutBinding backgroundBinding{
        0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment
    };

    backgroundSetLayout = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
        .bindingCount = 1,
        .pBindings = &backgroundBinding
    });

    const vk::DescriptorSetLayoutBinding textBinding{
        0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment
    };

    textSetLayout = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
        .bindingCount = 1,
        .pBindings = &textBinding
    });

    const std::array poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 7 * FRAMES_IN_FLIGHT + 4},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, FRAMES_IN_FLIGHT + 4},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, FRAMES_IN_FLIGHT + 2}
    };

    descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = FRAMES_IN_FLIGHT + 4,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    });

    std::vector<vk::DescriptorSetLayout> sceneLayouts(FRAMES_IN_FLIGHT, *sceneSetLayout);

    vk::raii::DescriptorSets allocatedSceneSets(device, vk::DescriptorSetAllocateInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(sceneLayouts.size()),
        .pSetLayouts = sceneLayouts.data()
    });

    sceneSets.clear();
    for (auto& set : allocatedSceneSets) sceneSets.push_back(std::move(set));

    vk::raii::DescriptorSets allocatedBackgroundSet(device, vk::DescriptorSetAllocateInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*backgroundSetLayout
    });
    backgroundSet = std::move(allocatedBackgroundSet.front());

    vk::raii::DescriptorSets allocatedTextSet(device, vk::DescriptorSetAllocateInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*textSetLayout
    });
    textSet = std::move(allocatedTextSet.front());

    // Create the per-frame dynamic buffers now so the descriptor writes
    // below have something valid to point at (empty SSBOs, matching GL's
    // glBufferData(..., 0, nullptr, ...) pattern in CreateMap()).
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        sceneUboBuffers[i] = CreateDynamicBuffer(sizeof(VulkanRendererInternal::SceneUBO), vk::BufferUsageFlagBits::eUniformBuffer);

        constexpr vk::DeviceSize placeholderSize = 256; // grows on first real UploadToDynamicBuffer call
        wallBuffers[i] = CreateDynamicBuffer(placeholderSize, vk::BufferUsageFlagBits::eStorageBuffer);
        flatBuffers[i] = CreateDynamicBuffer(placeholderSize, vk::BufferUsageFlagBits::eStorageBuffer);
        spriteBuffers[i] = CreateDynamicBuffer(placeholderSize, vk::BufferUsageFlagBits::eStorageBuffer);
        sectorBuffers[i] = CreateDynamicBuffer(placeholderSize, vk::BufferUsageFlagBits::eStorageBuffer);
        sectorFloorBuffers[i] = CreateDynamicBuffer(placeholderSize, vk::BufferUsageFlagBits::eStorageBuffer);
        colliderBuffers[i] = CreateDynamicBuffer(placeholderSize, vk::BufferUsageFlagBits::eStorageBuffer);
    }

    return true;
}

// ============================================================================
// Pipelines
// ============================================================================
bool Vulkan::InitScenePipelines() {
    using namespace VulkanRendererInternal;

    scenePipelineLayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*sceneSetLayout
    });

    // The GL renderer used ONE program (projectionShader) for wall/flat/
    // sprite/collider and switched behaviour with the renderMode uniform.
    // Point all four pipelines at the same pair of SPIR-V modules; the
    // shader itself can still branch on a specialization constant or just
    // be four thin wrapper entry points around shared logic - that's a
    // shader-authoring decision on the SPIR-V side, not something the
    // pipeline objects need to know about.
    const fs::path vsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Rendering" / "Rendering.vert.spv");
    const fs::path fsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Rendering" / "Rendering.frag.spv");

    const vk::raii::ShaderModule vertModule = LoadShaderModule(vsPath.string());
    const vk::raii::ShaderModule fragModule = LoadShaderModule(fsPath.string());

    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = vertModule, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = fragModule, .pName = "main"}
    };

    // Every scene draw pulls its vertex data from an SSBO indexed by
    // gl_InstanceIndex/gl_VertexIndex in the shader - matches the GL side's
    // completely empty VAO (glGenVertexArrays + glBindVertexArray, zero
    // glVertexAttribPointer calls).
    constexpr vk::PipelineVertexInputStateCreateInfo emptyVertexInput{};

    constexpr vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1
    };

    constexpr std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    constexpr vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = vk::SampleCountFlagBits::e1
    };

    const vk::PipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    const vk::PipelineColorBlendStateCreateInfo blendState{
        .attachmentCount = 1,
        .pAttachments = &blendAttachment
    };

    const vk::Format colorFormat = swapchainFormat;
    const vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };

    // Builds one pipeline, varying only the state the four render modes
    // actually differ on. See Vulkan.hpp / VulkanUpdate.cpp for the trace
    // through the original Update()'s sticky GL state (glDepthFunc,
    // glCullFace...) that these four combinations come from.
    const auto makePipeline = [&](
        const vk::PrimitiveTopology topology,
        const vk::CullModeFlags cullMode,
        const vk::FrontFace frontFace,
        const vk::CompareOp depthCompare
    ) {
        const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = topology};

        const vk::PipelineRasterizationStateCreateInfo rasterization{
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = cullMode,
            .frontFace = frontFace,
            .lineWidth = 1.0f
        };

        const vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable = vk::True,
            .depthWriteEnable = vk::True,
            .depthCompareOp = depthCompare
        };

        const vk::GraphicsPipelineCreateInfo pipelineInfo{
            .pNext = &renderingInfo,
            .stageCount = static_cast<uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &emptyVertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &blendState,
            .pDynamicState = &dynamicState,
            .layout = scenePipelineLayout
        };

        return device.createGraphicsPipeline(nullptr, pipelineInfo);
    };

    // flat/sector: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glDepthFunc(GL_GREATER)
    flatPipeline = makePipeline(vk::PrimitiveTopology::eTriangleList, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, vk::CompareOp::eGreater);

    // wall: glDisable(GL_CULL_FACE); glDepthFunc(GL_GREATER)
    wallPipeline = makePipeline(vk::PrimitiveTopology::eTriangleList, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, vk::CompareOp::eGreater);

    // sprite: GL_TRIANGLE_STRIP, no cull, glDepthFunc(GL_GEQUAL)
    spritePipeline = makePipeline(vk::PrimitiveTopology::eTriangleStrip, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, vk::CompareOp::eGreaterOrEqual);

    // collider: GL_LINES. Update() never re-sets glDepthFunc between the
    // sprite block and the collider block, so it inherits GL_GEQUAL from
    // sprites rather than the GL_GREATER used by flat/wall - easy to miss
    // when porting, so calling it out here explicitly.
    colliderPipeline = makePipeline(vk::PrimitiveTopology::eLineList, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, vk::CompareOp::eGreaterOrEqual);

    spdlog::info("Scene pipelines (wall/flat/sprite/collider) created");

    return true;
}

bool Vulkan::InitUIPipeline() {
    const vk::PushConstantRange pushRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(VulkanRendererInternal::UIPushConstants)
    };

    uiPipelineLayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*sceneSetLayout, // needs bindings 5 (region SSBO) + 8 (atlas) from the shared set
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    });

    const fs::path vsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "UI" / "UI.vert.spv");
    const fs::path fsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "UI" / "UI.frag.spv");

    const vk::raii::ShaderModule vertModule = LoadShaderModule(vsPath.string());
    const vk::raii::ShaderModule fragModule = LoadShaderModule(fsPath.string());

    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = vertModule, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = fragModule, .pName = "main"}
    };

    // Matches InitUI()'s vertices[] layout exactly: location 0 = vec2 pos
    // (offset 0), location 1 = vec2 uv (offset 2 floats), stride 4 floats.
    const vk::VertexInputBindingDescription binding{0, 4 * sizeof(float), vk::VertexInputRate::eVertex};
    const std::array attributes{
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat, 0},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat, 2 * sizeof(float)}
    };

    const vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data()
    };

    constexpr vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};

    constexpr vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

    constexpr std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    constexpr vk::PipelineRasterizationStateCreateInfo rasterization{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .lineWidth = 1.0f
    };

    constexpr vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};

    // Update()'s "Rendering UI Sprites" block calls glDisable(GL_DEPTH_TEST) first.
    constexpr vk::PipelineDepthStencilStateCreateInfo depthStencil{.depthTestEnable = vk::False, .depthWriteEnable = vk::False};

    const vk::PipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    const vk::PipelineColorBlendStateCreateInfo blendState{.attachmentCount = 1, .pAttachments = &blendAttachment};

    const vk::Format colorFormat = swapchainFormat;
    const vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };

    const vk::GraphicsPipelineCreateInfo pipelineInfo{
        .pNext = &renderingInfo,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamicState,
        .layout = uiPipelineLayout
    };

    uiPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

    // The static unit quad InitUI() uploaded once with GL_STATIC_DRAW.
    // Small and never rewritten, so one shared (non-per-frame) buffer is
    // fine here unlike the SSBOs above.
    constexpr float quadVertices[] = {
         0.5f,  0.5f, 1.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 0.0f
    };
    constexpr uint32_t quadIndices[] = {0, 1, 3, 1, 2, 3};

    // Reuses wallBuffers[0]'s slot conventions isn't appropriate here since
    // this data is static - stash it in dedicated members instead. (Added
    // as uiVertexBuffer/uiIndexBuffer below the pipeline members if you're
    // diffing against Vulkan.hpp - see the note in that file.)
    uiVertexBuffer = CreateDynamicBuffer(sizeof(quadVertices), vk::BufferUsageFlagBits::eVertexBuffer);
    std::memcpy(uiVertexBuffer.mapped, quadVertices, sizeof(quadVertices));

    uiIndexBuffer = CreateDynamicBuffer(sizeof(quadIndices), vk::BufferUsageFlagBits::eIndexBuffer);
    std::memcpy(uiIndexBuffer.mapped, quadIndices, sizeof(quadIndices));

    spdlog::info("UI pipeline and static quad buffers created");

    return true;
}

bool Vulkan::InitBackgroundPipeline() {
    const vk::PushConstantRange pushRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(VulkanRendererInternal::BackgroundPushConstants)
    };

    backgroundPipelineLayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*backgroundSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    });

    const fs::path vsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Background" / "Background.vert.spv");
    const fs::path fsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Background" / "Background.frag.spv");

    const vk::raii::ShaderModule vertModule = LoadShaderModule(vsPath.string());
    const vk::raii::ShaderModule fragModule = LoadShaderModule(fsPath.string());

    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = vertModule, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = fragModule, .pName = "main"}
    };

    constexpr vk::PipelineVertexInputStateCreateInfo emptyVertexInput{}; // 3 verts, no VAO, same as the scene pipelines
    constexpr vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};
    constexpr vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

    constexpr std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    // DrawBackground runs first each frame, right after BeginFrame() -
    // cull is still disabled from BeginFrame's glDisable(GL_CULL_FACE)
    // and nothing has re-enabled depth test yet in the GL trace.
    constexpr vk::PipelineRasterizationStateCreateInfo rasterization{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .lineWidth = 1.0f
    };

    constexpr vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};
    constexpr vk::PipelineDepthStencilStateCreateInfo depthStencil{.depthTestEnable = vk::False, .depthWriteEnable = vk::False};

    const vk::PipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    const vk::PipelineColorBlendStateCreateInfo blendState{.attachmentCount = 1, .pAttachments = &blendAttachment};

    const vk::Format colorFormat = swapchainFormat;
    const vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };

    const vk::GraphicsPipelineCreateInfo pipelineInfo{
        .pNext = &renderingInfo,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &emptyVertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamicState,
        .layout = backgroundPipelineLayout
    };

    backgroundPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

    spdlog::info("Background pipeline created");

    return true;
}

bool Vulkan::InitTextPipeline() {
    const vk::PushConstantRange pushRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(VulkanRendererInternal::TextPushConstants)
    };

    textPipelineLayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*textSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    });

    const fs::path vsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Glyph" / "glyph.vert.spv");
    const fs::path fsPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Glyph" / "glyph.frag.spv");

    const vk::raii::ShaderModule vertModule = LoadShaderModule(vsPath.string());
    const vk::raii::ShaderModule fragModule = LoadShaderModule(fsPath.string());

    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = vertModule, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = fragModule, .pName = "main"}
    };

    // InitializeFont(): ONE vec4 attribute (pos.xy packed with uv.zw) at
    // location 0, NOT two separate vec2s like the UI quad - worth double
    // checking against your shader if you change this layout later.
    const vk::VertexInputBindingDescription binding{0, 4 * sizeof(float), vk::VertexInputRate::eVertex};
    const vk::VertexInputAttributeDescription attribute{0, 0, vk::Format::eR32G32B32A32Sfloat, 0};

    const vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attribute
    };

    constexpr vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};
    constexpr vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

    constexpr std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    constexpr vk::PipelineRasterizationStateCreateInfo rasterization{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .lineWidth = 1.0f
    };
    constexpr vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};

    // RenderText() saves/restores whatever the depth-test enable state was
    // on entry, but its only caller in Update() (RenderUIText, via the "UI
    // Text" block) always runs after "UI Sprites" has already disabled
    // depth testing - so baking depthTestEnable=false into the pipeline
    // matches the state text actually renders under in practice.
    constexpr vk::PipelineDepthStencilStateCreateInfo depthStencil{.depthTestEnable = vk::False, .depthWriteEnable = vk::False};

    const vk::PipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    const vk::PipelineColorBlendStateCreateInfo blendState{.attachmentCount = 1, .pAttachments = &blendAttachment};

    const vk::Format colorFormat = swapchainFormat;
    const vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthFormat
    };

    const vk::GraphicsPipelineCreateInfo pipelineInfo{
        .pNext = &renderingInfo,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamicState,
        .layout = textPipelineLayout
    };

    textPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        glyphVertexBuffers[i] = CreateDynamicBuffer(sizeof(float) * 6 * 4, vk::BufferUsageFlagBits::eVertexBuffer);
    }

    if (!InitializeFont()) {
        spdlog::critical("Failed to initialize renderer font system");
        return false;
    }

    spdlog::info("Text pipeline and font system initialized");

    return true;
}

// ============================================================================
// InitImGui
// ============================================================================
bool Vulkan::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan(window);

    // ImGui's Vulkan backend gets its own descriptor pool, generous and
    // separate from `descriptorPool` above (matches the classic imgui
    // Vulkan example's pool sizing - editor UIs can add a lot of ad hoc
    // textures via GetImGuiTextureID/AddTexture over a session).
    const std::array<vk::DescriptorPoolSize, 1> imguiPoolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 256}
    };

    imguiDescriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 256,
        .poolSizeCount = static_cast<uint32_t>(imguiPoolSizes.size()),
        .pPoolSizes = imguiPoolSizes.data()
    });

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = vk::ApiVersion13;
    initInfo.Instance = *instance;
    initInfo.PhysicalDevice = *physicalDevice;
    initInfo.Device = *device;
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = *graphicsQueue;
    initInfo.DescriptorPool = *imguiDescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<uint32_t>(std::max<size_t>(swapchainImages.size(), 2));
    initInfo.UseDynamicRendering = true;

    // NOTE - ImGui's Vulkan backend struct shape around dynamic rendering
    // has changed more than once (most recently Sept 2025, nesting
    // RenderPass/MSAASamples/PipelineRenderingCreateInfo under a new
    // PipelineInfoMain member). The fields below match the current
    // (docking-branch, mid-2026) layout - if your vendored imgui predates
    // that change, this needs initInfo.PipelineRenderingCreateInfo /
    // initInfo.MSAASamples set directly instead. Check
    // imgui_impl_vulkan.h's struct definition against what's below if this
    // doesn't compile.
    const vk::Format colorFormat = swapchainFormat;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
        reinterpret_cast<const VkFormat*>(&colorFormat);
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = static_cast<VkFormat>(depthFormat);

    ImGui_ImplVulkan_Init(&initInfo);

    spdlog::info("Renderer ImGui initialized (Vulkan backend, dynamic rendering)");

    return true;
}

// ============================================================================
// BuildTextureAtlasFromLevel - CPU-side packing ported verbatim from
// OpenGLInit.cpp (it's pure SDL_surface/memcpy work, nothing GL-specific);
// only the final upload-to-GPU-texture step below is new.
// ============================================================================
bool Vulkan::BuildTextureAtlasFromLevel() {
    using namespace VulkanRendererInternal;
    DestroyAllTextures();

    const Level& level = LevelManager::CurrentLevel();

    std::vector<std::string> referencedFileNames = CollectReferencedTextureFileNames(level);

    for (const ComponentUISprite& uiSprite : level.ui_sprites.components) {
        if (uiSprite.texture.empty()) continue;

        if (std::find(referencedFileNames.begin(), referencedFileNames.end(), uiSprite.texture)
            == referencedFileNames.end()) referencedFileNames.push_back(uiSprite.texture);
    }

    textureRegions.clear();
    textureRegions.resize(referencedFileNames.size());
    textureRegionIndexByName.clear();

    int cursorX = ATLAS_PADDING;
    int cursorY = ATLAS_PADDING;
    int shelfHeight = 0;

    std::vector<unsigned char> atlasPixels(static_cast<size_t>(ATLAS_SIZE) * ATLAS_SIZE * 4, 0);

    const auto copyPixel = [&](const int dstX, const int dstY, const int srcX, const int srcY) {
        if (dstX < 0 || dstX >= ATLAS_SIZE || dstY < 0 || dstY >= ATLAS_SIZE) return;
        if (srcX < 0 || srcX >= ATLAS_SIZE || srcY < 0 || srcY >= ATLAS_SIZE) return;

        unsigned char* dst = atlasPixels.data() + (dstY * ATLAS_SIZE + dstX) * 4;
        const unsigned char* src = atlasPixels.data() + (srcY * ATLAS_SIZE + srcX) * 4;

        std::memcpy(dst, src, 4);
    };

    for (int i = 0; i < static_cast<int>(referencedFileNames.size()); ++i) {
        const std::string& fileName = referencedFileNames[i];

        const std::filesystem::path path = ProjectManager::GetAssetsPath() / std::filesystem::path(fileName).lexically_normal();

        SDL_Surface* loadedSurface = IMG_Load(path.string().c_str());

        if (loadedSurface == nullptr) {
            spdlog::error("IMG_Load failed for {}: {}", path.string(), SDL_GetError());
            continue;
        }

        SDL_Surface* surface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loadedSurface);

        if (surface == nullptr) {
            spdlog::error("SDL_ConvertSurface failed for {}: {}", path.string(), SDL_GetError());
            continue;
        }

        const int textureWidth = surface->w;
        const int textureHeight = surface->h;

        if (textureWidth + ATLAS_PADDING * 2 > ATLAS_SIZE || textureHeight + ATLAS_PADDING * 2 > ATLAS_SIZE) {
            spdlog::error("Texture '{}' is too large for atlas: {}x{}", fileName, textureWidth, textureHeight);
            SDL_DestroySurface(surface);
            continue;
        }

        if (cursorX + textureWidth + ATLAS_PADDING > ATLAS_SIZE) {
            cursorX = ATLAS_PADDING;
            cursorY += shelfHeight + ATLAS_PADDING;
            shelfHeight = 0;
        }

        if (cursorY + textureHeight + ATLAS_PADDING > ATLAS_SIZE) {
            spdlog::error("Texture atlas is full. Could not add '{}'", fileName);
            SDL_DestroySurface(surface);
            continue;
        }

        for (int row = 0; row < textureHeight; ++row) {
            const auto* srcRow = static_cast<unsigned char*>(surface->pixels) + row * surface->pitch;
            unsigned char* dstRow = atlasPixels.data() + ((cursorY + row) * ATLAS_SIZE + cursorX) * 4;
            std::memcpy(dstRow, srcRow, textureWidth * 4);
        }

        constexpr float halfTexel = .5f;
        const float uMin = (cursorX + halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float vMin = (cursorY + halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float uMax = (cursorX + textureWidth - halfTexel) / static_cast<float>(ATLAS_SIZE);
        const float vMax = (cursorY + textureHeight - halfTexel) / static_cast<float>(ATLAS_SIZE);

        textureRegions[i] = {{uMin, vMin, uMax, vMax}, {1.0f, 0.0f, 0.0f, 0.0f}};
        textureRegionIndexByName[fileName] = i;

        for (int pad = 1; pad <= ATLAS_PADDING; ++pad) {
            for (int y = 0; y < textureHeight; ++y) {
                copyPixel(cursorX - pad, y + cursorY, cursorX, y + cursorY);
                copyPixel(cursorX + textureWidth - 1 + pad, y + cursorY, cursorX + textureWidth - 1, y + cursorY);
            }
            for (int x = 0; x < textureWidth; ++x) {
                copyPixel(x + cursorX, cursorY - pad, x + cursorX, cursorY);
                copyPixel(x + cursorX, cursorY + textureHeight - 1 + pad, x + cursorX, cursorY + textureHeight - 1);
            }
            for (int yPad = 1; yPad <= ATLAS_PADDING; ++yPad) {
                copyPixel(cursorX - pad, cursorY - yPad, cursorX, cursorY);
                copyPixel(cursorX + textureWidth - 1 + pad, cursorY - yPad, cursorX + textureWidth - 1, cursorY);
                copyPixel(cursorX - pad, cursorY + textureHeight - 1 + yPad, cursorX, cursorY + textureHeight - 1);
                copyPixel(cursorX + textureWidth - 1 + pad, cursorY + textureHeight - 1 + yPad, cursorX + textureWidth - 1, cursorY + textureHeight - 1);
            }
        }

        spdlog::info("Packed texture '{}' at atlas position {}, {} size {}x{}", fileName, cursorX, cursorY, textureWidth, textureHeight);

        cursorX += textureWidth + ATLAS_PADDING;
        shelfHeight = std::max(shelfHeight, textureHeight);

        SDL_DestroySurface(surface);
    }

    // ---- GPU upload (new for Vulkan; GL equivalent was glTexImage2D + glTexParameteri below this line) ----

    const Level& levelForSettings = LevelManager::CurrentLevel();

    bool wantsMips = false;
    vk::Filter magFilter = vk::Filter::eNearest;
    vk::Filter minFilter = vk::Filter::eNearest;
    vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eNearest;

    switch (levelForSettings.rendererSettings.textureSetting) {
        case PIXEL_ART_SHIMMERY:
            magFilter = vk::Filter::eNearest; minFilter = vk::Filter::eNearest;
            wantsMips = false;
            break;
        case PIXEL_ART_LESS_MOIRE:
            magFilter = vk::Filter::eNearest; minFilter = vk::Filter::eNearest;
            mipmapMode = vk::SamplerMipmapMode::eNearest;
            wantsMips = true;
            break;
        case PIXEL_ART_SMOOTH_DISTANCE:
            magFilter = vk::Filter::eNearest; minFilter = vk::Filter::eNearest;
            mipmapMode = vk::SamplerMipmapMode::eLinear;
            wantsMips = true;
            break;
        case REALISTIC_NORMAL:
            magFilter = vk::Filter::eLinear; minFilter = vk::Filter::eLinear;
            mipmapMode = vk::SamplerMipmapMode::eLinear;
            wantsMips = true;
            break;
        case RETRO:
            magFilter = vk::Filter::eNearest; minFilter = vk::Filter::eNearest;
            mipmapMode = vk::SamplerMipmapMode::eLinear;
            wantsMips = true;
            break;
        default:
            magFilter = vk::Filter::eNearest; minFilter = vk::Filter::eNearest;
            wantsMips = false;
            break;
    }

    const uint32_t mipLevels = wantsMips
        ? static_cast<uint32_t>(std::floor(std::log2(std::max(ATLAS_SIZE, ATLAS_SIZE)))) + 1
        : 1;

    GPUImageResources atlas = CreateGPUImage(
        ATLAS_SIZE, ATLAS_SIZE, mipLevels, vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc
    );

    atlasImage = std::move(atlas.image);
    atlasMemory = std::move(atlas.memory);

    UploadPixelsToImage(atlasImage, atlasPixels.data(), ATLAS_SIZE, ATLAS_SIZE, 4, mipLevels);

    if (mipLevels > 1) {
        GenerateMipmaps(atlasImage, ATLAS_SIZE, ATLAS_SIZE, mipLevels);
    }

    atlasView = device.createImageView(vk::ImageViewCreateInfo{
        .image = atlasImage,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
    });

    atlasSampler = device.createSampler(vk::SamplerCreateInfo{
        .magFilter = magFilter,
        .minFilter = minFilter,
        .mipmapMode = mipmapMode,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = static_cast<float>(mipLevels)
    });

    // Push the freshly-built regions to the GPU and (re)point every frame's
    // descriptor set at the new atlas/region buffer. Called rarely (level
    // load / texture-setting change), so a full waitIdle is an acceptable
    // way to guarantee nothing in flight is still reading the old atlas.
    device.waitIdle();

    if (!textureRegions.empty()) {
        UploadToDynamicBuffer(
            textureRegionBuffer, textureRegions.data(),
            textureRegions.size() * sizeof(VulkanRendererInternal::GPUTextureRegion),
            vk::BufferUsageFlagBits::eStorageBuffer
        );
    }

    for (int frame = 0; frame < FRAMES_IN_FLIGHT; ++frame) {
        const vk::DescriptorImageInfo atlasInfo{
            .sampler = atlasSampler,
            .imageView = atlasView,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        const vk::DescriptorBufferInfo regionInfo{
            .buffer = textureRegionBuffer.buffer,
            .offset = 0,
            .range = vk::WholeSize
        };

        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet = sceneSets[frame], .dstBinding = 8, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &atlasInfo
            },
            vk::WriteDescriptorSet{
                .dstSet = sceneSets[frame], .dstBinding = 5, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &regionInfo
            }
        };

        device.updateDescriptorSets(writes, {});
    }

    spdlog::info("Created texture atlas with {} texture region(s)", textureRegions.size());

    return true;
}

int Vulkan::GetTextureRegionIndex(const std::string& fileName) const {
    if (fileName.empty()) return -1;

    const auto found = textureRegionIndexByName.find(fileName);
    if (found == textureRegionIndexByName.end()) return -1;

    return found->second;
}

// ============================================================================
// Initialize() - public entry point, same call order as the GL version's
// InitSDL -> RefreshTexturesFromLevel -> InitImGui -> InitProjection ->
// InitUI -> InitText chain.
// ============================================================================
bool Vulkan::Initialize(const std::string windowName) {
    if (!InitVulkanInstance(windowName)) {
        spdlog::critical("Renderer initialization stopped at InitVulkanInstance");
        return false;
    }

    if (!InitDevice()) {
        spdlog::critical("Renderer initialization stopped at InitDevice");
        return false;
    }

    if (!InitSwapchain()) {
        spdlog::critical("Renderer initialization stopped at InitSwapchain");
        return false;
    }

    if (!InitCommands()) {
        spdlog::critical("Renderer initialization stopped at InitCommands");
        return false;
    }

    if (!InitDepthResources()) {
        spdlog::critical("Renderer initialization stopped at InitDepthResources");
        return false;
    }

    if (!InitSyncObjects()) {
        spdlog::critical("Renderer initialization stopped at InitSyncObjects");
        return false;
    }

    if (!InitDescriptors()) {
        spdlog::critical("Renderer initialization stopped at InitDescriptors");
        return false;
    }

    RefreshTexturesFromLevel();

    if (!InitImGui()) {
        spdlog::critical("Renderer initialization stopped at InitImGui");
        return false;
    }

    if (!InitScenePipelines()) {
        spdlog::critical("Renderer initialization stopped at InitScenePipelines");
        return false;
    }

    if (!InitUIPipeline()) {
        spdlog::critical("Renderer initialization stopped at InitUIPipeline");
        return false;
    }

    if (!InitBackgroundPipeline()) {
        spdlog::critical("Renderer initialization stopped at InitBackgroundPipeline");
        return false;
    }

    if (!InitTextPipeline()) {
        spdlog::critical("Renderer initialization stopped at InitTextPipeline");
        return false;
    }

    SDL_SetWindowRelativeMouseMode(window, true);

    spdlog::info("Vulkan renderer initialized successfully");

    return true;
}