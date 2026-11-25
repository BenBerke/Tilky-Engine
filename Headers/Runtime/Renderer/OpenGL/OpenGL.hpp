#ifndef TILKY_ENGINE_OPENGLRENDERER_HPP
#define TILKY_ENGINE_OPENGLRENDERER_HPP

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glad/glad.h>
#include <SDL3/SDL.h>

#include "Headers/Runtime/Renderer/RendererCore.hpp"
#include "Headers/Runtime/Renderer/Shader.hpp"

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector4.hpp"
#include "Headers/Map/Gateways.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Level.hpp"

struct Texture;

namespace OpenGLRendererInternal {
    // The GPU structs and constants are shared with every backend.
    using namespace RendererGpu;

    inline constexpr SDL_WindowFlags WINDOW_FLAGS = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;

    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
        unsigned materialIndex = 0;
    };

    struct GpuModelMaterial {
        Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
        GLuint texture = 0; // 0 = untextured
    };

    // One mesh placed by one model node, column major.
    struct GpuModelDraw {
        unsigned meshIndex = 0;
        float localMatrix[16]{};
        float localNormalMatrix[9]{};
    };

    // GPU resources of one model file, shared by every entity that uses it.
    // Vertex data is uploaded once and never changes.
    struct GpuModelAsset {
        std::vector<GpuMesh> meshes;
        std::vector<GpuModelMaterial> materials;
        std::vector<GLuint> textures;
        std::vector<GpuModelDraw> draws;
        Uint64 lastUsedTicks = 0;
    };

    // Every entity using one asset this frame; their instances are contiguous
    // in the model SSBO starting at firstInstance.
    struct GpuModelBatch {
        const GpuModelAsset* asset = nullptr;
        GLint firstInstance = 0;
        GLsizei instanceCount = 0;
    };

    // Min/mag filtering and mipmaps for the texture bound to `target`,
    // following the level's texture setting. Shared by the atlas (a
    // GL_TEXTURE_2D_ARRAY, one layer per page) and model textures so both
    // look the same.
    void ApplyTextureSampling(RendererTextureSettings setting, GLenum target = GL_TEXTURE_2D);
}

class OpenGL final : public RendererCore {
public:
    OpenGL() = default;
    ~OpenGL() override = default;

    bool Initialize(std::string windowName, bool startHidden) override;
    void Shutdown() override;

    void SetWindowVisible(bool visible) override;
    void MakeCurrent() override;
    void SetVSync(int mode) override;
    void SetDrawMode(DrawMode mode) override;

    void BeginFrame() override;
    void Update(bool renderDebug, bool renderUI) override;
    void EndFrame() override;

    // On resize Window
    void OnResize(int width, int height) override;

    bool CreateMap() override;
    void ReloadMap() override;

    void DrawScreenRectangle(Vector2 position, Vector2 size, Vector4 color, const std::string& texture) override;

    void DrawUIRectangle(
        const Vector2 &position,
        const Vector2 &size,
        const Vector4 &color,
        float rotation,
        const std::string &texture
    ) const;

    [[nodiscard]] SDL_Window* GetWindow() const override {
        return window;
    }

    [[nodiscard]] const char* GetName() const override {
        return "OPENGL";
    }

    void BeginImGuiFrame() const override;
    void EndImGuiFrame() const override;

    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiBackendNewFrame() override;
    void RenderImGuiDrawData(ImDrawData* drawData, int width, int height) override;
    void RenderImGuiPlatformWindows() override;
    void ClearWindow(Vector4 color, int width, int height) override;

protected:
    [[nodiscard]] bool AtlasExists() const override;
    [[nodiscard]] int MaxAtlasPages() const override;
    void CreateAtlasGpu(int pageCount) override;
    void UploadAtlasPage(int page, const unsigned char* pixels) override;
    void FinishAtlasGpu(RendererTextureSettings setting) override;
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
    using GpuEditorGizmoVertex = RendererGpu::GpuEditorGizmoVertex;
    using GpuModelAsset = OpenGLRendererInternal::GpuModelAsset;
    using GpuModelBatch = OpenGLRendererInternal::GpuModelBatch;

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    // Play's overlays (the runtime editor draws in the editor's context).
    // Stays nullptr in the standalone game, which has no ImGui.
    ImGuiContext* imguiContext = nullptr;
    bool windowShownOnce = false;

