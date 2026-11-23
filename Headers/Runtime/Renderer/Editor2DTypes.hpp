#pragma once

// What the 2D editors (map editor, UI editor) hand the renderer to draw one
// canvas frame. The editor side fills a Frame (no GL calls there); the
// renderer uploads it to SSBOs and draws it (OpenGLEditor2D.cpp,
// Shaders/Editor2D).
//
// Level things (sectors, walls, linedefs, entities, colliders) each get their
// own buffer of plain level data plus a per-frame buffer of state flags
// (selected, hovered, faint...). The shader picks colours from those flags
// and Frame::style, so selection only changes the small flags buffers.
// Everything else (previews, gizmos, links, the grid...) is screen-space
// triangles in the overlay buffer, drawn between the level passes by layer.
//
// The Gpu* structs are std430 layouts mirrored in Shaders/Editor2D/Editor2D.vs.glsl.

#include <cstdint>
#include <string>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector4.hpp"

namespace Editor2D {
    struct GpuSector {
        Vector4 light;   // rgb 0..255, w = hue 0..1
        Vector4 heights; // x = lowest floor's floor height, y = its ceiling height, z = 1 when it has a floor
    };

    struct GpuSectorTriangle {
        Vector4 ab;      // world a.xy, b.xy
        Vector4 cSector; // world c.xy, z = sector index, w unused
    };

    struct GpuLine {
        Vector4 startEnd; // world start.xy, end.xy
        Vector4 data;     // walls: x = 1 for a portal (two-sided) wall
    };

    struct GpuEntity {
        Vector4 positionHalf; // world x, y (map), half size x, y (world units)
        Vector4 data;         // x = has a Sprite component, y = enabled
    };

    struct GpuCollider {
        Vector4 shape; // world base x, y (map); round: z = radius; box: z/w = half x/y
        Vector4 data;  // x = 1 round, y = 1 trigger, z = 1 active
    };

    struct GpuOverlayVertex {
        Vector4 positionUV; // screen pixels xy, uv
        Vector4 color;      // rgba 0..1
    };

    static_assert(sizeof(GpuSector) == 32);
    static_assert(sizeof(GpuSectorTriangle) == 32);
    static_assert(sizeof(GpuLine) == 32);
    static_assert(sizeof(GpuEntity) == 32);
    static_assert(sizeof(GpuCollider) == 32);
    static_assert(sizeof(GpuOverlayVertex) == 32);

    // Per-frame state flags, one uint per entry of the matching buffer.
    enum SectorFlags : std::uint32_t {
        SECTOR_SELECTED = 1u << 0,
        SECTOR_HOVER_FILL = 1u << 1, // hovered in Sector Mode (filled, not outlined)
    };

    enum LineFlags : std::uint32_t {
        LINE_FAINT = 1u << 0,    // filtered out / not editable in this mode
        LINE_HIDDEN = 1u << 1,   // not drawn by the base pass (the highlight pass draws it)
        LINE_SELECTED = 1u << 2, // highlight pass: selection colour
        LINE_HOVERED = 1u << 3,  // highlight pass: amber
    };

    enum EntityFlags : std::uint32_t {
        ENTITY_FAINT = 1u << 0,
        ENTITY_SELECTED = 1u << 1, // the entity the inspector shows
        ENTITY_HOVERED = 1u << 2,  // amber frame
    };

    enum ColliderFlags : std::uint32_t {
        COLLIDER_FAINT = 1u << 0,
    };

    // Matches MapEditorInternal::TextureViewMode.
    enum ViewMode : int {
        VIEW_OFF, VIEW_FLOOR, VIEW_CEILING, VIEW_BRIGHTNESS, VIEW_FLOOR_HEIGHT, VIEW_CEILING_HEIGHT, VIEW_ROOM_HEIGHT
    };

    // Where overlay triangles go between the level passes (the UI editor,
    // with no level passes, uses Background, AfterSectors and Top). Drawn in this
    // order: Background, sectors, AfterSectors, walls, linedefs,
    // AfterLinedefs, wall/linedef highlights, AfterHighlights, entities,
    // colliders, Top, then labels.
    enum class Layer : std::uint8_t {
        Background,
        AfterSectors,
        AfterLinedefs,
        AfterHighlights,
        Top,
        Count
    };

    // A run of consecutive entries drawn with one texture ("" = untextured).
    struct TextureRun {
        std::string texture;
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    struct OverlayRun {
        Layer layer = Layer::Top;
        std::string texture; // "" = untextured
        bool clampTexture = false;
        bool clip = false; // cut to Frame::clipRect
        std::uint32_t first = 0; // vertices
        std::uint32_t count = 0;
    };

    enum class LabelStyle : std::uint8_t {
        WorldLabel, // centred above the anchor, kept on screen
        LengthPill, // centred on the anchor on a dark pill, skipped off screen
        TopLeft,    // its top-left at the anchor (UI texts)
        Tag,        // top-left at the anchor on a dark backdrop, kept on screen
    };

    struct ClipRect {
        int x = 0; // screen pixels, top-left origin
        int y = 0;
        int width = 0;
        int height = 0;
    };

