#ifndef TILKY_ENGINE_IRENDERER_HPP
#define TILKY_ENGINE_IRENDERER_HPP

#ifndef TILKY_STANDALONE
#include <imgui.h>
#endif
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Runtime/Renderer/SurfaceMetaData.hpp"

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // startHidden: the engine creates the game window once at launch and
    // shows it with SetWindowVisible(true) for each Play session.
    virtual bool Initialize(std::string windowName, bool startHidden) = 0;
    virtual void Shutdown() = 0;

    virtual void SetWindowVisible(bool visible) = 0;

    // Makes this renderer's graphics and ImGui contexts current. The editor
    // keeps its own alive next to them, so call this before using either.
    virtual void MakeCurrent() = 0;

    // Swap interval of the game window: 1 VSync, -1 adaptive VSync, 0 off.
    // Call with this renderer's context current (after MakeCurrent()).
    virtual void SetVSync(int mode) = 0;

    enum class DrawMode : bool {
        WIREFRAME = false,
        SOLID = true
    };

    virtual void SetDrawMode(DrawMode mode) = 0;

    // Session state that shouldn't outlive one Play / runtime editor session.
    void EndSession() {
        useEditorCamera = false;
        DestroyEditorCamera();
        ClearSurfaceMarks();
        fullbright = false;
        entityMarkers = EntityMarkers::Off;
    }

    virtual void BeginFrame() = 0;
    virtual void Update(bool renderDebug, bool renderUI) = 0;
    virtual void EndFrame() = 0;

#ifndef TILKY_STANDALONE
    // Not in the standalone game, which draws nothing with ImGui.
    virtual void BeginImGuiFrame() const = 0;
    virtual void EndImGuiFrame() const = 0;
#endif

    int screenWidth = 1600, screenHeight = 900;
    virtual void OnResize(int width, int height) = 0;

    [[nodiscard]] virtual const char* GetName() const = 0;
    [[nodiscard]] virtual SDL_Window* GetWindow() const = 0;

    virtual int CreateTexture(const std::string& fileName) = 0;

    virtual void RefreshTexturesFromLevel() = 0;

    // RefreshTexturesFromLevel, but only when the level now uses a texture
    // the renderer doesn't have yet - cheap enough to call after every edit.
    // Renderers without a cheaper check just refresh.
    virtual void RefreshTexturesIfLevelNeedsNew() {
        RefreshTexturesFromLevel();
    }

    virtual void RenderTextRaw(const std::string& text, Vector2 position, Vector2 scale, Vector3 color) = 0;

    // Text in a project font (relative to Assets, empty = the default font)
    // at `pixelSize` screen pixels. Characters the font lacks come from the
    // default font.
    struct FontMetrics {
        float ascender = 0.0f;   // baseline below the top of a line
        float lineHeight = 0.0f; // baseline to baseline
    };
    [[nodiscard]] virtual FontMetrics GetFontMetrics(const std::string& font, float pixelSize) = 0;

    // Width of one line of text (no line breaks), in pixels.
    [[nodiscard]] virtual float MeasureTextLine(const std::string& font, float pixelSize, const std::string& text) = 0;

    // One line of text with its baseline starting at `baseline`. RGBA 0..1.
    virtual void DrawTextLine(const std::string& font, float pixelSize, const std::string& text,
                              Vector2 baseline, Vector4 color) = 0;

    // A screen rectangle from its top-left `position`, in pixels: `color`
    // (RGBA 0..1) times `texture`, or solid with an empty texture.
    virtual void DrawScreenRectangle(Vector2 position, Vector2 size, Vector4 color, const std::string& texture) = 0;

    virtual bool CreateMap() = 0;

    // Rebuilds the map's GPU data and textures for a level that replaced
    // the current one while the game runs (Game.LoadLevel). Call after
    // CreateMap() has run once.
    virtual void ReloadMap() = 0;

    void SetUseEditorCamera(const bool enabled) {
        useEditorCamera = enabled;

        if (useEditorCamera) CreateEditorCamera();
    }

    [[nodiscard]] bool IsUsingEditorCamera() const {
        return useEditorCamera;
    }

    [[nodiscard]] ComponentCamera* GetEditorCamera() {
        CreateEditorCamera();
        return editorCamera;
    }

    [[nodiscard]] ComponentTransform* GetEditorCameraTransform() {
        CreateEditorCamera();
        return editorCameraTransform;
    }

