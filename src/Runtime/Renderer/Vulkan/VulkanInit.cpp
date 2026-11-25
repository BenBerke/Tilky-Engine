// Start, shutdown and ImGui for the Vulkan renderer. Compiled per executable
// (TILKY_VARIANT_SOURCES): the standalone game has no ImGui.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

#ifndef TILKY_STANDALONE
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "Headers/Editor/EditorStyle.hpp"
#endif

using namespace VulkanInternal;

bool Vulkan::Initialize(const std::string windowName, const bool startHidden) {
    if (!InitWindow(windowName, startHidden)) return false;

    if (!InitDevice()) {
        spdlog::error("Vulkan renderer: no usable Vulkan device");
        return false;
    }

    if (!InitFrames() || !InitBindless() || !InitSamplers()) {
        spdlog::error("Vulkan renderer: setting up frames, descriptors or samplers failed");
        return false;
    }

    // A hidden window may have no size yet; the first frame makes it then.
    if (!CreateSwapchain()) swapchainDirty = true;

    SetViewport(screenWidth, screenHeight);

    if (!InitScenePrograms()) return false;

    RefreshTexturesFromLevel();

    if (!InitializeFontLibrary()) {
        spdlog::critical("Failed to initialize renderer font system");
        return false;
    }

#ifndef TILKY_STANDALONE
    if (!InitImGui()) {
        spdlog::critical("Renderer initialization stopped at InitImGui");
        return false;
    }
#endif

    if (!startHidden) SDL_SetWindowRelativeMouseMode(window, true);

    initialized = true;
    spdlog::info("Vulkan renderer initialized successfully");
    return true;
}

void Vulkan::Shutdown() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        // An open frame's commands are never submitted.
        if (frameOpen) {
            EndRendering();
            vkEndCommandBuffer(frames[frameIndex].commands);
            frameOpen = false;
            imageAcquired = false;
        }

        DestroyAllTextures();
        DestroyAllModelAssets();
        DestroyEditorCamera();
        DestroyFonts();
        DestroyPanelTargets();

        DestroyBuffer(flatBuffer);
        DestroyBuffer(textureRegionBuffer);
        DestroyBuffer(atlasStaging);
        DestroyBuffer(uiQuadVertices);
        DestroyBuffer(uiQuadIndices);
        DestroyImage(atlas);

        for (Image& image : standaloneTextures) DestroyImage(image);
        standaloneTextures.clear();
        freeStandaloneIds.clear();

        ShutdownImGui();
        DestroyPrograms();
    }

    DestroyDevice();

    gpuSectors.clear();
    gpuWalls.clear();
    flatTriangles.clear();
    gpuSprites.clear();
    gpuWallCount = 0;
    flatTriangleCount = 0;
    flatLayoutSignature = 0;
    spriteCount = 0;
    mapCreated = false;
    initialized = false;

    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Vulkan::SetWindowVisible(const bool visible) {
    if (window == nullptr) return;

    if (!visible) {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_HideWindow(window);
        return;
    }

    MakeCurrent();

#ifndef TILKY_STANDALONE
    // The editor's design may have changed since the last session.
    if (imguiContext != nullptr) EditorStyle::Apply(EditorStyle::ActiveDesign());
#endif

    SDL_ShowWindow(window);

    if (!windowShownOnce) {
        // Later sessions reopen at whatever size and position it was left.
        SDL_MaximizeWindow(window);
        windowShownOnce = true;
    }

    SDL_RaiseWindow(window);
}

#ifndef TILKY_STANDALONE
namespace {
    void CheckImGuiVulkanResult(const VkResult result) {
        if (result != VK_SUCCESS) spdlog::error("Vulkan (ImGui): VkResult {}", static_cast<int>(result));
    }
}

void Vulkan::MakeCurrent() {
    if (imguiContext != nullptr) ImGui::SetCurrentContext(imguiContext);
}

