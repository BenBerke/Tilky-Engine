#ifndef TILKY_ENGINE_RENDERERCORE_HPP
#define TILKY_ENGINE_RENDERERCORE_HPP

// What every graphics backend (OpenGL, Vulkan) shares: the CPU side of
// drawing a level. The GPU structs the shaders read, the texture atlas
// layout, standalone textures, fonts and the glyph atlas, visibility
// culling, the camera's eye and step smoothing, and the per-frame arrays
// (walls, flats, sectors, sprites, colliders, markers, gizmos) are built
// here. A backend only uploads and draws them, through the hooks below.
//
// One layout in every executable: no TILKY_STANDALONE in here.

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Headers/Runtime/Renderer/IRenderer.hpp"

#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector4.hpp"
#include "Headers/Map/Gateways.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Level.hpp"

namespace Editor2D {
    struct Frame;
    struct Label;
    struct GpuOverlayVertex;
}

namespace RendererGpu {
    inline constexpr float FLAT_NEAR_PLANE = 0.1f;

    inline constexpr int RENDER_WALL = 0;
    inline constexpr int RENDER_FLAT = 1;
    inline constexpr int RENDER_SPRITE = 2;
    inline constexpr int RENDER_COLLIDER = 4;
    inline constexpr int RENDER_MODEL = 5;
    inline constexpr int RENDER_ENTITY_MARKER = 6;
    inline constexpr int RENDER_EDITOR_GIZMO = 7;
    inline constexpr int RENDER_GATEWAY = 8;

    inline constexpr int ATLAS_SIZE = 4096;
    inline constexpr int ATLAS_PADDING = 2;

    // Pixel size RenderTextRaw's scale 1 stands for (the console and debug text).
    inline constexpr float UI_FONT_SIZE = 48.0f;
    // Inset of UI Text from its rectangle's top-left, at the project's UI Reference Height.
    inline constexpr float UI_TEXT_PADDING = 8.0f;

    // Glyphs are rasterized the first time they're drawn, into single-channel
    // GLYPH_PAGE_SIZE square atlas pages. When GLYPH_MAX_PAGES are full the
    // whole atlas is dropped and refilled with whatever is drawn next.
    inline constexpr int GLYPH_PAGE_SIZE = 1024;
    inline constexpr int GLYPH_MAX_PAGES = 8;
    inline constexpr int GLYPH_PADDING = 2;
    inline constexpr unsigned MAX_GLYPH_PIXEL_SIZE = 512;

    // Vertices per collider outline (Rendering.vs.glsl), per entity marker.
    inline constexpr int COLLIDER_VERTICES_PER_COLLIDER = 24 * 6;
    inline constexpr int VERTICES_PER_ENTITY_MARKER = 6;

    // However many gateways are in sight, a frame draws at most this many
    // views through them.
    inline constexpr int MAX_GATEWAY_VIEWS = 32;

    // One rasterized glyph: a font face, a pixel size and a glyph index
    // (not a character, so fallback and "missing" glyphs share the cache).
    struct GlyphKey {
        FT_Face face = nullptr;
        unsigned pixelSize = 0;
        unsigned glyphIndex = 0;

        bool operator==(const GlyphKey&) const = default;
    };

