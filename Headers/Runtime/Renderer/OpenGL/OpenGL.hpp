#ifndef TILKY_ENGINE_OPENGLRENDERER_HPP
#define TILKY_ENGINE_OPENGLRENDERER_HPP

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glad/glad.h>
#include <SDL3/SDL.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Headers/Runtime/Renderer/IRenderer.hpp"
#include "Headers/Runtime/Renderer/Shader.hpp"

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector4.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Level.hpp"

struct Texture;

namespace OpenGLRendererInternal {
    inline constexpr float FLAT_NEAR_PLANE = 0.1f;

    inline constexpr int RENDER_WALL = 0;
    inline constexpr int RENDER_FLAT = 1;
    inline constexpr int RENDER_SPRITE = 2;
    inline constexpr int RENDER_COLLIDER = 4;
    inline constexpr int RENDER_MODEL = 5;

    inline constexpr int ATLAS_SIZE = 4096;
    inline constexpr int ATLAS_PADDING = 2;

    inline constexpr float UI_FONT_SIZE = 48.0f;
    inline constexpr float UI_TEXT_PADDING = 8.0f;

    inline constexpr SDL_WindowFlags WINDOW_FLAGS = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;

    // Glyph Characters
    struct Character {
        unsigned int textureID = 0;
        Vector2 Size;
        Vector2 Bearing;
        unsigned int Advance = 0;
    };

    // Structs that will be pushed to the GPU
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
        //data.x = texture region/index;
        //data.y = unused;
        //data.z = texture anchor height;
        //data.w = texture direction;
        Vector4 data2;
        //x = textureOffset.x
        //y = textureOffset.y
        //z = textureScale.x
        //w = textureScale.y
        Vector4 data3;
        //x = flipX
        //y = flipY
        //z = unused
        //w = unused
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

        IntVector4 textureIndices0; // N, NE, E, SE
        IntVector4 textureIndices1; // S, SW, W, NW

        Vector4 data;
        // data.x = sprite width / scale.x
        // data.y = sideCount
        // data.z = forward.x
        // data.w = forward.y

        Vector4 rotation;
        // Quaternion: x, y, z, w

        IntVector4 flags;
        // flags.x = isStatic
    };

    static_assert(sizeof(GpuSprite) == 112);

    struct GpuSector {
        Vector4 floorData; // x = offset into sectorFloors, y = floor count
    };

    struct GpuSectorFloor {
        Vector4 heights;
        Vector4 slopeData;
        Vector4 floorColor;
        Vector4 ceilingColor;
        Vector4 textureData;

        // xy = floor offset, zw = ceiling offset
        Vector4 textureOffsets;

        // xy = floor flip X/Y, zw = ceiling flip X/Y
        Vector4 textureFlip;
        //todo TILKYTODO bitpacking

        // xy = floor scale X/Y, zw = ceiling scale X/Y
        Vector4 textureScales = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct GpuCollider {
        Vector4 positionType; // World x, y, z; type. 0 = sphere, 1 = AABB
        Vector4 scale; // if sphere, x = radius. if box use vec3 is x y z
    };

    static_assert(sizeof(GpuCollider) == sizeof(float) * 8);

    // One entity drawing a model. Matches ModelInstance in Rendering.vs.glsl
    // (std430, SSBO binding 3). Matrices are column major.
    struct alignas(16) GpuModelInstance {
        float modelMatrix[16]{};  // mat4: transform.position * rotation * scale
        float normalMatrix[12]{}; // mat3: std430 pads each column to a vec4
        Vector4 color;            // rgb = sector light, a = 1
    };

    static_assert(sizeof(GpuModelInstance) == 128);

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

    struct GPUTexture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    struct GPUTextureRegion {
        Vector4 uvRect; // x = uMin, y = vMin, z = uMax, w = vMax
        Vector4 data;   // x = valid, y/z/w unused for now
    };

    struct LoadedTextureSurface {
        SDL_Surface* surface = nullptr;
        int textureIndex = -1;
        int x = 0;
        int y = 0;
    };

    // Min/mag filtering and mipmaps for the texture bound to GL_TEXTURE_2D,
    // following the level's texture setting. Shared by the atlas and model
    // textures so both look the same.
    void ApplyTextureSampling(RendererTextureSettings setting);
}

class OpenGL final : public IRenderer {
public:
    OpenGL() = default;
    ~OpenGL() override = default;

    bool Initialize(std::string windowName) override;
    void Shutdown() override;

    void BeginFrame() override;
    void Update(bool renderDebug, bool renderUI) override;
    void EndFrame() override;

    // On resize Window
    void OnResize(int width, int height) override;

    int CreateTexture(const std::string& fileName) override;

    bool CreateMap() override;

    void RenderText(
        const Shader &shader,
        const std::string &text,
        float x,
        float y,
        Vector2 scale,
        Vector3 color
    );

    // Calls the RenderText function with the default text rendering shader
    void RenderTextRaw(
        const std::string &text,
        Vector2 position,
        Vector2 scale,
        Vector3 color
    ) override;

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

    bool BuildTextureAtlasFromLevel();

    void BeginImGuiFrame() const override;
    void EndImGuiFrame() const override;

