#include "RendererInternal.hpp"
#include "Headers/Runtime/Renderer/TextureManager.hpp"
#include "Headers/Objects/Player.hpp"
#include "Headers/Runtime/Renderer/Renderer/Renderer.hpp"

namespace RendererInternal {
    void DrawBackground(const float playerAngle) {
        if (!backgroundShader || backgroundTextureIndex < 0) {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        backgroundShader->use();

        glUniform1f(
            glGetUniformLocation(backgroundShader->ID, "playerAngle"),
            -playerAngle
        );

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(
            GL_TEXTURE_2D,
            TextureManager::GetTexture(backgroundTextureIndex).id
        );

        glBindVertexArray(Renderer::VAO);

        glDrawArrays(
            GL_TRIANGLES,
            0,
            3
        );

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }
}