// The current ImGui context's platform and renderer backends, drawing into
// this renderer's window.
bool Vulkan::InitImGuiBackend() {
    if (!ImGui_ImplSDL3_InitForVulkan(window)) return false;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;
    info.QueueFamily = queueFamily;
    info.Queue = queue;
    info.DescriptorPoolSize = 256;
    info.MinImageCount = 2;
    info.ImageCount = std::max<uint32_t>(2, static_cast<uint32_t>(swapchainImages.size()));
    info.PipelineCache = pipelineCache;
    info.UseDynamicRendering = true;
    info.CheckVkResultFn = CheckImGuiVulkanResult;

    // The window pass has the depth/stencil attachment the scene uses.
    VkPipelineRenderingCreateInfoKHR& rendering = info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchainFormat;
    rendering.depthAttachmentFormat = depthFormat;
    rendering.stencilAttachmentFormat = depthFormat;

    if (!ImGui_ImplVulkan_Init(&info)) {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    imguiBackendReady = true;
    return true;
}

void Vulkan::ShutdownImGuiBackend() {
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void Vulkan::ImGuiBackendNewFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void Vulkan::RenderImGuiDrawData(ImDrawData* drawData, const int width, const int height) {
    if (drawData == nullptr) return;
    if (activePanelTarget != nullptr) EndPanelTarget();

    SetViewport(width, height);
    if (!rendering) BeginRendering(false, {}, false);
    if (!rendering) return; // minimized

    ImGui_ImplVulkan_RenderDrawData(drawData, Commands());

    // It bound its own pipeline, viewport and scissor.
    boundPipeline = VK_NULL_HANDLE;
    descriptorsBound = false;
}

void Vulkan::RenderImGuiPlatformWindows() {
    // The backend makes, draws and presents each pulled-out panel's window itself.
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

// Only for Play in the engine (the runtime editor draws in the editor's own
// context); the standalone game has no ImGui.
bool Vulkan::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGuiContext* previous = ImGui::GetCurrentContext();
    imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(imguiContext);

    ImGuiIO& io = ImGui::GetIO();
    // As OpenGLInit.cpp: no layout file, keyboard navigation, the editor's font and design.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    std::error_code ec;
    if (const std::filesystem::path textFont = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");
        std::filesystem::exists(textFont, ec)) {
        if (ImFont* uiFont = io.Fonts->AddFontFromFileTTF(textFont.string().c_str(), 18.0f)) io.FontDefault = uiFont;
    }

    EditorStyle::Apply(EditorStyle::ActiveDesign());

    const bool ok = InitImGuiBackend();
    if (previous != nullptr) ImGui::SetCurrentContext(previous);

    if (!ok) {
        ImGui::DestroyContext(imguiContext);
        imguiContext = nullptr;
        return false;
    }

    spdlog::info("Renderer ImGui initialized");
    return true;
}

void Vulkan::ShutdownImGui() {
    if (imguiContext == nullptr) return;

    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* destroyed = imguiContext;
    ImGui::SetCurrentContext(destroyed);
    ShutdownImGuiBackend();
    ImGui::DestroyContext(destroyed);
    imguiContext = nullptr;

    ImGui::SetCurrentContext(previous != destroyed ? previous : nullptr);
}

void Vulkan::BeginImGuiFrame() const {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Vulkan::EndImGuiFrame() const {
    ImGui::Render();
    auto* self = const_cast<Vulkan*>(this);
    self->RenderImGuiDrawData(ImGui::GetDrawData(), self->screenWidth, self->screenHeight);
}
#else
// The game draws no ImGui; these only fill IRenderer's vtable.
void Vulkan::MakeCurrent() {}
bool Vulkan::InitImGuiBackend() { return false; }
void Vulkan::ShutdownImGuiBackend() {}
void Vulkan::ImGuiBackendNewFrame() {}
void Vulkan::RenderImGuiDrawData(ImDrawData*, int, int) {}
void Vulkan::RenderImGuiPlatformWindows() {}
bool Vulkan::InitImGui() { return true; }
void Vulkan::ShutdownImGui() {}
void Vulkan::BeginImGuiFrame() const {}
void Vulkan::EndImGuiFrame() const {}
#endif
