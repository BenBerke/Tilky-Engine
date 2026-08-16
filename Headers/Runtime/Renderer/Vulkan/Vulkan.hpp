#ifndef TILKY_ENGINE_VULKANRENDERER_HPP
#define TILKY_ENGINE_VULKANRENDERER_HPP

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forces every vk::*CreateInfo / vk::*Info struct to be built with designated
// initializers ( .sType = ..., .pNext = ... ) instead of the old chained
// .setXxx() builder methods. This is the style used throughout the current
// vulkan.org RAII tutorial/samples and reads closest to the rest of this
// codebase's C++20 style.
//
// IMPORTANT: this macro must be defined before the *first* inclusion of any
// vulkan.hpp header anywhere in the translation unit graph. Every .cpp file
// in this renderer includes this header first, so as long as nothing else
// in the engine drags in <vulkan/vulkan.hpp> earlier without the macro,
// you're fine - but it's a project-wide-consistency requirement, not a
// per-file one, so worth grepping for if you ever see conflicting-macro
// compile errors.
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Headers/Runtime/Renderer/IRenderer.hpp"

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector4.hpp"
#include "Headers/Objects/Components.hpp"

struct Texture;

namespace VulkanRendererInternal {
    inline constexpr int FRAMES_IN_FLIGHT = 2;

    constexpr int ATLAS_SIZE = 4096;
    constexpr int ATLAS_PADDING = 2;

    // The OpenGL font system gave every ASCII glyph its own GL texture and
    // rebound texture unit 0 per character. Vulkan has no equivalent of a
    // cheap "just rebind this texture" call mid-command-buffer, so instead
    // all 128 glyphs are packed into one atlas image (same shelf-pack idea
    // as the main texture atlas) and RenderText looks up a UV rect per
    // character instead of a texture handle. See VulkanText.cpp.
    constexpr int FONT_ATLAS_SIZE = 1024;
    constexpr int FONT_ATLAS_PADDING = 2;

    inline constexpr SDL_WindowFlags WINDOW_FLAGS =
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;

    // ---------------------------------------------------------------
    // GPU-visible structs. Field layout is byte-for-byte identical to
    // OpenGLRendererInternal's versions - std430 SSBO layout rules for
    // plain vec4-of-floats structs are the same in Vulkan GLSL as they
    // were in GL, so any shader already consuming these can keep the
    // same `layout(std430, binding = N) buffer` block bodies.
    // ---------------------------------------------------------------

    struct GpuFlatTriangle {
        Vector4 a;
        Vector4 b;
        Vector4 c;
        Vector4 color;
        Vector4 data;
    };

    struct GpuWall {
        Vector4 startEnd;
        Vector4 color;
        Vector4 heights;
        Vector4 data;
        Vector4 textureOffset_padding;
    };

    struct alignas(16) IntVector4 {
        int x = -1;
        int y = -1;
        int z = -1;
        int w = -1;
    };

    struct GpuSprite {
        Vector4 positionSize;
        Vector4 color;
        IntVector4 textureIndices0;
        IntVector4 textureIndices1;
        Vector4 data;
        Vector4 rotation;
        IntVector4 flags;
    };

    static_assert(sizeof(GpuSprite) == 112);

    struct GpuSector {
        Vector4 floorData;
    };

    struct GpuSectorFloor {
        Vector4 heights;
        Vector4 slopeData;
        Vector4 floorColor;
        Vector4 ceilingColor;
        Vector4 textureData;
    };

    struct GpuCollider {
        Vector4 positionType;
        Vector4 scale;
    };

    static_assert(sizeof(GpuCollider) == sizeof(float) * 8);

    struct GPUTextureRegion {
        Vector4 uvRect;
        Vector4 data;
    };

