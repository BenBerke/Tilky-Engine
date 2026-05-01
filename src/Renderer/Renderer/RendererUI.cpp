//
// Created by berke on 5/1/2026.
//

#include "RendererInternal.hpp"
#include "../../Headers/Renderer/Renderer/Renderer.hpp"

#include "../../Headers/Math/Vector/Vector2.hpp"
#include "../../Headers/Math/Vector/Vector4.hpp"


namespace Renderer {
    using namespace RendererInternal;
    void DrawUIRectangle(
            const Vector2& position,
            const Vector2& size,
            const Vector4& color
        ) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        uiShader->use();

        glUniform2f(
            glGetUniformLocation(uiShader->ID, "uScreenSize"),
            static_cast<float>(SCREEN_WIDTH),
            static_cast<float>(SCREEN_HEIGHT)
        );

        glUniform2f(
            glGetUniformLocation(uiShader->ID, "uPosition"),
            position.x,
            position.y
        );

        glUniform2f(
            glGetUniformLocation(uiShader->ID, "uSize"),
            size.x,
            size.y
        );

        glUniform4f(
            glGetUniformLocation(uiShader->ID, "uColor"),
            color.x / 255.0f,
            color.y / 255.0f,
            color.z / 255.0f,
            color.w / 255.0f
        );

        glBindVertexArray(uiVAO);

        glDrawElements(
            GL_TRIANGLES,
            6,
            GL_UNSIGNED_INT,
            nullptr
        );

        glBindVertexArray(0);
    }
}