//
// Created by berke on 4/10/2026.
//


#include <glad/glad.h>
#include "../../Headers/Renderer/Renderer.hpp"

#include <map>
#include <memory>
#include <SDL3/SDL_init.h>

#include "../../Headers/Objects/Wall.hpp"
#include "../../Headers/Renderer/MapEditor.hpp"

#include "../../Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "../../Headers/Math/Vector/Vector4.hpp"

#include "../../Headers/Objects/Player.hpp"

#include "../../Headers/Renderer/TextureManager.hpp"

#define SCREEN_WIDTH 1680
#define SCREEN_HEIGHT 960

#define DEBUG true

constexpr Uint64 windowFlags = SDL_WINDOW_OPENGL;

std::unique_ptr<Shader> projectionShader;
static GLint playerPosUniform;
static GLint playerAngleUniform;

std::unique_ptr<Shader> debugShader;

std::unique_ptr<Shader> textShader;
static FT_Library ft;
static FT_Face face;

static GLint playerHeightUniform;
static GLint renderModeUniform;

static GLuint flatSSBO = 0;
static GLsizei flatTriangleCount = 0;

constexpr int RENDER_WALL = 0;
constexpr int RENDER_FLAT = 1;

namespace {
    struct Character {
        unsigned int textureID;
        Vector2 Size;
        Vector2 Bearing;
        unsigned int Advance;
    };

    std::map<char, Character> Characters;

    struct GpuFlatTriangle {
        Vector4 a; // x, y, height, unused
        Vector4 b;
        Vector4 c;
        Vector4 color; // r, g, b, a
        Vector4 data; // x = textureIndex
    };

    struct GpuWall {
        Vector4 startEnd; // start.x, start.y, end.x, end.y
        Vector4 color; // r, g, b, a
        Vector4 heights; // floorHeight, ceilingHeight, unused, unused
        Vector4 data; // textureIndex, unused, unused, unused
    };

    std::vector<GpuWall> gpuWalls;
    static GLsizei gpuWallCount = 0;

    bool IsValidSectorIndex(const int index) {
        return index >= 0 && index < static_cast<int>(MapEditor::sectors.size());
    }

    bool IsPortalWall(const Wall &wall) {
        return IsValidSectorIndex(wall.frontSector) &&
               IsValidSectorIndex(wall.backSector);
    }

    void PushGpuWallPiece(
        const Wall &wall,
        const float bottomHeight,
        const float topHeight,
        const Vector4 &color
    ) {
        if (topHeight <= bottomHeight + 0.0001f) {
            return;
        }

        GpuWall gpuWall;

        gpuWall.data = {
            static_cast<float>(wall.textureIndex),
            0.0f,
            0.0f,
            0.0f
        };

        gpuWall.startEnd = {
            wall.start.x,
            wall.start.y,
            wall.end.x,
            wall.end.y
        };

        gpuWall.color = color;

        gpuWall.heights = {
            bottomHeight,
            topHeight,
            0.0f,
            0.0f
        };

        gpuWalls.push_back(gpuWall);
    }

    void BuildGpuWallsFromMap() {
        gpuWalls.clear();

        for (const Wall &wall: MapEditor::walls) {
            // -----------------------------------------------------
            // Portal wall:
            // draw only the height differences between the sectors.
            // -----------------------------------------------------
            if (IsPortalWall(wall)) {
                const Sector &front = MapEditor::sectors[wall.frontSector];
                const Sector &back = MapEditor::sectors[wall.backSector];

                // Lower wall piece, for floor height differences.
                const float lowerBottom = std::min(front.floorHeight, back.floorHeight);
                const float lowerTop = std::max(front.floorHeight, back.floorHeight);

                PushGpuWallPiece(
                    wall,
                    lowerBottom,
                    lowerTop,
                    wall.color
                );

                // Upper wall piece, for ceiling height differences.
                const float upperBottom = std::min(front.ceilingHeight, back.ceilingHeight);
                const float upperTop = std::max(front.ceilingHeight, back.ceilingHeight);

                PushGpuWallPiece(
                    wall,
                    upperBottom,
                    upperTop,
                    wall.color
                );

                continue;
            }

            // -----------------------------------------------------
            // Solid wall:
            // draw from sector floor to sector ceiling.
            // -----------------------------------------------------
            int sectorIndex = -1;

            if (IsValidSectorIndex(wall.frontSector)) {
                sectorIndex = wall.frontSector;
            } else if (IsValidSectorIndex(wall.backSector)) {
                sectorIndex = wall.backSector;
            }

            if (sectorIndex == -1) {
                PushGpuWallPiece(
                    wall,
                    0.0f,
                    32.0f,
                    wall.color
                );

                continue;
            }

            const Sector &sector = MapEditor::sectors[sectorIndex];

            PushGpuWallPiece(
                wall,
                sector.floorHeight,
                sector.ceilingHeight,
                wall.color
            );
        }

        gpuWallCount = static_cast<GLsizei>(gpuWalls.size());
    }