    // Per-frame uniform data shared by the wall/flat/sprite/collider
    // pipelines. In the GL renderer this was `viewUniform`/`projectionUniform`
    // set once per Update() call and left bound across several draw calls;
    // a small per-frame-in-flight UBO is the direct Vulkan equivalent of
    // "set once, reused by several draws". renderMode does NOT need a
    // field here - each render mode gets its own vk::raii::Pipeline
    // (see Vulkan::wallPipeline etc.), so which pipeline is bound at
    // draw time replaces the GL renderMode uniform branch entirely.
    //
    // Layout note for whoever writes the GLSL side: cameraWorldPos and
    // textureCount are laid out here as 4 contiguous floats with no gap
    // (C++ gives plain float/float[] members no extra alignment). To
    // guarantee that matches on the shader side under std140 - where a
    // bare `vec3` immediately followed by a separate `float` can legally
    // pick up padding depending on how strictly a given compiler applies
    // the vec3-alignment rule - declare this UBO member as a single
    // `vec4 cameraWorldPos_textureCount` (.xyz = position, .w = count)
    // rather than a separate vec3 + float. Same applies to `view`/
    // `projection`: plain mat4, no surprises there.
    struct alignas(16) SceneUBO {
        float view[16];
        float projection[16];
        float cameraWorldPos[3];
        float textureCount; // packed into the 4th slot alongside cameraWorldPos
    };

    // Matches DrawUIRectangle's uniform set exactly (uScreenSize, uPosition,
    // uSize, uColor, rotation, uUseTexture, uTextureIndex, uTextureCount),
    // just pushed as one block instead of eight separate glUniform calls.
    // 15 floats/ints = 60 bytes, comfortably inside the 128-byte push
    // constant range guaranteed on every Vulkan implementation.
    struct UIPushConstants {
        float screenSize[2];
        float position[2];
        float size[2];
        float color[4];
        float rotation;
        int useTexture;
        int textureIndex;
        int textureCount;
    };

    struct BackgroundPushConstants {
        float playerPitch;
        float playerAngle;
        float horizontalFov;
        float parallaxStrength;
        float backgroundScroll;
    };

    struct TextPushConstants {
        float projection[16];
        float textColor[3];
    };

    // GPU-side glyph metadata. `uvRect` replaces GL's per-glyph textureID -
    // see the FONT_ATLAS_SIZE comment above.
    struct Character {
        Vector4 uvRect;
        Vector2 Size;
        Vector2 Bearing;
        unsigned int Advance = 0;
    };

    struct GPUTexture {
        vk::raii::Image image = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::ImageView view = nullptr;
        vk::raii::Sampler sampler = nullptr;
        int width = 0;
        int height = 0;
    };

    struct LoadedTextureSurface {
        SDL_Surface* surface = nullptr;
        int textureIndex = -1;
        int x = 0;
        int y = 0;
    };

    // A host-visible, persistently-mapped buffer that's rewritten every
    // frame (the wall/flat/sprite/sector/sectorFloor/collider SSBOs, and
    // the scene UBO). One instance per frame-in-flight - see the
    // FRAMES_IN_FLIGHT comment on Vulkan::PerFrame.
    struct DynamicBuffer {
        vk::raii::Buffer buffer = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
        void* mapped = nullptr;
        vk::DeviceSize capacity = 0; // bytes currently backing `buffer`
    };
}

class Vulkan final : public IRenderer {
public:
    Vulkan() = default;
    ~Vulkan() override = default;

    bool Initialize(std::string windowName) override;
    void Shutdown() override;

    void BeginFrame() override;
    void Update(bool renderDebug, bool renderUI) override;
    void EndFrame() override;

    void OnResize(int width, int height) override;

    int CreateTexture(const std::string& fileName) override;

    bool CreateMap() override;

    // NOTE: the OpenGL version of this was `RenderText(const Shader&, ...)`
    // because a single GL program object could be swapped per call. There's
    // only ever one text pipeline here, so the Shader& parameter is dropped;
    // RenderTextRaw calls this directly instead of threading `*textShader`
    // through it.
    void RenderText(
        const std::string& text,
        float x,
        float y,
        Vector2 scale,
        Vector3 color
    );

    void RenderTextRaw(
        const std::string& text,
        Vector2 position,
        Vector2 scale,
        Vector3 color
    ) override;

