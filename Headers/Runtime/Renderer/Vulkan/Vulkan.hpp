#ifndef TILKY_ENGINE_VULKANRENDERER_HPP
#define TILKY_ENGINE_VULKANRENDERER_HPP

// The Vulkan backend (Project > Switch Renderer). Same pictures as the
// OpenGL one: it draws with the same GLSL shaders (compiled to SPIR-V when
// loaded, VulkanShaders.cpp) and the same RendererCore CPU side.
//
// To keep the drawing code a port of the OpenGL renderer's, the backend
// keeps GL-like state - the bound program and its uniform values, the
// storage buffer bindings, depth/stencil/blend state - and turns it into
// pipelines, dynamic state and one push constant per draw:
//
//   - loose uniforms of a program live in one std430 block in a per-frame
//     buffer; the push constant is its device address,
//   - SSBOs become buffer references in that block,
//   - samplers are indices into one bindless descriptor set.
//
// Frames: the first call that records (a clear, a draw, a glyph upload...)
// opens the frame; EndFrame() submits it and presents. A frame draws into
// the window and the editor panels' targets in any order, as GL's did.
//
// One layout in every executable: no TILKY_STANDALONE in here.

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <volk.h>

#include "Headers/Runtime/Renderer/RendererCore.hpp"

struct VmaAllocator_T;
struct VmaAllocation_T;
typedef VmaAllocator_T* VmaAllocator;
typedef VmaAllocation_T* VmaAllocation;

namespace VulkanInternal {
    inline constexpr int FRAMES_IN_FLIGHT = 2;

    // Bindless descriptor set: set 0.
    inline constexpr uint32_t BINDING_TEXTURES_2D = 0;
    inline constexpr uint32_t BINDING_TEXTURE_ARRAYS = 1;
    inline constexpr uint32_t BINDING_SAMPLERS = 2;
    inline constexpr uint32_t MAX_TEXTURES_2D = 8192;
    inline constexpr uint32_t MAX_TEXTURE_ARRAYS = 64;
    inline constexpr uint32_t MAX_SAMPLERS = 32;

    // SSBO binding points the shaders use (0-12).
    inline constexpr int STORAGE_BINDINGS = 16;

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkDeviceAddress address = 0;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };

    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        int width = 0;
        int height = 0;
        int layers = 1;
        int mips = 1;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool arrayView = false;              // a 2D_ARRAY view (bindless binding 1)
        uint32_t slot = UINT32_MAX;          // bindless index, UINT32_MAX = none
        VkDescriptorSet imguiSet = VK_NULL_HANDLE; // made on first ask
    };

    // A run of a frame's arena.
    struct ArenaAllocation {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceAddress address = 0;
        void* pointer = nullptr;
    };

    struct Arena {
        std::vector<Buffer> chunks;
        size_t chunk = 0;
        VkDeviceSize used = 0;
    };

    // One loose uniform of a program, at its std430 place in the uniform block.
    struct UniformField {
        std::string type;  // GLSL type ("vec4", "mat4", "sampler2D" ...)
        uint32_t offset = 0;
        uint32_t size = 0;      // one element
        uint32_t stride = 0;    // array stride
        uint32_t count = 1;     // array length
        bool sampler = false;   // the two uints of a texture/sampler pair
    };

    struct VertexAttribute {
        uint32_t location = 0;
        VkFormat format = VK_FORMAT_R32G32_SFLOAT;
        uint32_t offset = 0;
    };

    struct Program {
        std::string name;
        VkShaderModule vertex = VK_NULL_HANDLE;
        VkShaderModule fragment = VK_NULL_HANDLE;
        std::unordered_map<std::string, UniformField> uniforms;
        uint32_t blockSize = 0;
        std::vector<uint8_t> values; // the block as the next draw sends it
        std::vector<VertexAttribute> attributes; // empty = no vertex input
        bool writesClipDistance = false;
        int clipPlaneOffset = -1; // "uClipPlane", replaced while clipping is off
    };

    // GL blend functions the renderer uses.
    enum class Blend : uint8_t {
        Alpha,          // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
        AlphaSeparate,  // the same for colour, ONE, ONE_MINUS_SRC_ALPHA for alpha (the 2D canvas)
    };

    struct PipelineKey {
        const Program* program = nullptr;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool blend = false;
        Blend blendFunc = Blend::Alpha;
        uint8_t colorMask = 0xF;
        bool wireframe = false;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        bool operator==(const PipelineKey&) const = default;
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& key) const noexcept;
    };

    // GL state as the OpenGL renderer leaves it between calls.
    struct DrawState {
        bool depthTest = false;
        bool depthWrite = true;
        VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
        bool blend = false;
        Blend blendFunc = Blend::Alpha;
        bool cull = false;
        bool stencilTest = false;
        VkCompareOp stencilCompare = VK_COMPARE_OP_ALWAYS;
        uint32_t stencilReference = 0;
        uint32_t stencilCompareMask = 0xFF;
        uint32_t stencilWriteMask = 0xFF;
        VkStencilOp stencilFail = VK_STENCIL_OP_KEEP;
        VkStencilOp stencilDepthFail = VK_STENCIL_OP_KEEP;
        VkStencilOp stencilPass = VK_STENCIL_OP_KEEP;
        uint8_t colorMask = 0xF;
        bool polygonOffset = false;
        bool scissorTest = false;
        int scissor[4] = {0, 0, 0, 0}; // GL: x, y from the bottom left, width, height
        bool clipDistance = false;
        int viewport[4] = {0, 0, 0, 0}; // GL: from the bottom left
    };

    struct Mesh {
        Buffer vertices;
        Buffer indices;
        uint32_t indexCount = 0;
        unsigned materialIndex = 0;
    };

    struct ModelMaterial {
        Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
        int texture = -1; // into ModelAsset::textures
    };

    struct ModelDraw {
        unsigned meshIndex = 0;
        float localMatrix[16]{};
        float localNormalMatrix[9]{};
    };

    struct ModelAsset {
        std::vector<Mesh> meshes;
        std::vector<ModelMaterial> materials;
        std::vector<Image> textures;
        std::vector<ModelDraw> draws;
        Uint64 lastUsedTicks = 0;
    };

    struct ModelBatch {
        const ModelAsset* asset = nullptr;
        int firstInstance = 0;
        int instanceCount = 0;
    };

    struct PanelTargetImages {
        Image color;
        Image depth;
        int width = 0;
        int height = 0;
    };
}

