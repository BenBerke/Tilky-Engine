#include "../../../Headers/Renderer/Renderer/Renderer.hpp"
#include "RendererInternal.hpp"

namespace Renderer {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;

    GLuint VAO = 0;
    GLuint wallSSBO = 0;

    GLuint debugVAO = 0;
    GLuint debugVBO = 0;
    GLint debugColorUniform = -1;

    GLuint textVAO = 0;
    GLuint textVBO = 0;

    GLuint uiVAO = 0;
    GLuint uiVBO = 0;
    GLuint uiEBO = 0;
}

namespace RendererInternal {
    std::unique_ptr<Shader> projectionShader;
    std::unique_ptr<Shader> debugShader;
    std::unique_ptr<Shader> textShader;
    std::unique_ptr<Shader> uiShader;

    GLint playerPosUniform = -1;
    GLint playerAngleUniform = -1;
    GLint playerHeightUniform = -1;
    GLint playerCamZUniform = -1;
    GLint renderModeUniform = -1;

    FT_Library ft = nullptr;
    FT_Face face = nullptr;

    GLuint flatSSBO = 0;
    GLsizei flatTriangleCount = 0;

    GLuint spriteSSBO = 0;
    GLsizei spriteCount = 0;

    GLuint decalSSBO = 0;
    GLsizei decalCount = 0;

    GLuint sectorSSBO = 0;
    std::vector<GpuSector> gpuSectors;

    std::map<char, Character> Characters;

    std::vector<GpuWall> gpuWalls;
    GLsizei gpuWallCount = 0;

    std::vector<GpuFlatTriangle> flatTriangles;
    std::vector<GpuFlatTriangle> visibleFlatTriangles;

    std::vector<GpuSprite> gpuSprites;

    std::vector<GpuDecal> gpuDecals;

    std::unique_ptr<Shader> backgroundShader = nullptr;
    int backgroundTextureIndex = -1;
}