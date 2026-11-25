//
// Created by berke on 5/1/2026.
//
// The OpenGL side of text: glyph pages as GL_R8 textures and the quads drawn
// with the text shader. Fonts, glyph packing and layout are RendererCore's
// (Core/RendererCoreText.cpp).

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <vector>

#include <spdlog/spdlog.h>

namespace {
    void SetTextProjection(
        const Shader& shader,
        const int screenWidth,
        const int screenHeight
    ) {
        if (screenWidth <= 0 || screenHeight <= 0) {
            return;
        }

        const float w = static_cast<float>(screenWidth);
        const float h = static_cast<float>(screenHeight);

        // Top-left origin:
        // x: 0 -> left, screenWidth -> right
        // y: 0 -> top,  screenHeight -> bottom
        const float projection[16] = {
            2.0f / w, 0.0f,     0.0f, 0.0f,
            0.0f,    -2.0f / h, 0.0f, 0.0f,
            0.0f,     0.0f,    -1.0f, 0.0f,
           -1.0f,     1.0f,     0.0f, 1.0f
        };

        glUniformMatrix4fv(
            glGetUniformLocation(shader.ID, "projection"),
            1,
            GL_FALSE,
            projection
        );
    }
}

bool OpenGL::InitializeFont() {
    if (!InitializeFontLibrary()) return false;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return true;
}

void OpenGL::AddGlyphPageGpu() {
    using namespace RendererGpu;

    GLuint page = 0;
    glGenTextures(1, &page);
    glBindTexture(GL_TEXTURE_2D, page);

    // Cleared so the padding between glyphs never bleeds old pixels.
    const std::vector<unsigned char> empty(static_cast<size_t>(GLYPH_PAGE_SIZE) * GLYPH_PAGE_SIZE, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GLYPH_PAGE_SIZE, GLYPH_PAGE_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, empty.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glyphPages.push_back(page);
}

void OpenGL::UploadGlyphGpu(const int page, const int x, const int y, const int width, const int height, const unsigned char* coverage) {
    glBindTexture(GL_TEXTURE_2D, glyphPages[page]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RED, GL_UNSIGNED_BYTE, coverage);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void OpenGL::DestroyGlyphPagesGpu() {
    if (!glyphPages.empty()) glDeleteTextures(static_cast<GLsizei>(glyphPages.size()), glyphPages.data());
    glyphPages.clear();
}

void OpenGL::BeginTextGpu(const Vector4 color) {
    textDepthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    textBlendWasEnabled = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const Shader& shader = *textShader;
    shader.use();
    SetTextProjection(shader, screenWidth, screenHeight);

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(shader.ID, "text"), 0);
    glUniform4f(
        glGetUniformLocation(shader.ID, "textColor"),
        std::clamp(color.x, 0.0f, 1.0f),
        std::clamp(color.y, 0.0f, 1.0f),
        std::clamp(color.z, 0.0f, 1.0f),
        std::clamp(color.w, 0.0f, 1.0f)
    );

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
}

void OpenGL::DrawGlyphsGpu(const int page, const std::vector<float>& vertices) {
    glBindTexture(GL_TEXTURE_2D, glyphPages[page]);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));
}

void OpenGL::EndTextGpu() {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!textBlendWasEnabled) glDisable(GL_BLEND);
    if (textDepthWasEnabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
}
