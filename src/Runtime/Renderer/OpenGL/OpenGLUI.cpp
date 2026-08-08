#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include <spdlog/spdlog.h>

void OpenGL::DrawUIRectangle(
    const Vector2& position,
    const Vector2& size,
    const Vector4& color,
    const float rotation,
    const std::string& texture
) const {
    using namespace OpenGLRendererInternal;

    if (uiShader == nullptr) {
        spdlog::critical("DrawUIRectangle failed: uiShader is null");
        return;
    }

    if (uiShader->ID == 0) {
        spdlog::critical("DrawUIRectangle failed: uiShader program ID is 0");
        return;
    }

    if (uiVAO == 0 || glIsVertexArray(uiVAO) == GL_FALSE) {
        spdlog::critical("DrawUIRectangle failed: invalid uiVAO {}", uiVAO);
        return;
    }

    if (size.x <= 0.0f || size.y <= 0.0f) {
        spdlog::critical(
            "DrawUIRectangle failed: invalid size ({}, {}) for texture '{}'",
            size.x,
            size.y,
            texture
        );
        return;
    }

    if (texture.empty()) {
        spdlog::critical(
            "DrawUIRectangle: texture name is empty at position ({}, {})",
            position.x,
            position.y
        );
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    uiShader->use();

    const auto SetUniform2f =
        [&](const char* name, const float x, const float y) {
            const GLint location =
                glGetUniformLocation(uiShader->ID, name);

            if (location == -1) {
                spdlog::critical(
                    "DrawUIRectangle failed: uniform '{}' was not found",
                    name
                );
                return;
            }

            glUniform2f(location, x, y);
        };

    const auto SetUniform1i =
        [&](const char* name, const int value) {
            const GLint location =
                glGetUniformLocation(uiShader->ID, name);

            if (location == -1) {
                spdlog::critical(
                    "DrawUIRectangle failed: uniform '{}' was not found",
                    name
                );
                return;
            }

            glUniform1i(location, value);
        };

    SetUniform2f(
        "uScreenSize",
        static_cast<float>(screenWidth),
        static_cast<float>(screenHeight)
    );

    SetUniform2f("uPosition", position.x, position.y);
    SetUniform2f("uSize", size.x, size.y);

    const GLint colorLocation =
        glGetUniformLocation(uiShader->ID, "uColor");

    if (colorLocation == -1) {
        spdlog::critical(
            "DrawUIRectangle failed: uniform 'uColor' was not found"
        );
    } else {
        glUniform4f(
            colorLocation,
            color.x / 255.0f,
            color.y / 255.0f,
            color.z / 255.0f,
            color.w / 255.0f
        );
    }

    const GLint rotationLocation =
        glGetUniformLocation(uiShader->ID, "rotation");

    if (rotationLocation == -1) {
        spdlog::critical(
            "DrawUIRectangle failed: uniform 'rotation' was not found"
        );
    } else {
        glUniform1f(
            rotationLocation,
            static_cast<GLfloat>(
                rotation * (3.14159265358979323846 / 180.0)
            )
        );
    }

    int textureIndex = -1;

    if (!texture.empty()) {
        textureIndex = GetTextureRegionIndex(texture);
    }

    const bool useTexture =
        textureIndex >= 0 &&
        textureIndex < static_cast<int>(textureRegions.size());

    if (!texture.empty() && textureRegions.empty()) {
        spdlog::critical(
            "DrawUIRectangle failed: textureRegions is empty while drawing '{}'",
            texture
        );
    }

    if (!texture.empty() && textureIndex < 0) {
        spdlog::critical(
            "DrawUIRectangle failed: texture '{}' was not found in the atlas",
            texture
        );
    }

    if (
        textureIndex >= static_cast<int>(textureRegions.size()) &&
        textureIndex >= 0
    ) {
        spdlog::critical(
            "DrawUIRectangle failed: texture '{}' resolved to invalid index {} "
            "with only {} atlas regions",
            texture,
            textureIndex,
            textureRegions.size()
        );
    }

    SetUniform1i("uUseTexture", useTexture ? 1 : 0);

    if (useTexture) {
        if (atlasTexture == 0 || glIsTexture(atlasTexture) == GL_FALSE) {
            spdlog::critical(
                "DrawUIRectangle failed: invalid atlas texture {}",
                atlasTexture
            );
            return;
        }

        if (
            textureRegionSSBO == 0 ||
            glIsBuffer(textureRegionSSBO) == GL_FALSE
        ) {
            spdlog::critical(
                "DrawUIRectangle failed: invalid texture-region SSBO {}",
                textureRegionSSBO
            );
            return;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);

        SetUniform1i("uAtlas", 0);
        SetUniform1i("uTextureIndex", textureIndex);
        SetUniform1i(
            "uTextureCount",
            static_cast<int>(textureRegions.size())
        );

        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER,
            5,
            textureRegionSSBO
        );
    }

    glBindVertexArray(uiVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    const GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        spdlog::critical(
            "DrawUIRectangle OpenGL error: {} while drawing texture '{}'",
            static_cast<unsigned int>(error),
            texture
        );
    }

    glDisable(GL_BLEND);
}