    [[nodiscard]] ImTextureID GetImGuiTextureID(const std::string& fileName) override;

private:
    using Character = OpenGLRendererInternal::Character;
    using GpuFlatTriangle = OpenGLRendererInternal::GpuFlatTriangle;
    using GpuWall = OpenGLRendererInternal::GpuWall;
    using GpuSprite = OpenGLRendererInternal::GpuSprite;
    using GpuSector = OpenGLRendererInternal::GpuSector;
    using GPUTexture = OpenGLRendererInternal::GPUTexture;
    using GpuCollider = OpenGLRendererInternal::GpuCollider;
    using GpuSectorFloor = OpenGLRendererInternal::GpuSectorFloor;
    using GpuModelInstance = OpenGLRendererInternal::GpuModelInstance;
    using GpuModelAsset = OpenGLRendererInternal::GpuModelAsset;
    using GpuModelBatch = OpenGLRendererInternal::GpuModelBatch;

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;

    GLuint VAO = 0;
    GLuint wallSSBO = 0;
    GLuint sectorFloorSSBO = 0;

    std::vector<GpuSector> gpuSectors;
    std::vector<GpuSectorFloor> gpuSectorFloors;

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

    FT_Library ft = nullptr;
    FT_Face face = nullptr;

    GLuint flatSSBO = 0;
    GLsizei flatTriangleCount = 0;

    GLuint spriteSSBO = 0;
    GLsizei spriteCount = 0;

    GLuint sectorSSBO = 0;

    GLuint colliderSSBO = 0;
    GLsizei colliderCount = 0;

    GLuint modelSSBO = 0;
    GLsizeiptr modelSSBOCapacity = 0;

    GLint modelInstanceOffsetUniform = -1;
    GLint modelLocalUniform = -1;
    GLint modelLocalNormalUniform = -1;
    GLint modelBaseColorUniform = -1;
    GLint modelHasTextureUniform = -1;
    GLint modelTextureUniform = -1;
    GLint cameraWorldPosUniform = -1;

    std::map<char, Character> Characters;

    std::vector<GpuWall> gpuWalls;
    GLsizei gpuWallCount = 0;

    std::vector<GpuFlatTriangle> flatTriangles;

    // Fingerprint of the sector/floor/triangle layout flatTriangles was built
    // from. Floors can be added or removed while the level is live, so the
    // instance list has to follow them.
    size_t flatLayoutSignature = 0;

    std::vector<GpuSprite> gpuSprites;
    std::vector<GpuCollider> gpuColliders;

    // Keyed by ComponentModel::fileName. Assets nobody used for
    // MODEL_UNUSED_LIFETIME_MS are released; failed loads are remembered so a
    // broken reference is not retried every frame (cleared by
    // RefreshTexturesFromLevel, the editor's "assets may have changed" signal).
    std::unordered_map<std::string, GpuModelAsset> modelAssets;
    std::unordered_set<std::string> failedModelFiles;
    RendererTextureSettings modelTextureSetting = PIXEL_ART_SHIMMERY;

    std::vector<GpuModelInstance> gpuModelInstances;
    std::vector<GpuModelInstance> uploadedModelInstances;
    std::vector<GpuModelBatch> modelBatches;

    std::vector<GPUTexture> textures;
    GLuint atlasTexture = 0;
    GLuint textureRegionSSBO = 0;

    std::vector<OpenGLRendererInternal::GPUTextureRegion> textureRegions;
    int backgroundTextureIndex = -1;
    std::unordered_map<std::string, int> textureRegionIndexByName;
    std::unordered_map<std::string, int> textureIndexByName;
    std::string backgroundTextureFileName;

    bool InitializeOpenGL();
    bool InitializeFont();

    bool InitSDL(const std::string& windowName);
    bool InitImGui() const;
    bool InitProjection();
    bool InitUI();
    bool InitText();

    void BuildGpuSectors();
    void BuildGpuSprites();
    void BuildGpuColliders();

    // OpenGLModel.cpp
    void BuildGpuModels();
    void DrawGpuModels() const;
    GpuModelAsset* AcquireModelAsset(const std::string& fileName);
    void ReleaseUnusedModelAssets(Uint64 nowTicks);
    void ApplyModelTextureSampling(RendererTextureSettings setting);
    void DestroyAllModelAssets();

    void BuildGpuWallsFromMap();
    void UploadGpuWallsFromMap();

    void BuildFlatTrianglesFromSectors();
    void RefreshFlatTrianglesIfLayoutChanged();

    void DrawBackground(float pitch, float yaw, float horizontalFov, float parallaxStrength, float backgroundScroll);
    int GetOrCreateTextureIndex(const std::string& fileName);
    int GetTextureRegionIndex(const std::string& fileName) const;

    void RefreshTexturesFromLevel() override;
    [[nodiscard]] const GPUTexture& GetTexture(int index) const;
    [[nodiscard]] int GetTextureCount() const;
    void DestroyAllTextures();

    void RenderUIText(const ComponentUIText& text, const ComponentUITransform& transform);

    static constexpr int SECTOR_FLOOR_COUNT = 3;
    static constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;
};

#endif