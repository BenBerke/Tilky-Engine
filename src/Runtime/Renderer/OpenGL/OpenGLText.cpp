//
// Created by berke on 5/1/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

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

    constexpr char32_t REPLACEMENT_CHARACTER = 0xFFFD;

    // Decodes the UTF-8 sequence at text[i] and moves i past it. Malformed
    // bytes, overlong forms and surrogates come back as U+FFFD.
    char32_t NextCodepoint(const std::string& text, size_t& i) {
        const auto byteAt = [&text](const size_t index) { return static_cast<unsigned char>(text[index]); };

        const unsigned char lead = byteAt(i);
        if (lead < 0x80) {
            ++i;
            return lead;
        }

        int extra = 0;
        char32_t codepoint = 0;
        if ((lead & 0xE0) == 0xC0) { extra = 1; codepoint = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { extra = 2; codepoint = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { extra = 3; codepoint = lead & 0x07; }
        else {
            ++i;
            return REPLACEMENT_CHARACTER;
        }

        if (i + extra >= text.size()) { // cut off at the end of the string
            i = text.size();
            return REPLACEMENT_CHARACTER;
        }

        for (int k = 1; k <= extra; ++k) {
            const unsigned char continuation = byteAt(i + k);
            if ((continuation & 0xC0) != 0x80) {
                ++i;
                return REPLACEMENT_CHARACTER;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        i += extra + 1;

        constexpr char32_t smallestForLength[] = {0, 0x80, 0x800, 0x10000};
        if (codepoint < smallestForLength[extra] || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return REPLACEMENT_CHARACTER;

        return codepoint;
    }

    unsigned PixelSizeFor(const float size) {
        if (!(size > 0.0f)) return 0;
        return std::clamp(static_cast<unsigned>(std::lround(size)), 1u, OpenGLRendererInternal::MAX_GLYPH_PIXEL_SIZE);
    }

    // The glyph bitmap FreeType just rendered, as one byte of coverage per pixel.
    std::vector<unsigned char> CoverageFromBitmap(const FT_Bitmap& bitmap) {
        const int width = static_cast<int>(bitmap.width);
        const int height = static_cast<int>(bitmap.rows);
        std::vector<unsigned char> coverage(static_cast<size_t>(width) * height, 0);

        const int pitch = bitmap.pitch;
        const auto row = [&](const int r) {
            return pitch >= 0 ? bitmap.buffer + r * pitch : bitmap.buffer + (height - 1 - r) * -pitch;
        };

        for (int r = 0; r < height; ++r) {
            const unsigned char* source = row(r);
            unsigned char* target = coverage.data() + static_cast<size_t>(r) * width;

            switch (bitmap.pixel_mode) {
                case FT_PIXEL_MODE_GRAY:
                    std::copy_n(source, width, target);
                    break;
                case FT_PIXEL_MODE_MONO:
                    for (int x = 0; x < width; ++x)
                        target[x] = (source[x >> 3] >> (7 - (x & 7))) & 1 ? 255 : 0;
                    break;
                case FT_PIXEL_MODE_BGRA: // colour fonts: keep the shape, the text colour comes from the component
                    for (int x = 0; x < width; ++x) target[x] = source[x * 4 + 3];
                    break;
                default:
                    break;
            }
        }

        return coverage;
    }
}

// ============================================================================
// Fonts
// ============================================================================

bool OpenGL::InitializeFont() {
    if (FT_Init_FreeType(&ft)) {
        spdlog::critical("FT_Init_FreeType failed. FreeType could not be initialized.");
        return false;
    }

    const auto fontPath =
        ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

    if (FT_New_Face(ft, fontPath.string().c_str(), 0, &defaultFontFace)) {
        spdlog::critical(
            "FT_New_Face failed. Could not load font face. Tried path: {}",
            fontPath.string()
        );

        defaultFontFace = nullptr;
        FT_Done_FreeType(ft);
        ft = nullptr;
        return false;
    }

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

    spdlog::info("Font initialized successfully from: {}", fontPath.string());
    return true;
}

// A UI Text's Font field: a path inside Assets with its extension, or empty
// for the default font. Fonts that fail to load fall back to the default.
FT_Face OpenGL::GetFontFace(const std::string& fontReference) {
    if (fontReference.empty() || ft == nullptr) return defaultFontFace;

    const std::string path = (ProjectManager::GetAssetsPath() / fontReference).lexically_normal().generic_string();

    if (const auto it = fontFaces.find(path); it != fontFaces.end())
        return it->second != nullptr ? it->second : defaultFontFace;

    FT_Face face = nullptr;
    if (FT_New_Face(ft, path.c_str(), 0, &face)) {
        spdlog::error("Could not load font '{}' ({}). Using the default font.", fontReference, path);
        fontFaces.emplace(path, nullptr);
        return defaultFontFace;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    fontFaces.emplace(path, face);
    spdlog::info("Loaded font: {}", path);
    return face;
}

void OpenGL::SetFontPixelSize(const FT_Face face, const unsigned pixelSize) {
    if (face->size != nullptr && face->size->metrics.y_ppem == pixelSize) return;
    FT_Set_Pixel_Sizes(face, 0, pixelSize);
}

// The editor's "assets may have changed" signal: fonts are reread from disk,
// and ones that failed before get another try.
void OpenGL::ReloadFonts() {
    for (auto& face : fontFaces | std::views::values)
        if (face != nullptr) FT_Done_Face(face);
    fontFaces.clear();
    warnedMissingCharacters.clear();
    ResetGlyphAtlas();
}

void OpenGL::DestroyFonts() {
    ResetGlyphAtlas();

    for (auto& face : fontFaces | std::views::values)
        if (face != nullptr) FT_Done_Face(face);
    fontFaces.clear();

    if (defaultFontFace != nullptr) {
        FT_Done_Face(defaultFontFace);
        defaultFontFace = nullptr;
    }

    if (ft != nullptr) {
        FT_Done_FreeType(ft);
        ft = nullptr;
    }
}

// ============================================================================
// Glyph atlas
// ============================================================================

void OpenGL::AddGlyphPage() {
    using namespace OpenGLRendererInternal;

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
    glyphCursorX = 0;
    glyphCursorY = 0;
    glyphShelfHeight = 0;
}

void OpenGL::ResetGlyphAtlas() {
    if (!glyphPages.empty()) glDeleteTextures(static_cast<GLsizei>(glyphPages.size()), glyphPages.data());
    glyphPages.clear();
    glyphCache.clear();
    glyphCursorX = 0;
    glyphCursorY = 0;
    glyphShelfHeight = 0;
}

// Shelf packing: glyphs go left to right in rows as tall as their tallest
// glyph, then onto a new page. False when every page is full.
bool OpenGL::PackGlyph(const int width, const int height, int& page, int& x, int& y) {
    using namespace OpenGLRendererInternal;

    const int paddedWidth = width + GLYPH_PADDING;
    const int paddedHeight = height + GLYPH_PADDING;
    if (paddedWidth > GLYPH_PAGE_SIZE || paddedHeight > GLYPH_PAGE_SIZE) return false;

    if (glyphPages.empty()) AddGlyphPage();

    if (glyphCursorX + paddedWidth > GLYPH_PAGE_SIZE) {
        glyphCursorX = 0;
        glyphCursorY += glyphShelfHeight;
        glyphShelfHeight = 0;
    }

    if (glyphCursorY + paddedHeight > GLYPH_PAGE_SIZE) {
        if (static_cast<int>(glyphPages.size()) >= GLYPH_MAX_PAGES) return false;
        AddGlyphPage();
    }

    page = static_cast<int>(glyphPages.size()) - 1;
    x = glyphCursorX;
    y = glyphCursorY;
    glyphCursorX += paddedWidth;
    glyphShelfHeight = std::max(glyphShelfHeight, paddedHeight);
    return true;
}

// The cached glyph, rasterizing it on first use. If the atlas is full,
// beforeAtlasReset runs (to draw text still referring to the old atlas)
// and the atlas starts over.
const OpenGL::Glyph* OpenGL::GetGlyph(
    const FT_Face face,
    const unsigned pixelSize,
    const unsigned glyphIndex,
    const std::function<void()>& beforeAtlasReset
) {
    using namespace OpenGLRendererInternal;

    const GlyphKey key{face, pixelSize, glyphIndex};
    if (const auto it = glyphCache.find(key); it != glyphCache.end()) return &it->second;

    SetFontPixelSize(face, pixelSize);

    Glyph glyph;
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_COLOR) != 0)
        return &(glyphCache[key] = glyph);

    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    glyph.size = Vector2(static_cast<float>(bitmap.width), static_cast<float>(bitmap.rows));
    glyph.bearing = Vector2(static_cast<float>(slot->bitmap_left), static_cast<float>(slot->bitmap_top));
    glyph.advance = static_cast<float>(slot->advance.x) / 64.0f;

    const int width = static_cast<int>(bitmap.width);
    const int height = static_cast<int>(bitmap.rows);

    if (width > 0 && height > 0) {
        const std::vector<unsigned char> coverage = CoverageFromBitmap(bitmap);

        int page = 0, x = 0, y = 0;
        bool packed = PackGlyph(width, height, page, x, y);
        if (!packed) {
            beforeAtlasReset();
            ResetGlyphAtlas();
            packed = PackGlyph(width, height, page, x, y);
        }

        if (packed) {
            glBindTexture(GL_TEXTURE_2D, glyphPages[page]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

            constexpr float pageSize = GLYPH_PAGE_SIZE;
            glyph.page = page;
            glyph.uv = Vector4(x / pageSize, y / pageSize, (x + width) / pageSize, (y + height) / pageSize);
        }
    }

    return &(glyphCache[key] = glyph);
}

// ============================================================================
// Drawing
// ============================================================================

void OpenGL::RenderTextString(
    const Shader& shader,
    const FT_Face face,
    const unsigned pixelSize,
    const std::string& text,
    float x,
    float baselineY,
    const float xStretch,
    const Vector3 color
) {
    if (face == nullptr || pixelSize == 0 || text.empty() || screenWidth <= 0 || screenHeight <= 0) return;

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader.use();
    SetTextProjection(shader, screenWidth, screenHeight);

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(shader.ID, "text"), 0);
    glUniform3f(glGetUniformLocation(shader.ID, "textColor"), color.x / 255.0f, color.y / 255.0f, color.z / 255.0f);

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);

    // One vertex batch per atlas page, drawn with one call each.
    std::vector<std::vector<float>> batches;
    const auto flush = [&]() {
        for (size_t page = 0; page < batches.size(); ++page) {
            std::vector<float>& vertices = batches[page];
            if (vertices.empty() || page >= glyphPages.size()) continue;

            glBindTexture(GL_TEXTURE_2D, glyphPages[page]);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));
            vertices.clear();
        }
    };

    SetFontPixelSize(face, pixelSize);
    const float lineHeight = static_cast<float>(face->size->metrics.height) / 64.0f;
    const float startX = x;

    size_t i = 0;
    while (i < text.size()) {
        const char32_t codepoint = NextCodepoint(text, i);

        if (codepoint == U'\r') continue;
        if (codepoint == U'\n') {
            x = startX;
            baselineY += lineHeight;
            continue;
        }

        const bool isTab = codepoint == U'\t';
        const char32_t lookup = isTab ? U' ' : codepoint;

        FT_Face glyphFace = face;
        unsigned glyphIndex = FT_Get_Char_Index(face, lookup);
        if (glyphIndex == 0 && face != defaultFontFace && defaultFontFace != nullptr) {
            if (const unsigned fallback = FT_Get_Char_Index(defaultFontFace, lookup); fallback != 0) {
                glyphFace = defaultFontFace;
                glyphIndex = fallback;
            }
        }

        // Neither font has it: draw the font's "missing" box, and say so once.
        if (glyphIndex == 0) {
            const std::string warningKey = std::string(face->family_name ? face->family_name : "?") + "|" + std::to_string(static_cast<unsigned>(lookup));
            if (warnedMissingCharacters.insert(warningKey).second)
                spdlog::warn("Font '{}' has no glyph for U+{:04X}.", face->family_name ? face->family_name : "?", static_cast<unsigned>(lookup));
        }

        const Glyph* glyph = GetGlyph(glyphFace, pixelSize, glyphIndex, flush);
        if (glyph == nullptr) continue;

        if (isTab) {
            x += glyph->advance * 4.0f * xStretch;
            continue;
        }

        if (glyph->page >= 0) {
            if (batches.size() <= static_cast<size_t>(glyph->page)) batches.resize(glyph->page + 1);

            const float left = x + glyph->bearing.x * xStretch;
            const float top = baselineY - glyph->bearing.y;
            const float right = left + glyph->size.x * xStretch;
            const float bottom = top + glyph->size.y;
            const Vector4& uv = glyph->uv;

            const float quad[6][4] = {
                {left,  top,    uv.x, uv.y},
                {left,  bottom, uv.x, uv.w},
                {right, bottom, uv.z, uv.w},

                {left,  top,    uv.x, uv.y},
                {right, bottom, uv.z, uv.w},
                {right, top,    uv.z, uv.y}
            };

            std::vector<float>& vertices = batches[glyph->page];
            vertices.insert(vertices.end(), &quad[0][0], &quad[0][0] + 24);
        }

        x += glyph->advance * xStretch;
    }

    flush();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!blendWasEnabled) glDisable(GL_BLEND);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
}