    struct Label {
        std::string text;
        Vector2 anchor; // screen pixels
        Vector4 color;  // rgba 0..1
        float pixelSize = 15.0f;
        LabelStyle style = LabelStyle::WorldLabel;
        std::string font;          // a project font, "" = the default font
        Layer layer = Layer::Count; // drawn after this layer's overlay; Count = after everything
        bool clip = false;          // cut to Frame::clipRect
    };

    struct View {
        Vector2 camera;     // world point at the screen's centre
        float zoom = 1.0f;  // pixels per world unit
        int width = 0;      // pixels
        int height = 0;
    };

    // Colours are rgba 0..1 unless noted.
    struct Style {
        Vector4 clearColor;

        // Walls: alpha for normal and faint ones, layer toggle already applied.
        Vector4 normalWallColor;
        Vector4 portalWallColor;
        Vector4 highlightedWallColor;
        Vector4 hoverColor; // amber
        float wallAlpha = 1.0f;
        float wallFaintAlpha = 1.0f;

        Vector4 linedefColor;
        Vector4 linedefEndpointColor;
        float linedefAlpha = 1.0f;
        float linedefThickness = 2.0f;
        float linedefEndpointSize = 4.0f;

        // Sectors
        Vector4 highlightedSectorColor;
        Vector4 hoveredSectorColor;
        bool sectorLayerOn = true;
        int viewMode = VIEW_OFF;
        float viewRangeLow = 0.0f;
        float viewRangeHigh = 0.0f;
        float playerHeight = 0.0f;

        // Entities
        Vector4 normalEntityColor;
        Vector4 spriteEntityColor;
        Vector4 disabledEntityColor;
        Vector4 highlightedEntityColor;
        float entityAlpha = 1.0f;
        float entityFaintAlpha = 1.0f;

        // Colliders
        bool showColliders = true;
        Vector4 colliderColor;
        Vector4 triggerColor;
        float colliderThickness = 2.0f;
    };

    // The level's background image, tiled from the world origin.
    struct TiledBackground {
        std::string texture;    // "" = none
        float tileScale = 1.0f; // screen pixels per image pixel
        Vector2 origin;         // the world origin on screen
        float opacity = 1.0f;
    };

    struct Frame {
        View view;
        Style style;
        TiledBackground background;
        ClipRect clipRect; // for overlay runs and labels marked `clip`

        std::vector<GpuSector> sectors;
        std::vector<std::uint32_t> sectorFlags;
        std::vector<GpuSectorTriangle> sectorTriangles;
        std::vector<TextureRun> sectorRuns; // over sectorTriangles

        std::vector<GpuLine> walls;
        std::vector<std::uint32_t> wallFlags;

        std::vector<GpuLine> linedefs;
        std::vector<std::uint32_t> linedefFlags;

        std::vector<GpuEntity> entities;
        std::vector<std::uint32_t> entityFlags;
        std::vector<TextureRun> entityRuns; // over entities

        std::vector<GpuCollider> colliders;
        std::vector<std::uint32_t> colliderFlags;

        std::vector<GpuOverlayVertex> overlayVertices;
        std::vector<OverlayRun> overlayRuns;

        std::vector<Label> labels;

        void Clear() {
            background = {};
            clipRect = {};
            sectors.clear();
            sectorFlags.clear();
            sectorTriangles.clear();
            sectorRuns.clear();
            walls.clear();
            wallFlags.clear();
            linedefs.clear();
            linedefFlags.clear();
            entities.clear();
            entityFlags.clear();
            entityRuns.clear();
            colliders.clear();
            colliderFlags.clear();
            overlayVertices.clear();
            overlayRuns.clear();
            labels.clear();
        }

        // Appends one overlay triangle, starting a new run when the layer
        // or texture changes.
        void AddOverlayTriangle(const Layer layer, const GpuOverlayVertex& a, const GpuOverlayVertex& b, const GpuOverlayVertex& c,
                                const std::string& texture = {}, const bool clampTexture = false, const bool clip = false) {
            if (overlayRuns.empty() || overlayRuns.back().layer != layer || overlayRuns.back().texture != texture ||
                overlayRuns.back().clampTexture != clampTexture || overlayRuns.back().clip != clip) {
                overlayRuns.push_back({layer, texture, clampTexture, clip, static_cast<std::uint32_t>(overlayVertices.size()), 0});
            }

            overlayVertices.push_back(a);
            overlayVertices.push_back(b);
            overlayVertices.push_back(c);
            overlayRuns.back().count += 3;
        }

        // Adds `count` entries starting at `first` to the last run when it
        // has the same texture, otherwise starts one.
        static void ExtendRun(std::vector<TextureRun>& runs, const std::string& texture, const std::uint32_t first, const std::uint32_t count) {
            if (count == 0) return;
            if (!runs.empty() && runs.back().texture == texture && runs.back().first + runs.back().count == first) {
                runs.back().count += count;
                return;
            }
            runs.push_back({texture, first, count});
        }
    };
}
