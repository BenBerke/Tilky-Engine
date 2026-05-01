#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "RendererInternal.hpp"

#include "../../Headers/Math/Vector/Vector3.hpp"

#include <SDL3/SDL_log.h>

namespace RendererInternal {
    bool InitializeFont() {
        if (FT_Init_FreeType(&ft)) {
            SDL_Log("FT_Init_FreeType Error");
            return false;
        }

        if (FT_New_Face(ft, "../Assets/Fonts/arial.ttf", 0, &face)) {
            SDL_Log("FT_New_Face Error");
            return false;
        }

        FT_Set_Pixel_Sizes(face, 0, 48);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        for (unsigned char c = 0; c < 128; c++) {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                SDL_Log("Failed to load glyph: %c", c);
                continue;
            }

            unsigned int texture;
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
        FT_Done_FreeType(ft);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glGenVertexArrays(1, &Renderer::textVAO);
        glGenBuffers(1, &Renderer::textVBO);

        glBindVertexArray(Renderer::textVAO);
        glBindBuffer(GL_ARRAY_BUFFER, Renderer::textVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        return true;
    }
}

namespace Renderer {
    void RenderText(
        const Shader& s,
        const std::string& text,
        float x,
        const float y,
        const float scale,
        const Vector3 color
    ) {
        using namespace RendererInternal;

        glDisable(GL_DEPTH_TEST);

        glBindVertexArray(textVAO);
        glActiveTexture(GL_TEXTURE0);

        s.use();
        glUniform3f(glGetUniformLocation(s.ID, "textColor"), color.x, color.y, color.z);

        for (char c : text) {
            auto [textureID, Size, Bearing, Advance] = Characters[c];

            const float xPos = x + Bearing.x * scale;
            const float yPos = y - (Size.y - Bearing.y) * scale;

            const float w = Size.x * scale;
            const float h = Size.y * scale;

            float vertices[6][4] = {
                {xPos,     yPos + h, 0.0f, 0.0f},
                {xPos,     yPos,     0.0f, 1.0f},
                {xPos + w, yPos,     1.0f, 1.0f},

                {xPos,     yPos + h, 0.0f, 0.0f},
                {xPos + w, yPos,     1.0f, 1.0f},
                {xPos + w, yPos + h, 1.0f, 0.0f}
            };

            glBindTexture(GL_TEXTURE_2D, textureID);

            glBindBuffer(GL_ARRAY_BUFFER, textVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            glDrawArrays(GL_TRIANGLES, 0, 6);

            x += (Advance >> 6) * scale;
        }

        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void RenderTextRaw(
        const std::string& text,
        const float x,
        const float y,
        const float scale,
        const Vector3 color
    ) {
        RenderText(*RendererInternal::textShader, text, x, y, scale, color);
    }
}