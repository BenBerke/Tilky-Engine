// The 2D editors' canvas: uploads an Editor2D::Frame to SSBOs and draws it
// with Shaders/Editor2D. See Headers/Runtime/Renderer/Editor2DTypes.hpp.

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
    namespace fs = std::filesystem;

    // SSBO binding points; above the runtime's (0-9), which it binds once.
    constexpr GLuint BINDING_A = 10;
    constexpr GLuint BINDING_B = 11;
    constexpr GLuint BINDING_C = 12;

    const char* PASS_DEFINES[] = {"PASS_SECTOR", "PASS_WALL", "PASS_LINEDEF", "PASS_ENTITY", "PASS_COLLIDER", "PASS_OVERLAY"};

    bool ReadFile(const fs::path& path, std::string& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        std::stringstream stream;
        stream << file.rdbuf();
        out = stream.str();
        return true;
    }

    // The source with `#define <define>` right after its #version line.
    std::string WithDefine(const std::string& source, const char* define) {
        const size_t versionEnd = source.find('\n');
        if (versionEnd == std::string::npos) return source;
        return source.substr(0, versionEnd + 1) + "#define " + define + "\n" + source.substr(versionEnd + 1);
    }

    GLuint CompileStage(const GLenum stage, const std::string& source, const char* label) {
        const GLuint shader = glCreateShader(stage);
        const char* text = source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);

        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            spdlog::error("Editor2D {} {} shader failed to compile: {}", label, stage == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint LinkProgram(const std::string& vertexSource, const std::string& fragmentSource, const char* define) {
        const GLuint vertex = CompileStage(GL_VERTEX_SHADER, WithDefine(vertexSource, define), define);
        const GLuint fragment = CompileStage(GL_FRAGMENT_SHADER, WithDefine(fragmentSource, define), define);
        if (vertex == 0 || fragment == 0) {
            if (vertex != 0) glDeleteShader(vertex);
            if (fragment != 0) glDeleteShader(fragment);
            return 0;
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);

        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            spdlog::error("Editor2D {} program failed to link: {}", define, log);
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    void SetInt(const GLuint program, const char* name, const int value) {
        glUniform1i(glGetUniformLocation(program, name), value);
    }

    void SetFloat(const GLuint program, const char* name, const float value) {
        glUniform1f(glGetUniformLocation(program, name), value);
    }

    void SetVec2(const GLuint program, const char* name, const Vector2 value) {
        glUniform2f(glGetUniformLocation(program, name), value.x, value.y);
    }

    void SetVec3(const GLuint program, const char* name, const Vector4 value) {
        glUniform3f(glGetUniformLocation(program, name), value.x, value.y, value.z);
    }

    void SetVec4(const GLuint program, const char* name, const Vector4 value) {
        glUniform4f(glGetUniformLocation(program, name), value.x, value.y, value.z, value.w);
    }

    // As OpenGLText.cpp rounds a font size to whole pixels.
    unsigned PixelSizeFor(const float size) {
        if (!(size > 0.0f)) return 0;
        return std::clamp(static_cast<unsigned>(std::lround(size)), 1u, OpenGLRendererInternal::MAX_GLYPH_PIXEL_SIZE);
    }

    template <typename T>
    void Upload(const GLuint buffer, const std::vector<T>& data) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        // Never zero bytes, so binding it is always valid.
        const GLsizeiptr size = static_cast<GLsizeiptr>(std::max<size_t>(data.size() * sizeof(T), 16));
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_STREAM_DRAW);
        if (!data.empty()) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(data.size() * sizeof(T)), data.data());
    }
}