    void DrawUIRectangle(
        const Vector2& position,
        const Vector2& size,
        const Vector4& color,
        float rotation,
        const std::string& texture
    ) const;

    [[nodiscard]] SDL_Window* GetWindow() const override {
        return window;
    }

    [[nodiscard]] const char* GetName() const override {
        return "VULKAN";
    }

    bool BuildTextureAtlasFromLevel();

    void BeginImGuiFrame() const override;
    void EndImGuiFrame() const override;

    [[nodiscard]] ImTextureID GetImGuiTextureID(const std::string& fileName) override;

private:
    using Character = VulkanRendererInternal::Character;
    using GpuFlatTriangle = VulkanRendererInternal::GpuFlatTriangle;
    using GpuWall = VulkanRendererInternal::GpuWall;
    using GpuSprite = VulkanRendererInternal::GpuSprite;
    using GpuSector = VulkanRendererInternal::GpuSector;
    using GpuSectorFloor = VulkanRendererInternal::GpuSectorFloor;
    using GpuCollider = VulkanRendererInternal::GpuCollider;
    using GPUTexture = VulkanRendererInternal::GPUTexture;
    using DynamicBuffer = VulkanRendererInternal::DynamicBuffer;

    static constexpr int FRAMES_IN_FLIGHT = VulkanRendererInternal::FRAMES_IN_FLIGHT;

    // screenWidth / screenHeight are public members of IRenderer itself
    // (default 1600x900, confirmed via IRenderer.hpp) - not redeclared
    // here, same as OpenGL.hpp never redeclared them either.

    SDL_Window* window = nullptr;

    // ---- Instance / device -----------------------------------------
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;

    uint32_t graphicsQueueFamily = 0;
    vk::raii::Queue graphicsQueue = nullptr;

    // ---- Swapchain ----------------------------------------------------
    vk::raii::SwapchainKHR swapchain = nullptr;
    vk::Format swapchainFormat = vk::Format::eUndefined;
    vk::Extent2D swapchainExtent{};
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    std::vector<vk::Image> swapchainImages; // owned by swapchain, not individually RAII-wrapped
    std::vector<vk::raii::ImageView> swapchainImageViews;
    bool swapchainDirty = false;

    vk::Format depthFormat = vk::Format::eD32Sfloat;
    vk::raii::Image depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView depthImageView = nullptr;

    // ---- Commands / sync, one set per frame-in-flight -----------------
    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;

    uint32_t currentFrame = 0;
    uint32_t currentImageIndex = 0;
    vk::raii::CommandBuffer* activeCommandBuffer = nullptr; // valid between BeginFrame/EndFrame

    // ---- Descriptors ----------------------------------------------------
    // "Scene" set: bindings match the GL SSBO binding indices 1:1 so the
    // shaders are easy to cross-reference (0 wall, 1 flat, 2 sprite,
    // 4 sector, 5 textureRegion, 6 collider, 7 sectorFloor - 3 is skipped,
    // same as the GL code). Binding 8 is the atlas combined-image-sampler
    // (GL kept textures and SSBOs in separate namespaces via texture
    // units; Vulkan descriptor bindings share one namespace, so the atlas
    // needed a binding number that doesn't collide with the SSBOs).
    // Binding 9 is the per-frame SceneUBO. Shared by the wall/flat/sprite/
    // collider pipelines AND the UI pipeline (UI needs bindings 5 + 8 for
    // its own atlas lookups).
    vk::raii::DescriptorSetLayout sceneSetLayout = nullptr;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> sceneSets; // [FRAMES_IN_FLIGHT]

    // Dear ImGui's Vulkan backend owns whatever pool it's given and can
    // allocate a growing number of descriptor sets from it over a session
    // (GetImGuiTextureID -> ImGui_ImplVulkan_AddTexture, once per unique
    // asset preview etc.) - kept separate from `descriptorPool` above so
    // the renderer's own tightly-sized pool can't be starved by editor UI.
    vk::raii::DescriptorPool imguiDescriptorPool = nullptr;

