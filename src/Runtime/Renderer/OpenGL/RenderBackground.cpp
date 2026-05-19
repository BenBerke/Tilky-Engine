#include "Headers/Runtime/Renderer/OpenGL/OpenGLRenderer.hpp"

void OpenGLRenderer::DrawBackground(const float playerAngle) const {
    if (!backgroundShader || backgroundTextureIndex < 0) {
        return;
    }

    if (backgroundTextureIndex >= GetTextureCount()) {
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
        GetTexture(backgroundTextureIndex).id
    );

    glBindVertexArray(VAO);

    glDrawArrays(
        GL_TRIANGLES,
        0,
        3
    );

    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
