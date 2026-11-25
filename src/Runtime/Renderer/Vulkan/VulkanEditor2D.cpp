// The 2D editors' canvas (see OpenGLEditor2D.cpp): an Editor2D::Frame's
// arrays go to this frame's arena and are drawn with Shaders/Editor2D.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"
#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

using namespace VulkanInternal;

namespace {
    // Buffer bindings the Editor2D shaders read; above the runtime's (0-9).
    constexpr int BINDING_A = 10;
    constexpr int BINDING_B = 11;
    constexpr int BINDING_C = 12;

    enum Pass { PASS_SECTOR, PASS_WALL, PASS_LINEDEF, PASS_ENTITY, PASS_COLLIDER, PASS_OVERLAY, PASS_COUNT };
    const char* PASS_DEFINES[PASS_COUNT] = {"PASS_SECTOR", "PASS_WALL", "PASS_LINEDEF", "PASS_ENTITY", "PASS_COLLIDER", "PASS_OVERLAY"};

    enum Array { SECTORS, SECTOR_TRIANGLES, SECTOR_FLAGS, WALLS, WALL_FLAGS, LINEDEFS, LINEDEF_FLAGS, ENTITIES, ENTITY_FLAGS,
                 COLLIDERS, COLLIDER_FLAGS, OVERLAY, ARRAY_COUNT };
}

bool Vulkan::InitEditor2D() {
    if (editor2DInitTried) return editor2DReady;
    editor2DInitTried = true;

    namespace fs = std::filesystem;
    const fs::path vertexPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Editor2D" / "Editor2D.vs.glsl");
    const fs::path fragmentPath = ProjectManager::FindAssetPath(fs::path("Shaders") / "Editor2D" / "Editor2D.fs.glsl");

    for (int pass = 0; pass < PASS_COUNT; ++pass) {
        editor2DPrograms[pass] = LoadProgram(std::string("Editor2D ") + PASS_DEFINES[pass], vertexPath, fragmentPath, PASS_DEFINES[pass], {});
        if (editor2DPrograms[pass] == nullptr) return false;
    }

    editor2DReady = true;
    return true;
}

const VulkanInternal::Image* Vulkan::Editor2DTexture(const std::string& fileName) {
    if (fileName.empty()) return nullptr;
    const int index = GetOrCreateTextureIndex(fileName);
    if (index < 0 || index >= GetTextureCount()) return nullptr;
    return StandaloneImage(GetTexture(index).id);
}

void Vulkan::SetEditor2DClip(const Editor2D::Frame& frame, const bool clip) {
    if (!clip) {
        state.scissorTest = false;
        return;
    }

    // Frame::clipRect is top-left origin; the scissor is GL's, bottom-left.
    const Editor2D::ClipRect& rect = frame.clipRect;
    state.scissorTest = true;
    state.scissor[0] = rect.x;
    state.scissor[1] = frame.view.height - (rect.y + rect.height);
    state.scissor[2] = std::max(rect.width, 0);
    state.scissor[3] = std::max(rect.height, 0);
}

void Vulkan::DrawEditor2DOverlay(const Editor2D::Frame& frame, const int layer, const VkDeviceAddress buffers[]) {
    bool bound = false;

    for (const Editor2D::OverlayRun& run : frame.overlayRuns) {
        if (static_cast<int>(run.layer) != layer || run.count == 0) continue;

        if (!bound) {
            UseProgram(editor2DPrograms[PASS_OVERLAY].get());
            BindStorage(BINDING_A, buffers[OVERLAY]);
            bound = true;
        }

        const Image* texture = Editor2DTexture(run.texture);
        // A texture that failed to load leaves its triangles out, as SDL did.
        if (!run.texture.empty() && texture == nullptr) continue;

        SetInt("uTextured", texture != nullptr ? 1 : 0);
        SetTexture("uTexture", texture, run.clampTexture ? SAMPLER_LINEAR_CLAMP : SAMPLER_LINEAR_REPEAT);
        SetEditor2DClip(frame, run.clip);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, run.count, 1, run.first);
    }

    SetEditor2DClip(frame, false);
    DrawEditor2DLabels(frame, layer);
}

void Vulkan::DrawEditor2DLabels(const Editor2D::Frame& frame, const int layer) {
    if (frame.labels.empty() || defaultFontFace == nullptr) return;

    std::vector<PlacedEditor2DLabel> placed;
    std::vector<Editor2D::GpuOverlayVertex> backdrops;
    LayoutEditor2DLabels(frame, layer, placed, backdrops);

    if (!backdrops.empty()) {
        UseProgram(editor2DPrograms[PASS_OVERLAY].get());
        BindStorage(BINDING_A, UploadFrame(backdrops.data(), backdrops.size() * sizeof(Editor2D::GpuOverlayVertex)));
        SetInt("uTextured", 0);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(backdrops.size()));
    }

    for (const PlacedEditor2DLabel& entry : placed) {
        SetEditor2DClip(frame, entry.label->clip);
        RenderTextString(entry.face, PixelSizeFor(entry.label->pixelSize), entry.label->text, entry.x, entry.baseline, 1.0f,
                         entry.label->color);
    }
    SetEditor2DClip(frame, false);
}

