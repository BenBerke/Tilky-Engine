// Fonts, the glyph atlas and text layout, shared by every backend. The
// backends make the glyph pages and draw the quads (RendererCore hooks).

#include "Headers/Runtime/Renderer/RendererCore.hpp"

#include <list>
#include <vector>
#include "Headers/Engine/AssetFS.hpp"
#include <algorithm>
#include <cmath>
#include <ranges>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

namespace {
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

unsigned RendererCore::PixelSizeFor(const float size) {
    if (!(size > 0.0f)) return 0;
    return std::clamp(static_cast<unsigned>(std::lround(size)), 1u, RendererGpu::MAX_GLYPH_PIXEL_SIZE);
}

// ============================================================================
// Fonts
// ============================================================================

namespace {
    // FT_New_Face, or FT_New_Memory_Face for a font in an exported game's
    // Data.pak. FreeType reads a memory face's bytes for as long as the face
    // lives, so they're kept until the program ends (a few fonts at most).
    FT_Error NewFontFace(const FT_Library library, const std::filesystem::path& path, const std::string& diskPath, FT_Face* face) {
        if (!AssetFS::CoversPath(path)) return FT_New_Face(library, diskPath.c_str(), 0, face);

        static std::list<std::vector<unsigned char>> fontBytes;
        std::vector<unsigned char> bytes;
        if (!AssetFS::ReadFile(path, bytes) || bytes.empty()) return FT_Err_Cannot_Open_Resource;

        std::vector<unsigned char>& kept = fontBytes.emplace_back(std::move(bytes));
        return FT_New_Memory_Face(library, kept.data(), static_cast<FT_Long>(kept.size()), 0, face);
    }
}

bool RendererCore::InitializeFontLibrary() {
    if (FT_Init_FreeType(&ft)) {
        spdlog::critical("FT_Init_FreeType failed. FreeType could not be initialized.");
        return false;
    }

    const auto fontPath =
        ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

    if (NewFontFace(ft, fontPath, fontPath.string(), &defaultFontFace)) {
        spdlog::critical(
            "FT_New_Face failed. Could not load font face. Tried path: {}",
            fontPath.string()
        );

        defaultFontFace = nullptr;
        FT_Done_FreeType(ft);
        ft = nullptr;
        return false;
    }

    spdlog::info("Font initialized successfully from: {}", fontPath.string());
    return true;
}

// A UI Text's Font field: a path inside Assets with its extension, or empty
// for the default font. Fonts that fail to load fall back to the default.
FT_Face RendererCore::GetFontFace(const std::string& fontReference) {
    if (fontReference.empty() || ft == nullptr) return defaultFontFace;

    const std::string path = (ProjectManager::GetAssetsPath() / fontReference).lexically_normal().generic_string();

    if (const auto it = fontFaces.find(path); it != fontFaces.end())
        return it->second != nullptr ? it->second : defaultFontFace;

    FT_Face face = nullptr;
    if (NewFontFace(ft, std::filesystem::path(path), path, &face)) {
        spdlog::error("Could not load font '{}' ({}). Using the default font.", fontReference, path);
        fontFaces.emplace(path, nullptr);
        return defaultFontFace;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    fontFaces.emplace(path, face);
    spdlog::info("Loaded font: {}", path);
    return face;
}

void RendererCore::SetFontPixelSize(const FT_Face face, const unsigned pixelSize) {
    if (face->size != nullptr && face->size->metrics.y_ppem == pixelSize) return;
    FT_Set_Pixel_Sizes(face, 0, pixelSize);
}

// The editor's "assets may have changed" signal: fonts are reread from disk,
// and ones that failed before get another try.
void RendererCore::ReloadFonts() {
    for (auto& face : fontFaces | std::views::values)
        if (face != nullptr) FT_Done_Face(face);
    fontFaces.clear();
    warnedMissingCharacters.clear();
    ResetGlyphAtlas();
}

void RendererCore::DestroyFonts() {
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

void RendererCore::AddGlyphPage() {
    AddGlyphPageGpu();

    ++glyphPageCount;
    glyphCursorX = 0;
    glyphCursorY = 0;
    glyphShelfHeight = 0;
}

void RendererCore::ResetGlyphAtlas() {
    if (glyphPageCount > 0) DestroyGlyphPagesGpu();
    glyphPageCount = 0;
    glyphCache.clear();
    glyphCursorX = 0;
    glyphCursorY = 0;
    glyphShelfHeight = 0;
}

// Shelf packing: glyphs go left to right in rows as tall as their tallest
// glyph, then onto a new page. False when every page is full.
bool RendererCore::PackGlyph(const int width, const int height, int& page, int& x, int& y) {
    using namespace RendererGpu;

    const int paddedWidth = width + GLYPH_PADDING;
    const int paddedHeight = height + GLYPH_PADDING;
    if (paddedWidth > GLYPH_PAGE_SIZE || paddedHeight > GLYPH_PAGE_SIZE) return false;

    if (glyphPageCount == 0) AddGlyphPage();

    if (glyphCursorX + paddedWidth > GLYPH_PAGE_SIZE) {
        glyphCursorX = 0;
        glyphCursorY += glyphShelfHeight;
        glyphShelfHeight = 0;
    }

    if (glyphCursorY + paddedHeight > GLYPH_PAGE_SIZE) {
        if (glyphPageCount >= GLYPH_MAX_PAGES) return false;
        AddGlyphPage();
    }

    page = glyphPageCount - 1;
    x = glyphCursorX;
    y = glyphCursorY;
    glyphCursorX += paddedWidth;
    glyphShelfHeight = std::max(glyphShelfHeight, paddedHeight);
    return true;
}

// The cached glyph, rasterizing it on first use. If the atlas is full,
// beforeAtlasReset runs (to draw text still referring to the old atlas)
// and the atlas starts over.
const RendererCore::Glyph* RendererCore::GetGlyph(
    const FT_Face face,
    const unsigned pixelSize,
    const unsigned glyphIndex,
    const std::function<void()>& beforeAtlasReset
) {
    using namespace RendererGpu;

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
            UploadGlyphGpu(page, x, y, width, height, coverage.data());

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

void RendererCore::RenderTextString(
    const FT_Face face,
    const unsigned pixelSize,
    const std::string& text,
    float x,
    float baselineY,
    const float xStretch,
    const Vector4 color
) {
    if (face == nullptr || pixelSize == 0 || text.empty() || screenWidth <= 0 || screenHeight <= 0) return;

    BeginTextGpu(color);

    // One vertex batch per atlas page, drawn with one call each.
    std::vector<std::vector<float>> batches;
    const auto flush = [&]() {
        for (size_t page = 0; page < batches.size(); ++page) {
            std::vector<float>& vertices = batches[page];
            if (vertices.empty() || page >= static_cast<size_t>(glyphPageCount)) continue;

            DrawGlyphsGpu(static_cast<int>(page), vertices);
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

    EndTextGpu();
}

// Default font at UI_FONT_SIZE * scale.y pixels, y is the baseline. The
// glyphs are rendered at that size rather than scaled, so small text stays sharp.
void RendererCore::RenderTextRaw(
    const std::string& text,
    const Vector2 position,
    const Vector2 scale,
    const Vector3 color
) {
    if (scale.y <= 0.0f) return;
    const Vector4 rgba(color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, 1.0f);
    RenderTextString(defaultFontFace, PixelSizeFor(RendererGpu::UI_FONT_SIZE * scale.y), text, position.x, position.y,
                     scale.x / scale.y, rgba);
}

IRenderer::FontMetrics RendererCore::GetFontMetrics(const std::string& font, const float pixelSize) {
    const unsigned size = PixelSizeFor(pixelSize);
    const FT_Face face = GetFontFace(font);
    if (size == 0 || face == nullptr) return {};

    SetFontPixelSize(face, size);
    return {
        static_cast<float>(face->size->metrics.ascender) / 64.0f,
        static_cast<float>(face->size->metrics.height) / 64.0f
    };
}

// Same glyphs, fallback and advances as RenderTextString.
float RendererCore::MeasureTextLine(const std::string& font, const float pixelSize, const std::string& text) {
    const unsigned size = PixelSizeFor(pixelSize);
    const FT_Face face = GetFontFace(font);
    if (size == 0 || face == nullptr) return 0.0f;

    float width = 0.0f;

    size_t i = 0;
    while (i < text.size()) {
        const char32_t codepoint = NextCodepoint(text, i);
        if (codepoint == U'\r' || codepoint == U'\n') continue;

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

        // Nothing is waiting to be drawn, so the atlas can reset freely.
        const Glyph* glyph = GetGlyph(glyphFace, size, glyphIndex, [] {});
        if (glyph == nullptr) continue;

        width += glyph->advance * (isTab ? 4.0f : 1.0f);
    }

    return width;
}

void RendererCore::DrawTextLine(const std::string& font, const float pixelSize, const std::string& text,
                                const Vector2 baseline, const Vector4 color) {
    if (color.w <= 0.0f) return;
    RenderTextString(GetFontFace(font), PixelSizeFor(pixelSize), text, baseline.x, baseline.y, 1.0f, color);
}

// Font Size (and the inset from the rectangle) are pixels at the project's
// UI Reference Height, scaled by the window's actual height.
void RendererCore::RenderUIText(
    const ComponentUIText& text,
    const std::string& shown,
    const ComponentUITransform& transform
) {
    if (shown.empty() || text.color.w <= 0.0f) return;

    const float referenceHeight = std::max(1.0f, ProjectManager::GetUIReferenceHeight());
    const float windowScale = static_cast<float>(screenHeight) / referenceHeight;

    const unsigned pixelSize = PixelSizeFor(text.fontSize * windowScale);
    if (pixelSize == 0) return;

    const FT_Face face = GetFontFace(text.font);
    if (face == nullptr) return;

    SetFontPixelSize(face, pixelSize);
    const float ascender = static_cast<float>(face->size->metrics.ascender) / 64.0f;
    const float padding = RendererGpu::UI_TEXT_PADDING * windowScale;

    RenderTextString(
        face,
        pixelSize,
        shown,
        transform.resolvedPosition.x + padding,
        transform.resolvedPosition.y + padding + ascender,
        1.0f,
        text.color
    );
}