    std::vector<GpuFlatTriangle> flatTriangles;
    std::vector<GpuFlatTriangle> visibleFlatTriangles;

    constexpr float FLAT_NEAR_PLANE = 0.1f;

    constexpr float DEBUG_MAP_SCALE = 200.0f;
    constexpr float DEBUG_PLAYER_HALF_SIZE = 0.01f;
    constexpr float DEBUG_FOV_DEG = 90.0f;
    constexpr float DEBUG_FOV_LINE_LENGTH = 100.0f;

    float DegToRad(const float degrees) {
        return degrees * 3.14159265359f / 180.0f;
    }

    Vector2 RotatePoint(const Vector2 &p, const float angleRad) {
        const float c = std::cos(angleRad);
        const float s = std::sin(angleRad);

        return {
            p.x * c - p.y * s,
            p.x * s + p.y * c
        };
    }

    Vector2 WorldToDebugNdc(const Vector2 &worldPoint, const Vector2 &playerPos) {
        const Vector2 relative = {
            worldPoint.x - playerPos.x,
            worldPoint.y - playerPos.y
        };

        return {
            relative.x / DEBUG_MAP_SCALE,
            relative.y / DEBUG_MAP_SCALE
        };
    }

    float GetViewDepth(const Vector4 &point, const Vector2 &playerPos, const float playerAngle) {
        const Vector2 worldPos = {
            point.x,
            point.y
        };

        const Vector2 relative = {
            worldPos.x - playerPos.x,
            worldPos.y - playerPos.y
        };

        const Vector2 view = RotatePoint(relative, DegToRad(playerAngle));

        return view.y;
    }

    Vector4 LerpVector4(const Vector4 &a, const Vector4 &b, const float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        };
    }

    void ClipFlatTriangleAgainstNearPlane(
        const GpuFlatTriangle &triangle,
        const Vector2 &playerPos,
        const float playerAngle
    ) {
        std::vector<Vector4> input = {
            triangle.a,
            triangle.b,
            triangle.c
        };

        std::vector<Vector4> output;

        for (int i = 0; i < static_cast<int>(input.size()); ++i) {
            const Vector4 current = input[i];
            const Vector4 next = input[(i + 1) % input.size()];

            const float currentDepth = GetViewDepth(current, playerPos, playerAngle);
            const float nextDepth = GetViewDepth(next, playerPos, playerAngle);

            const bool currentInside = currentDepth >= FLAT_NEAR_PLANE;
            const bool nextInside = nextDepth >= FLAT_NEAR_PLANE;

            if (currentInside && nextInside) {
                output.push_back(next);
            } else if (currentInside && !nextInside) {
                const float t = (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);
                output.push_back(LerpVector4(current, next, t));
            } else if (!currentInside && nextInside) {
                const float t = (FLAT_NEAR_PLANE - currentDepth) / (nextDepth - currentDepth);
                output.push_back(LerpVector4(current, next, t));
                output.push_back(next);
            }
        }

        if (output.size() < 3) {
            return;
        }

        if (output.size() == 3) {
            visibleFlatTriangles.push_back({
                output[0],
                output[1],
                output[2],
                triangle.color,
                triangle.data
            });
        } else if (output.size() == 4) {
            visibleFlatTriangles.push_back({
                output[0],
                output[1],
                output[2],
                triangle.color,
                triangle.data
            });

            visibleFlatTriangles.push_back({
                output[0],
                output[2],
                output[3],
                triangle.color,
                triangle.data
            });
        }
    }

    void BuildVisibleFlatTriangles(const Vector2 &playerPos, const float playerAngle) {
        visibleFlatTriangles.clear();

        for (const GpuFlatTriangle &triangle: flatTriangles) {
            ClipFlatTriangleAgainstNearPlane(triangle, playerPos, playerAngle);
        }

        flatTriangleCount = static_cast<GLsizei>(visibleFlatTriangles.size());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);

        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            visibleFlatTriangles.size() * sizeof(GpuFlatTriangle),
            visibleFlatTriangles.empty() ? nullptr : visibleFlatTriangles.data(),
            GL_DYNAMIC_DRAW
        );

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
    }

    void BuildFlatTrianglesFromSectors() {
        flatTriangles.clear();
        visibleFlatTriangles.clear();

        for (const Sector &sector: MapEditor::sectors) {
            const Vector4 floorColor = {
                sector.floorColor.x,
                sector.floorColor.y,
                sector.floorColor.z,
                255.0f
            };

            const Vector4 ceilingColor = {
                sector.ceilingColor.x,
                sector.ceilingColor.y,
                sector.ceilingColor.z,
                255.0f
            };

            for (const Triangle &triangle: sector.triangles) {
                GpuFlatTriangle floorTriangle;

                floorTriangle.a = {triangle.a.x, triangle.a.y, sector.floorHeight, 0.0f};
                floorTriangle.b = {triangle.c.x, triangle.c.y, sector.floorHeight, 0.0f};
                floorTriangle.c = {triangle.b.x, triangle.b.y, sector.floorHeight, 0.0f};
                floorTriangle.color = floorColor;
                floorTriangle.data = {
                    static_cast<float>(sector.floorTextureIndex),
                    0.0f,
                    0.0f,
                    0.0f
                };

                flatTriangles.push_back(floorTriangle);

                GpuFlatTriangle ceilingTriangle;

                ceilingTriangle.a = {triangle.a.x, triangle.a.y, sector.ceilingHeight, 0.0f};
                ceilingTriangle.b = {triangle.b.x, triangle.b.y, sector.ceilingHeight, 0.0f};
                ceilingTriangle.c = {triangle.c.x, triangle.c.y, sector.ceilingHeight, 0.0f};
                ceilingTriangle.color = ceilingColor;
                ceilingTriangle.data = {
                    static_cast<float>(sector.ceilingTextureIndex),
                    0.0f,
                    0.0f,
                    0.0f
                };

                flatTriangles.push_back(ceilingTriangle);
            }
        }

        flatTriangleCount = static_cast<GLsizei>(flatTriangles.size());
    }
}

