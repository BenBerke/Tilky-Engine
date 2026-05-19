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

struct Texture;

namespace OpenGLRendererInternal {
    inline constexpr int SCREEN_WIDTH = 1680;
    inline constexpr int SCREEN_HEIGHT = 960;

    inline constexpr float DEBUG_MAP_SCALE = 200.0f;
    inline constexpr float DEBUG_PLAYER_HALF_SIZE = 0.01f;
    inline constexpr float DEBUG_FOV_DEG = 90.0f;
    inline constexpr float DEBUG_FOV_LINE_LENGTH = 100.0f;

    inline constexpr float FLAT_NEAR_PLANE = 0.1f;

    inline constexpr int RENDER_WALL = 0;
    inline constexpr int RENDER_FLAT = 1;
    inline constexpr int RENDER_SPRITE = 2;
    inline constexpr int RENDER_DECAL = 3;

    inline constexpr bool DEBUG_ENABLED = true;

    inline constexpr SDL_WindowFlags WINDOW_FLAGS =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL);

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
    };

    struct GpuSprite {
        Vector4 positionSize;
        Vector4 color;
        Vector4 data;
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

    struct GPUTexture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };
}

class OpenGLRenderer final : public IRenderer {
public:
    OpenGLRenderer() = default;
    ~OpenGLRenderer() override = default;

    bool Initialize() override;
    void Shutdown() override;

    void BeginFrame() override;
    void RenderFrame() override;
    void EndFrame() override;

    void OnResize(int width, int height) override;

    bool CreateMap();
    int CreateTexture(const std::string& fileName) override;

    void RenderText(
    const Shader& shader,
    const std::string& text,
    float x,
    float y,
    float scale,
    Vector3 color
    );

    void RenderTextRaw(
        const std::string& text,
        float x,
        float y,
        float scale,
        Vector3 color
    );

    void DrawUIRectangle(
        const Vector2& position,
        const Vector2& size,
        const Vector4& color = Vector4{255.0f, 255.0f, 255.0f, 255.0f},
        int textureIndex = -1
    ) const;

    [[nodiscard]] SDL_Window* GetWindow() const override {
        return window;
    }

    [[nodiscard]] const char* GetName() const override {
        return "OpenGL";
    }

private:
    using Character = OpenGLRendererInternal::Character;
    using GpuFlatTriangle = OpenGLRendererInternal::GpuFlatTriangle;
    using GpuWall = OpenGLRendererInternal::GpuWall;
    using GpuSprite = OpenGLRendererInternal::GpuSprite;
    using GpuDecal = OpenGLRendererInternal::GpuDecal;
    using GpuSector = OpenGLRendererInternal::GpuSector;
    using GPUTexture = OpenGLRendererInternal::GPUTexture;

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;

    GLuint VAO = 0;
    GLuint wallSSBO = 0;

    GLuint textVAO = 0;
    GLuint textVBO = 0;

    GLuint uiVAO = 0;
    GLuint uiVBO = 0;
    GLuint uiEBO = 0;

    GLuint sectorSSBO = 0;
    std::vector<GpuSector> gpuSectors;

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

    std::map<char, Character> Characters;

    std::vector<GpuWall> gpuWalls;
    GLsizei gpuWallCount = 0;

    std::vector<GpuFlatTriangle> flatTriangles;
    std::vector<GpuFlatTriangle> visibleFlatTriangles;

    std::vector<GpuDecal> gpuDecals;
    std::vector<GpuSprite> gpuSprites;

    std::vector<GPUTexture> textures;

    int backgroundTextureIndex = -1;

    bool InitializeOpenGL();
    bool InitializeFont();

    bool InitSDL();
    bool InitImGui();
    bool InitProjection();
    bool InitUI();
    bool InitText();

    void BuildGpuSectors();

    void BuildGpuSprites();
    void BuildGpuDecals();

    void BuildGpuWallsFromMap();
    void UploadGpuWallsFromMap();

    void BuildVisibleFlatTriangles(const Vector2& playerPos, float playerAngle);
    void BuildFlatTrianglesFromSectors();

    void DrawBackground(float playerAngle) const;

    void RefreshTexturesFromLevel();
    [[nodiscard]] const GPUTexture& GetTexture(int index) const;
    [[nodiscard]] int GetTextureCount() const;
    void BindAllTextures(int firstTextureUnit) const;
    void DestroyAllTextures();

    static constexpr int SECTOR_FLOOR_COUNT = 3;
    static constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;
};

#endif