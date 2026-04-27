#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "../../Headers/Renderer/Renderer/RendererInternal.hpp"

namespace RendererInternal {
    void DrawDebugLine(const Vector2 start, const Vector2 end) {
        const float verts[] = {
            start.x, start.y,
            end.x, end.y
        };

        debugShader->use();
        glUniform4f(Renderer::debugColorUniform, 1.0f, 1.0f, 1.0f, 1.0f);

        glBindVertexArray(Renderer::debugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, Renderer::debugVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDrawArrays(GL_LINES, 0, 2);
    }

    void DrawDebugRect(const Vector2 pos, const float sizeX, const float sizeY) {
        const float verts[] = {
            pos.x - sizeX, pos.y - sizeY,
            pos.x + sizeX, pos.y - sizeY,
            pos.x + sizeX, pos.y + sizeY,
            pos.x - sizeX, pos.y + sizeY
        };

        debugShader->use();
        glUniform4f(Renderer::debugColorUniform, 1.0f, 0.0f, 0.0f, 1.0f);

        glBindVertexArray(Renderer::debugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, Renderer::debugVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }
}