#ifndef TILKY_STANDALONE
    [[nodiscard]] virtual ImTextureID GetImGuiTextureID(const std::string& fileName) {
        return static_cast<ImTextureID>(0);
    }

    // The icon font (Font Awesome) in this renderer's own ImGui context, or
    // nullptr. A font from the editor's context can't be drawn here.
    [[nodiscard]] virtual ImFont* GetImGuiIconFont() const {
        return nullptr;
    }
#endif

    // Surfaces to draw differently (see SurfaceMetaData). Kept until
    // cleared, so a caller re-marking every frame clears first.
    void ClearSurfaceMarks() {
        surfaceMarks.clear();
    }

    void MarkSurface(const SurfaceMark& mark) {
        if (mark.metaData != SURFACE_META_NONE) surfaceMarks.push_back(mark);
    }

    // Draws every sector at full light, with no distance darkening or wall
    // shading - for judging textures in dark areas. Ends with the session.
    void SetFullbright(const bool enabled) { fullbright = enabled; }
    [[nodiscard]] bool IsFullbright() const { return fullbright; }

    // A ring on every entity, so ones with nothing to draw (no sprite, model
    // or collider) can still be found. Ends with the session.
    enum class EntityMarkers {
        Off,
        Always,     // drawn over walls too
        VisibleOnly // depth tested, so walls hide them
    };

    void SetEntityMarkers(const EntityMarkers markers) { entityMarkers = markers; }

protected:
    std::vector<SurfaceMark> surfaceMarks;
    bool fullbright = false;
    EntityMarkers entityMarkers = EntityMarkers::Off;

    // A sector's light as the renderer should use it.
    [[nodiscard]] Vector3 SectorLight(const Sector& sector) const {
        return fullbright ? Vector3{255.0f, 255.0f, 255.0f} : sector.light;
    }

    // The flags of every mark on that wall piece / floor or ceiling, OR'd.
    [[nodiscard]] uint8_t WallPieceMetaData(const int wallIndex, const WallSurfaceSlot slot) const {
        uint8_t metaData = SURFACE_META_NONE;

        for (const SurfaceMark& mark : surfaceMarks)
            if (mark.kind == MarkedSurfaceKind::Wall && mark.wallIndex == wallIndex && mark.wallSlot == slot)
                metaData |= mark.metaData;

        return metaData;
    }

    [[nodiscard]] uint8_t FlatMetaData(const int sectorIndex, const int floorIndex, const MarkedSurfaceKind kind) const {
        uint8_t metaData = SURFACE_META_NONE;

        for (const SurfaceMark& mark : surfaceMarks)
            if (mark.kind == kind && mark.sectorIndex == sectorIndex && mark.floorIndex == floorIndex)
                metaData |= mark.metaData;

        return metaData;
    }

    [[nodiscard]] uint8_t EntityMetaData(const ID entityID) const {
        uint8_t metaData = SURFACE_META_NONE;

        for (const SurfaceMark& mark : surfaceMarks)
            if (mark.kind == MarkedSurfaceKind::Entity && mark.entityID == entityID) metaData |= mark.metaData;

        return metaData;
    }

    bool useEditorCamera = false;

    static constexpr ID EDITOR_CAMERA_ENTITY_ID = 0;

    inline static ComponentCamera* editorCamera = nullptr;
    inline static ComponentTransform* editorCameraTransform = nullptr;

    static void CreateEditorCamera() {
        if (editorCamera != nullptr && editorCameraTransform != nullptr) return;

        DestroyEditorCamera();

        editorCamera = new ComponentCamera();
        editorCameraTransform = new ComponentTransform();

        editorCamera->ownerID = EDITOR_CAMERA_ENTITY_ID;
        editorCameraTransform->ownerID = EDITOR_CAMERA_ENTITY_ID;

        editorCameraTransform->position = {0.0f, 0.0f, 10.0f};

        editorCamera->yaw = 0.0f;
        editorCamera->pitch = 0.0f;

        editorCamera->fov = 90.0f;
        editorCamera->nearPlane = 1.0f;
        editorCamera->farPlane = 10000.0f;

        editorCamera->isActive = true;
    }

    static void DestroyEditorCamera() {
        delete editorCamera;
        editorCamera = nullptr;

        delete editorCameraTransform;
        editorCameraTransform = nullptr;
    }
};

#endif