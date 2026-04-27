#include "../../../Headers/Renderer/Renderer/Renderer.hpp"
#include "../../../Headers/Renderer/Renderer/RendererInternal.hpp"

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
}

namespace RendererInternal {
    std::unique_ptr<Shader> projectionShader;
    std::unique_ptr<Shader> debugShader;
    std::unique_ptr<Shader> textShader;

    GLint playerPosUniform = -1;
    GLint playerAngleUniform = -1;
    GLint playerHeightUniform = -1;
    GLint renderModeUniform = -1;

    FT_Library ft = nullptr;
    FT_Face face = nullptr;

    GLuint flatSSBO = 0;
    GLsizei flatTriangleCount = 0;

    GLuint spriteSSBO = 0;
    GLsizei spriteCount = 0;

    std::map<char, Character> Characters;

    std::vector<GpuWall> gpuWalls;
    GLsizei gpuWallCount = 0;

    std::vector<GpuFlatTriangle> flatTriangles;
    std::vector<GpuFlatTriangle> visibleFlatTriangles;

    std::vector<GpuSprite> gpuSprites;

    Vector2 testSpritePosition = {100.0f, 100.0f};
}