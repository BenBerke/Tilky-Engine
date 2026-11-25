// The Vulkan side of textures and text: standalone images, the atlas (a 2D
// array image with its region buffer), glyph pages and the glyph quads.
// Which images go where, and text layout, are RendererCore's.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include <spdlog/spdlog.h>

using namespace VulkanInternal;

namespace {
    int MipCount(const int width, const int height) {
        return static_cast<int>(std::bit_width(static_cast<unsigned>(std::max(std::max(width, height), 1))));
    }
}

// ============================================================================
// Standalone textures
// ============================================================================

unsigned Vulkan::UploadTextureGpu(const void* pixels, const int width, const int height) {
    Image image;
    if (!UploadImage(pixels, width, height, VK_FORMAT_R8G8B8A8_UNORM, MipCount(width, height), image)) return 0;

    image.slot = AllocateTextureSlot(false);
    WriteTextureSlot(image, false);

    if (!freeStandaloneIds.empty()) {
        const unsigned id = freeStandaloneIds.back();
        freeStandaloneIds.pop_back();
        standaloneTextures[id - 1] = image;
        return id;
    }

    standaloneTextures.push_back(image);
    return static_cast<unsigned>(standaloneTextures.size());
}

void Vulkan::DestroyTextureGpu(const unsigned id) {
    if (id == 0 || id > standaloneTextures.size()) return;

    Image& image = standaloneTextures[id - 1];
    if (image.image == VK_NULL_HANDLE) return;

    // The editor keeps ImGui ids it was given; the image stays for them.
    if (image.imguiSet != VK_NULL_HANDLE) retiredImages.push_back(image);
    else DeferDestroyImage(image);

    image = {};
    freeStandaloneIds.push_back(id);
}

ImTextureID Vulkan::TextureImGuiID(const unsigned id) const {
    if (id == 0 || id > standaloneTextures.size()) return ImTextureID{};

    // Made on first ask: the game never needs them.
    auto& image = const_cast<Image&>(standaloneTextures[id - 1]);
    if (image.image == VK_NULL_HANDLE) return ImTextureID{};
    if (image.imguiSet == VK_NULL_HANDLE) image.imguiSet = const_cast<Vulkan*>(this)->MakeImGuiSet(image.view);

    return reinterpret_cast<ImTextureID>(image.imguiSet);
}

const VulkanInternal::Image* Vulkan::StandaloneImage(const unsigned id) const {
    if (id == 0 || id > standaloneTextures.size()) return nullptr;
    const Image& image = standaloneTextures[id - 1];
    return image.image != VK_NULL_HANDLE ? &image : nullptr;
}

// ============================================================================
// The atlas
// ============================================================================

bool Vulkan::AtlasExists() const {
    return atlas.image != VK_NULL_HANDLE;
}

int Vulkan::MaxAtlasPages() const {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    return static_cast<int>(std::min<uint32_t>(properties.limits.maxImageArrayLayers, 2048));
}

void Vulkan::CreateAtlasGpu(const int pageCount) {
    using RendererGpu::ATLAS_SIZE;

    if (!CreateImage(ATLAS_SIZE, ATLAS_SIZE, pageCount, MipCount(ATLAS_SIZE, ATLAS_SIZE), VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, true, atlas))
        return;

    VkCommandBuffer commands = BeginOneShot();
    Transition(commands, atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EndOneShot(commands);

    CreateBuffer(static_cast<VkDeviceSize>(ATLAS_SIZE) * ATLAS_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, atlasStaging);
}

void Vulkan::UploadAtlasPage(const int page, const unsigned char* pixels) {
    using RendererGpu::ATLAS_SIZE;
    if (atlas.image == VK_NULL_HANDLE || atlasStaging.mapped == nullptr) return;

    std::memcpy(atlasStaging.mapped, pixels, static_cast<size_t>(ATLAS_SIZE) * ATLAS_SIZE * 4);

    VkCommandBuffer commands = BeginOneShot();
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(page), 1};
    copy.imageExtent = {static_cast<uint32_t>(ATLAS_SIZE), static_cast<uint32_t>(ATLAS_SIZE), 1};
    vkCmdCopyBufferToImage(commands, atlasStaging.buffer, atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    EndOneShot(commands);
}

void Vulkan::FinishAtlasGpu(const RendererTextureSettings setting) {
    DestroyBuffer(atlasStaging); // every copy was waited for
    if (atlas.image == VK_NULL_HANDLE) return;

    // Every level, whatever the setting: its sampler decides whether they're read.
    VkCommandBuffer commands = BeginOneShot();
    GenerateMips(commands, atlas);
    EndOneShot(commands);

    atlasSetting = setting;
    atlas.slot = AllocateTextureSlot(true);
    WriteTextureSlot(atlas, true);

    DeferDestroyBuffer(textureRegionBuffer);
    const size_t bytes = textureRegions.size() * sizeof(RendererGpu::GPUTextureRegion);
    if (CreateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true, textureRegionBuffer) && bytes > 0)
        std::memcpy(textureRegionBuffer.mapped, textureRegions.data(), bytes);
}