bool OpenGL::InitEditor2D() {
    if (editor2DInitTried) return editor2DReady;
    editor2DInitTried = true;

    const fs::path vertexPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Editor2D" / "Editor2D.vs.glsl");
    const fs::path fragmentPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Editor2D" / "Editor2D.fs.glsl");

    std::string vertexSource;
    std::string fragmentSource;
    if (!ReadFile(vertexPath, vertexSource) || !ReadFile(fragmentPath, fragmentSource)) {
        spdlog::error("Editor2D shaders not found ({}, {})", vertexPath.string(), fragmentPath.string());
        return false;
    }

    for (int pass = 0; pass < EDITOR2D_PASS_COUNT; ++pass) {
        editor2DPrograms[pass] = LinkProgram(vertexSource, fragmentSource, PASS_DEFINES[pass]);
        if (editor2DPrograms[pass] == 0) {
            DestroyEditor2D();
            editor2DInitTried = true;
            return false;
        }
    }

    glGenBuffers(EDITOR2D_BUFFER_COUNT, editor2DBuffers);
    glGenVertexArrays(1, &editor2DVAO);

    // SDL's renderer filtered linearly without mipmaps; the same here, so
    // the canvas looks as it did. Samplers leave the shared textures' own
    // settings (the inspector thumbnails, the game) alone.
    glGenSamplers(1, &editor2DSamplerRepeat);
    glSamplerParameteri(editor2DSamplerRepeat, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(editor2DSamplerRepeat, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(editor2DSamplerRepeat, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(editor2DSamplerRepeat, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glGenSamplers(1, &editor2DSamplerClamp);
    glSamplerParameteri(editor2DSamplerClamp, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(editor2DSamplerClamp, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(editor2DSamplerClamp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(editor2DSamplerClamp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    editor2DReady = true;
    return true;
}

void OpenGL::DestroyEditor2D() {
    for (GLuint& program : editor2DPrograms) {
        if (program != 0) glDeleteProgram(program);
        program = 0;
    }

    if (editor2DBuffers[0] != 0) glDeleteBuffers(EDITOR2D_BUFFER_COUNT, editor2DBuffers);
    for (GLuint& buffer : editor2DBuffers) buffer = 0;

    if (editor2DVAO != 0) glDeleteVertexArrays(1, &editor2DVAO);
    editor2DVAO = 0;

    if (editor2DSamplerRepeat != 0) glDeleteSamplers(1, &editor2DSamplerRepeat);
    if (editor2DSamplerClamp != 0) glDeleteSamplers(1, &editor2DSamplerClamp);
    editor2DSamplerRepeat = 0;
    editor2DSamplerClamp = 0;

    editor2DReady = false;
    editor2DInitTried = false;
}

GLuint OpenGL::Editor2DTexture(const std::string& fileName) {
    if (fileName.empty()) return 0;
    const int index = GetOrCreateTextureIndex(fileName);
    if (index < 0 || index >= GetTextureCount()) return 0;
    return GetTexture(index).id;
}

void OpenGL::DrawEditor2DOverlay(const Editor2D::Frame& frame, const int layer) {
    const GLuint program = editor2DPrograms[EDITOR2D_OVERLAY];
    bool bound = false;

    for (const Editor2D::OverlayRun& run : frame.overlayRuns) {
        if (static_cast<int>(run.layer) != layer || run.count == 0) continue;

        if (!bound) {
            glUseProgram(program);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_OVERLAY]);
            bound = true;
        }

        const GLuint texture = Editor2DTexture(run.texture);
        // A texture that failed to load leaves its triangles out, as SDL did.
        if (!run.texture.empty() && texture == 0) continue;

        SetInt(program, "uTextured", texture != 0 ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindSampler(0, run.clampTexture ? editor2DSamplerClamp : editor2DSamplerRepeat);
        SetEditor2DClip(frame, run.clip);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
    }

    SetEditor2DClip(frame, false);
    DrawEditor2DLabels(frame, layer);
}

void OpenGL::SetEditor2DClip(const Editor2D::Frame& frame, const bool clip) {
    if (!clip) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    // Frame::clipRect is top-left origin; GL's scissor is bottom-left.
    const Editor2D::ClipRect& rect = frame.clipRect;
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, frame.view.height - (rect.y + rect.height), std::max(rect.width, 0), std::max(rect.height, 0));
}

void OpenGL::DrawEditor2DLabels(const Editor2D::Frame& frame, const int layer) {
    if (frame.labels.empty() || defaultFontFace == nullptr) return;

    struct Placed {
        const Editor2D::Label* label;
        FT_Face face;
        float x;
        float baseline;
    };

    std::vector<Placed> placed;
    std::vector<Editor2D::GpuOverlayVertex> pills;
    placed.reserve(frame.labels.size());

    const float width = static_cast<float>(frame.view.width);
    const float height = static_cast<float>(frame.view.height);

    const auto addRect = [&pills](const float x, const float y, const float w, const float h, const Vector4& color) {
        const Editor2D::GpuOverlayVertex v0{{x, y, 0, 0}, color};
        const Editor2D::GpuOverlayVertex v1{{x + w, y, 0, 0}, color};
        const Editor2D::GpuOverlayVertex v2{{x + w, y + h, 0, 0}, color};
        const Editor2D::GpuOverlayVertex v3{{x, y + h, 0, 0}, color};
        pills.insert(pills.end(), {v0, v1, v2, v0, v2, v3});
    };

    for (const Editor2D::Label& label : frame.labels) {
        if (label.text.empty() || static_cast<int>(label.layer) != layer) continue;

        const FT_Face face = GetFontFace(label.font);
        if (face == nullptr) continue;

        const unsigned pixelSize = PixelSizeFor(label.pixelSize);
        if (pixelSize == 0) continue;
        SetFontPixelSize(face, pixelSize);
        const float ascender = static_cast<float>(face->size->metrics.ascender) / 64.0f;
        const float descender = static_cast<float>(face->size->metrics.descender) / 64.0f;
        const float textHeight = std::ceil(ascender - descender);

        if (label.style == Editor2D::LabelStyle::TopLeft) {
            placed.push_back({&label, face, label.anchor.x, label.anchor.y + ascender});
            continue;
        }

        const float textWidth = std::ceil(MeasureTextLine(label.font, label.pixelSize, label.text));

        if (label.style == Editor2D::LabelStyle::Tag) {
            constexpr float PADDING = 4.0f;
            const float tagW = textWidth + PADDING * 2.0f;
            const float tagH = textHeight + PADDING * 2.0f;
            const float x = std::clamp(label.anchor.x, 2.0f, std::max(2.0f, width - tagW - 2.0f));
            const float y = std::clamp(label.anchor.y, 2.0f, std::max(2.0f, height - tagH - 2.0f));

            addRect(x, y, tagW, tagH, {20.0f / 255.0f, 20.0f / 255.0f, 26.0f / 255.0f, 210.0f / 255.0f});
            placed.push_back({&label, face, x + PADDING, y + PADDING + ascender});
            continue;
        }

        if (label.style == Editor2D::LabelStyle::LengthPill) {
            constexpr float PAD_X = 5.0f;
            constexpr float PAD_Y = 1.0f;

            const Vector2 mid = label.anchor;
            if (mid.x < 0.0f || mid.y < 0.0f || mid.x > width || mid.y > height) continue;

            const float pillW = textWidth + PAD_X * 2.0f;
            const float pillH = textHeight + PAD_Y * 2.0f;
            const float x = std::floor(mid.x - pillW * 0.5f);
            const float y = std::floor(mid.y - pillH * 0.5f);

            // Corners cut by one pixel.
            const Vector4 pillColor = {18.0f / 255.0f, 18.0f / 255.0f, 22.0f / 255.0f, 200.0f / 255.0f};
            addRect(x, y + 1.0f, pillW, pillH - 2.0f, pillColor);
            addRect(x + 1.0f, y, pillW - 2.0f, 1.0f, pillColor);
            addRect(x + 1.0f, y + pillH - 1.0f, pillW - 2.0f, 1.0f, pillColor);

            placed.push_back({&label, face, x + PAD_X, y + PAD_Y + ascender});
            continue;
        }

        // Centred just above the anchor, kept on screen.
        const float maxX = std::max(2.0f, width - textWidth - 2.0f);
        const float maxY = std::max(2.0f, height - textHeight - 2.0f);
        const float x = std::clamp(label.anchor.x - textWidth * 0.5f, 2.0f, maxX);
        const float y = std::clamp(label.anchor.y - textHeight - 8.0f, 2.0f, maxY);
        placed.push_back({&label, face, x, y + ascender});
    }

    if (!pills.empty()) {
        const GLuint program = editor2DPrograms[EDITOR2D_OVERLAY];
        glUseProgram(program);
        Upload(editor2DBuffers[EDITOR2D_BUFFER_LABEL_OVERLAY], pills);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_LABEL_OVERLAY]);
        SetInt(program, "uTextured", 0);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(pills.size()));
    }

    glBindSampler(0, 0);

    for (const Placed& entry : placed) {
        SetEditor2DClip(frame, entry.label->clip);
        RenderTextString(entry.face, PixelSizeFor(entry.label->pixelSize), entry.label->text, entry.x, entry.baseline, 1.0f,
                         entry.label->color);
    }
    SetEditor2DClip(frame, false);

    // The text shader leaves its own VAO bound.
    glBindVertexArray(editor2DVAO);
}

void OpenGL::DrawEditor2D(const Editor2D::Frame& frame) {
    if (frame.view.width <= 0 || frame.view.height <= 0) return;
    if (!InitEditor2D()) return;

    // The text code projects with screenWidth/screenHeight.
    const int savedWidth = screenWidth;
    const int savedHeight = screenHeight;
    screenWidth = frame.view.width;
    screenHeight = frame.view.height;

    const Editor2D::Style& style = frame.style;
    const Vector2 screen = {static_cast<float>(frame.view.width), static_cast<float>(frame.view.height)};

    glViewport(0, 0, frame.view.width, frame.view.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glClearColor(style.clearColor.x, style.clearColor.y, style.clearColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindVertexArray(editor2DVAO);
    glActiveTexture(GL_TEXTURE0);

    Upload(editor2DBuffers[EDITOR2D_BUFFER_SECTORS], frame.sectors);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_SECTOR_TRIANGLES], frame.sectorTriangles);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_SECTOR_FLAGS], frame.sectorFlags);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_WALLS], frame.walls);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_WALL_FLAGS], frame.wallFlags);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_LINEDEFS], frame.linedefs);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_LINEDEF_FLAGS], frame.linedefFlags);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_ENTITIES], frame.entities);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_ENTITY_FLAGS], frame.entityFlags);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_COLLIDERS], frame.colliders);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_COLLIDER_FLAGS], frame.colliderFlags);
    Upload(editor2DBuffers[EDITOR2D_BUFFER_OVERLAY], frame.overlayVertices);

    for (const GLuint program : editor2DPrograms) {
        glUseProgram(program);
        SetVec2(program, "uCamera", frame.view.camera);
        SetFloat(program, "uZoom", frame.view.zoom);
        SetVec2(program, "uScreen", screen);
        SetInt(program, "uTexture", 0);
        SetInt(program, "uTextured", 0);
    }

    using Editor2D::Layer;

    // The background image, tiled from the world origin so the tiles stay
    // put on the map as the view moves.
    if (const Editor2D::TiledBackground& background = frame.background; !background.texture.empty()) {
        const int index = GetOrCreateTextureIndex(background.texture);

        if (index >= 0 && index < GetTextureCount()) {
            const GPUTexture& texture = GetTexture(index);
            const float tileWidth = static_cast<float>(texture.width) * background.tileScale;
            const float tileHeight = static_cast<float>(texture.height) * background.tileScale;

            // Tiles this small would read as a flat colour anyway.
            constexpr float MIN_TILE_PIXELS = 2.0f;

            if (tileWidth >= MIN_TILE_PIXELS && tileHeight >= MIN_TILE_PIXELS) {
                const float startX = background.origin.x - std::ceil(background.origin.x / tileWidth) * tileWidth;
                const float startY = background.origin.y - std::ceil(background.origin.y / tileHeight) * tileHeight;
                const float endU = (screen.x - startX) / tileWidth;
                const float endV = (screen.y - startY) / tileHeight;
                const Vector4 color = {1.0f, 1.0f, 1.0f, std::clamp(background.opacity, 0.0f, 1.0f)};

                const Editor2D::GpuOverlayVertex v0{{startX, startY, 0.0f, 0.0f}, color};
                const Editor2D::GpuOverlayVertex v1{{screen.x, startY, endU, 0.0f}, color};
                const Editor2D::GpuOverlayVertex v2{{screen.x, screen.y, endU, endV}, color};
                const Editor2D::GpuOverlayVertex v3{{startX, screen.y, 0.0f, endV}, color};
                const std::vector<Editor2D::GpuOverlayVertex> quad = {v0, v1, v2, v0, v2, v3};

                const GLuint program = editor2DPrograms[EDITOR2D_OVERLAY];
                glUseProgram(program);
                Upload(editor2DBuffers[EDITOR2D_BUFFER_LABEL_OVERLAY], quad);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_LABEL_OVERLAY]);
                SetInt(program, "uTextured", 1);
                glBindTexture(GL_TEXTURE_2D, texture.id);
                glBindSampler(0, editor2DSamplerRepeat);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }
    }

    DrawEditor2DOverlay(frame, static_cast<int>(Layer::Background));

    // Sectors, in runs of one texture.
    if (!frame.sectorTriangles.empty()) {
        const GLuint program = editor2DPrograms[EDITOR2D_SECTOR];
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_SECTORS]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_B, editor2DBuffers[EDITOR2D_BUFFER_SECTOR_TRIANGLES]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_C, editor2DBuffers[EDITOR2D_BUFFER_SECTOR_FLAGS]);
        SetInt(program, "uViewMode", style.viewMode);
        SetFloat(program, "uRangeLow", style.viewRangeLow);
        SetFloat(program, "uRangeHigh", style.viewRangeHigh);
        SetFloat(program, "uPlayerHeight", style.playerHeight);
        SetInt(program, "uSectorLayerOn", style.sectorLayerOn ? 1 : 0);
        SetVec3(program, "uHighlightedSector", style.highlightedSectorColor);
        SetVec3(program, "uHoveredSector", style.hoveredSectorColor);
        glBindSampler(0, editor2DSamplerRepeat);

        for (const Editor2D::TextureRun& run : frame.sectorRuns) {
            const GLuint texture = Editor2DTexture(run.texture);
            SetInt(program, "uTextured", texture != 0 ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first * 3), static_cast<GLsizei>(run.count * 3));
        }
    }

    DrawEditor2DOverlay(frame, static_cast<int>(Layer::AfterSectors));

    const auto drawWalls = [&](const int highlight) {
        if (frame.walls.empty()) return;
        const GLuint program = editor2DPrograms[EDITOR2D_WALL];
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_WALLS]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_B, editor2DBuffers[EDITOR2D_BUFFER_WALL_FLAGS]);
        SetInt(program, "uHighlight", highlight);
        SetVec4(program, "uNormalWall", style.normalWallColor);
        SetVec4(program, "uPortalWall", style.portalWallColor);
        SetVec4(program, "uHighlightedWall", style.highlightedWallColor);
        SetVec4(program, "uHoverColor", style.hoverColor);
        SetFloat(program, "uWallAlpha", style.wallAlpha);
        SetFloat(program, "uWallFaintAlpha", style.wallFaintAlpha);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.walls.size() * 6));
    };

    const auto drawLinedefs = [&](const int highlight) {
        if (frame.linedefs.empty()) return;
        const GLuint program = editor2DPrograms[EDITOR2D_LINEDEF];
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_LINEDEFS]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_B, editor2DBuffers[EDITOR2D_BUFFER_LINEDEF_FLAGS]);
        SetInt(program, "uHighlight", highlight);
        SetVec4(program, "uLinedefColor", style.linedefColor);
        SetVec4(program, "uEndpointColor", style.linedefEndpointColor);
        SetFloat(program, "uLinedefAlpha", style.linedefAlpha);
        SetFloat(program, "uLinedefThickness", style.linedefThickness);
        SetFloat(program, "uEndpointSize", style.linedefEndpointSize);
        SetVec4(program, "uHighlightedWall", style.highlightedWallColor);
        SetVec4(program, "uHoverColor", style.hoverColor);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.linedefs.size() * 24));
    };

    drawWalls(0);
    drawLinedefs(0);
    DrawEditor2DOverlay(frame, static_cast<int>(Layer::AfterLinedefs));
    drawWalls(1);
    drawLinedefs(1);
    DrawEditor2DOverlay(frame, static_cast<int>(Layer::AfterHighlights));

    // Entities, in runs of one sprite texture.
    if (!frame.entities.empty()) {
        const GLuint program = editor2DPrograms[EDITOR2D_ENTITY];
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_ENTITIES]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_B, editor2DBuffers[EDITOR2D_BUFFER_ENTITY_FLAGS]);
        SetVec4(program, "uNormalEntity", style.normalEntityColor);
        SetVec4(program, "uSpriteEntity", style.spriteEntityColor);
        SetVec4(program, "uDisabledEntity", style.disabledEntityColor);
        SetVec4(program, "uHighlightedEntity", style.highlightedEntityColor);
        SetVec4(program, "uHoverColor", style.hoverColor);
        SetFloat(program, "uEntityAlpha", style.entityAlpha);
        SetFloat(program, "uEntityFaintAlpha", style.entityFaintAlpha);
        glBindSampler(0, editor2DSamplerClamp);

        for (const Editor2D::TextureRun& run : frame.entityRuns) {
            const GLuint texture = Editor2DTexture(run.texture);
            SetInt(program, "uTextured", texture != 0 ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first * 6), static_cast<GLsizei>(run.count * 6));
        }
    }

    if (style.showColliders && !frame.colliders.empty()) {
        const GLuint program = editor2DPrograms[EDITOR2D_COLLIDER];
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_A, editor2DBuffers[EDITOR2D_BUFFER_COLLIDERS]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_B, editor2DBuffers[EDITOR2D_BUFFER_COLLIDER_FLAGS]);
        SetVec4(program, "uColliderColor", style.colliderColor);
        SetVec4(program, "uTriggerColor", style.triggerColor);
        SetFloat(program, "uColliderThickness", style.colliderThickness);
        SetFloat(program, "uEntityAlpha", style.entityAlpha);
        SetFloat(program, "uEntityFaintAlpha", style.entityFaintAlpha);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.colliders.size() * 6));
    }

    DrawEditor2DOverlay(frame, static_cast<int>(Layer::Top));
    DrawEditor2DLabels(frame, static_cast<int>(Layer::Count));

    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);

    screenWidth = savedWidth;
    screenHeight = savedHeight;
}
