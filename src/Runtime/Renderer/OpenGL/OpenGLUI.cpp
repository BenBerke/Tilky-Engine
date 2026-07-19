//
// Created by berke on 5/1/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

void OpenGL::DrawUIRectangle(
    const Vector2& position,
    const Vector2& size,
    const Vector4& color,
    const float rotation,
    const int textureIndex
) const {
    using namespace OpenGLRendererInternal;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    uiShader->use();

    glUniform2f(glGetUniformLocation(
        uiShader->ID, "uScreenSize"),
        static_cast<float>(screenWidth),
        static_cast<float>(screenHeight)
    );

    glUniform2f(glGetUniformLocation(uiShader->ID, "uPosition"), position.x, position.y);

    glUniform2f(glGetUniformLocation(uiShader->ID, "uSize"), size.x, size.y);

    glUniform4f(
        glGetUniformLocation(uiShader->ID, "uColor"),
        color.x / 255.0f,
        color.y / 255.0f,
        color.z / 255.0f,
        color.w / 255.0f
    );

    glUniform1f(
        glGetUniformLocation(uiShader->ID, "rotation"),
        static_cast<GLfloat>(rotation * (3.14159265358979323846 / 180.f))
    );


    const bool useTexture = textureIndex >= 0 && textureIndex < static_cast<int>(textureRegions.size());

    glUniform1i(glGetUniformLocation(uiShader->ID, "uUseTexture"), useTexture ? 1 : 0);

    if (useTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);

        glUniform1i(glGetUniformLocation(uiShader->ID, "uAtlas"),0);

        glUniform1i( glGetUniformLocation(uiShader->ID, "uTextureIndex"), textureIndex);

        glUniform1i(glGetUniformLocation(uiShader->ID, "uTextureCount"), static_cast<int>(textureRegions.size()));

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);
    }

    glBindVertexArray(uiVAO);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);

    glDisable(GL_BLEND);
}
