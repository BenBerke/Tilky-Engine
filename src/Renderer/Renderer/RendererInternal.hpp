#pragma once

#include <map>
#include <memory>
#include <vector>

#include <glad/glad.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "../../../Headers/Renderer/Shader.hpp"
#include "../../../Headers/Math/Vector/Vector2.hpp"
#include "../../../Headers/Math/Vector/Vector4.hpp"

namespace RendererInternal {
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

    struct Character {
        unsigned int textureID;
        Vector2 Size;
        Vector2 Bearing;
        unsigned int Advance;
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
        // x = start.x
        // y = start.y
        // z = end.x
        // w = end.y

        Vector4 color;
        // r, g, b, a

        Vector4 heights;
        // x = bottom height
        // y = top height

        Vector4 data;
        // x = texture index
    };

    struct GpuSector {
        Vector4 heights;
        // x = floorHeight
        // y = ceilingHeight
        // z = unused
        // w = unused

        Vector4 floorColor;
        // r, g, b, a

        Vector4 ceilingColor;
        // r, g, b, a

        Vector4 textureData;
        // x = floorTextureIndex
        // y = ceilingTextureIndex
        // z = unused
        // w = unused
    };

    extern GLuint sectorSSBO;
    extern std::vector<GpuSector> gpuSectors;

    void BuildGpuSectors();

    extern std::unique_ptr<Shader> projectionShader;
    extern std::unique_ptr<Shader> debugShader;
    extern std::unique_ptr<Shader> textShader;
    extern std::unique_ptr<Shader> uiShader;

    extern GLint playerPosUniform;
    extern GLint playerAngleUniform;
    extern GLint playerHeightUniform;
    extern GLint playerCamZUniform;
    extern GLint renderModeUniform;

    extern FT_Library ft;
    extern FT_Face face;

    extern GLuint flatSSBO;
    extern GLsizei flatTriangleCount;

    extern GLuint spriteSSBO;
    extern GLsizei spriteCount;

    extern GLuint decalSSBO;
    extern GLsizei decalCount;

    extern std::map<char, Character> Characters;

    extern std::vector<GpuWall> gpuWalls;
    extern GLsizei gpuWallCount;

    extern std::vector<GpuFlatTriangle> flatTriangles;
    extern std::vector<GpuFlatTriangle> visibleFlatTriangles;

    extern std::vector<GpuDecal> gpuDecals;

    extern std::vector<GpuSprite> gpuSprites;

    float DegToRad(float degrees);
    Vector2 RotatePoint(const Vector2& p, float angleRad);
    Vector2 WorldToDebugNdc(const Vector2& worldPoint, const Vector2& playerPos);
    float GetViewDepth(const Vector4& point, const Vector2& playerPos, float playerAngle);
    Vector4 LerpVector4(const Vector4& a, const Vector4& b, float t);

    inline constexpr bool DEBUG_ENABLED = true;

    inline constexpr SDL_WindowFlags WINDOW_FLAGS = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL);

    extern std::unique_ptr<Shader> backgroundShader;
    extern int backgroundTextureIndex;

    void DrawBackground(float playerAngle);

    bool InitializeOpenGL();
    bool InitializeFont();

    void BuildGpuSprites();
    void BuildGpuDecals();

    void BuildGpuWallsFromMap();
    void UploadGpuWallsFromMap();

    void BuildVisibleFlatTriangles(const Vector2& playerPos, float playerAngle);
    void BuildFlatTrianglesFromSectors();

    void DrawDebugLine(Vector2 start, Vector2 end);
    void DrawDebugRect(Vector2 pos, float sizeX, float sizeY);
}