void Vulkan::DestroyAtlasGpu() {
    DestroyBuffer(atlasStaging);
    DeferDestroyImage(atlas);
    DeferDestroyBuffer(textureRegionBuffer);
}

// ============================================================================
// Glyph pages and text
// ============================================================================

void Vulkan::AddGlyphPageGpu() {
    using RendererGpu::GLYPH_PAGE_SIZE;

    Image page;
    // Cleared so the padding between glyphs never bleeds old pixels.
    if (!UploadImage(nullptr, GLYPH_PAGE_SIZE, GLYPH_PAGE_SIZE, VK_FORMAT_R8_UNORM, 1, page)) {
        glyphPages.emplace_back();
        return;
    }

    page.slot = AllocateTextureSlot(false);
    WriteTextureSlot(page, false);
    glyphPages.push_back(page);
}

void Vulkan::UploadGlyphGpu(const int pageIndex, const int x, const int y, const int width, const int height, const unsigned char* coverage) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(glyphPages.size()) || width <= 0 || height <= 0) return;
    Image& page = glyphPages[pageIndex];
    if (page.image == VK_NULL_HANDLE) return;

    // In this frame's commands, so text drawn before keeps what it saw:
    // a copy can't happen inside rendering, which picks up again (keeping
    // what's drawn) at the next draw.
    const size_t bytes = static_cast<size_t>(width) * height;
    const ArenaAllocation staging = ArenaAllocate(bytes, 16);
    std::memcpy(staging.pointer, coverage, bytes);

    VkCommandBuffer commands = Commands();
    EndRendering();

    Transition(commands, page, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copy{};
    copy.bufferOffset = staging.offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {x, y, 0};
    copy.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(commands, staging.buffer, page.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    Transition(commands, page, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Vulkan::DestroyGlyphPagesGpu() {
    for (Image& page : glyphPages) DeferDestroyImage(page);
    glyphPages.clear();
}

namespace {
    // Saved by BeginTextGpu, put back by EndTextGpu.
    struct TextSavedState {
        bool depthTest = false;
        bool blend = false;
        Blend blendFunc = Blend::Alpha;
        Program* program = nullptr;
    };
    TextSavedState textSaved;
}

void Vulkan::BeginTextGpu(const Vector4 color) {
    textSaved = {state.depthTest, state.blend, state.blendFunc, program};

    state.depthTest = false;
    state.blend = true;
    state.blendFunc = Blend::Alpha;

    UseProgram(textProgram.get());

    if (screenWidth > 0 && screenHeight > 0) {
        const float w = static_cast<float>(screenWidth);
        const float h = static_cast<float>(screenHeight);

        // Top-left origin, as OpenGLText.cpp's.
        const float projection[16] = {
            2.0f / w, 0.0f,     0.0f, 0.0f,
            0.0f,    -2.0f / h, 0.0f, 0.0f,
            0.0f,     0.0f,    -1.0f, 0.0f,
           -1.0f,     1.0f,     0.0f, 1.0f
        };
        SetMat4ColumnMajor("projection", projection);
    }

    SetVec4("textColor", std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f), std::clamp(color.z, 0.0f, 1.0f),
            std::clamp(color.w, 0.0f, 1.0f));
}

void Vulkan::DrawGlyphsGpu(const int page, const std::vector<float>& vertices) {
    if (page < 0 || page >= static_cast<int>(glyphPages.size()) || vertices.empty()) return;

    SetTexture("text", &glyphPages[page], SAMPLER_LINEAR_CLAMP);
    DrawVertices(vertices.data(), vertices.size() * sizeof(float), 4 * sizeof(float), static_cast<uint32_t>(vertices.size() / 4));
}

void Vulkan::EndTextGpu() {
    state.depthTest = textSaved.depthTest;
    state.blend = textSaved.blend;
    state.blendFunc = textSaved.blendFunc;
    UseProgram(textSaved.program);
}