// Default font at UI_FONT_SIZE * scale.y pixels, y is the baseline. The
// glyphs are rendered at that size rather than scaled, so small text stays sharp.
void OpenGL::RenderText(
    const Shader& shader,
    const std::string& text,
    const float x,
    const float y,
    const Vector2 scale,
    const Vector3 color
) {
    if (scale.y <= 0.0f) return;
    RenderTextString(shader, defaultFontFace, PixelSizeFor(OpenGLRendererInternal::UI_FONT_SIZE * scale.y), text, x, y, scale.x / scale.y, color);
}

void OpenGL::RenderTextRaw(
    const std::string& text,
    const Vector2 position,
    const Vector2 scale,
    const Vector3 color
) {
    RenderText(*textShader, text, position.x, position.y, scale, color);
}

// Font Size (and the inset from the rectangle) are pixels at the project's
// UI Reference Height, scaled by the window's actual height.
void OpenGL::RenderUIText(
    const ComponentUIText& text,
    const ComponentUITransform& transform
) {
    if (text.text.empty()) return;

    const float referenceHeight = std::max(1.0f, ProjectManager::GetUIReferenceHeight());
    const float windowScale = static_cast<float>(screenHeight) / referenceHeight;

    const unsigned pixelSize = PixelSizeFor(text.fontSize * windowScale);
    if (pixelSize == 0) return;

    const FT_Face face = GetFontFace(text.font);
    if (face == nullptr) return;

    SetFontPixelSize(face, pixelSize);
    const float ascender = static_cast<float>(face->size->metrics.ascender) / 64.0f;
    const float padding = OpenGLRendererInternal::UI_TEXT_PADDING * windowScale;

    RenderTextString(
        *textShader,
        face,
        pixelSize,
        text.text,
        transform.resolvedPosition.x + padding,
        transform.resolvedPosition.y + padding + ascender,
        1.0f,
        {255.0f, 255.0f, 255.0f}
    );
}