    // Background and text each bind exactly one sampler that isn't part
    // of the main atlas lifecycle, so they get their own tiny set layouts
    // rather than bloating the shared one.
    vk::raii::DescriptorSetLayout backgroundSetLayout = nullptr;
    vk::raii::DescriptorSet backgroundSet = nullptr;

    vk::raii::DescriptorSetLayout textSetLayout = nullptr;
    vk::raii::DescriptorSet textSet = nullptr;

    // ---- Pipelines ----------------------------------------------------
    // wall/flat/sprite/collider share one layout (sceneSetLayout, no push
    // constants - everything they need is either an SSBO or the SceneUBO).
    // Depth/blend/topology/cull state that GL toggled per-draw via
    // glDepthFunc/glEnable/glCullFace is baked into each pipeline instead;
    // see VulkanInit.cpp for the exact state each one carries and why.
    vk::raii::PipelineLayout scenePipelineLayout = nullptr;
    vk::raii::Pipeline wallPipeline = nullptr;
    vk::raii::Pipeline flatPipeline = nullptr;
    vk::raii::Pipeline spritePipeline = nullptr;
    vk::raii::Pipeline colliderPipeline = nullptr;

    vk::raii::PipelineLayout uiPipelineLayout = nullptr;
    vk::raii::Pipeline uiPipeline = nullptr;
    // The static unit quad from InitUI() - uploaded once, never rewritten,
    // so (unlike wallBuffers etc. above) this does NOT need one copy per
    // frame-in-flight.
    DynamicBuffer uiVertexBuffer;
    DynamicBuffer uiIndexBuffer;

    vk::raii::PipelineLayout backgroundPipelineLayout = nullptr;
    vk::raii::Pipeline backgroundPipeline = nullptr;

    vk::raii::PipelineLayout textPipelineLayout = nullptr;
    vk::raii::Pipeline textPipeline = nullptr;

    // ---- Per-frame dynamic buffers (double-buffered) -------------------
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> wallBuffers;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> flatBuffers;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> spriteBuffers;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> sectorBuffers;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> sectorFloorBuffers;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> colliderBuffers;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> sceneUboBuffers;

    // textureRegionSSBO is only rebuilt on RefreshTexturesFromLevel (rare),
    // not every frame, so it doesn't need double buffering - a
    // device.waitIdle() around the rebuild is enough (see VulkanTexture.cpp).
    DynamicBuffer textureRegionBuffer;

    int gpuWallCount = 0;
    int flatTriangleCount = 0;
    int spriteCount = 0;
    int colliderCount = 0;

    std::map<char, Character> Characters;
    vk::raii::Image fontAtlasImage = nullptr;
    vk::raii::DeviceMemory fontAtlasMemory = nullptr;
    vk::raii::ImageView fontAtlasView = nullptr;
    vk::raii::Sampler fontAtlasSampler = nullptr;
    std::array<DynamicBuffer, FRAMES_IN_FLIGHT> glyphVertexBuffers; // tiny, one quad at a time

    FT_Library ft = nullptr;
    FT_Face face = nullptr;

    std::vector<GpuWall> gpuWalls;
    std::vector<GpuFlatTriangle> flatTriangles;
    std::vector<GpuSprite> gpuSprites;
    std::vector<GpuCollider> gpuColliders;
    std::vector<GpuSector> gpuSectors;
    std::vector<GpuSectorFloor> gpuSectorFloors;

    std::vector<GPUTexture> textures;
    vk::raii::Image atlasImage = nullptr;
    vk::raii::DeviceMemory atlasMemory = nullptr;
    vk::raii::ImageView atlasView = nullptr;
    vk::raii::Sampler atlasSampler = nullptr;

    std::vector<VulkanRendererInternal::GPUTextureRegion> textureRegions;
    int backgroundTextureIndex = -1;
    std::unordered_map<std::string, int> textureRegionIndexByName;
    std::unordered_map<std::string, int> textureIndexByName;
    std::string backgroundTextureFileName;

    // fileName -> ImGui_ImplVulkan_AddTexture result, so repeated
    // GetImGuiTextureID calls for the same file don't leak descriptor sets.
    std::unordered_map<std::string, ImTextureID> imguiTextureCache;

