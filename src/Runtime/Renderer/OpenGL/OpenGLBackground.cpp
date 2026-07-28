#include "Headers/Editor/Editor.hpp"
#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

void OpenGL::DrawBackground(const float playerAngle) {
    const std::string& fileName = Editor::backgroundTextureFileName;

    if (backgroundShader == nullptr || fileName.empty()) return;

    const int textureIndex = GetOrCreateTextureIndex(fileName);
    if (textureIndex < 0 || textureIndex >= GetTextureCount()) {
        spdlog::error("Failed to resolve background texture '{}'", fileName);
        return;
    }

    const GLuint textureID = GetTexture(textureIndex).id;
    if (textureID == 0) return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    backgroundShader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glUniform1i(
        glGetUniformLocation(backgroundShader->ID, "backgroundTexture"),
        0
    );

    glUniform1f(
        glGetUniformLocation(backgroundShader->ID, "playerAngle"),
        -playerAngle
    );

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
}