    struct GlyphKeyHash {
        size_t operator()(const GlyphKey& key) const noexcept {
            size_t hash = std::hash<const void*>{}(key.face);
            hash ^= std::hash<unsigned>{}(key.pixelSize) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<unsigned>{}(key.glyphIndex) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct Glyph {
        int page = -1;              // atlas page, -1 = nothing to draw (spaces)
        Vector4 uv;                 // uMin, vMin, uMax, vMax
        Vector2 size;               // bitmap size in pixels
        Vector2 bearing;            // left, top: from the pen position to the bitmap's top-left
        float advance = 0.0f;       // pixels to the next pen position
    };

    // Structs that will be pushed to the GPU
    struct GpuFlatTriangle {
        Vector4 a;
        Vector4 b;
        Vector4 c;
        Vector4 color;
        Vector4 data;
    };

    struct GpuWall {
        Vector4 startEnd;
        Vector4 color;
        Vector4 heights;
        Vector4 data;
        //data.x = texture region/index;
        //data.y = 0 seen from the front sector, 1 from the back;
        //data.z = texture anchor height (world height of V = 0);
        //data.w = SurfaceMetaData flags of this piece;
        Vector4 data2;
        //x = textureOffset.x
        //y = textureOffset.y
        //z = textureScale.x
        //w = textureScale.y
        Vector4 data3;
        //x = flipX
        //y = flipY
        //z = distance from the wall's start to the piece's start
        //w = length of the whole wall
    };

    struct alignas(16) IntVector4 {
        int x = -1;
        int y = -1;
        int z = -1;
        int w = -1;
    };

    struct GpuSprite {
        Vector4 positionSize;
        Vector4 color;

        IntVector4 textureIndices0; // N, NE, E, SE
        IntVector4 textureIndices1; // S, SW, W, NW

        Vector4 data;
        // data.x = sprite width / scale.x
        // data.y = sideCount
        // data.z = facing.x (rotation's local +Z on the map)
        // data.w = facing.z

        Vector4 rotation;
        // Quaternion: x, y, z, w

        IntVector4 flags;
        // flags.x = isStatic
    };

    static_assert(sizeof(GpuSprite) == 112);

    struct GpuSector {
        Vector4 floorData; // x = offset into sectorFloors, y = floor count
        // minX, minY, maxX, maxY of the sector's flat triangles in map XY -
        // the rectangle slopes are measured from (WallPieces::SectorBounds).
        Vector4 bounds;
    };

    struct GpuSectorFloor {
        Vector4 heights;
        Vector4 slopeData;
        Vector4 floorColor;
        Vector4 ceilingColor;
        Vector4 textureData; // x/y = floor/ceiling texture region, z/w = floor/ceiling SurfaceMetaData flags

        // xy = floor offset, zw = ceiling offset
        Vector4 textureOffsets;

        // xy = floor flip X/Y, zw = ceiling flip X/Y
        Vector4 textureFlip;
        //todo TILKYTODO bitpacking

        // xy = floor scale X/Y, zw = ceiling scale X/Y
        Vector4 textureScales = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct GpuCollider {
        Vector4 positionType; // World x, y, z; type. 0 = sphere, 1 = AABB
        Vector4 scale; // if sphere, x = radius. if box use vec3 is x y z
    };

    static_assert(sizeof(GpuCollider) == sizeof(float) * 8);

    // One corner of a runtime editor gizmo (facing arrows, camera frustums),
    // built on the CPU in world space.
    struct GpuEditorGizmoVertex {
        Vector4 position; // w unused
        Vector4 color;
    };

    static_assert(sizeof(GpuEditorGizmoVertex) == sizeof(float) * 8);

    // One entity drawing a model. Matches ModelInstance in Rendering.vs.glsl
    // (std430). Matrices are column major.
    struct alignas(16) GpuModelInstance {
        float modelMatrix[16]{};  // mat4: transform.position * rotation * scale
        float normalMatrix[12]{}; // mat3: std430 pads each column to a vec4
        Vector4 color;            // rgb = sector light, a = SurfaceMetaData flags
    };

    static_assert(sizeof(GpuModelInstance) == 128);

    // A standalone texture (not in the atlas): previews, the sky, the 2D
    // editor's images. `id` is the backend's handle for it, 0 = none.
    struct GPUTexture {
        unsigned id = 0;
        int width = 0;
        int height = 0;

        // The file it was loaded from, so an atlas rebuild can reload just
        // the textures whose file changed.
        std::filesystem::path path;
        std::filesystem::file_time_type writeTime{};
    };

    struct GPUTextureRegion {
        Vector4 uvRect; // x = uMin, y = vMin, z = uMax, w = vMax
        Vector4 data;   // x = valid, y = atlas page (texture array layer), z/w = image width/height in pixels
    };

    // An image decoded to tightly packed RGBA8, kept between atlas rebuilds
    // until its file changes on disk.
    struct DecodedImage {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::filesystem::file_time_type writeTime;
    };

    // Model helpers shared by the backends' model code.
    namespace Models {
        // How long a loaded model stays in memory after the last entity using
        // it is removed or switched to another file. Long enough that a script
        // swapping between two models does not reload them from disk every time.
        inline constexpr Uint64 MODEL_UNUSED_LIFETIME_MS = 5000;

        void ToColumnMajor(const Matrix4& matrix, float out[16]);
        // Inverse transpose of the upper 3x3, column major.
        void ToNormalMatrix(const Matrix4& matrix, float out[9]);
        GpuModelInstance BuildModelInstance(const ComponentTransform& transform, const Vector3& offset, const Level& level, bool fullbright);
    }

    // Gateway view math shared by the backends (see OpenGLGateway.cpp).
    namespace GatewayMath {
        // The view through a gateway keeps what is in front of its exit wall
        // and a hair behind it.
        inline constexpr float CLIP_EPSILON = 0.001f;

        float BoxDepth(const ComponentCamera& camera);
        bool NeedsWholeBox(const Gateways::Opening& opening, Vector2 eye, float boxDepth);
        Matrix4 ExitToEntry(const Gateways::Opening& opening);
        std::array<Vector3, 4> OpeningCorners(const Gateways::Opening& opening);
        bool OpeningScreenRect(const Gateways::Opening& opening, const Matrix4& viewProjection,
                               int screenWidth, int screenHeight, const int bounds[4], int out[4]);
        // The opening's box: 8 corners (front 0-3, behind 4-7) as x, y, z, 1.
        std::array<float, 8 * 4> BoxCorners(const Gateways::Opening& opening, float depthBehind);
    }
}

class RendererCore : public IRenderer {
public:
    int CreateTexture(const std::string& fileName) override;

    void RefreshTexturesFromLevel() override;
    void RefreshTexturesIfLevelNeedsNew() override;

    void RenderTextRaw(const std::string& text, Vector2 position, Vector2 scale, Vector3 color) override;
    [[nodiscard]] FontMetrics GetFontMetrics(const std::string& font, float pixelSize) override;
    [[nodiscard]] float MeasureTextLine(const std::string& font, float pixelSize, const std::string& text) override;
    void DrawTextLine(const std::string& font, float pixelSize, const std::string& text, Vector2 baseline, Vector4 color) override;

    [[nodiscard]] ImTextureID GetImGuiTextureID(const std::string& fileName) override;
    [[nodiscard]] bool FindLoadedTexture(const std::string& fileName, LoadedTexture* out) const override;
    bool AddDecodedTexture(const std::string& fileName, const std::filesystem::path& path, const void* pixels, int width, int height) override;

    bool BuildTextureAtlasFromLevel();
    const RendererGpu::DecodedImage* GetDecodedImage(const std::filesystem::path& path);

    void UpdateBackgroundLoad() override;
    void FinishBackgroundLoad() override;

    void BeginTexturePreload(const std::vector<std::string>& imageNames) override;
    [[nodiscard]] std::pair<size_t, size_t> TexturePreloadProgress() const override;
    void FinishTexturePreload() override;
    [[nodiscard]] bool IsBackgroundLoading() const override;
    [[nodiscard]] BackgroundLoadProgress GetBackgroundLoadProgress() const override;

protected:
    using Glyph = RendererGpu::Glyph;
    using GlyphKey = RendererGpu::GlyphKey;
    using GpuFlatTriangle = RendererGpu::GpuFlatTriangle;
    using GpuWall = RendererGpu::GpuWall;
    using GpuSprite = RendererGpu::GpuSprite;
    using GpuSector = RendererGpu::GpuSector;
    using GPUTexture = RendererGpu::GPUTexture;
    using GpuCollider = RendererGpu::GpuCollider;
    using GpuSectorFloor = RendererGpu::GpuSectorFloor;
    using GpuModelInstance = RendererGpu::GpuModelInstance;

    // ---- Backend hooks ----

    // The atlas: a 2D array texture of ATLAS_SIZE square RGBA8 pages.
    [[nodiscard]] virtual bool AtlasExists() const = 0;
    [[nodiscard]] virtual int MaxAtlasPages() const = 0;
    virtual void CreateAtlasGpu(int pageCount) = 0;
    virtual void UploadAtlasPage(int page, const unsigned char* pixels) = 0;
    // After every page is uploaded: sampling for the level's setting and the
    // region list (textureRegions) for the shaders.
    virtual void FinishAtlasGpu(RendererTextureSettings setting) = 0;
    virtual void DestroyAtlasGpu() = 0;
    // A late texture, already padded, into a built atlas: then
    // RefreshAtlasGpu redoes the mips and the region list.
    virtual void UploadAtlasRegionGpu(int page, int x, int y, int width, int height, const unsigned char* pixels) = 0;
    virtual void RefreshAtlasGpu(RendererTextureSettings setting) = 0;

    // A standalone RGBA8 texture, mipmapped and repeating. Returns its id (not 0).
    virtual unsigned UploadTextureGpu(const void* pixels, int width, int height) = 0;
    virtual void DestroyTextureGpu(unsigned id) = 0;
    [[nodiscard]] virtual ImTextureID TextureImGuiID(unsigned id) const = 0;

    // The glyph atlas: GLYPH_PAGE_SIZE square single-channel pages, cleared to 0.
    virtual void AddGlyphPageGpu() = 0;
    virtual void UploadGlyphGpu(int page, int x, int y, int width, int height, const unsigned char* coverage) = 0;
    virtual void DestroyGlyphPagesGpu() = 0;
    // Around one RenderTextString: no depth test, alpha blended, `color`
    // (RGBA 0..1, clamped) times each glyph's coverage, top-left origin
    // screenWidth x screenHeight projection. Vertices are x, y, u, v.
    virtual void BeginTextGpu(Vector4 color) = 0;
    virtual void DrawGlyphsGpu(int page, const std::vector<float>& vertices) = 0;
    virtual void EndTextGpu() = 0;

    // ---- Textures and the atlas ----

    std::vector<GPUTexture> textures;
    std::unordered_map<std::string, RendererGpu::DecodedImage> decodedImageCache; // keyed by absolute path
    // Fingerprint of the image list, their file times and the sampling
    // setting the atlas was built from; an unchanged one skips the rebuild.
    size_t atlasSignature = 0;

    // The engine's first atlas, decoding on worker threads (RendererCoreTextures.cpp).
    // Shared so the type can stay in that file; it joins its threads when destroyed.
    struct BackgroundAtlasLoad;
    std::shared_ptr<BackgroundAtlasLoad> backgroundAtlas;
    bool backgroundAtlasStarted = false; // only the first build goes to the background

    // BeginTexturePreload's decode (RendererCoreTextures.cpp).
    struct TexturePreload;
    std::shared_ptr<TexturePreload> texturePreload;
    void CancelBackgroundAtlas();

    std::vector<RendererGpu::GPUTextureRegion> textureRegions;
    std::unordered_map<std::string, int> textureRegionIndexByName;
    std::unordered_map<std::string, int> textureIndexByName;

    // Level textures a refresh already failed to find (missing or broken
    // files), so RefreshTexturesIfLevelNeedsNew doesn't rescan for them after
    // every edit. Cleared by a full RefreshTexturesFromLevel.
    std::unordered_set<std::string> unresolvedLevelTextures;

    // An exported game with per-level sets builds the atlas from the level's
    // set plus one spare page. A texture outside it is noted on first use
    // (missedTextures) and added at the start of the next frame
    // (ServiceLateTextures) where there's room; once there's none the atlas
    // is rebuilt with it. Late ones are dropped when the level changes.
    struct AtlasCursor {
        int page = 0;
        int x = RendererGpu::ATLAS_PADDING;
        int y = RendererGpu::ATLAS_PADDING;
        int shelfHeight = 0;
    };
    bool lateTexturesEnabled = false;
    int atlasPageCount = 0;
    AtlasCursor atlasCursor;
    std::vector<std::string> lateTextures;
    std::uint64_t lateTexturesGeneration = 0; // AssetSets::SessionGeneration() they belong to
    mutable std::unordered_set<std::string> missedTextures;
    // Editor Play: each region checked against the level's set once (see GetTextureRegionIndex).
    mutable std::vector<char> regionSetChecked;
    mutable std::uint64_t regionSetCheckedGeneration = 0;

    // Places a w x h image at the cursor; false when it doesn't fit in `pageLimit` pages.
    static bool PlaceInAtlas(int width, int height, int pageLimit, AtlasCursor& cursor, int& outPage, int& outX, int& outY);
    void ServiceLateTextures();

    // A model that failed to load may have been imported or fixed since; a
    // full refresh clears this (the backends' model code fills it).
    std::unordered_set<std::string> failedModelFiles;

    int GetOrCreateTextureIndex(const std::string& fileName);
    [[nodiscard]] int GetTextureRegionIndex(const std::string& fileName) const;
    [[nodiscard]] const GPUTexture& GetTexture(int index) const;
    [[nodiscard]] int GetTextureCount() const;
    void DestroyAllTextures();
    // Loads `path` into a new texture. False (and `out` untouched) if it can't be read.
    bool LoadTextureFile(const std::filesystem::path& path, GPUTexture& out);
    // The texture for RGBA8 pixels, set up as LoadTextureFile's are.
    void UploadTexturePixels(const std::filesystem::path& path, const void* pixels, int width, int height, GPUTexture& out);
    // The atlas and its regions only, not the standalone textures.
    void DestroyAtlas();
    // Re-uploads every standalone texture whose file changed on disk since
    // it was loaded. Indices (and with them textureIndexByName) stay valid.
    void ReloadChangedTextures();

    // A surface's offset per second so far, wrapped by one repeat of the
    // atlas region it shows, so the GPU only ever sees a small offset.
    [[nodiscard]] Vector2 SurfaceScroll(const SurfaceTexture& texture, int regionIndex) const;

    // ---- Text ----

    FT_Library ft = nullptr;
    FT_Face defaultFontFace = nullptr; // EngineAssets/Fonts/Notosans.ttf, also the fallback for missing characters
    // Project fonts by absolute path. nullptr = failed to load (not retried
    // until RefreshTexturesFromLevel).
    std::unordered_map<std::string, FT_Face> fontFaces;
    std::unordered_map<GlyphKey, Glyph, RendererGpu::GlyphKeyHash> glyphCache;
    int glyphPageCount = 0;
    int glyphCursorX = 0;
    int glyphCursorY = 0;
    int glyphShelfHeight = 0;
    std::unordered_set<std::string> warnedMissingCharacters; // "font|codepoint", warned once

    bool InitializeFontLibrary();
    FT_Face GetFontFace(const std::string& fontReference);
    static void SetFontPixelSize(FT_Face face, unsigned pixelSize);
    const Glyph* GetGlyph(FT_Face face, unsigned pixelSize, unsigned glyphIndex, const std::function<void()>& beforeAtlasReset);
    bool PackGlyph(int width, int height, int& page, int& x, int& y);
    void AddGlyphPage();
    void ResetGlyphAtlas();
    void ReloadFonts();
    void DestroyFonts();
    // Draws UTF-8 text with its first line's baseline at baselineY. '\n'
    // starts a new line; characters the face lacks come from the default font.
    void RenderTextString(FT_Face face, unsigned pixelSize, const std::string& text,
                          float x, float baselineY, float xStretch, Vector4 color);
    // `shown` is what the label says (Level::ShownUIText).
    void RenderUIText(const ComponentUIText& text, const std::string& shown, const ComponentUITransform& transform);
    static unsigned PixelSizeFor(float size);

    // ---- The level's GPU arrays ----

    static constexpr int SECTOR_FLOOR_COUNT = 3;
    static constexpr int SECTOR_HEIGHT_COUNT = SECTOR_FLOOR_COUNT + 1;

    std::vector<GpuSector> gpuSectors;
    std::vector<GpuSectorFloor> gpuSectorFloors;

    std::vector<GpuWall> gpuWalls;
    int gpuWallCount = 0;

    std::vector<GpuFlatTriangle> flatTriangles;
    int flatTriangleCount = 0;

    // Each sector's run of flatTriangles, by sector index -
    // BuildFlatTrianglesFromSectors writes them sector by sector.
    struct FlatSectorRange {
        int first = 0;
        int count = 0;
    };
    std::vector<FlatSectorRange> flatSectorRanges;
    // Each sector's bounds from the same triangles, so slopes on the GPU
    // match the baked flats (GpuSector::bounds).
    std::vector<Vector4> flatSectorBounds;

    // Fingerprint of the sector/floor/triangle layout flatTriangles was built
    // from. Floors can be added or removed while the level is live, so the
    // instance list has to follow them.
    size_t flatLayoutSignature = 0;

    std::vector<GpuSprite> gpuSprites;
    int spriteCount = 0;

    std::vector<GpuCollider> gpuColliders;
    int colliderCount = 0;

    std::vector<Vector4> gpuEntityMarkers; // xyz = position
    int entityMarkerCount = 0;

    std::vector<RendererGpu::GpuEditorGizmoVertex> gpuEditorGizmoTriangles;
    int editorGizmoVertexCount = 0; // triangles

    void BuildGpuSectors();
    void BuildGpuSprites();
    void BuildGpuColliders();
    void BuildGpuEntityMarkers();
    // Skips `cameraOwner`'s gizmos: the view is from its own eye. The view
    // sizes them, so they keep a fixed width on screen.
    void BuildGpuEditorGizmos(ID cameraOwner, const ComponentTransform& viewTransform, const ComponentCamera& viewCamera);
    void BuildGpuWallsFromMap();
    void BuildFlatTrianglesFromSectors();
    // The flat layout no longer matches the one flatTriangles was built from.
    [[nodiscard]] bool FlatLayoutChanged() const;

    // ---- Visibility culling ----

    // Decided each frame by UpdateVisibleSectors. Only on with the game
    // camera (Play and the standalone game); the runtime editor's camera
    // always draws everything.
    bool visibilityCulling = false;
    std::vector<const Sector*> visibleSectors;
    std::vector<uint8_t> sectorVisible; // by sector index
    // Visible or next to a visible sector: an entity standing by a portal
    // can reach into the sector the camera sees.
    std::vector<uint8_t> sectorShowsEntities;

    void UpdateVisibleSectors(const Level& level, const ComponentCamera& camera, const ComponentTransform& renderCameraTransform);
    // The same from a given sector (a gateway's exit room) and view.
    void UpdateVisibleSectorsFrom(const Level& level, int startSector, const Matrix4& viewProjection);
    [[nodiscard]] bool IsSectorCulled(int sectorIndex) const;
    [[nodiscard]] bool IsWallCulled(const Level& level, const Wall& wall) const;
    [[nodiscard]] bool IsEntityCulled(const ComponentTransform& transform) const;

    // ---- Camera ----

    enum class FrameCamera {
        Ready,
        NoCamera, // in game, no active camera or its entity is disabled: draw the no-camera screen
        Failed
    };

    // The camera this frame is drawn from: the editor camera, or the level's
    // active one.
    FrameCamera ResolveFrameCamera(Level& level, ComponentCamera*& camera, ComponentTransform*& cameraTransform);
    // Where the eye is: the game camera's transform lifted by its player
    // controller's eye height, with step smoothing; the editor camera's own.
    ComponentTransform ComputeRenderCameraTransform(Level& level, ComponentCamera& camera, const ComponentTransform& cameraTransform);

    // This frame's working gateways.
    std::vector<Gateways::Opening> gatewayOpenings;

    // One view through a gateway (see OpenGLGateway.cpp).
    struct GatewayView {
        ComponentCamera camera;           // matrices already built
        Vector3 eye;
        const Wall* exitWall = nullptr;   // the wall this view looks out of
        Vector4 clipPlane;                // world (x, y, z, 1) dot plane >= 0 is kept
        bool clipped = false;
        int depth = 0;                    // gateways gone through
        int scissor[4] = {0, 0, 0, 0};    // x, y, width, height in pixels, from the bottom left
    };

    struct SeenGateway {
        const Gateways::Opening* opening;
        int scissor[4];
    };

    // The openings `view` shows, before any is drawn: drawing a view through
    // one replaces the visibility this view was culled with.
    std::vector<SeenGateway> CollectSeenGateways(const GatewayView& view) const;
    // The view through `opening` from `view`.
    static GatewayView ViewThrough(const GatewayView& view, const SeenGateway& seen);

    // ---- UI ----

    // Every visible UI Sprite (color 0..255, the texture it shows) and UI
    // Text, in draw order.
    static void ForEachUIDraw(
        Level& level,
        const std::function<void(const ComponentUITransform& transform, const Vector4& color255, const std::string& texture)>& sprite,
        const std::function<void(const ComponentUIText& text, const ComponentUITransform& transform)>& text);

    // ---- 2D editor labels ----

    struct PlacedEditor2DLabel {
        const Editor2D::Label* label;
        FT_Face face;
        float x;
        float baseline;
    };

    // Where each of `layer`'s labels goes, and the backdrops (pills, tags)
    // drawn under them.
    void LayoutEditor2DLabels(const Editor2D::Frame& frame, int layer, std::vector<PlacedEditor2DLabel>& placed,
                              std::vector<Editor2D::GpuOverlayVertex>& backdrops);
    // The background image's tiles as one quad; false when there's nothing to draw.
    static bool BuildEditor2DBackground(const Editor2D::Frame& frame, const GPUTexture& texture, std::vector<Editor2D::GpuOverlayVertex>& quad);
};

#endif