namespace Renderer {
    // region Initialization
    bool InitializeOpenGL() {
        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4)) {
            SDL_Log("SDL_GL_SetAttribute Major Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)) {
            SDL_Log("SDL_GL_SetAttribute Minor Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)) {
            SDL_Log("SDL_GL_SetAttribute Core Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)) {
            SDL_Log("SDL_GL_SetAttribute Double Error: %s\n", SDL_GetError());
            return false;
        }

        if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)) {
            SDL_Log("SDL_GL_SetAttribute Depth Error: %s\n", SDL_GetError());
            return false;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            SDL_Log("SDL_GL_CreateContext Error: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            window = nullptr;
            return false;
        }

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            SDL_Log("Failed to initialize GLAD");
            return false;
        }
        glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        if (!SDL_GL_SetSwapInterval(1)) {
            SDL_Log("SDL_GL_SetSwapInterval Warning: %s", SDL_GetError());
        }
        return true;
    }

    bool InitializeFont() {
        if (FT_Init_FreeType(&ft)) {
            SDL_Log("FT_Init_FreeType Error");
            return false;
        }
        if (FT_New_Face(ft, "../Assets/Fonts/arial.ttf", 0, &face)) {
            SDL_Log("FT_New_Face Error");
            return false;
        }
        FT_Set_Pixel_Sizes(face, 0, 48);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        for (unsigned char c = 0; c < 128; c++) {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                SDL_Log("Failed to load glyph: %c", c);
                continue;
            }

            unsigned int texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, face->glyph->bitmap.width, face->glyph->bitmap.rows, 0, GL_RED,
                         GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            Character character = {
                texture,
                Vector2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
                Vector2(face->glyph->bitmap_left, face->glyph->bitmap_top),
                static_cast<unsigned int>(face->glyph->advance.x),
            };
            Characters.insert(std::pair<char, Character>(c, character));
        }

        FT_Done_Face(face);
        FT_Done_FreeType(ft);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glGenVertexArrays(1, &textVAO);
        glGenBuffers(1, &textVBO);
        glBindVertexArray(textVAO);
        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        return true;
    }

    bool Initialize() {
        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init Error: %s\n", SDL_GetError());
            return false;
        }

        window = SDL_CreateWindow("Wolfy Engine", SCREEN_WIDTH, SCREEN_HEIGHT, windowFlags);
        if (!window) {
            SDL_Log("SDL_CreateWindow Error: %s\n", SDL_GetError());
            return false;
        }

        if (!InitializeOpenGL()) {
            SDL_Log("Initialize OpenGL Error: %s\n", SDL_GetError());
            return false;
        }

        // --- WALL / PROJECTION SHADER ---
        projectionShader = std::make_unique<Shader>(
            "../Shaders/Wall/wallProjection.vs.glsl",
            "../Shaders/Wall/wallColoring.fs.glsl"
        );
        if (projectionShader->ID == 0) {
            SDL_Log("Projection Shader creation failed");
            return false;
        }

        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);
        glBindVertexArray(0);

        // --- DEBUG SHADER + DEBUG VAO/VBO ---
        if (DEBUG) {
            debugShader = std::make_unique<Shader>(
                "../Shaders/Debug/debug.vs.glsl",
                "../Shaders/Debug/debug.fs.glsl"
            );
            if (debugShader->ID == 0) {
                SDL_Log("Debug Shader creation failed");
                return false;
            }

            glGenVertexArrays(1, &debugVAO);
            glGenBuffers(1, &debugVBO);

            glBindVertexArray(debugVAO);
            glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *) 0);
            glEnableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            debugColorUniform = glGetUniformLocation(debugShader->ID, "uColor");
            if (debugColorUniform == -1) {
                SDL_Log("Failed to get debug shader uniform location uColor");
                return false;
            }
        }

        // --- TEXT SHADER ---
        textShader = std::make_unique<Shader>(
            "../Shaders/Glyph/glyph.vs.glsl",
            "../Shaders/Glyph/glyph.fs.glsl"
        );
        if (textShader->ID == 0) {
            SDL_Log("Text Shader creation failed");
            return false;
        }

        textShader->use();

        Matrix4 projection = Matrix4::Orthographic(
            0.0f, static_cast<float>(SCREEN_WIDTH),
            0.0f, static_cast<float>(SCREEN_HEIGHT),
            -1.0f, 1.0f
        );

        glUniformMatrix4fv(
            glGetUniformLocation(textShader->ID, "projection"),
            1,
            GL_TRUE,
            &projection.m[0][0]
        );

        glUniform1i(glGetUniformLocation(textShader->ID, "text"), 0);

        if (!InitializeFont()) {
            SDL_Log("Failed to initialize font");
            return false;
        }

        SDL_SetWindowRelativeMouseMode(window, true);
        return true;
    }

    // endregion

    void RenderText(const Shader &s, const std::string &text, float x, const float y, const float scale,
                    const Vector3 color) {
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(textVAO);
        glActiveTexture(GL_TEXTURE0);
        s.use();
        glUniform3f(glGetUniformLocation(s.ID, "textColor"), color.x, color.y, color.z);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(textVAO);

        for (char c: text) {
            auto [textureID, Size, Bearing, Advance] = Characters[c];

            const float xPos = x + Bearing.x * scale;
            const float yPos = y - (Size.y - Bearing.y) * scale;

            const float w = Size.x * scale;
            const float h = Size.y * scale;
            // update VBO for each character
            float vertices[6][4] = {
                {xPos, yPos + h, 0.0f, 0.0f},
                {xPos, yPos, 0.0f, 1.0f},
                {xPos + w, yPos, 1.0f, 1.0f},

                {xPos, yPos + h, 0.0f, 0.0f},
                {xPos + w, yPos, 1.0f, 1.0f},
                {xPos + w, yPos + h, 1.0f, 0.0f}
            };
            // render glyph texture over quad
            glBindTexture(GL_TEXTURE_2D, textureID);
            // update content of VBO memory
            glBindBuffer(GL_ARRAY_BUFFER, textVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            // render quad
            glDrawArrays(GL_TRIANGLES, 0, 6);
            // now advance cursors for next glyph (note that advance is number of 1/64 pixels)
            x += (Advance >> 6) * scale; // bitshift by 6 to get value in pixels (2^6 = 64)
        }
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void RenderTextRaw(const std::string &text, const float x, const float y, const float scale, const Vector3 color) {
        RenderText(*textShader, text, x, y, scale, color);
    }

    bool CreateMap() {
        MapEditor::TriangulateSectors();

        BuildFlatTrianglesFromSectors();
        BuildGpuWallsFromMap();

        glGenBuffers(1, &wallSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallSSBO);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            gpuWalls.size() * sizeof(GpuWall),
            gpuWalls.empty() ? nullptr : gpuWalls.data(),
            GL_DYNAMIC_DRAW
        );
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);

        glGenBuffers(1, &flatSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            flatTriangles.size() * sizeof(GpuFlatTriangle),
            flatTriangles.empty() ? nullptr : flatTriangles.data(),
            GL_DYNAMIC_DRAW
        );
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);

        glEnable(GL_PROGRAM_POINT_SIZE);

        projectionShader->use();
        constexpr int MAX_WALL_TEXTURES = 8;

        for (int i = 0; i < MAX_WALL_TEXTURES; ++i) {
            std::string uniformName = "wallTextures[" + std::to_string(i) + "]";

            GLint location = glGetUniformLocation(
                projectionShader->ID,
                uniformName.c_str()
            );

            if (location != -1) {
                glUniform1i(location, i);
            }
        }

        playerPosUniform = glGetUniformLocation(projectionShader->ID, "playerPos");
        if (playerPosUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerPos");
            return false;
        }

        playerAngleUniform = glGetUniformLocation(projectionShader->ID, "playerAngle");
        if (playerAngleUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerAngle");
            return false;
        }

        playerHeightUniform = glGetUniformLocation(projectionShader->ID, "playerHeight");
        if (playerHeightUniform == -1) {
            SDL_Log("Failed to get shader uniform location playerHeight");
            return false;
        }

        renderModeUniform = glGetUniformLocation(projectionShader->ID, "renderMode");
        if (renderModeUniform == -1) {
            SDL_Log("Failed to get shader uniform location renderMode");
            return false;
        }

        return true;
    }

    // region Debug Functions
    void DrawDebugLine(const Vector2 start, const Vector2 end) {
        const float verts[] = {
            start.x, start.y,
            end.x, end.y
        };

        debugShader->use();
        glUniform4f(debugColorUniform, 1.0f, 1.0f, 1.0f, 1.0f);

        glBindVertexArray(debugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDrawArrays(GL_LINES, 0, 2);
    }

    void DrawDebugRect(const Vector2 pos, const float sizeX, const float sizeY) {
        const float verts[] = {
            pos.x - sizeX, pos.y - sizeY, // bottom-left
            pos.x + sizeX, pos.y - sizeY, // bottom-right
            pos.x + sizeX, pos.y + sizeY, // top-right
            pos.x - sizeX, pos.y + sizeY // top-left
        };

        debugShader->use();
        glUniform4f(debugColorUniform, 1.0f, 0.0f, 0.0f, 1.0f);

        glBindVertexArray(debugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }

    // endregion

    void Update(const Vector2 &playerPos, const float playerAngle) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glDisable(GL_CULL_FACE);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        projectionShader->use();
        glBindVertexArray(VAO);

        glUniform2f(playerPosUniform, playerPos.x, playerPos.y);
        glUniform1f(playerAngleUniform, playerAngle);
        glUniform1f(playerHeightUniform, Player::currentEyeHeight);

        BuildVisibleFlatTriangles(playerPos, playerAngle);

        // Bind textures before drawing anything that samples textures.
        TextureManager::BindAllTextures(0);

        // Draw floors and ceilings first
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
        glDepthFunc(GL_LESS);
        glUniform1i(renderModeUniform, RENDER_FLAT);

        glDrawArraysInstanced(
            GL_TRIANGLES,
            0,
            3,
            flatTriangleCount
        );

        // Draw walls after
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
        glDepthFunc(GL_LEQUAL);
        glUniform1i(renderModeUniform, RENDER_WALL);

        glDrawArraysInstanced(
            GL_TRIANGLE_STRIP,
            0,
            4,
            gpuWallCount
        );

        if (!InputManager::GetKey(SDL_SCANCODE_TAB)) {
            return;
        }

        glDisable(GL_DEPTH_TEST);

        DrawDebugRect({0.0f, 0.0f}, DEBUG_PLAYER_HALF_SIZE, DEBUG_PLAYER_HALF_SIZE);

        for (const Wall &wall: MapEditor::walls) {
            const Vector2 start = WorldToDebugNdc(wall.start, playerPos);
            const Vector2 end = WorldToDebugNdc(wall.end, playerPos);

            DrawDebugLine(start, end);
        }

        const float halfFovRad = DegToRad(DEBUG_FOV_DEG * 0.5f);
        const float angleRad = DegToRad(playerAngle);

        constexpr Vector2 baseForward = {0.0f, DEBUG_FOV_LINE_LENGTH};

        Vector2 leftFov = RotatePoint(baseForward, angleRad - halfFovRad);
        Vector2 rightFov = RotatePoint(baseForward, angleRad + halfFovRad);

        leftFov.x *= -1.0f;
        rightFov.x *= -1.0f;

        DrawDebugLine({0.0f, 0.0f}, leftFov);
        DrawDebugLine({0.0f, 0.0f}, rightFov);
    }

    void Destroy() {
        TextureManager::DestroyAll();
        glDeleteBuffers(1, &wallSSBO);
        glDeleteBuffers(1, &flatSSBO);

        glDeleteVertexArrays(1, &VAO);

        if (glContext) {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }

        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();
    }
}