class Vulkan final : public RendererCore {
public:
    Vulkan() = default;
    ~Vulkan() override = default;

    bool Initialize(std::string windowName, bool startHidden) override;
    void Shutdown() override;

    void SetWindowVisible(bool visible) override;
    void MakeCurrent() override;
    void SetVSync(int mode) override;
    void SetDrawMode(DrawMode mode) override;

    void BeginFrame() override;
    void Update(bool renderDebug, bool renderUI) override;
    void EndFrame() override;

    void BeginImGuiFrame() const override;
    void EndImGuiFrame() const override;

    void OnResize(int width, int height) override;

    [[nodiscard]] const char* GetName() const override { return "VULKAN"; }
    [[nodiscard]] SDL_Window* GetWindow() const override { return window; }

    bool CreateMap() override;
    void ReloadMap() override;

    void DrawScreenRectangle(Vector2 position, Vector2 size, Vector4 color, const std::string& texture) override;

    void DrawEditor2D(const Editor2D::Frame& frame) override;
    void BeginPanelTarget(PanelTarget target, int width, int height) override;
    void EndPanelTarget() override;
    [[nodiscard]] PanelImage GetPanelImage(PanelTarget target) const override;

    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiBackendNewFrame() override;
    void RenderImGuiDrawData(ImDrawData* drawData, int width, int height) override;
    void RenderImGuiPlatformWindows() override;
    void ClearWindow(Vector4 color, int width, int height) override;

protected:
    // ---- RendererCore hooks (VulkanTextures.cpp) ----
    [[nodiscard]] bool AtlasExists() const override;
    [[nodiscard]] int MaxAtlasPages() const override;
    void CreateAtlasGpu(int pageCount) override;
    void UploadAtlasPage(int page, const unsigned char* pixels) override;
    void FinishAtlasGpu(RendererTextureSettings setting) override;
    void UploadAtlasRegionGpu(int page, int x, int y, int width, int height, const unsigned char* pixels) override;
    void RefreshAtlasGpu(RendererTextureSettings setting) override;

    // The level's UI Sprites and Texts over what's drawn (the end of Update).
    void DrawLevelUI(Level& level);
public:
    void DrawLoadingScreen(Level& uiLevel) override;
protected:
    void DestroyAtlasGpu() override;

