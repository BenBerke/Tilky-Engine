#pragma once

#include "../../Headers/Editor/Editor.hpp"
#include "../../Headers/Editor/AssetBrowser.hpp"

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace MapEditorInternal {
    constexpr int FONT_SIZE = 24;

    extern int screenWidth;
    extern int screenHeight;

    extern float editorZoom;
    extern float GRID_SIZE;
    constexpr float MIN_EDITOR_ZOOM = 0.20f;
    constexpr float MAX_EDITOR_ZOOM = 5.00f;

    constexpr float ENTITY_SIZE = 32.0f;

    enum Mode {
        MODE_DOT,
        MODE_SECTOR,
        MODE_ENTITY,

        MODE_COUNT
    };

    enum Action {
        ACTION_CREATE_SECTOR,
        ACTION_CREATE_WALL,
        ACTION_CREATE_CORNER, // historical name - now fires for Dot placement/undo.
        ACTION_CREATE_OBJECT,
        ACTION_APPLY_GEOMETRY, // one MapEditorInternal::ApplyDrawnGeometry call, however many walls/sectors it touched internally.
    };

    enum State {
        STATE_MAP,
        STATE_UI,

        STATE_COUNT,
    };

    enum EditorTheme {
        THEME_DARK,
        THEME_LIGHT
    };

    // A Dot is a free-floating, off-grid-capable editor
    // reference point with a stable ID. It is editor-session data (kept in
    // MapEditorInternal, NOT in Level/LevelSerialization used as a visual aid and as a snap target for sector chains.
    struct Dot {
        ID id = INVALID_ID;
        Vector2 position{};
    };

    // Snapshot of the sector-creation parameters from the Editor menu, taken
    // the instant a chain starts
    struct PendingSectorParams {
        std::string wallTexture;

        std::vector<SectorFloor> floors = {
            {
                {0.0f, std::numeric_limits<uint_fast32_t>::max(), {}},
                {40.0f, std::numeric_limits<uint_fast32_t>::max(), {}}
            }
        };

        Vector3 lightValue = {255.0f, 255.0f, 255.0f};
        uint_fast32_t wallColor = std::numeric_limits<uint_fast32_t>::max();
    };

    // Whole-operation undo snapshot for ApplyDrawnGeometry - it can create
    // and split many walls and sectors in one call, so a single
    // ACTION_APPLY_GEOMETRY entry carries exactly one of these rather than
    // pushing a separate undo action per internal wall split. Captured
    // before the call is attempted; only kept (pushed onto
    // geometrySnapshots) if the call actually succeeds.
    struct GeometrySnapshot {
        std::vector<Wall> walls;
        std::vector<Sector> sectors;
        ID nextWallID = 0;
        ID nextSectorID = 0;

        ID selectedSectorID = INVALID_ID;
        ID selectedWallID = INVALID_ID;
        ID selectedDotID = INVALID_ID;
        bool editingSector = false;
        bool editingWall = false;
    };

    // Internal variables do not touch
    extern std::vector<Action> actions;

    // Undo stack for ACTION_APPLY_GEOMETRY entries - kept parallel to
    // `actions` (one entry per ACTION_APPLY_GEOMETRY present in it), not
    // indexed by position within it.
    extern std::vector<GeometrySnapshot> geometrySnapshots;

    // Set by ApplyDrawnGeometry whenever it rejects an edit (cleared on
    // success); MapEditorUI.cpp surfaces it as a toast once per rejection.
    extern std::string lastGeometryError;

    extern SDL_Window* window;
    extern SDL_Renderer* renderer;
    extern TTF_Font* font;
    extern TTF_TextEngine* textEngine;

    extern ImFont* scriptEditorFont;

    // --

    extern Vector2 cameraPos;

    extern std::vector<Dot> dots;
    extern ID nextDotID;
    extern std::unordered_map<ID, int> dotIDToIndex;
    extern ID selectedDotID;

    // Sector creation chain
    extern std::vector<Vector2> sectorBeingCreated;
    extern PendingSectorParams pendingSectorParams;

    extern bool editingSector;
    extern ID selectedSectorID; // stable ID, NOT a vector index

    extern bool editingComponent;
    extern bool editingEntity;
    extern Entity selectedEntity;
    extern float entitySize;

    extern std::string currentMap;

    extern Mode currentMode;
    extern State currentState;

    extern EditorTheme currentTheme;
    extern bool textureViewMode;

    extern bool playerPlaced;

    extern int currentFloor;

    extern bool quit;
    extern bool play;
    extern bool shutdown;
    extern bool switchToRuntime;

    extern bool hasEntityInClipboard;
    extern Entity entityInClipboard;

    //-- Sector to be created variables
    extern std::string wallTexture;
    extern std::string ceilTexture;
    extern std::string floorTexture;

    extern float floorHeight;
    extern float ceilHeight;

    extern Vector3 lightValue;

    extern uint_fast32_t wallColor;
    extern uint_fast32_t ceilColor;
    extern uint_fast32_t floorColor;

    extern bool manualSectorMode;
    extern std::vector<Vector2> manualSectorDots;

    // Dot Mode — Wall inspector (right-click select)
    extern bool editingWall;
    extern ID selectedWallID;

    // Shared Asset Browser instance, rooted at the project's Assets folder.
    // Defined in MapEditorUI.cpp so both it and ImGuiDrawFunctions.cpp (via
    // this header) can reach the one shared browser and its pending
    // double-click-to-assign selection.
    extern AssetBrowser assetBrowser;
    extern bool assetBrowserInitialized;

    // =========================================================================
    //  UI Editor — shared state
    // =========================================================================
    // Added alongside the UIEditorUI.cpp / UIEditorDraw.cpp / UIEditorInput.cpp
    // implementation. Mirrors the existing conventions above: selection is a
    // bare stable ID (like selectedSectorID/selectedDotID/selectedWallID, NOT
    // a cached Entity copy — resolve via LevelManager::CurrentLevel().GetEntity()
    // right before use), and view/overlay state is plain extern data owned by
    // EditorState.cpp.
    //
    // The UI canvas renders full-screen via UIEditorDraw() (the same way
    // DrawGridDots()/DrawWalls()/DrawEntities() etc. draw the Map Editor's
    // view directly onto the SDL window, unclipped, with ImGui panels
    // floating on top) rather than into a bounded ImGui child panel — so
    // uiCanvasPan/uiCanvasZoom play exactly the role cameraPos/editorZoom
    // play for the Map Editor: uiCanvasPan is the canvas-space point
    // currently at the centre of the screen.

    constexpr float MIN_UI_CANVAS_ZOOM = 0.10f;
    constexpr float MAX_UI_CANVAS_ZOOM = 8.00f;

    // Selection within the UI Editor. Independent of selectedEntity/editingEntity
    // above, which belong to the Map Editor's MODE_ENTITY and are left untouched.
    extern ID selectedUIEntityID;
    extern ID hoveredUIEntityID; // recomputed each frame in HandleUIEditorInput(); read-only elsewhere, for hover highlighting.

    // Canvas pan/zoom. uiCanvasPan is the canvas-space point currently at
    // the centre of the screen (mirrors cameraPos's role exactly).
    extern Vector2 uiCanvasPan;
    extern float uiCanvasZoom;

    // The design resolution the canvas previews against. ComponentUITransform
    // anchors are fractions (0..1) of this rect; (0,0) is the top-left corner.
    extern Vector2 uiTargetResolution;

    // Canvas overlay toggles, set from the toolbar in DrawUIEditorUI().
    extern bool showUIGrid;
    extern bool showUICenterLines;
    extern bool showUISafeArea;

    // Canvas-space (0,0 = top-left of uiTargetResolution) <-> screen-space,
    // through the current uiCanvasPan/uiCanvasZoom and screenWidth/screenHeight
    // (same inputs WorldToScreen/ScreenToWorld use for the Map Editor's own
    // camera). Defined in UIEditorUI.cpp.
    [[nodiscard]] Vector2 UICanvasToScreen(const Vector2& canvasPos);
    [[nodiscard]] Vector2 ScreenToUICanvas(const Vector2& screenPos);

    [[nodiscard]] bool SamePoint(const Vector2& a, const Vector2& b);
    [[nodiscard]] bool WithinRadius(const Vector2& a, const Vector2& b, float radius);
    [[nodiscard]] Entity* EntityAt(const Vector2& mouseClick);
    [[nodiscard]] bool HasLineBetween(const Vector2& a, const Vector2& b);

    // Automatically strips a trailing duplicate-of-front vertex if one is
    // ever present (defensive; the new chain flow never adds one). Used by the preview renderer.
    std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate();
    bool IsSectorClosed(const std::vector<Vector2>& vertices);
    void AddSectorSelectionPoint(const Vector2& point);

    // Sector chain workflow
    void TrySectorChainClick(const Vector2& resolvedPoint);
    void FinishSectorSelection();
    void CancelSectorChain();

    // Inserts `drawnPoints` into the current level's wall graph and
    // rebuilds every sector touched by the edit (see MapTopology.hpp for
    // the underlying, editor-independent algorithm). `drawnPoints` is an
    // open polyline - repeat the first point at the end to close a loop
    // back on itself. Returns false and leaves the level untouched if
    // the edit is rejected (see lastGeometryError for why); on success,
    // pushes one ACTION_APPLY_GEOMETRY undo entry and selects one of the
    // resulting sectors.
    bool ApplyDrawnGeometry(const std::vector<Vector2>& drawnPoints, const PendingSectorParams& params);

    // Restores level walls/sectors/ID counters and the affected
    // selection from a GeometrySnapshot, then rebuilds runtime links.
    // Used only by the ACTION_APPLY_GEOMETRY case of the undo handler.
    void RestoreGeometrySnapshot(const GeometrySnapshot& snapshot);

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos);
    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos);
    Vector2 SnapToGrid(const Vector2& worldPos);

    // Snapping nearest of {dots, wall starts, wall ends} within
    // radius, else grid snap.
    Vector2 ResolveSnapPoint(const Vector2& mouseWorld);

    [[nodiscard]] bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point);

    float GetActiveGridSize();

    void DrawThickLine(SDL_Renderer* renderer, Vector2 start, Vector2 end, float thickness);
    void DrawFilledTriangle(const Triangle& triangle, SDL_FColor color);
    void DrawFilledTriangleTextured(const Triangle& triangle, SDL_Texture* texture, SDL_FColor tint);
    void DrawSectorPreview();
    void DrawSnapIndicator();
    void DrawExistingSectors();
    void DrawDots();
    void DrawWalls();
    void DrawEntities();
    void DrawGridDots();

    void HandleEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);
    void DrawEditorUI();

    void UIEditorDraw();
    void DrawUIEditorUI();
    void HandleUIEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);

    void ChangeMode();
    void ApplyEditorTheme(EditorTheme theme);

    bool Save(const std::string& saveTo);

    float DistancePointToSegmentSq(const Vector2& point, const Vector2& a, const Vector2& b);
    int GetWallAtPoint(const Vector2& worldPoint);

    // Dot lifecycle.
    void AddDot(const Vector2& position);
    void RemoveDot(ID dotID);

    // Sector deletion with full ID-safety cleanup.
    void DeleteSector(ID sectorID);

    void DeleteWall(ID wallID);

    void HandleEntityModeLeftClick(const Vector2& point);
    void HandleEntityModeRightClick(const Vector2& point);
    void HandleSectorModeRightClick(const Vector2& point);
    void HandleDotModeRightClick(const Vector2& point);

    // Texture preview / Texture View Mode access point. Textures are
    // identified by name (relative to the Textures folder), not by index.
    // Returns nullptr safely if the file is missing or unavailable - never
    // crashes on a missing texture.
    SDL_Texture* GetEditorTexture(const std::string& textureFileName);
    void DrawTextureThumbnailBox(const std::string& textureFileName, float size);
    void DrawTextureThumbnailRow(const std::string& textureFileName);

    // Drag-and-drop / click-to-assign field for referencing a project
    // asset (texture, sound, or script) by name. Draws `label`, the
    // current value (or a placeholder), and a Clear button; for Texture
    // fields with previewSize > 0, also draws an inline thumbnail.
    // Accepts a drop from the Asset Browser, or a click here right after
    // double-clicking a matching asset there. Returns true the frame
    // `value` changes.
    bool DrawAssetField(const char* label, std::string& value, AssetKind kind, float previewSize = 0.0f);

    // Call this from your SDL event loop whenever you receive
    // SDL_EVENT_DROP_FILE while the Map Editor window is active, e.g.:
    //
    //   case SDL_EVENT_DROP_FILE:
    //       MapEditorInternal::HandleAssetBrowserFileDrop(
    //           event.drop.windowID, event.drop.x, event.drop.y, event.drop.data);
    //       break;
    //
    // Safe to call unconditionally - it no-ops if the drop didn't land on
    // the Asset Browser panel, or if the Map Editor window isn't the one
    // the drop occurred over.
    void HandleAssetBrowserFileDrop(SDL_WindowID windowID, float x, float y, const char* filePath);

    void QueueLevelLoad(const std::string& levelName);
    bool ProcessPendingLevelLoad();

    void UpdateLevels();

    void ClearManualSectorSelection();
    void CreateManualSector();
}