void Vulkan::DrawEditor2D(const Editor2D::Frame& frame) {
    if (frame.view.width <= 0 || frame.view.height <= 0) return;
    if (!InitEditor2D()) return;

    // The text code projects with screenWidth/screenHeight.
    const int savedWidth = screenWidth;
    const int savedHeight = screenHeight;
    screenWidth = frame.view.width;
    screenHeight = frame.view.height;

    const Editor2D::Style& style = frame.style;
    const Vector2 screen = {static_cast<float>(frame.view.width), static_cast<float>(frame.view.height)};

    SetViewport(frame.view.width, frame.view.height);
    state.depthTest = false;
    state.stencilTest = false;
    state.cull = false;
    state.scissorTest = false;
    state.blend = true;
    state.blendFunc = Blend::AlphaSeparate;
    state.colorMask = 0xF;
    const bool savedWireframe = wireframe;
    wireframe = false;

    ClearColor({style.clearColor.x, style.clearColor.y, style.clearColor.z, 1.0f});

    VkDeviceAddress buffers[ARRAY_COUNT];
    const auto upload = [this](const auto& data) {
        using T = typename std::decay_t<decltype(data)>::value_type;
        return UploadFrame(data.data(), data.size() * sizeof(T));
    };
    buffers[SECTORS] = upload(frame.sectors);
    buffers[SECTOR_TRIANGLES] = upload(frame.sectorTriangles);
    buffers[SECTOR_FLAGS] = upload(frame.sectorFlags);
    buffers[WALLS] = upload(frame.walls);
    buffers[WALL_FLAGS] = upload(frame.wallFlags);
    buffers[LINEDEFS] = upload(frame.linedefs);
    buffers[LINEDEF_FLAGS] = upload(frame.linedefFlags);
    buffers[ENTITIES] = upload(frame.entities);
    buffers[ENTITY_FLAGS] = upload(frame.entityFlags);
    buffers[COLLIDERS] = upload(frame.colliders);
    buffers[COLLIDER_FLAGS] = upload(frame.colliderFlags);
    buffers[OVERLAY] = upload(frame.overlayVertices);

    for (const std::unique_ptr<Program>& pass : editor2DPrograms) {
        UseProgram(pass.get());
        SetVec2("uCamera", frame.view.camera.x, frame.view.camera.y);
        SetFloat("uZoom", frame.view.zoom);
        SetVec2("uScreen", screen.x, screen.y);
        SetTexture("uTexture", nullptr, SAMPLER_LINEAR_REPEAT);
        SetInt("uTextured", 0);
    }

    using Editor2D::Layer;

    // The background image, tiled from the world origin so the tiles stay
    // put on the map as the view moves.
    if (const Editor2D::TiledBackground& background = frame.background; !background.texture.empty()) {
        const int index = GetOrCreateTextureIndex(background.texture);

        if (index >= 0 && index < GetTextureCount()) {
            const GPUTexture& texture = GetTexture(index);
            std::vector<Editor2D::GpuOverlayVertex> quad;

            if (BuildEditor2DBackground(frame, texture, quad)) {
                UseProgram(editor2DPrograms[PASS_OVERLAY].get());
                BindStorage(BINDING_A, UploadFrame(quad.data(), quad.size() * sizeof(Editor2D::GpuOverlayVertex)));
                SetInt("uTextured", 1);
                SetTexture("uTexture", StandaloneImage(texture.id), SAMPLER_LINEAR_REPEAT);
                Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(quad.size()));
            }
        }
    }

    DrawEditor2DOverlay(frame, static_cast<int>(Layer::Background), buffers);

    // Sectors, in runs of one texture.
    if (!frame.sectorTriangles.empty()) {
        UseProgram(editor2DPrograms[PASS_SECTOR].get());
        BindStorage(BINDING_A, buffers[SECTORS]);
        BindStorage(BINDING_B, buffers[SECTOR_TRIANGLES]);
        BindStorage(BINDING_C, buffers[SECTOR_FLAGS]);
        SetInt("uViewMode", style.viewMode);
        SetFloat("uRangeLow", style.viewRangeLow);
        SetFloat("uRangeHigh", style.viewRangeHigh);
        SetFloat("uPlayerHeight", style.playerHeight);
        SetInt("uSectorLayerOn", style.sectorLayerOn ? 1 : 0);
        SetVec3("uHighlightedSector", style.highlightedSectorColor.x, style.highlightedSectorColor.y, style.highlightedSectorColor.z);
        SetVec3("uHoveredSector", style.hoveredSectorColor.x, style.hoveredSectorColor.y, style.hoveredSectorColor.z);

        for (const Editor2D::TextureRun& run : frame.sectorRuns) {
            const Image* texture = Editor2DTexture(run.texture);
            SetInt("uTextured", texture != nullptr ? 1 : 0);
            SetTexture("uTexture", texture, SAMPLER_LINEAR_REPEAT);
            Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, run.count * 3, 1, run.first * 3);
        }
    }

    DrawEditor2DOverlay(frame, static_cast<int>(Layer::AfterSectors), buffers);

    const auto vec4 = [this](const char* name, const Vector4& value) { SetVec4(name, value.x, value.y, value.z, value.w); };

    const auto drawWalls = [&](const int highlight) {
        if (frame.walls.empty()) return;
        UseProgram(editor2DPrograms[PASS_WALL].get());
        BindStorage(BINDING_A, buffers[WALLS]);
        BindStorage(BINDING_B, buffers[WALL_FLAGS]);
        SetInt("uHighlight", highlight);
        vec4("uNormalWall", style.normalWallColor);
        vec4("uPortalWall", style.portalWallColor);
        vec4("uHighlightedWall", style.highlightedWallColor);
        vec4("uHoverColor", style.hoverColor);
        SetFloat("uWallAlpha", style.wallAlpha);
        SetFloat("uWallFaintAlpha", style.wallFaintAlpha);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(frame.walls.size() * 6));
    };

    const auto drawLinedefs = [&](const int highlight) {
        if (frame.linedefs.empty()) return;
        UseProgram(editor2DPrograms[PASS_LINEDEF].get());
        BindStorage(BINDING_A, buffers[LINEDEFS]);
        BindStorage(BINDING_B, buffers[LINEDEF_FLAGS]);
        SetInt("uHighlight", highlight);
        vec4("uLinedefColor", style.linedefColor);
        vec4("uEndpointColor", style.linedefEndpointColor);
        SetFloat("uLinedefAlpha", style.linedefAlpha);
        SetFloat("uLinedefThickness", style.linedefThickness);
        SetFloat("uEndpointSize", style.linedefEndpointSize);
        vec4("uHighlightedWall", style.highlightedWallColor);
        vec4("uHoverColor", style.hoverColor);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(frame.linedefs.size() * 24));
    };

    drawWalls(0);
    drawLinedefs(0);
    DrawEditor2DOverlay(frame, static_cast<int>(Layer::AfterLinedefs), buffers);
    drawWalls(1);
    drawLinedefs(1);
    DrawEditor2DOverlay(frame, static_cast<int>(Layer::AfterHighlights), buffers);

    // Entities, in runs of one sprite texture.
    if (!frame.entities.empty()) {
        UseProgram(editor2DPrograms[PASS_ENTITY].get());
        BindStorage(BINDING_A, buffers[ENTITIES]);
        BindStorage(BINDING_B, buffers[ENTITY_FLAGS]);
        vec4("uNormalEntity", style.normalEntityColor);
        vec4("uSpriteEntity", style.spriteEntityColor);
        vec4("uDisabledEntity", style.disabledEntityColor);
        vec4("uHighlightedEntity", style.highlightedEntityColor);
        vec4("uHoverColor", style.hoverColor);
        SetFloat("uEntityAlpha", style.entityAlpha);
        SetFloat("uEntityFaintAlpha", style.entityFaintAlpha);

        for (const Editor2D::TextureRun& run : frame.entityRuns) {
            const Image* texture = Editor2DTexture(run.texture);
            SetInt("uTextured", texture != nullptr ? 1 : 0);
            SetTexture("uTexture", texture, SAMPLER_LINEAR_CLAMP);
            Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, run.count * 6, 1, run.first * 6);
        }
    }

    if (style.showColliders && !frame.colliders.empty()) {
        UseProgram(editor2DPrograms[PASS_COLLIDER].get());
        BindStorage(BINDING_A, buffers[COLLIDERS]);
        BindStorage(BINDING_B, buffers[COLLIDER_FLAGS]);
        vec4("uColliderColor", style.colliderColor);
        vec4("uTriggerColor", style.triggerColor);
        SetFloat("uColliderThickness", style.colliderThickness);
        SetFloat("uEntityAlpha", style.entityAlpha);
        SetFloat("uEntityFaintAlpha", style.entityFaintAlpha);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(frame.colliders.size() * 6));
    }

    DrawEditor2DOverlay(frame, static_cast<int>(Layer::Top), buffers);
    DrawEditor2DLabels(frame, static_cast<int>(Layer::Count));

    state.blendFunc = Blend::Alpha;
    wireframe = savedWireframe;
    UseProgram(nullptr);

    screenWidth = savedWidth;
    screenHeight = savedHeight;
}