    unsigned UploadTextureGpu(const void* pixels, int width, int height) override;
    void DestroyTextureGpu(unsigned id) override;
    [[nodiscard]] ImTextureID TextureImGuiID(unsigned id) const override;

    void AddGlyphPageGpu() override;
    void UploadGlyphGpu(int page, int x, int y, int width, int height, const unsigned char* coverage) override;
    void DestroyGlyphPagesGpu() override;
    void BeginTextGpu(Vector4 color) override;
    void DrawGlyphsGpu(int page, const std::vector<float>& vertices) override;
    void EndTextGpu() override;

private:
    using Buffer = VulkanInternal::Buffer;
    using Image = VulkanInternal::Image;
    using Program = VulkanInternal::Program;
    using DrawState = VulkanInternal::DrawState;

    SDL_Window* window = nullptr;
    bool windowShownOnce = false;
    // Play's overlays (the runtime editor draws in the editor's context).
    // Stays nullptr in the standalone game, which has no ImGui.
    ImGuiContext* imguiContext = nullptr;
    bool initialized = false;

    // ---- Device (VulkanContext.cpp) ----
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VmaAllocator allocator = nullptr;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    float maxAnisotropy = 1.0f;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkImageLayout> swapchainLayouts;
    std::vector<VkSemaphore> renderFinished; // one per swapchain image
    Image windowDepth;
    bool swapchainDirty = false;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    int vsyncMode = -1;

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        std::vector<std::function<void()>> deletions;
        VulkanInternal::Arena arena;
    };

    std::array<Frame, VulkanInternal::FRAMES_IN_FLIGHT> frames;
    int frameIndex = 0;
    bool frameOpen = false;
    bool imageAcquired = false;
    bool windowUnavailable = false; // minimized, or the swapchain couldn't be made
    uint32_t swapchainImageIndex = 0;

    VkCommandPool uploadPool = VK_NULL_HANDLE;

    // Deletions queued outside a frame wait for the next one's fence.
    std::vector<std::function<void()>> pendingDeletions;

    // ---- Bindless set ----
    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE;
    VkDescriptorPool bindlessPool = VK_NULL_HANDLE;
    VkDescriptorSet bindlessSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<uint32_t> freeTextureSlots;
    std::vector<uint32_t> freeArraySlots;
    uint32_t nextTextureSlot = 0;
    uint32_t nextArraySlot = 0;

    // ImGui texture ids: SAMPLED_IMAGE sets laid out as imgui_impl_vulkan's.
    VkDescriptorSetLayout imguiTextureLayout = VK_NULL_HANDLE;
    VkDescriptorPool imguiTexturePool = VK_NULL_HANDLE;
    VkDescriptorPool imguiBackendPool = VK_NULL_HANDLE;
    bool imguiBackendReady = false;

    // Fixed samplers, by what the OpenGL renderer set on its textures.
    enum SamplerSlot : uint32_t {
        SAMPLER_STANDALONE,       // linear mipmapped, nearest mag, bias -0.25, repeat
        SAMPLER_STANDALONE_CLAMP, // the same, clamped (sky cubemap faces)
        SAMPLER_STANDALONE_REPEAT_CLAMP, // repeat S, clamp T (equirectangular sky)
        SAMPLER_LINEAR_REPEAT,    // the 2D canvas
        SAMPLER_LINEAR_CLAMP,     // the 2D canvas, glyphs
        SAMPLER_NEAREST_CLAMP,    // panel images
        SAMPLER_ATLAS_FIRST,      // + RendererTextureSettings, clamped
        SAMPLER_MODEL_FIRST = SAMPLER_ATLAS_FIRST + 8, // + RendererTextureSettings, repeat
        SAMPLER_COUNT = SAMPLER_MODEL_FIRST + 8
    };
    std::array<VkSampler, SAMPLER_COUNT> samplers{};

    Buffer zeroBuffer; // bound in place of an empty buffer, and as the dummy vertex buffer
    Image placeholder2D;    // bindless slot 0 of each kind: black, as GL's texture 0
    Image placeholderArray;
    // Standalone textures ImGui was given: its ids are cached by the editor,
    // so they stay until Shutdown.
    std::vector<Image> retiredImages;

    // ---- Shaders and pipelines (VulkanShaders.cpp) ----
    std::unique_ptr<Program> projectionProgram;
    std::unique_ptr<Program> backgroundProgram;
    std::unique_ptr<Program> uiProgram;
    std::unique_ptr<Program> textProgram;
    std::unique_ptr<Program> editor2DPrograms[6];
    bool editor2DInitTried = false;
    bool editor2DReady = false;

    std::unordered_map<VulkanInternal::PipelineKey, VkPipeline, VulkanInternal::PipelineKeyHash> pipelines;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    // ---- GL-like state ----
    DrawState state;
    Program* program = nullptr;
    std::array<VkDeviceAddress, VulkanInternal::STORAGE_BINDINGS> storage{};
    bool wireframe = false;

    // Where draws go: the window or a panel target.
    struct Target {
        Image* color = nullptr; // nullptr = the swapchain image
        Image* depth = nullptr;
        int width = 0;
        int height = 0;
    };

    VulkanInternal::PanelTargetImages panelTargets[static_cast<int>(PanelTarget::Count)];
    VulkanInternal::PanelTargetImages* activePanelTarget = nullptr;
    bool rendering = false; // inside vkCmdBeginRendering
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    bool descriptorsBound = false;

    // ---- The level's GPU data ----
    std::unordered_map<std::string, VulkanInternal::ModelAsset> modelAssets;
    RendererTextureSettings modelTextureSetting = PIXEL_ART_SHIMMERY;
    std::vector<GpuModelInstance> gpuModelInstances;
    std::vector<VulkanInternal::ModelBatch> modelBatches;
    VkDeviceAddress modelInstanceAddress = 0;

    Image atlas;
    Buffer textureRegionBuffer; // binding 5
    Buffer flatBuffer;          // binding 1, rebuilt when the flat layout changes
    Buffer atlasStaging;        // one page, while the atlas is being filled
    RendererTextureSettings atlasSetting = PIXEL_ART_SHIMMERY;
    std::vector<Image> standaloneTextures; // by GPUTexture::id - 1
    std::vector<unsigned> freeStandaloneIds;
    std::vector<Image> glyphPages;
    Buffer uiQuadVertices;
    Buffer uiQuadIndices;
    bool mapCreated = false;

    // ---- VulkanContext.cpp ----
    bool InitWindow(const std::string& windowName, bool startHidden);
    bool InitDevice();
    bool CreateSwapchain();
    void DestroySwapchain();
    bool InitFrames();
    bool InitBindless();
    bool InitSamplers();
    void DestroyDevice();

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, Buffer& out);
    void DestroyBuffer(Buffer& buffer);
    bool CreateImage(int width, int height, int layers, int mips, VkFormat format, VkImageUsageFlags usage,
                     VkImageAspectFlags aspect, bool arrayView, Image& out);
    void DestroyImage(Image& image);
    // After the GPU is done with whatever this frame recorded.
    void DeferDelete(std::function<void()> deletion);
    void DeferDestroyImage(Image& image);
    void DeferDestroyBuffer(Buffer& buffer);

    uint32_t AllocateTextureSlot(bool array);
    void FreeTextureSlot(uint32_t slot, bool array);
    void WriteTextureSlot(const Image& image, bool array);
    VkDescriptorSet MakeImGuiSet(VkImageView view);

    // Commands outside a frame, waited for before returning.
    VkCommandBuffer BeginOneShot();
    void EndOneShot(VkCommandBuffer commands);

    static void Transition(VkCommandBuffer commands, Image& image, VkImageLayout to);
    static void Transition(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                           VkImageLayout to, uint32_t mips = VK_REMAINING_MIP_LEVELS, uint32_t layers = VK_REMAINING_ARRAY_LAYERS);
    // Fills every mip of `image` from mip 0, then leaves it shader readable.
    static void GenerateMips(VkCommandBuffer commands, Image& image);
    // RGBA8 or R8 pixels into a new image (mips made when `mips` > 1).
    bool UploadImage(const void* pixels, int width, int height, VkFormat format, int mips, Image& out);

    // Opens this frame if it isn't: waits for its slot, resets its arena.
    VkCommandBuffer Commands();
    VulkanInternal::ArenaAllocation ArenaAllocate(VkDeviceSize size, VkDeviceSize alignment = 16);
    VkDeviceAddress UploadFrame(const void* data, size_t bytes);

    bool AcquireWindowImage();
    [[nodiscard]] Target CurrentTarget();
    // Starts rendering to the current target, clearing what's asked.
    void BeginRendering(bool clearColor, Vector4 color, bool clearDepthStencil);
    void EndRendering();
    // glClear(GL_COLOR_BUFFER_BIT) on the current target.
    void ClearColor(Vector4 color);
    // glViewport(0, 0, width, height).
    void SetViewport(int width, int height);

    // ---- VulkanShaders.cpp ----
    // The shaders are named relative to Shaders/ (EmbeddedShaders).
    std::unique_ptr<Program> LoadProgram(const std::string& name, std::string_view vertexShader,
                                         std::string_view fragmentShader, const char* define,
                                         std::vector<VulkanInternal::VertexAttribute> attributes);
    VkPipeline GetPipeline(const VulkanInternal::PipelineKey& key);
    void DestroyPrograms();

    // GL-like calls.
    void UseProgram(Program* next) { program = next; }
    void SetInt(const char* name, int value);
    void SetFloat(const char* name, float value);
    void SetVec2(const char* name, float x, float y);
    void SetVec3(const char* name, float x, float y, float z);
    void SetVec4(const char* name, float x, float y, float z, float w);
    void SetVec4Array(const char* name, const float* values, int count);
    // Row-major Matrix4 data, as glUniformMatrix4fv(..., GL_TRUE, ...) takes it.
    void SetMat4RowMajor(const char* name, const float* rowMajor);
    void SetMat4ColumnMajor(const char* name, const float* columnMajor);
    void SetMat3ColumnMajor(const char* name, const float* columnMajor);
    // A sampler uniform: which image, through which sampler.
    void SetTexture(const char* name, const Image* image, uint32_t sampler);
    void BindStorage(int binding, VkDeviceAddress address) { storage[binding] = address; }

    void Draw(VkPrimitiveTopology topology, uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0);
    void DrawIndexed(const Buffer& vertices, uint32_t vertexStride, const Buffer& indices, uint32_t indexCount, uint32_t instanceCount = 1);
    // Vertices from this frame's arena.
    void DrawVertices(const void* vertices, size_t bytes, uint32_t stride, uint32_t vertexCount);
    // Prepares a draw: rendering, pipeline, dynamic state, uniforms. False when it can't be drawn.
    bool PrepareDraw(VkPrimitiveTopology topology);
    // glClear(GL_DEPTH_BUFFER_BIT) in the middle of drawing.
    void ClearDepth();

    // ---- VulkanScene.cpp ----
    bool UpdateScreenSize();
    void SetSceneCamera(const ComponentCamera& camera, const Vector3& eye);
    void SetupSceneShader(const Level& level, const ComponentCamera& camera, const Vector3& eye);
    void DrawSceneGeometry();
    void DrawFlats();
    void UploadGpuWalls();
    void UploadFlatTriangles();
    void RefreshFlatTrianglesIfLayoutChanged();
    void DrawGatewayViews(const Level& level, const GatewayView& view, int stencilRef, int& passesLeft);
    void DrawGatewayBox(const Gateways::Opening& opening, float depthBehind, bool farDepth, bool whole);
    void SetGatewayClip(const GatewayView& view);
    void DrawBackground(const ComponentCamera& camera, const SkySettings& sky);
    const Image* SkyTexture(const std::string& fileName, Vector2* outSize);
    void DrawNoCameraScreen();
    void DrawUIRectangle(const Vector2& position, const Vector2& size, const Vector4& color, float rotation, const std::string& texture);
    bool InitScenePrograms();

    // ---- VulkanModel.cpp ----
    void BuildGpuModels();
    void DrawGpuModels();
    VulkanInternal::ModelAsset* AcquireModelAsset(const std::string& fileName);
    void ReleaseUnusedModelAssets(Uint64 nowTicks);
    void DestroyModelAsset(VulkanInternal::ModelAsset& asset);
    void DestroyAllModelAssets();

    // ---- VulkanEditor2D.cpp ----
    bool InitEditor2D();
    void DrawEditor2DOverlay(const Editor2D::Frame& frame, int layer, const VkDeviceAddress buffers[]);
    void DrawEditor2DLabels(const Editor2D::Frame& frame, int layer);
    void SetEditor2DClip(const Editor2D::Frame& frame, bool clip);
    const Image* Editor2DTexture(const std::string& fileName);

    // ---- VulkanTextures.cpp ----
    [[nodiscard]] const Image* StandaloneImage(unsigned id) const;
    void DestroyPanelTargets();
    uint32_t AtlasSampler() const { return SAMPLER_ATLAS_FIRST + static_cast<uint32_t>(atlasSetting); }

    // Vulkan.cpp variant parts.
    bool InitImGui();
    void ShutdownImGui();
};

#endif
