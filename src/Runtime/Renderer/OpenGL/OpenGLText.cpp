//
// Created by berke on 5/1/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

bool OpenGL::InitializeFont() {
    if (FT_Init_FreeType(&ft)) {
        spdlog::critical("FT_Init_FreeType failed. FreeType could not be initialized.");
        return false;
    }

    const auto fontPath =
        ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

    if (FT_New_Face(ft, fontPath.string().c_str(), 0, &face)) {
        spdlog::critical(
            "FT_New_Face failed. Could not load font face. Tried path: {}",
            fontPath.string()
        );

        FT_Done_FreeType(ft);
        ft = nullptr;
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, 48);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int failedGlyphCount = 0;

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            spdlog::warn(
                "Failed to load glyph '{}'. Text rendering may miss this character.",
                static_cast<char>(c)
            );

            failedGlyphCount++;
            continue;
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            Vector2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            Vector2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x),
        };

        Characters.insert(std::pair<char, Character>(c, character));
    }

    FT_Done_Face(face);
    face = nullptr;

    FT_Done_FreeType(ft);
    ft = nullptr;

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

    if (failedGlyphCount > 0) {
        spdlog::warn(
            "Font initialized, but {} glyph(s) failed to load.",
            failedGlyphCount
        );
    } else {
        spdlog::info(
            "Font initialized successfully from: {}",
            fontPath.string()
        );
    }

    return true;
}

void OpenGL::RenderText(
    const Shader& shader,
    const std::string& text,
    float x,
    const float y,
    const Vector2 scale,
    const Vector3 color
) {
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader.use();

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(shader.ID, "text"), 0);

    glUniform3f(
        glGetUniformLocation(shader.ID, "textColor"),
        color.x / 255.0f,
        color.y / 255.0f,
        color.z / 255.0f
    );

    glBindVertexArray(textVAO);

    for (char c : text) {
        auto characterIt = Characters.find(c);

        if (characterIt == Characters.end()) {
            spdlog::warn(
                "Tried to render missing glyph '{}'. Skipping character.",
                c
            );
            continue;
        }

        const auto& [textureID, Size, Bearing, Advance] = characterIt->second;

        const float xPos = x + Bearing.x * scale.x;
        const float yPos = y - Bearing.y * scale.y;

        const float w = Size.x * scale.x;
        const float h = Size.y * scale.y;

        const float vertices[6][4] = {
            {xPos,     yPos,     0.0f, 0.0f},
            {xPos,     yPos + h, 0.0f, 1.0f},
            {xPos + w, yPos + h, 1.0f, 1.0f},

            {xPos,     yPos,     0.0f, 0.0f},
            {xPos + w, yPos + h, 1.0f, 1.0f},
            {xPos + w, yPos,     1.0f, 0.0f}
        };

        glBindTexture(GL_TEXTURE_2D, textureID);

        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += static_cast<float>(Advance >> 6) * scale.x;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void OpenGL::RenderTextRaw(
    const std::string& text,
    const Vector2 position,
    const Vector2 scale,
    const Vector3 color
) {
    RenderText(*textShader, text, position.x, position.y, scale, color);
}

void OpenGL::RenderUIText(
    const ComponentUIText& text,
    const ComponentUITransform& transform
) {
    constexpr float fontPixelSize = 48.0f;
    constexpr float padding = 8.0f;
    constexpr float fontScale = 1.0f;

    const Vector2 textPosition = {
        transform.resolvedPosition.x + padding,
        transform.resolvedPosition.y + padding + fontPixelSize * fontScale
    };

    RenderTextRaw(
        text.text,
        textPosition,
        {fontScale, fontScale},
        {255.0f, 255.0f, 255.0f}
    );
}