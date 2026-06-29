#ifndef TILKY_ENGINE_OPENGLRENDERER_HPP
#define TILKY_ENGINE_OPENGLRENDERER_HPP

#include <map>
#include <memory>
#include <string>
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

struct Texture;

namespace OpenGLRendererInternal {
    inline constexpr float FLAT_NEAR_PLANE = 0.1f;

    inline constexpr int RENDER_WALL = 0;
    inline constexpr int RENDER_FLAT = 1;
    inline constexpr int RENDER_SPRITE = 2;
    inline constexpr int RENDER_DECAL = 3;
    inline constexpr int RENDER_COLLIDER = 4;

    constexpr int ATLAS_SIZE = 4096;
    constexpr int ATLAS_PADDING = 2;

    inline constexpr SDL_WindowFlags WINDOW_FLAGS = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

    struct Character {
        unsigned int textureID = 0;
        Vector2 Size;
        Vector2 Bearing;
        unsigned int Advance = 0;
    };

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

        IntVector4 textureIndices0; // N, NE, E, SE
        IntVector4 textureIndices1; // S, SW, W, NW

        Vector4 data;
        // data.x = sprite width / scale.x
        // data.y = sideCount
        // data.z = facingYaw
        // data.w = unused
    };

    struct GpuDecal {
        Vector4 startEnd;
        Vector4 color;
        Vector4 heights;
        Vector4 data;
    };

    struct GpuSector {
        Vector4 heights;
        Vector4 floorColor;
        Vector4 ceilingColor;
        Vector4 textureData;
    };

    struct GpuCollider {
        Vector4 positionType; // World x, y, z; type. 0 = sphere, 1 = AABB
        Vector4 scale; // if sphere, x = radius. if box use vec3 is x y z
    };

    static_assert(sizeof(GpuCollider) == sizeof(float) * 8);

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

    void RenderTextRaw(
        const std::string &text,
        Vector2 position,
        Vector2 scale,
        Vector3 color
    ) override;

    void DrawUIRectangle(
        const Vector2& position,
        const Vector2& size,
        const Vector4& color = Vector4{255.0f, 255.0f, 255.0f, 255.0f},
        float rotation = 0,
        int textureIndex = -1
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

private:
    using Character = OpenGLRendererInternal::Character;
    using GpuFlatTriangle = OpenGLRendererInternal::GpuFlatTriangle;
    using GpuWall = OpenGLRendererInternal::GpuWall;
    using GpuSprite = OpenGLRendererInternal::GpuSprite;
    using GpuDecal = OpenGLRendererInternal::GpuDecal;
    using GpuSector = OpenGLRendererInternal::GpuSector;
    using GPUTexture = OpenGLRendererInternal::GPUTexture;
    using GpuCollider = OpenGLRendererInternal::GpuCollider;

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;

    GLuint VAO = 0;
    GLuint wallSSBO = 0;

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

    GLuint decalSSBO = 0;
    GLsizei decalCount = 0;

    GLuint sectorSSBO = 0;

    GLuint colliderSSBO = 0;
    GLsizei colliderCount = 0;

    std::map<char, Character> Characters;

    std::vector<GpuWall> gpuWalls;
    GLsizei gpuWallCount = 0;

    std::vector<GpuFlatTriangle> flatTriangles;
    std::vector<GpuFlatTriangle> visibleFlatTriangles;

    std::vector<GpuDecal> gpuDecals;
    std::vector<GpuSprite> gpuSprites;
    std::vector<GpuSector> gpuSectors;
    std::vector<GpuCollider> gpuColliders;

    std::vector<GPUTexture> textures;
    GLuint atlasTexture = 0;
    GLuint textureRegionSSBO = 0;

    std::vector<OpenGLRendererInternal::GPUTextureRegion> textureRegions;

    int backgroundTextureIndex = -1;

    bool InitializeOpenGL();
    bool InitializeFont();

    bool InitSDL(const std::string& windowName);
    bool InitImGui() const;
    bool InitProjection();
    bool InitUI();
    bool InitText();

    void BuildGpuSectors();

    void BuildGpuSprites();
    void BuildGpuDecals();
    void BuildGpuColliders();

    void BuildGpuWallsFromMap();
    void UploadGpuWallsFromMap();

    void BuildVisibleFlatTriangles(const Vector2& playerPos, float playerAngle);
    void BuildFlatTrianglesFromSectors();

    void DrawBackground(float playerAngle) const;

    void RefreshTexturesFromLevel();
    [[nodiscard]] const GPUTexture& GetTexture(int index) const;
    [[nodiscard]] int GetTextureCount() const;
    void DestroyAllTextures();
    void RenderUIText(const ComponentUIText& text, const ComponentUITransform& transform);

    static constexpr int SECTOR_FLOOR_COUNT = 3;
    static constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;
};

#endif