    GLuint VAO = 0;
    GLuint wallSSBO = 0;
    GLuint sectorFloorSSBO = 0;

    GLuint textVAO = 0;
    GLuint textVBO = 0;

    GLuint uiVAO = 0;
    GLuint uiVBO = 0;
    GLuint uiEBO = 0;

    std::unique_ptr<Shader> projectionShader;
    std::unique_ptr<Shader> textShader;
    std::unique_ptr<Shader> uiShader;
    std::unique_ptr<Shader> backgroundShader;

    GLint renderModeUniform = -1;
    GLint viewUniform = -1;
    GLint projectionUniform = -1;

    // ---- Text (OpenGLText.cpp) ----
    std::vector<GLuint> glyphPages; // GL_TEXTURE_2D, GL_R8
    GLboolean textDepthWasEnabled = GL_FALSE;
    GLboolean textBlendWasEnabled = GL_FALSE;

    GLuint flatSSBO = 0;
    GLuint spriteSSBO = 0;
    GLuint sectorSSBO = 0;
    GLuint colliderSSBO = 0;
    GLuint entityMarkerSSBO = 0;
    GLuint editorGizmoSSBO = 0;

    GLuint modelSSBO = 0;
    GLsizeiptr modelSSBOCapacity = 0;

    GLint modelInstanceOffsetUniform = -1;
    GLint modelLocalUniform = -1;
    GLint modelLocalNormalUniform = -1;
    GLint modelBaseColorUniform = -1;
    GLint modelHasTextureUniform = -1;
    GLint modelTextureUniform = -1;
    GLint cameraWorldPosUniform = -1;
    GLint fullbrightUniform = -1;
    GLint distanceLightStartUniform = -1;
    GLint distanceLightEndUniform = -1;
    GLint distanceLightDarkestUniform = -1;
    GLint viewportSizeUniform = -1;

    GLint flatInstanceOffsetUniform = -1;

    GLint clipPlaneUniform = -1;
    GLint gatewayCornersUniform = -1;
    GLint gatewayFarDepthUniform = -1;

    // The window got a stencil buffer. Without one gateways draw black.
    bool hasStencil = false;

    // Keyed by ComponentModel::fileName. Assets nobody used for
    // MODEL_UNUSED_LIFETIME_MS are released; failed loads are remembered so a
    // broken reference is not retried every frame (cleared by
    // RefreshTexturesFromLevel, the editor's "assets may have changed" signal).
    std::unordered_map<std::string, GpuModelAsset> modelAssets;
    RendererTextureSettings modelTextureSetting = PIXEL_ART_SHIMMERY;

    std::vector<GpuModelInstance> gpuModelInstances;
    std::vector<GpuModelInstance> uploadedModelInstances;
    std::vector<GpuModelBatch> modelBatches;

    GLuint atlasTexture = 0; // GL_TEXTURE_2D_ARRAY, one ATLAS_SIZE layer per page
    GLuint textureRegionSSBO = 0;

    bool InitializeOpenGL();

    bool InitSDL(const std::string& windowName, bool startHidden);
    bool InitImGui(); // editor only; not defined in the standalone game
    bool InitProjection();
    bool InitUI();
    bool InitText();

    // glBufferData of `bytes` into `buffer`, bound at `binding`.
    static void UploadSSBO(GLuint buffer, GLuint binding, const void* data, size_t bytes);

    // OpenGLModel.cpp
    void BuildGpuModels();
    void DrawGpuModels() const;
    GpuModelAsset* AcquireModelAsset(const std::string& fileName);
    void ReleaseUnusedModelAssets(Uint64 nowTicks);
    void ApplyModelTextureSampling(RendererTextureSettings setting);
    void DestroyAllModelAssets();

    void UploadGpuWallsFromMap();
    void RefreshFlatTrianglesIfLayoutChanged();

    void DrawFlats() const;

    // OpenGLUpdate.cpp: one view of the world - the camera's own, or one
    // seen through a gateway. The view's uniforms, then flats, walls,
    // models and sprites as the current visibility allows.
    void SetSceneCamera(const ComponentCamera& camera, const Vector3& eye) const;
    void SetupSceneShader(const Level& level, const ComponentCamera& camera, const Vector3& eye);
    void DrawSceneGeometry();

