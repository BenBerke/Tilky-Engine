#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <cmath>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include "imgui_impl_vulkan.h"

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Map/LevelManager.hpp"

int Vulkan::CreateTexture(const std::string& fileName) {
    std::filesystem::path path(fileName);

    if (!path.is_absolute()) path = ProjectManager::GetAssetsPath() / std::filesystem::path(fileName).lexically_normal();

    SDL_Surface* loadedSurface = IMG_Load(path.string().c_str());

    if (loadedSurface == nullptr) {
        spdlog::error("IMG_Load failed for {}: {}", path.string(), SDL_GetError());
        return -1;
    }

    SDL_Surface* convertedSurface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);

    SDL_DestroySurface(loadedSurface);

    if (convertedSurface == nullptr) {
        spdlog::error("SDL_ConvertSurface failed for {}: {}", path.string(), SDL_GetError());
        return -1;
    }

    // GL always mipmapped ad-hoc textures unconditionally (unlike the
    // atlas, which only mips for some textureSetting values) - matched
    // here the same way.
    const auto mipLevels = static_cast<uint32_t>(
        std::floor(std::log2(std::max(convertedSurface->w, convertedSurface->h)))
    ) + 1;

    GPUImageResources image = CreateGPUImage(
        static_cast<uint32_t>(convertedSurface->w), static_cast<uint32_t>(convertedSurface->h), mipLevels,
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc
    );

    UploadPixelsToImage(
        image.image, convertedSurface->pixels,
        static_cast<uint32_t>(convertedSurface->w), static_cast<uint32_t>(convertedSurface->h), 4, mipLevels
    );

    GenerateMipmaps(image.image, convertedSurface->w, convertedSurface->h, mipLevels);

    GPUTexture texture;
    texture.image = std::move(image.image);
    texture.memory = std::move(image.memory);
    texture.width = convertedSurface->w;
    texture.height = convertedSurface->h;

    texture.view = device.createImageView(vk::ImageViewCreateInfo{
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
    });

    // GL: GL_REPEAT wrap, GL_LINEAR_MIPMAP_LINEAR min filter,
    // GL_NEAREST mag filter, LOD bias -0.25.
    texture.sampler = device.createSampler(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = -0.25f,
        .maxLod = static_cast<float>(mipLevels)
    });

    SDL_DestroySurface(convertedSurface);

    textures.push_back(std::move(texture));

    return static_cast<int>(textures.size()) - 1;
}

// Lazy, filename-keyed resolution on top of the plain textures[] list
// above (NOT the atlas - see GetTextureRegionIndex for that). Used by
// anything that binds a single standalone texture directly, such as
// the background. Loads and caches on first request; returns -1 (never
// crashes) if the file is missing or fails to decode - a failed load is
// cached too, so a broken reference isn't retried every frame.
int Vulkan::GetOrCreateTextureIndex(const std::string& fileName) {
    if (fileName.empty()) return -1;

    const auto found = textureIndexByName.find(fileName);
    if (found != textureIndexByName.end()) return found->second;

    const int index = CreateTexture(fileName);
    textureIndexByName.emplace(fileName, index); // cache -1 too, on failure

    return index;
}

void Vulkan::RefreshTexturesFromLevel() {
    if (!BuildTextureAtlasFromLevel()) {
        spdlog::error("Failed to build texture atlas from level");
        return;
    }

    spdlog::info("Built texture atlas with {} texture region(s)", textureRegions.size());
}

const Vulkan::GPUTexture& Vulkan::GetTexture(const int index) const {
    return textures[index];
}

int Vulkan::GetTextureCount() const {
    return static_cast<int>(textures.size());
}

void Vulkan::DestroyAllTextures() {
    device.waitIdle(); // textures may still be referenced by an in-flight frame's descriptor set

    for (const auto& [fileName, id] : imguiTextureCache) {
        // C-style cast rather than reinterpret_cast: ImTextureID's
        // underlying type has changed across ImGui versions (historically
        // void*, more recently a 64-bit integer typedef so it can hold a
        // VkDescriptorSet on 32-bit platforms too) - a C-style cast
        // compiles against either representation, matching how ImGui's
        // own backends do this conversion.
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)(id));
    }
    imguiTextureCache.clear();

    textures.clear(); // each GPUTexture's raii members free themselves here
    textureIndexByName.clear();

    atlasView = nullptr;
    atlasSampler = nullptr;
    atlasImage = nullptr;
    atlasMemory = nullptr;

    textureRegionBuffer = DynamicBuffer{};
    textureRegions.clear();
    textureRegionIndexByName.clear();
}

ImTextureID Vulkan::GetImGuiTextureID(const std::string& fileName) {
    const auto cached = imguiTextureCache.find(fileName);
    if (cached != imguiTextureCache.end()) return cached->second;

    const int textureIndex = GetOrCreateTextureIndex(fileName);

    if (textureIndex < 0 || textureIndex >= GetTextureCount()) return ImTextureID{};

    const GPUTexture& texture = GetTexture(textureIndex);

    // Unlike GL (where an ImTextureID is just the raw GLuint), ImGui's
    // Vulkan backend represents a texture as a descriptor set it manages
    // internally - AddTexture allocates one from imguiDescriptorPool and
    // wires it to this sampler+view. Cached by filename so repeated calls
    // (e.g. an asset browser redrawing every frame) don't leak a new
    // descriptor set each time.
    const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
        *texture.sampler, *texture.view, static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)
    );

    const auto id = (ImTextureID)(descriptorSet); // see the cast comment in DestroyAllTextures above
    imguiTextureCache[fileName] = id;

    return id;
}