    // useEditorCamera is a protected member of IRenderer itself (confirmed
    // via IRenderer.hpp), along with GetEditorCamera()/
    // GetEditorCameraTransform()/DestroyEditorCamera() - all inherited,
    // not redeclared here.

    bool InitVulkanInstance(const std::string& windowName);
    bool InitDevice();
    bool InitSwapchain();
    bool InitDepthResources();
    bool InitCommands();
    bool InitSyncObjects();
    bool InitDescriptors();
    bool InitScenePipelines();
    bool InitUIPipeline();
    bool InitBackgroundPipeline();
    bool InitTextPipeline();
    bool InitImGui();
    bool InitializeFont();

    void RecreateSwapchain();
    void DestroySwapchainResources();

    [[nodiscard]] vk::raii::ShaderModule LoadShaderModule(const std::string& spirvPath) const;
    [[nodiscard]] uint32_t FindMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const;

    DynamicBuffer CreateDynamicBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage) const;
    void UploadToDynamicBuffer(DynamicBuffer& target, const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage);

    // Shared by BuildTextureAtlasFromLevel (main atlas), InitializeFont
    // (glyph atlas) and CreateTexture (ad-hoc single textures, e.g. the
    // background/window icon) - all three do "decode pixels on the CPU,
    // then get them into a sampled vk::raii::Image" and previously would
    // have needed the same ~100 lines of staging-buffer/barrier/mipmap
    // boilerplate duplicated three times.
    [[nodiscard]] vk::raii::CommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(vk::raii::CommandBuffer& cmd) const;

    void TransitionImageLayout(
        const vk::raii::CommandBuffer& cmd,
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::ImageAspectFlags aspectMask,
        uint32_t mipLevels = 1
    ) const;

    // Allocates+binds device-local memory and returns an image ready for a
    // view/sampler. `usage` should already include eTransferDst if pixels
    // will be uploaded, and eTransferSrc too if GenerateMipmaps will blit
    // from it.
    struct GPUImageResources {
        vk::raii::Image image = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
    };
    [[nodiscard]] GPUImageResources CreateGPUImage(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        vk::Format format,
        vk::ImageUsageFlags usage
    ) const;

    // Uploads `pixels` (tightly packed, `bytesPerPixel` per texel) into
    // `image` via a throwaway host-visible staging buffer, leaving the
    // image in eShaderReadOnlyOptimal. Mirrors the SDL_Surface -> glTexImage2D
    // upload every OpenGL*.cpp texture path did implicitly through the driver.
    void UploadPixelsToImage(
        vk::Image image,
        const void* pixels,
        uint32_t width,
        uint32_t height,
        uint32_t bytesPerPixel,
        uint32_t mipLevels
    ) const;

    // Blit-based mip chain generation - Vulkan has no glGenerateMipmap
    // equivalent, this is the standard manual replacement. Leaves the
    // image in eShaderReadOnlyOptimal across all mip levels.
    void GenerateMipmaps(vk::Image image, int32_t width, int32_t height, uint32_t mipLevels) const;

    void BuildGpuSectors();
    void BuildGpuSprites();
    void BuildGpuColliders();

    void BuildGpuWallsFromMap();
    void UploadGpuWallsFromMap();

    void BuildFlatTrianglesFromSectors();

    void DrawBackground(float pitch, float yaw, float horizontalFov, float parallaxStrength, float backgroundScroll);
    int GetOrCreateTextureIndex(const std::string& fileName);
    [[nodiscard]] int GetTextureRegionIndex(const std::string& fileName) const;

    void RefreshTexturesFromLevel() override;
    [[nodiscard]] const GPUTexture& GetTexture(int index) const;
    [[nodiscard]] int GetTextureCount() const;
    void DestroyAllTextures();

    void RenderUIText(const ComponentUIText& text, const ComponentUITransform& transform);

    static constexpr int SECTOR_FLOOR_COUNT = 3;
    static constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;
};

#endif