    // OpenGLGateway.cpp: everything seen through the gateways visible in
    // the view (recursively, Rendering > Gateway Depth deep), into the
    // stencil buffer's `stencilRef` area of the screen.
    void DrawGatewayViews(const Level& level, const GatewayView& view, int stencilRef, int& passesLeft);
    // The opening as a box reaching a little behind its wall, so a camera
    // right at the wall still has its view through it. farDepth puts it at
    // the far plane instead of where it is. Not whole: its front face alone.
    void DrawGatewayBox(const Gateways::Opening& opening, float depthBehind, bool farDepth, bool whole) const;
    void SetGatewayClip(const GatewayView& view) const;

    // In game, when there's no active camera or its entity is disabled: a
    // black screen saying so.
    void DrawNoCameraScreen();

    // OpenGLBackground.cpp: the level's sky, drawn before the world.
    void DrawBackground(const ComponentCamera& camera, const SkySettings& sky);
    GLuint GetSkyTextureID(const std::string& fileName, GLint wrapS, GLint wrapT, Vector2* outSize);

    bool InitializeFont();

    // 2D editor canvas (OpenGLEditor2D.cpp). Made on the first
    // DrawEditor2D, so the game never builds them. Present in every build
    // so the class layout is the same in editor and standalone objects.
    enum Editor2DPass { EDITOR2D_SECTOR, EDITOR2D_WALL, EDITOR2D_LINEDEF, EDITOR2D_ENTITY, EDITOR2D_COLLIDER, EDITOR2D_OVERLAY, EDITOR2D_PASS_COUNT };
    enum Editor2DBuffer { EDITOR2D_BUFFER_SECTORS, EDITOR2D_BUFFER_SECTOR_TRIANGLES, EDITOR2D_BUFFER_SECTOR_FLAGS,
                          EDITOR2D_BUFFER_WALLS, EDITOR2D_BUFFER_WALL_FLAGS, EDITOR2D_BUFFER_LINEDEFS, EDITOR2D_BUFFER_LINEDEF_FLAGS,
                          EDITOR2D_BUFFER_ENTITIES, EDITOR2D_BUFFER_ENTITY_FLAGS, EDITOR2D_BUFFER_COLLIDERS, EDITOR2D_BUFFER_COLLIDER_FLAGS,
                          EDITOR2D_BUFFER_OVERLAY, EDITOR2D_BUFFER_LABEL_OVERLAY, EDITOR2D_BUFFER_COUNT };
    GLuint editor2DPrograms[EDITOR2D_PASS_COUNT] = {};
    GLuint editor2DBuffers[EDITOR2D_BUFFER_COUNT] = {};
    GLuint editor2DVAO = 0;
    GLuint editor2DSamplerRepeat = 0;
    GLuint editor2DSamplerClamp = 0;
    bool editor2DInitTried = false;
    bool editor2DReady = false;

    bool InitEditor2D();
    void DestroyEditor2D();

    // The editor panels' offscreen targets (OpenGL.cpp), one per
    // PanelTarget. In every build, like the 2D editor's members, so the
    // layout is the same everywhere.
    struct PanelTargetBuffers {
        GLuint framebuffer = 0;
        GLuint colorTexture = 0;
        GLuint depthStencil = 0;
        int width = 0;
        int height = 0;
    };

    PanelTargetBuffers panelTargets[static_cast<int>(PanelTarget::Count)];
    // The bound one, or nullptr while drawing to the window.
    const PanelTargetBuffers* activePanelTarget = nullptr;

    static void DestroyPanelTarget(PanelTargetBuffers& target);
    void DestroyPanelTargets();
    // screenWidth/screenHeight for this frame: the panel target's size while
    // one is bound, else the window's. False if the window size can't be read.
    bool UpdateScreenSize();
    void DrawEditor2DOverlay(const Editor2D::Frame& frame, int layer);
    // The labels of one Layer (Layer::Count = those drawn after everything).
    void DrawEditor2DLabels(const Editor2D::Frame& frame, int layer);
    void SetEditor2DClip(const Editor2D::Frame& frame, bool clip);
    GLuint Editor2DTexture(const std::string& fileName);

public:
    void DrawEditor2D(const Editor2D::Frame& frame) override;
    void BeginPanelTarget(PanelTarget target, int width, int height) override;
    void EndPanelTarget() override;
    [[nodiscard]] PanelImage GetPanelImage(PanelTarget target) const override;
};

#endif