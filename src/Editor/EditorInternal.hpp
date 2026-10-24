#pragma once

#include <functional>
#include "../../Headers/Editor/Editor.hpp"
#include "../../Headers/Editor/AssetBrowser.hpp"

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"
#include "WallSnap.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace MapEditorInternal {
    inline constexpr float UI_FONT_SIZE = 34.0f;
    inline constexpr float LABEL_FONT_SIZE = 15.0f;
    inline constexpr float UI_TEXT_PADDING = 8.0f;

    extern int screenWidth;
    extern int screenHeight;

    extern float editorZoom;
    extern float GRID_SIZE; // fixed world-space grid/snap spacing - NOT affected by editorZoom. Adjustable at runtime via the Grid & Snapping panel.
    constexpr float MIN_EDITOR_ZOOM = 0.10f;
    constexpr float MAX_EDITOR_ZOOM = 10.00f;
    constexpr float MIN_GRID_SIZE = 1.0f;
    constexpr float DEFAULT_GRID_SIZE = 12.0f;

    constexpr float ENTITY_SIZE = 10.0f;

    // Below this world-space size, a drawing tool's shape is considered
    // degenerate (zero-width rectangle, zero-radius circle, a curve
    // whose start/end coincide, etc.) and is refused rather than
    // committed. Shared between the tool logic (MapEditorGeometry.cpp)
    // and the live-preview colouring (MapEditorDrawing.cpp) so the
    // preview's valid/invalid tint always matches what a click would do.
    constexpr float MIN_DRAW_SHAPE_DIMENSION = 0.25f;

    // Hard ceiling on how many step sectors the Staircase tool will ever
    // generate in one commit - guards against a tiny Step Length (or a
    // directly-typed huge Step Count) turning one rectangle drag into
    // thousands of sliver sectors. BuildStaircasePlan rejects with a
    // localised error instead of generating past this.
    constexpr int MAX_STAIRCASE_STEPS = 500;

    // MODE_GEOMETRY took over the slot (and the default) MODE_DOT used
    // to hold: dots are no longer a user-facing object type, and editing
    // the wall graph replaced placing them. Sector selection, editing and
    // creation stay entirely in MODE_SECTOR - MODE_GEOMETRY never edits a
    // sector directly, it edits walls and lets the existing topology pass
    // re-derive the sectors around them.
    enum Mode {
        MODE_GEOMETRY,
        MODE_SECTOR,
        MODE_ENTITY,

        MODE_COUNT
    };

    enum State {
        STATE_MAP,
        STATE_UI,

        STATE_COUNT,
    };

    // Sector Mode's geometry-building tools. DRAWTOOL_FREEHAND covers the
    // pre-existing point-by-point chain (including its manualSectorMode
    // sub-toggle, which is unchanged); the rest are additive.
    enum DrawTool {
        DRAWTOOL_FREEHAND,
        DRAWTOOL_RECTANGLE,
        DRAWTOOL_POLYGON,
        DRAWTOOL_CIRCLE,
        DRAWTOOL_CURVE,
        DRAWTOOL_STAIRCASE,

        DRAWTOOL_COUNT
    };

    // Staircase tool: which inputs determine the step count/height. See
    // BuildStaircasePlan (MapEditorGeometry.cpp) for the exact formulas.
    enum StaircaseCalculationMode {
        STAIRCASE_CALC_STEP_HEIGHT,   // step height + step length -> step count from the rectangle
        STAIRCASE_CALC_TARGET_HEIGHT, // target floor height + step length -> step count, then back-solve step height
        STAIRCASE_CALC_STEP_COUNT,    // step count + step height -> step length from the rectangle

        STAIRCASE_CALC_COUNT
    };

    // Which world axis the staircase progresses along. Horizontal uses the
    // drawn rectangle's width (world X); Vertical uses its height, which is
    // world Z (see the Vector2 world-space convention noted on
    // StaircaseStepSpan below).
    enum StaircaseDirection {
        STAIRCASE_DIR_HORIZONTAL,
        STAIRCASE_DIR_VERTICAL,

        STAIRCASE_DIR_COUNT
    };

    // Which point the Curve tool is currently waiting for.
    enum CurveStage {
        CURVE_STAGE_START,
        CURVE_STAGE_END,
        CURVE_STAGE_CONTROL,
    };

    // A Dot is a free-floating, off-grid-capable editor reference point
    // with a stable ID. It is editor-session data (kept in
    // MapEditorInternal, NOT in Level/LevelSerialization), used as a snap
    // target for sector chains.
    //
    // Dots are INTERNAL ONLY now that Dot Mode has become MODE_GEOMETRY:
    // nothing creates, selects, deletes or lists them from the UI any
    // more. The container and its lifecycle are kept because three live
    // readers still consult it - ResolveSnapPoint (snap candidates),
    // IsPointOnExistingGeometry (did a chain click land on existing
    // geometry) and FindExistingCorner (manual-sector corner picking) -
    // so removing it is a deliberate, separate change to those three,
    // not part of dropping the mode.
    struct Dot {
        ID id = INVALID_ID;
        Vector2 position{};
    };

    // Snapshot of the sector-creation parameters from the Editor menu, taken
    // the instant a chain starts
    struct PendingSectorParams {
        std::string wallTopTexture;
        std::string wallBottomTexture;

        std::vector<SectorFloor> floors = {
            {
                {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
                {40.0f, {1.0f, 1.0f, 1.0f, 1.0f}, {}}
            }
        };

        Vector3 lightValue = {255.0f, 255.0f, 255.0f};
        Vector4 wallColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // Whole-operation undo snapshot for ApplyDrawnGeometry - it can create
    // and split many walls and sectors in one call, so a single
    // UNDO_GEOMETRY step carries exactly one of these rather than
    // pushing a separate undo step per internal wall split. Captured
    // before the call is attempted; only kept (pushed onto undoSteps)
    // if the call actually succeeds.
    struct GeometrySnapshot {
        std::vector<Wall> walls;
        std::vector<Sector> sectors;
        ID nextWallID = 0;
        ID nextSectorID = 0;

        ID selectedSectorID = INVALID_ID;
        ID selectedWallID = INVALID_ID;
        std::vector<ID> selectedWalls;
        bool editingSector = false;
        bool editingWall = false;
    };

    // One generated step of an in-progress or committed staircase. Corners
    // are a closed quad in the same a/b/c/d winding the Rectangle tool
    // uses (see HandleRectangleClick) - the Vector2 world-space
    // convention throughout the Map Editor is (world X, world Z), matching
    // how DrawEntities()/HandleEditorInput() feed transform.x/transform.z
    // into WorldToScreen, so this needs no separate axis mapping.
    struct StaircaseStepSpan {
        Vector2 corners[4];
        float floorHeight = 0.0f;
        float ceilHeight = 0.0f;
        int index = 0; // 0 = bottom/starting step, increasing toward the top
    };

    // Everything BuildStaircasePlan derives from two corners and the
    // current Staircase panel settings: validated, divided into steps,
    // ready either to preview or to commit. The live preview
    // (MapEditorDrawing.cpp), the settings-panel readout
    // (MapEditorUI.cpp) and the commit path (HandleStaircaseClick) all
    // consume the exact same plan, so none of them can disagree about
    // what a given rectangle produces.
    struct StaircasePlan {
        bool valid = false;
        std::string error; // localised; meaningful only when !valid

        // Non-blocking notices - the plan is still valid/creatable.
        bool headroomWarning = false;      // Raise Ceiling is off and >=1 step has <= 0 headroom
        bool targetSingleSectorNote = false; // Target Height mode collapsed to one step (no rise possible)

        std::vector<StaircaseStepSpan> steps;

        int stepCount = 0;
        float effectiveStepHeight = 0.0f;
        float effectiveStepLength = 0.0f;
        float startingFloorHeight = 0.0f;
        float finalFloorHeight = 0.0f;
    };

    // Set by ApplyDrawnGeometry whenever it rejects an edit (cleared on
    // success); MapEditorUI.cpp surfaces it as a toast once per rejection.
    extern std::string lastGeometryError;

    extern SDL_Window* window;
    extern SDL_Renderer* renderer;
    extern TTF_Font* font;
    extern TTF_Font* labelFont; // small canvas labels (wall lengths)
    extern TTF_TextEngine* textEngine;

    extern ImFont* scriptEditorFont;

    // Font Awesome 6 Solid, for the toolbar's icon buttons. nullptr if the
    // font file is missing; the toolbar falls back to short text then.
    extern ImFont* editorIconFont;

    // --

    extern Vector2 cameraPos;

    // Internal snap anchors - see the Dot struct above for why these
    // outlived Dot Mode. Nothing user-facing reads or writes them.
    extern std::vector<Dot> dots;
    extern ID nextDotID;
    extern std::unordered_map<ID, int> dotIDToIndex;

    // Sector creation chain
    extern std::vector<Vector2> sectorBeingCreated;
    extern PendingSectorParams pendingSectorParams;

    extern bool editingSector;
    extern ID selectedSectorID; // stable ID, NOT a vector index

    extern bool editingComponent;
    extern bool editingEntity;
    extern Entity selectedEntity;

    extern std::string currentMap;

    extern Mode currentMode;
    extern State currentState;

    // What the 2D canvas fills sectors with. Its key cycles through these in
    // order. Sprite textures show in every mode but Off.
    enum class TextureViewMode {
        Off,
        Floor,   // the lowest floor's texture
        Ceiling, // that floor's ceiling texture
        Count
    };

    extern TextureViewMode textureViewMode;

    extern int currentFloor;

    extern bool quit;
    extern bool play;
    extern bool shutdown;
    extern bool switchToRuntime;

    // Entities Ctrl+C copied, primary first. Ctrl+V copies whichever of
    // them still exist, so it reflects edits made after copying.
    extern std::vector<ID> entityClipboard;

    //-- Sector to be created variables
    extern std::string wallTopTexture;
    extern std::string wallBottomTexture;
    extern std::string ceilTexture;
    extern std::string floorTexture;

    extern float floorHeight;
    extern float ceilHeight;

    extern Vector3 lightValue;

    extern Vector4 wallColor;
    extern Vector4 ceilColor;
    extern Vector4 floorColor;

    extern bool manualSectorMode;
    extern std::vector<Vector2> manualSectorDots;

    // =========================================================================
    //  Sector Mode — drawing tools (Rectangle / Polygon / Circle / Curve)
    // =========================================================================
    // currentDrawTool selects which of these is active; only one tool's
    // state is ever "in progress" at a time (SetActiveDrawTool/
    // CancelActiveDrawing keep the others reset). All of them ultimately
    // commit through the existing ApplyDrawnGeometry pipeline, the same
    // as the freehand chain above.
    extern DrawTool currentDrawTool;

    // Grid & vertex snapping toggles, read by ResolveSnapPoint.
    extern bool gridSnapEnabled;
    extern bool vertexSnapEnabled;

    // Rectangle tool: two opposite corners.
    extern bool rectangleHasFirstCorner;
    extern Vector2 rectangleFirstCorner;

    // Regular Polygon tool: centre + a radius/rotation handle point.
    constexpr int MAX_POLYGON_SIDES = 64; // upper bound for the [ / ] shortcuts and the UI slider
    extern bool polygonHasCenter;
    extern Vector2 polygonCenter;
    extern int polygonSideCount; // >= 3, <= MAX_POLYGON_SIDES

    // Circle / Ellipse tool: centre + a corner handle point (radiusX/radiusY
    // are derived from it, so a plain click-drag naturally makes an ellipse
    // and holding the constrain modifier makes a circle).
    extern bool circleHasCenter;
    extern Vector2 circleCenter;
    extern int circleSegments; // >= 3

    // Curve tool: start -> end -> control point (quadratic Bezier),
    // approximated with curveSubdivisions straight wall segments.
    extern CurveStage curveStage;
    extern Vector2 curveStart;
    extern Vector2 curveEnd;
    extern int curveSubdivisions; // >= 1

    // Staircase tool: two opposite corners (same click interaction as
    // Rectangle), plus the settings panel's persistent state. All of the
    // settings below deliberately survive SetActiveDrawTool/
    // CancelActiveDrawing switching away and back - only
    // staircaseHasFirstCorner/staircaseFirstCorner (the in-progress
    // rectangle) are tool-in-progress state that gets reset.
    extern bool staircaseHasFirstCorner;
    extern Vector2 staircaseFirstCorner;

    extern StaircaseCalculationMode staircaseCalcMode;
    extern StaircaseDirection staircaseDirection;
    extern bool staircaseRaiseCeiling;

    // Which end of the drawn rectangle is the bottom of the stairs -
    // false keeps the drag-direction default (see BuildStaircasePlan),
    // true flips it. Toggled by a single LALT/RALT press (edge-triggered
    // in HandleEditorInput, MapEditorInput.cpp - all key state reads live
    // there, never in the geometry/preview/UI code that just reads this
    // flag) or directly via the settings panel checkbox.
    extern bool staircaseReverseRise;

    extern float staircaseStepHeight;        // Step Height & Step Count modes
    extern float staircaseStepLength;        // Step Height & Target Height modes
    extern float staircaseTargetFloorHeight; // Target Height mode
    extern int staircaseStepCount;           // Step Count mode, >= 1

    // =========================================================================
    //  Geometry / Wall Edit Mode — selection state
    // =========================================================================
    // Invariants, maintained by SelectWall/ToggleWallSelection/
    // ExtendWallSelectionTo/ClearWallSelection (and by DeleteWall, which
    // drops deleted IDs out of them). Anything that selects a wall should
    // go through those rather than assigning here, so the three stay
    // consistent:
    //
    //   selectedWalls  - the whole selection, possibly empty.
    //   selectedWallID - the "primary" wall, the one the inspector shows.
    //                    Either INVALID_ID or a member of selectedWalls.
    //   editingWall    - whether the wall inspector is open. True exactly
    //                    when selectedWallID != INVALID_ID.
    extern bool editingWall;
    extern ID selectedWallID;

    // Recomputed every frame by HandleEditorInput() while in
    // MODE_GEOMETRY; INVALID_ID in every other mode and whenever ImGui
    // has the mouse. Read-only elsewhere, for the hover highlight.
    extern ID hoveredWallID;

    // The wall under the cursor while Snap to Wall waits for one, for its
    // highlight. INVALID_ID otherwise.
    extern ID wallSnapHoveredWallID;

    // True from the mouse-down that starts a wall body/endpoint drag
    // until that drag is committed.
    extern bool draggingWallGeometry;

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
    // bare stable ID (like selectedSectorID/selectedWallID, NOT
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

    constexpr float MIN_CAM_SPEED = 0.001f;
    constexpr float MAX_CAM_SPEED = 5.0f;
    constexpr float MIN_STEP_SIZE = 1.0f;
    constexpr float MAX_STEP_SIZE = 750.0f;

    constexpr float DEFAULT_RUNTIME_CAMERA_FOV = 90.0f;
    constexpr float MIN_RUNTIME_CAMERA_FOV = 30.0f;
    constexpr float MAX_RUNTIME_CAMERA_FOV = 120.0f;

    constexpr float DEFAULT_TEXTURE_FIELD_SIZE = 200.0f;
    constexpr float MIN_TEXTURE_FIELD_SIZE = 32.0f;
    constexpr float MAX_TEXTURE_FIELD_SIZE = 512.0f;

    // Selection within the UI Editor. Independent of selectedEntity/editingEntity
    // above, which belong to the Map Editor's MODE_ENTITY and are left untouched.
    extern ID selectedUIEntityID;
    extern ID hoveredUIEntityID; // recomputed each frame in HandleUIEditorInput(); read-only elsewhere, for hover highlighting.

    // Every selected UI entity, the primary (selectedUIEntityID, the one the
    // inspector and the gizmo are on) included. HandleUIEditorInput() keeps
    // the two in step: setting selectedUIEntityID from anywhere else makes it
    // the whole selection, and INVALID_ID clears it.
    extern std::vector<ID> selectedUIEntities;

    [[nodiscard]] bool IsUIEntitySelected(ID id);
    // Makes `id` the only selected UI entity (INVALID_ID clears the selection).
    void SelectUIEntity(ID id);
    // Adds `id` as the new primary, or removes it if it's already selected.
    void ToggleUIEntitySelection(ID id);

    // Snapping while moving and resizing on the UI canvas. Screen edges and
    // centre always, the grid and safe area while their overlays are shown,
    // other elements when uiSnapToElements is on.
    extern bool uiSnapEnabled;
    extern bool uiSnapToElements;

    // The canvas grid overlay's spacing and the safe area overlay's margin,
    // which snapping uses too.
    constexpr float UI_CANVAS_GRID_STEP = 32.0f;
    constexpr float UI_SAFE_AREA_FRACTION = 0.05f;

    // ---- UI canvas gizmo (UIEditorInput.cpp, drawn by UIEditorDraw.cpp) ----
    enum UIGizmoPart {
        UI_GIZMO_NONE,
        UI_GIZMO_MOVE_X,
        UI_GIZMO_MOVE_Y,
        UI_GIZMO_MOVE_FREE,
        UI_GIZMO_ROTATE,
        // The 8 resize handles, in UI_RESIZE_HANDLE_DIRECTIONS order.
        UI_GIZMO_RESIZE_FIRST,
        UI_GIZMO_RESIZE_LAST = UI_GIZMO_RESIZE_FIRST + 7,
        UI_GIZMO_BODY
    };

    // Each resize handle's side of the element, -1/0/1 per local axis.
    inline constexpr int UI_RESIZE_HANDLE_DIRECTIONS[8][2] = {
        {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}
    };

    // Screen-space layout of the primary selection's gizmo.
    struct UIGizmoLayout {
        Vector2 corners[4];        // TL, TR, BR, BL of the rotated rect
        Vector2 center;
        Vector2 xTip, yTip;        // move arrows, from `center`
        Vector2 rotateHandle;
        Vector2 rotateStem;        // middle of the top edge
        Vector2 resizeHandles[8];
        bool resizeShown[8];       // false on stretched axes and for edges too short to grab
        bool moveSquareOnTop;      // a small element's move square beats its resize handles
    };

    [[nodiscard]] bool GetUIGizmoLayout(UIGizmoLayout* layout);
    // The part being dragged, else the one under the mouse.
    [[nodiscard]] UIGizmoPart GetHighlightedUIGizmoPart();

    // A snap the current drag landed on, drawn as a guide across the canvas.
    struct UISnapGuide {
        bool vertical;     // a line of constant canvas x
        float canvasValue;
    };

    [[nodiscard]] const std::vector<UISnapGuide>& GetUISnapGuides();

    // The box selection being dragged, in screen space.
    [[nodiscard]] bool GetUIBoxSelection(Vector2* startScreen, Vector2* endScreen);

    // What the current drag shows next to the cursor ("X, Y", "W x H",
    // "45.0 deg"); empty when not dragging.
    [[nodiscard]] const std::string& GetUIDragReadout();

    // The UI editor's unsaved-changes flag (UIEditorUI.cpp).
    void MarkUIUnsavedChanges();

    // Selection actions of the UI editor (UIEditorInput.cpp). Each pushes one
    // undo step when it changed anything.
    enum UIAlignment {
        UI_ALIGN_LEFT,
        UI_ALIGN_CENTER_X,
        UI_ALIGN_RIGHT,
        UI_ALIGN_TOP,
        UI_ALIGN_CENTER_Y,
        UI_ALIGN_BOTTOM
    };

    // One element: against the screen. Several: against their combined bounds.
    void AlignSelectedUIEntities(UIAlignment alignment);
    // Spaces the selection's centres evenly between its two outermost ones
    // (needs 3 or more).
    void DistributeSelectedUIEntities(bool horizontally);
    // Centres the canvas on the selection (or the whole screen) and zooms to fit it.
    void FrameUISelection();
    void DeleteSelectedUIEntities();
    void DuplicateSelectedUIEntities();

    // True once per right click on the canvas, with where it was (canvas
    // space) and whether it was on an element (which is now selected).
    [[nodiscard]] bool ConsumeUICanvasContextMenuRequest(Vector2* canvasPoint, bool* onElement);

    // Canvas pan/zoom. uiCanvasPan is the canvas-space point currently at
    // the centre of the screen (mirrors cameraPos's role exactly).
    extern Vector2 uiCanvasPan;
    extern float uiCanvasZoom;

    // Canvas overlay toggles, set from the toolbar in DrawUIEditorUI().
    extern bool showUIGrid;
    extern bool showUICenterLines;
    extern bool showUISafeArea;

    extern std::vector<ID> selectedEntities;
    extern std::vector<ID> selectedSectors;
    extern std::vector<ID> selectedWalls;

    // User settings colors (0-255)
    extern Vector3 normalEntityColor;
    extern Vector3 highlightedEntityColor;
    extern Vector3 spriteEntityColor;

    extern Vector3 normalWallColor;
    extern Vector3 portalWallColor;
    extern Vector3 highlightedWallColor;

    extern Vector3 hoveredSectorColor;
    extern Vector3 highlightedSectorColor;

    extern Vector3 snapIndicatorColor;

    extern Vector3 kValidLineColor;
    extern Vector3 kInvalidLineColor;
    extern Vector3 kAnchorColor;
    extern Vector4 kValidFillColor;
    extern Vector4 kInvalidFillColor;

    extern Vector3 normalHandleColor;
    extern Vector3 highlightedHandleColor;
    extern Vector3 handleOutlineColor;

    // Gizmo axis colours, shared by the Map Editor's 2D move gizmo and the
    // runtime editor's move/rotate gizmos. A plane handle uses the colour of
    // the axis it is perpendicular to, so the 2D free-drag square (the XZ
    // plane) is gizmoYColor.
    extern Vector3 gizmoXColor;
    extern Vector3 gizmoYColor;
    extern Vector3 gizmoZColor;

    // Sets the Map Editor's unsaved-changes flag, for edits made outside it
    // (the runtime editor). Defined in MapEditorUI.cpp.
    void MarkUnsavedChanges();

    extern Vector3 themeTextColor;
    extern Vector3 gridColor;

    extern Vector3 backgroundColor;

    extern float cameraSpeed;
    extern float cameraStepSize;

    // Field of view of the runtime editor's fly camera, in degrees.
    extern float runtimeCameraFov;

    // Preview size of the inspector's wall and sector texture fields, in pixels.
    extern float textureFieldSize;

    extern bool hierarchyVisible;
    extern bool editorPanelVisible;
    extern bool worldSettingsVisible;
    extern bool assetBrowserVisible;

    // The History windows of the map editor and the runtime editor.
    extern bool historyVisible;
    extern bool runtimeHistoryVisible;

    // Which entity markers the runtime editor draws. Its toolbar button and
    // key cycle through these in order.
    enum class EntityMarkerMode {
        Always,      // through walls too
        VisibleOnly, // hidden behind level geometry
        Off,
        Count
    };

    extern EntityMarkerMode entityMarkerMode;

    // Canvas-space (0,0 = top-left of the current screen, matching how
    // UI_vs.glsl's uPosition/uScreenSize work at runtime - there is no
    // separate design/reference resolution, so the canvas boundary is
    // always exactly screenWidth x screenHeight, not a user-editable
    // value) <-> screen-space, through the current uiCanvasPan/
    // uiCanvasZoom and screenWidth/screenHeight (same inputs
    // WorldToScreen/ScreenToWorld use for the Map Editor's own camera).
    // Defined in UIEditorUI.cpp.
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

    // =========================================================================
    //  Sector Mode — drawing tool workflow
    // =========================================================================

    // Switches the active drawing tool, safely cancelling/discarding any
    // geometry the previously active tool (including the freehand chain
    // and manual-corner selection) had in progress.
    void SetActiveDrawTool(DrawTool tool);

    // Cancels whatever the currently active tool has in progress
    // (freehand chain, manual corner picks, rectangle's first corner,
    // polygon/circle centre, curve's start/end/control) without
    // changing the active tool or mode. Safe to call unconditionally -
    // a no-op if nothing is in progress. Also called automatically on
    // mode switches and level loads.
    void CancelActiveDrawing();

    // True if any drawing tool currently has an incomplete shape in
    // progress (freehand chain non-empty, manual mode active, or a
    // rectangle/polygon/circle/curve has a point placed). Used to keep
    // right-click's "cancel the in-progress shape" and "select/inspect
    // whatever is under the cursor" behaviours from both firing off the
    // same click.
    [[nodiscard]] bool IsDrawingInProgress();

    // Sector Mode's single left-click entry point: routes a raw (not
    // yet snapped) world-space click to whichever tool is active. Each
    // tool resolves its own snapping/constraining internally via the
    // Resolve*/Build* functions below, the same ones the live preview
    // uses, so a click always commits exactly what was last previewed.
    void HandleSectorDrawClick(const Vector2& rawMouseWorld);

    // Backspace: undoes the most recently placed *preview* point for
    // whichever tool/stage is active (one freehand chain point, or one
    // placement stage of rectangle/polygon/circle/curve). No-op if
    // nothing is in progress.
    void UndoLastDrawPoint();

    // Enter: commits the current live preview for whichever tool is
    // active, if it currently describes a valid shape. For the
    // freehand chain specifically this closes the chain early (like
    // clicking back on the start point) rather than adding a point.
    void ConfirmActiveDrawing();

    // True while the angle/proportion "constrain" modifier (Shift) is
    // held - shared by every tool that offers one. Meaning is
    // tool-specific: 45-degree steps for the freehand chain and curve
    // chord, a forced square for the rectangle, 15-degree rotation
    // steps for the regular polygon, and equal radii (a true circle)
    // for the circle/ellipse tool.
    [[nodiscard]] bool IsConstrainModifierHeld();

    // Snaps `target`'s direction from `reference` to the nearest
    // multiple of `stepRadians`, preserving distance.
    [[nodiscard]] Vector2 ConstrainToAngleStep(const Vector2& reference, const Vector2& target, float stepRadians);

    // Each of these resolves the exact point a click would use *right
    // now* for its tool/stage - grid/vertex snapped via
    // ResolveSnapPoint, then constrained on top if the modifier is
    // held. Live preview rendering (MapEditorDrawing.cpp) and the click
    // handlers (MapEditorGeometry.cpp) both call these so committed
    // geometry always matches what was last shown on screen.
    [[nodiscard]] bool SnapToPendingChainPoint(const Vector2& mouseWorld, Vector2* outPoint);
    [[nodiscard]] Vector2 ResolveFreehandPoint(const Vector2& mouseWorld);
    [[nodiscard]] Vector2 ResolveRectangleCorner(const Vector2& mouseWorld); // valid once rectangleHasFirstCorner
    [[nodiscard]] Vector2 ResolvePolygonHandle(const Vector2& mouseWorld);  // valid once polygonHasCenter
    [[nodiscard]] Vector2 ResolveCircleHandle(const Vector2& mouseWorld);   // valid once circleHasCenter
    [[nodiscard]] Vector2 ResolveCurveEnd(const Vector2& mouseWorld);       // valid during CURVE_STAGE_END
    [[nodiscard]] Vector2 ResolveStaircaseCorner(const Vector2& mouseWorld); // valid once staircaseHasFirstCorner

    // True for the two calculation modes that take a user-specified Step
    // Length (Step Height and Target Height) - Step Count mode derives its
    // step length from the rectangle instead. Shared between the panel
    // (which field to show), ResolveStaircaseCorner (whether Shift's
    // whole-step constraint applies) and BuildStaircasePlan.
    [[nodiscard]] bool StaircaseModeUsesStepLength(StaircaseCalculationMode mode);

    // Point generators for the three parametric tools. Never snapped
    // vertex-by-vertex (only the control points that define them are) -
    // snapping every generated point would turn a circle into a jagged
    // mess. Winding is consistently counter-clockwise (matching world
    // space, +Y up) for all three, regardless of which corner/handle
    // the user placed first.
    [[nodiscard]] std::vector<Vector2> BuildRegularPolygon(const Vector2& center, const Vector2& handle, int sideCount);
    [[nodiscard]] std::vector<Vector2> BuildEllipse(const Vector2& center, float radiusX, float radiusY, int segments);
    [[nodiscard]] std::vector<Vector2> BuildQuadraticCurve(const Vector2& start, const Vector2& control, const Vector2& end, int subdivisions);

    // Derives the full staircase layout (validated, divided into steps,
    // with per-step floor/ceiling heights) from two rectangle corners and
    // the current Staircase panel settings. Pure - touches no editor
    // state, so it's safe to call every frame for the live preview and
    // the settings-panel readout as well as at commit time. `plan.valid`
    // is false (with `plan.error` set to a localised message) whenever
    // the rectangle or the current settings can't produce geometry.
    [[nodiscard]] StaircasePlan BuildStaircasePlan(const Vector2& firstCorner, const Vector2& secondCorner);

    // Commits an already-`valid` StaircasePlan as one undoable operation:
    // one MapTopology::ApplyDrawnGeometry call per step (so adjacent steps
    // reuse/split each other's shared boundary wall the same way any two
    // adjacently-drawn sectors would), captured under a single
    // GeometrySnapshot/UNDO_GEOMETRY step rather than one per
    // step. If any step is rejected partway through, every step already
    // applied this call is rolled back and the level is left exactly as
    // it was before the call. Selects every newly created sector on
    // success. Returns false (and sets lastGeometryError) on rejection or
    // if `plan` isn't valid.
    bool CommitStaircasePlan(const StaircasePlan& plan);

    // Lightweight geometry validation shared between the drawing-tool
    // commit path and live-preview colouring. DedupeConsecutivePoints
    // strips consecutive (near-)duplicate points; ClosedLoopSelfIntersects
    // expects a de-duplicated, NOT re-closed ring (no repeated first
    // point) and checks for crossing edges "where practical" (proper
    // crossings only - touching/collinear edges are not flagged).
    [[nodiscard]] std::vector<Vector2> DedupeConsecutivePoints(const std::vector<Vector2>& points);
    [[nodiscard]] bool ClosedLoopSelfIntersects(const std::vector<Vector2>& uniqueRingPoints);

    // Display helpers for the Drawing Tools panel and the on-screen
    // status overlay.
    [[nodiscard]] std::string GetActiveDrawToolName();
    [[nodiscard]] std::string GetActiveDrawToolMeasurementText();

    // Inserts `drawnPoints` into the current level's wall graph and
    // rebuilds every sector touched by the edit (see MapTopology.hpp for
    // the underlying, editor-independent algorithm). `drawnPoints` is an
    // open polyline - repeat the first point at the end to close a loop
    // back on itself. Returns false and leaves the level untouched if
    // the edit is rejected (see lastGeometryError for why); on success,
    // pushes one UNDO_GEOMETRY undo step and selects one of the
    // resulting sectors.
    bool ApplyDrawnGeometry(const std::vector<Vector2>& drawnPoints, const PendingSectorParams& params);

    // Restores level walls/sectors/ID counters and the affected
    // selection from a GeometrySnapshot, then rebuilds runtime links.
    // Used by the UNDO_GEOMETRY case of the undo handler.
    void RestoreGeometrySnapshot(const GeometrySnapshot& snapshot);

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos);
    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos);
    Vector2 SnapToGrid(const Vector2& worldPos);

    // Snapping nearest of {dots, wall starts, wall ends} within
    // radius, else grid snap.
    Vector2 ResolveSnapPoint(const Vector2& mouseWorld);

    // ResolveSnapPoint, minus whatever is currently being dragged:
    // `ignoredPoints` drops those exact endpoints/dots from the point
    // candidates, `ignoredWallIDs` drops those walls from the "closest
    // point along a wall's interior" fallback. Without both, a dragged
    // endpoint snaps onto its own old position (or onto the wall it
    // belongs to) and can never actually move.
    [[nodiscard]] Vector2 ResolveSnapPointExcluding(const Vector2& mouseWorld,
                                                    const std::vector<Vector2>& ignoredPoints,
                                                    const std::vector<ID>& ignoredWallIDs);

    [[nodiscard]] bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point);

    // Render-only: the grid-dot spacing DrawGridDots() should iterate
    // at, so dots don't smear into an unreadable mass when zoomed out.
    // This is GRID_SIZE doubled as many times as needed to keep the
    // on-screen spacing above a minimum pixel threshold - it deliberately
    // changes with editorZoom. NOT used for snapping (see SnapToGrid,
    // which uses the fixed GRID_SIZE directly) and NOT a substitute for
    // the true, always-fixed grid unit.
    float GetActiveGridSize();

    void DrawThickLine(SDL_Renderer* renderer, Vector2 start, Vector2 end, float thickness);
    void DrawFilledTriangle(const Triangle& triangle, SDL_FColor color);
    void DrawFilledTriangleTextured(const Triangle& triangle, SDL_Texture* texture, SDL_FColor tint);
    void DrawSectorPreview();
    void DrawSnapIndicator();
    void DrawExistingSectors();
    void DrawWalls();

    // Geometry Mode's canvas overlay: hover highlight, selection
    // highlight, and the draggable endpoint handles of the selected
    // wall(s). Draws nothing in any other mode.
    void DrawGeometryEditOverlay();
    void DrawEntities();

    // =========================================================================
    //  Entity Mode move gizmo
    // =========================================================================
    // Two arrows (X to the right, Z pointing toward +Z, which is screen-down)
    // and a square (the XZ plane) between them, drawn on the primary selected entity. An
    // arrow moves the whole selection along its axis only, the square moves
    // it freely; both snap to the grid unless Ctrl is held. Dragging the
    // entity body itself stays the unsnapped free drag.

    enum EntityGizmoPart {
        GIZMO_NONE,
        GIZMO_X,
        GIZMO_Z,
        GIZMO_FREE
    };

    // Screen-space shape of the gizmo, in pixels. The parts start just
    // outside the entity's box so clicking the body still free-drags it.
    struct EntityGizmoLayout {
        Vector2 xStart, xTip;
        Vector2 zStart, zTip;
        SDL_FRect square;
    };

    // False when there is nothing to draw: not in Entity Mode, nothing
    // selected, or the primary has no transform. Defined in MapEditorInput.cpp.
    bool GetEntityGizmoLayout(EntityGizmoLayout* layout);

    // The part under `mouseScreen`, square first, then the closer arrow.
    EntityGizmoPart PickEntityGizmoPart(const Vector2& mouseScreen);

    // The part being dragged right now, GIZMO_NONE between drags.
    EntityGizmoPart GetActiveEntityGizmoPart();

    // Defined in MapEditorDrawing.cpp. Call after DrawEntities().
    void DrawEntityMoveGizmo();
    // Defined in MapEditorInput.cpp, next to the box selection it draws.
    // Call after DrawEntities().
    void DrawSelectionBox();
    void DrawGridDots();

    void HandleEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);
    // Save and editor-switch shortcuts. Defined in MapEditorUI.cpp, next to
    // the toolbar buttons that do the same.
    void HandleEditorShortcuts(bool keyboardBlockedByImgui);
    void DrawEditorUI();

    void UIEditorDraw();
    // Closes the fonts the UI canvas opened to preview UI Text. Before TTF_Quit().
    void DestroyUIPreviewFonts();
    void DrawUIEditorUI();
    // Ctrl+S and the key back to the Map Editor. Defined in UIEditorUI.cpp.
    void HandleUIEditorShortcuts(bool keyboardBlockedByImgui);
    // An asset of `kind` dragged from the Asset Browser and released over the
    // canvas (the middle of the dockspace `dockspaceID`) this frame.
    bool AcceptCanvasAssetDrop(ImGuiID dockspaceID, AssetKind kind, std::string& outReference);
    // A texture dropped on the canvas goes to the wall, sector or sprite
    // under the cursor in the current mode (a sector's floor or ceiling by
    // the texture view mode, Off counting as floor). False if nothing took it.
    bool DropTextureOnCanvas(const std::string& texture, const Vector2& mouseWorld);
    // The UI entity inspector (entity window plus its component windows) for
    // an entity the UI editor doesn't have selected - the prefab editor's.
    // idSuffix keeps its windows (and their state) apart from the UI
    // editor's own; *open goes false when the user closes it.
    void DrawUIEntityInspectorFor(Entity& entity, const std::string& windowTitle, const std::string& idSuffix, bool* open,
                                  const std::function<void()>& drawExtras);
    void HandleUIEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);

    void ChangeMode();

    bool Save(const std::string& saveTo);

    // Copies the current level on this thread and writes it on a background
    // one. False when it couldn't start (empty name, a save already running).
    bool StartAutoSave(const std::string& saveTo);
    enum class AutoSaveStatus { Idle, Running, Saved, Failed };
    // Main thread, each frame. Saved / Failed once, when the save finishes.
    AutoSaveStatus PollAutoSave();

    float DistancePointToSegmentSq(const Vector2& point, const Vector2& a, const Vector2& b);
    int GetWallAtPoint(const Vector2& worldPoint);

    // Dot lifecycle. Internal only - nothing user-facing creates or
    // deletes dots any more (see the Dot struct above).
    void AddDot(const Vector2& position);
    void DeleteDot(ID dotID);

    // Sector deletion with full ID-safety cleanup.
    void DeleteSector(ID sectorID);

    // Also drops `wallID` out of the Geometry Mode selection, and runs
    // the post-edit topology rebuild rather than a bare relink.
    void DeleteWall(ID wallID);

    void HandleEntityModeLeftClick(const Vector2& point);
    void HandleEntityModeRightClick(const Vector2& point);
    void HandleSectorModeRightClick(const Vector2& point);

    // =========================================================================
    //  Geometry / Wall Edit Mode
    // =========================================================================
    // Wall picking, selection and editing for MODE_GEOMETRY. Everything
    // here works on walls (and, through shared endpoints, on the wall
    // graph); sectors are never edited directly. RebuildGeometryAfterEdit()
    // hands the changed wall graph back to MapTopology and lets it
    // re-derive them, which is what keeps sector loops/holes/triangles
    // correct after a wall is moved, reshaped or deleted.

    // Both radii are in *pixels* and are converted using the current
    // editorZoom at pick time, so picking is exactly as easy zoomed all
    // the way in as it is zoomed out. The endpoint radius is the larger
    // of the two: when the cursor is near both a handle and the wall
    // body, the handle wins.
    constexpr float WALL_PICK_RADIUS_PIXELS = 10.0f;
    constexpr float WALL_ENDPOINT_PICK_RADIUS_PIXELS = 12.0f;

    // Wall under `mouseWorld` within the screen-space tolerance, or
    // INVALID_ID. Nearest wall wins when several are in range.
    [[nodiscard]] ID PickWallAt(const Vector2& mouseWorld);

    // Endpoint handle under `mouseWorld`, searching *only* the currently
    // selected walls - handles are drawn for those and nothing else, so
    // an unselected wall must never have an invisible grab zone.
    // `outIsStart` distinguishes wall.start from wall.end.
    [[nodiscard]] bool PickSelectedWallEndpointAt(const Vector2& mouseWorld, ID* outWallID, bool* outIsStart);

    // The supported ways to change the wall selection (see the invariants
    // next to selectedWalls above). SelectWall replaces the selection and
    // opens the inspector; ExtendWallSelectionTo mirrors the hierarchy's
    // Shift behaviour, taking every wall whose ID lies between the
    // primary and `wallID`.
    void SelectWall(ID wallID);
    void ToggleWallSelection(ID wallID);
    void ExtendWallSelectionTo(ID wallID);
    void ClearWallSelection();

    // The same four operations for the other two object types, defined in
    // MapEditorInput.cpp next to the wall ones. Before these existed only
    // walls had them, and the sector/entity hierarchy rows assigned
    // selectedSectorID / selectedEntity / selectedSectors / selectedEntities
    // by hand while the canvas never touched the plural lists at all - so
    // the same gesture produced different selection state depending on where
    // you clicked. Everything that selects a sector or an entity now goes
    // through these.
    //
    // Invariants match the wall block above:
    //   selectedSectors / selectedEntities - the whole selection.
    //   selectedSectorID / selectedEntity  - the primary: the object the
    //     inspector shows, and the one every multi-edit is diffed from.
    //     Either unset or a member of the corresponding list.
    //
    // Unlike SelectWall these deliberately do NOT open the inspector.
    // Whether editingSector/editingEntity gets set is the caller's call: the
    // hierarchy opens it on click, a canvas left-click does not. That rule
    // already existed for entities ("left click only selects/places/moves -
    // it must never open the inspector"); sectors now match it.
    void SelectSector(ID sectorID);
    void ToggleSectorSelection(ID sectorID);
    void ExtendSectorSelectionTo(ID sectorID);
    void ClearSectorSelection();

    void SelectEntity(ID entityID);
    void ToggleEntitySelection(ID entityID);
    void ExtendEntitySelectionTo(ID entityID);
    void ClearEntitySelection();

    // Moves every wall endpoint - and every sector boundary vertex -
    // sitting exactly on `from` to `to`. Moving the walls alone is not
    // enough: a sector's `vertices`/`innerLoops` are its own copy of that
    // boundary, so leaving them behind would strand the floor where the
    // wall used to be. Exact coordinate comparison is deliberate and
    // matches the rest of the editor (see IsPointOnExistingGeometry):
    // walls and the sectors built from them store the identical value,
    // they are never two independently computed floats.
    //
    // Sectors touched this way keep their ID, floors, textures, light,
    // colours and entity list - only the moved points and the triangles
    // derived from them change.
    void MoveSharedEndpoint(const Vector2& from, const Vector2& to);

    // One frame of an in-progress drag. `originalWalls`/`originalSectors`
    // are the lists captured when the drag started, and `movingPoints` the
    // endpoint positions (from that same capture) that follow the cursor.
    // Everything is rewritten from the capture each frame rather than
    // nudged by a per-frame delta, so a long drag can't accumulate float
    // drift and a shared corner can't half-follow the cursor.
    //
    // Sectors are edited in place, for the same reason MoveSharedEndpoint
    // does: re-deriving them from the moved walls would hand back
    // brand-new Sector objects and lose everything that isn't geometry.
    void ApplyGeometryDragOffset(const std::vector<Wall>& originalWalls,
                                 const std::vector<Sector>& originalSectors,
                                 const std::vector<Vector2>& movingPoints,
                                 const Vector2& delta);

    // The two post-edit rebuilds. Which one an edit needs depends on
    // whether it changed the *shape* of the level or its *topology*:
    //
    //  - RebuildGeometryAfterMove: for edits that only moved existing
    //    points. The sector layer is still correct, so it is kept exactly
    //    as it is (IDs, floors, textures, light, entities all intact) -
    //    this only re-triangulates and relinks. Use it for wall and
    //    endpoint drags and for coordinate edits in the inspector.
    //
    //  - RebuildGeometryAfterEdit: for edits that changed which faces
    //    exist - a deleted wall can open a sector's boundary, and no
    //    amount of moving points fixes that. Hands the wall graph back to
    //    MapTopology to re-derive the sector layer, falling back to a
    //    plain relink if it can't reconcile the level (the same contract
    //    DeleteSector relies on). This one CAN lose per-sector properties,
    //    because the sectors it produces are newly built.
    //
    // Both are once-per-completed-edit calls, never per frame of a drag.
    void RebuildGeometryAfterMove();
    void RebuildGeometryAfterEdit();

    // End of a Geometry Mode drag. Usually just RebuildGeometryAfterMove,
    // but a drag that dropped geometry onto other walls - a point onto
    // another wall's corner or side, a wall across or along another one -
    // changed the topology, so the walls are planarized and the sectors
    // re-derived (RebuildGeometryAfterEdit) to give every wall the right
    // front/back sector. `movedPoints` are where the dragged points ended
    // up; `draggedWallIDs` every wall that had one of them as an endpoint.
    void RebuildGeometryAfterWallDrag(const std::vector<Vector2>& movedPoints,
                                      const std::vector<ID>& draggedWallIDs);

    // Undo support for geometry edits: capture before touching anything,
    // push only once the edit has actually changed something. `labelKey`
    // (en.json) and `count` name the step in the History window.
    [[nodiscard]] GeometrySnapshot CaptureGeometryUndoSnapshot();
    void PushGeometryUndoSnapshot(GeometrySnapshot snapshot, const char* labelKey, int count = 0);

    // Deletes every wall in the current selection as one undoable
    // operation, with a single topology rebuild at the end.
    void DeleteSelectedWalls();

    // ---- Geometry Mode panel actions (MapEditorGeometry.cpp) ------------
    // Each one is a single undoable operation (a geometry snapshot, pushed
    // only when something changed). Splitting and merging edit the sector
    // outlines in place, like a drag, so sector properties all survive.

    // Splits the wall at `point` (projected onto it). The new half gets a
    // texture offset that keeps the texture continuous. False when the
    // point is too close to either end.
    bool SplitWallAt(ID wallID, const Vector2& point);

    // Splits every selected wall at its midpoint; returns how many split.
    int SplitSelectedWallsAtMidpoint();

    // Joins selected walls that meet end to end in a straight line, have
    // the same sectors on each side and share their corner with no other
    // wall. The first wall of each pair keeps its properties. Returns how
    // many joins were made.
    int MergeSelectedWalls();

    struct WallBatchValues {
        bool applyTopTexture = false;
        bool applyBottomTexture = false;
        bool applyColor = false;

        std::string topTexture;
        std::string bottomTexture;
        Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // Writes the ticked values to every selected wall; returns how many.
    int ApplyToSelectedWalls(const WallBatchValues& values);

    // Starting at the primary selected wall, walks through the connected
    // selected walls and sets each one's horizontal offset (and its Flip X,
    // when the wall runs the other way) so the texture continues across
    // the corners. Vertical offsets are copied from the primary. Top and
    // bottom are aligned separately. Returns how many walls were changed.
    int AlignSelectedWallTextures();

    // Zeroes the top and bottom texture offsets of the selected walls.
    int ResetSelectedWallOffsets();

    // Sets each selected wall's vertical texture scale so the texture fits
    // the tallest visible piece of that slot exactly once, and its vertical
    // offset to 0. Returns how many walls were changed.
    int FitSelectedWallTexturesToHeight();

    // ---- Canvas filters (MapEditorGeometry.cpp) -------------------------
    // Filtered-out walls and entities are drawn dimmed and can't be picked.
    // They only apply in their own mode and reset when the editor restarts.
    extern bool showSolidWalls;
    extern bool showPortalWalls;
    [[nodiscard]] bool WallPassesFilter(const Wall& wall);

    // Component types (CMP_*) and tags whose entities are filtered out.
    extern std::vector<int> hiddenEntityComponentTypes;
    extern std::vector<std::string> hiddenEntityTags;
    [[nodiscard]] bool EntityPassesFilter(const Entity& entity);

    // ---- Entity Mode placement (MapEditorGeometry.cpp) ------------------
    // What E places: empty for a new empty entity, otherwise a prefab's
    // asset reference ("Prefabs/Goblin.prefab").
    extern std::string entityPaletteSelection;
    extern bool placeOnFloor;
    extern bool placeRandomYaw;
    extern std::string placeDefaultTag;

    // ---- Key bindings (EditorState.cpp) ---------------------------------
    // Every single-key shortcut and held modifier of both editors. Each
    // has KEYBIND_SLOTS keys (a primary and an alternate), either of which
    // triggers it; SDL_SCANCODE_UNKNOWN is an empty slot. Two-key shortcuts
    // (Ctrl+Z, Shift+C, ...) are EditorCombo below. Only Escape (cancel)
    // stays fixed.
    //
    // Adding one: an enum value here and a row in KEY_BIND_INFO - the User
    // Settings window, save and load all loop over the table.
    enum EditorKeyBind {
        // Camera
        KEYBIND_PAN_UP,
        KEYBIND_PAN_DOWN,
        KEYBIND_PAN_LEFT,
        KEYBIND_PAN_RIGHT,
        KEYBIND_STEP_UP,
        KEYBIND_STEP_DOWN,
        KEYBIND_STEP_LEFT,
        KEYBIND_STEP_RIGHT,
        KEYBIND_ZOOM_IN,
        KEYBIND_ZOOM_OUT,
        KEYBIND_FOCUS_SELECTION,

        // Modes
        KEYBIND_CYCLE_MODE,
        KEYBIND_GEOMETRY_MODE,
        KEYBIND_ENTITY_MODE,
        KEYBIND_SECTOR_MODE,
        KEYBIND_TOGGLE_UI_EDITOR,
        KEYBIND_TOGGLE_RUNTIME_EDITOR,
        KEYBIND_TOGGLE_TEXTURE_VIEW,
        KEYBIND_SAVE_AND_PLAY,

        // Editing
        KEYBIND_PLACE_ENTITY,
        KEYBIND_DELETE,

        // Drawing (Sector Mode)
        KEYBIND_FINISH_SHAPE,
        KEYBIND_UNDO_POINT,
        KEYBIND_FEWER_SIDES,
        KEYBIND_MORE_SIDES,
        KEYBIND_FLIP_STAIRCASE,
        KEYBIND_TOOL_FREEHAND,
        KEYBIND_TOOL_RECTANGLE,
        KEYBIND_TOOL_POLYGON,
        KEYBIND_TOOL_CIRCLE,
        KEYBIND_TOOL_CURVE,
        KEYBIND_TOOL_STAIRCASE,

        // Held modifiers
        KEYBIND_DISABLE_SNAP,
        KEYBIND_MULTI_SELECT,
        KEYBIND_RANGE_SELECT,
        KEYBIND_CONSTRAIN_ANGLE,
        KEYBIND_SPLIT_WALL,

        // Grid
        KEYBIND_TOGGLE_GRID_SNAP,
        KEYBIND_GRID_SMALLER,
        KEYBIND_GRID_LARGER,

        // Runtime editor: camera
        KEYBIND_RT_FLY_FORWARD,
        KEYBIND_RT_FLY_BACK,
        KEYBIND_RT_FLY_LEFT,
        KEYBIND_RT_FLY_RIGHT,
        KEYBIND_RT_FLY_UP,
        KEYBIND_RT_FLY_DOWN,
        KEYBIND_RT_SPEED_UP,
        KEYBIND_RT_SPEED_DOWN,
        KEYBIND_RT_RESET_SPEED,

        // Runtime editor: editing
        KEYBIND_RT_MOVE_TOOL,
        KEYBIND_RT_ROTATE_TOOL,
        KEYBIND_RT_DISABLE_SNAP,
        KEYBIND_RT_ADD_TO_SELECTION,
        KEYBIND_RT_TEXTURE_X_ONLY,
        KEYBIND_RT_TEXTURE_Y_ONLY,
        KEYBIND_RT_FLIP_TEXTURE_X,
        KEYBIND_RT_FLIP_TEXTURE_Y,
        KEYBIND_RT_PREVIOUS_TEXTURE,
        KEYBIND_RT_NEXT_TEXTURE,
        KEYBIND_RT_FULLBRIGHT,
        KEYBIND_RT_FOCUS_SELECTION,
        KEYBIND_RT_TOGGLE_ENTITY_MARKERS,
        KEYBIND_RT_PLACE_ENTITY,
        KEYBIND_RT_SELECT_BY_TEXTURE, // held with right click
        KEYBIND_RT_FINE_SCROLL,       // held while scrolling
        KEYBIND_RT_BRIGHTNESS_SCROLL, // held while scrolling over a floor/ceiling
        KEYBIND_RT_BACK_TO_EDITOR,
        KEYBIND_RT_SAVE_AND_PLAY,

        // UI editor
        KEYBIND_UI_NUDGE_UP,
        KEYBIND_UI_NUDGE_DOWN,
        KEYBIND_UI_NUDGE_LEFT,
        KEYBIND_UI_NUDGE_RIGHT,
        KEYBIND_UI_BIG_NUDGE,       // held with a nudge key
        KEYBIND_UI_DELETE,
        KEYBIND_UI_FOCUS_SELECTION,
        KEYBIND_UI_ZOOM_IN,
        KEYBIND_UI_ZOOM_OUT,
        KEYBIND_UI_TOGGLE_SNAP,
        KEYBIND_UI_DISABLE_SNAP,    // held while dragging
        KEYBIND_UI_ADD_TO_SELECTION, // held while clicking or box selecting
        KEYBIND_UI_KEEP_ASPECT,     // held while resizing

        KEYBIND_COUNT
    };

    constexpr int KEYBIND_SLOTS = 2;

    // Which editor a binding belongs to. A combo only takes its key away
    // from the single-key bindings of its own editor (see GetKeyBind).
    enum KeyBindScope {
        KEYSCOPE_MAP,     // 2D level editor
        KEYSCOPE_RUNTIME, // runtime editor
        KEYSCOPE_UI       // UI editor
    };

    struct EditorKeyBindInfo {
        const char* saveName;   // its key in UserSettings.bson
        const char* labelKey;   // en.json
        const char* groupKey;   // en.json; rows sharing one are drawn under one heading
        KeyBindScope scope;
        SDL_Scancode defaults[KEYBIND_SLOTS];
    };

    extern const EditorKeyBindInfo KEY_BIND_INFO[KEYBIND_COUNT];

    // Runtime editor step sizes, set from its toolbar's Steps menu and saved
    // with the user settings. The fine ones replace the normal ones while
    // the fine scroll key is held.
    enum RuntimeStep {
        RT_STEP_HEIGHT,
        RT_STEP_FINE_HEIGHT,
        RT_STEP_TEXTURE_SCALE,
        RT_STEP_FINE_TEXTURE_SCALE,
        RT_STEP_BRIGHTNESS,
        RT_STEP_FINE_BRIGHTNESS,
        RT_STEP_TEXTURE_DRAG, // texture units per pixel of left-drag

        RT_STEP_COUNT
    };

    struct RuntimeStepInfo {
        const char* saveName;
        const char* labelKey; // en.json
        float defaultValue;
        float min;
        float max;
        const char* format;
    };

    extern const RuntimeStepInfo RUNTIME_STEP_INFO[RT_STEP_COUNT];
    extern float runtimeSteps[RT_STEP_COUNT];
    extern SDL_Scancode keyBindings[KEYBIND_COUNT][KEYBIND_SLOTS];

    // 0 turns auto save off.
    extern double autoSaveInSeconds;
    extern bool hasUnsavedChanges;

    // Pressed this frame / held, on any of its slots. A key some combo of
    // the same editor is using right now - its first key is held - doesn't
    // count, so Ctrl+A doesn't also pan left.
    [[nodiscard]] bool GetKeyBindDown(EditorKeyBind bind);
    [[nodiscard]] bool GetKeyBind(EditorKeyBind bind);

    // Two-key shortcuts: hold the first key, press the second. Each has two
    // sets of two keys (COMBO_SLOTS: set A is slots 0-1, set B slots 2-3),
    // either of which triggers it. A set with an empty first key is just
    // its second key; one with an empty second key is off.
    enum EditorCombo {
        // 2D editor
        COMBO_UNDO,
        COMBO_REDO,
        COMBO_COPY_ENTITIES,
        COMBO_PASTE_ENTITIES,
        COMBO_SELECT_ALL,
        COMBO_DUPLICATE,
        COMBO_SAVE,

        // Runtime editor
        COMBO_RT_UNDO,
        COMBO_RT_REDO,
        COMBO_RT_COPY_TEXTURE,
        COMBO_RT_COPY_TEXTURE_PLACEMENT,
        COMBO_RT_PASTE_TEXTURE,
        COMBO_RT_PASTE_TEXTURE_PLACEMENT,
        COMBO_RT_COPY_PROPERTIES,
        COMBO_RT_PASTE_PROPERTIES,
        COMBO_RT_RESET_PLACEMENT,
        COMBO_RT_ALIGN_TEXTURES,
        COMBO_RT_FIT_TEXTURES,

        // UI editor
        COMBO_UI_UNDO,
        COMBO_UI_REDO,
        COMBO_UI_DUPLICATE,
        COMBO_UI_SELECT_ALL,

        COMBO_COUNT
    };

    constexpr int COMBO_SLOTS = 4;

    struct EditorComboInfo {
        const char* saveName;
        const char* labelKey;
        const char* groupKey;
        KeyBindScope scope;
        SDL_Scancode defaults[COMBO_SLOTS];
    };

    extern const EditorComboInfo COMBO_INFO[COMBO_COUNT];
    extern SDL_Scancode comboBindings[COMBO_COUNT][COMBO_SLOTS];

    // A set's first key held and its second pressed this frame.
    [[nodiscard]] bool GetComboDown(EditorCombo combo);

    // The name of the key a binding is on: its primary, else its alternate,
    // else "None".
    [[nodiscard]] std::string KeyBindName(EditorKeyBind bind);

    // "Left Ctrl+Z": the first set that has a second key, else "None".
    [[nodiscard]] std::string ComboName(EditorCombo combo);

    // `text` with every "{key:<saveName>}" replaced by that binding's
    // KeyBindName (or a combo's ComboName), so hints and tooltips name the
    // keys the user actually bound. Unknown names are left as they are.
    [[nodiscard]] std::string FormatKeyBinds(const std::string& text);

    // The key slot the User Settings window is waiting on a key press for,
    // nullptr when none. While set, the editor's shortcuts are off so the
    // key being bound doesn't also fire.
    extern SDL_Scancode* keyBindBeingListened;

    // Floor height of the sector at `point`, or 0 outside every sector.
    [[nodiscard]] float FloorHeightAt(const Vector2& point);

    // Draws the hover highlight of the wall Snap to Wall would pick
    // (MapEditorDrawing.cpp).
    void DrawWallSnapPickHighlight();

    // The Entity Mode palette, for the map editor's panel and the runtime
    // editor's toolbar menu (MapEditorUI.cpp).
    void DrawEntityPalette(const char* hintKey);

    // Places the palette's selection at `point`, applying the placement
    // settings, and selects it. INVALID_ID if it couldn't be placed.
    ID PlacePaletteEntityAt(const Vector2& point);

    // The palette's selection at `position` with the random yaw and default
    // tag settings applied. No undo step and no selection - the caller's
    // editor records those. INVALID_ID if it couldn't be made.
    ID CreatePaletteEntity(Level& level, const Vector3& position);

    // ---- Entity Mode selection actions (MapEditorGeometry.cpp) ----------
    // Each pushes one UNDO_ENTITY_POSITIONS step when it moved anything.
    enum EntityAlignment {
        ALIGN_MIN_X,
        ALIGN_CENTER_X,
        ALIGN_MAX_X,
        ALIGN_MIN_Z,
        ALIGN_CENTER_Z,
        ALIGN_MAX_Z,
    };

    // Against the selection's bounds. Returns how many entities moved.
    int AlignSelectedEntities(EntityAlignment alignment);

    // Spaces the selection evenly between its two outermost entities on
    // that axis (needs 3 or more). Returns how many entities moved.
    int DistributeSelectedEntities(bool alongX);

    int SnapSelectedEntitiesToGrid();
    int DropSelectedEntitiesToFloor();

    // One .prefab per selected entity in Assets/Prefabs, named after the
    // entity; each entity becomes a linked instance of its new prefab.
    // Returns how many were made; `errorMessage` gets the last failure.
    int CreatePrefabsFromSelectedEntities(std::string* errorMessage);

    struct EntityTransformUndo {
        std::vector<std::pair<ID, Vector3>> positions;
    };

    // ---- Undo history ----------------------------------------------------
    // Every map editor undo step restores one kind of state. Undoing a step
    // swaps the current state of that kind into it, so the same step, moved
    // to redoSteps, redoes it.
    enum UndoKind {
        UNDO_GEOMETRY,          // walls and sectors
        UNDO_ENTITY_POSITIONS,  // some entities' positions
        UNDO_ENTITIES,          // every entity and component - adds and deletes
    };

    struct EntitiesSnapshot {
        std::vector<Entity> entities;
        ID nextEntityID = 1;

#define TILKY_SNAPSHOT_STORAGE(Type, Bit, Storage, LabelKey) decltype(Level::Storage) Storage;
        TILKY_COMPONENTS(TILKY_SNAPSHOT_STORAGE)
#undef TILKY_SNAPSHOT_STORAGE
    };

    struct UndoStep {
        UndoKind kind = UNDO_GEOMETRY;
        const char* labelKey = ""; // en.json
        int count = 0;             // shown after the label when above 1

        GeometrySnapshot geometry;       // UNDO_GEOMETRY
        EntityTransformUndo positions;   // UNDO_ENTITY_POSITIONS
        EntitiesSnapshot entities;       // UNDO_ENTITIES

        // Set on steps that change wall snaps (any kind): every snap, swapped
        // with WallSnap::All() along with the step's own state.
        std::optional<WallSnap::Snaps> wallSnaps;
    };

    constexpr std::size_t MAX_MAP_UNDO_STEPS = 128;

    extern std::vector<UndoStep> undoSteps; // oldest first
    extern std::vector<UndoStep> redoSteps; // the next one to redo last

    // Level loads and new levels start a fresh history.
    void ClearUndoHistory();

    // A new step; clears the redo steps.
    void PushUndoStep(UndoStep step);

    [[nodiscard]] EntitiesSnapshot CaptureEntitiesSnapshot();
    void PushEntitiesUndo(EntitiesSnapshot before, const char* labelKey, int count = 0);
    void PushEntityPositionsUndo(EntityTransformUndo before, const char* labelKey, int count = 0);

    // "Label" or "Label (count)", localised.
    [[nodiscard]] std::string HistoryLabel(const char* labelKey, int count);

    void UndoLastAction();
    void RedoLastAction();

    // Undoes or redoes until `position` steps are done (0: all undone).
    void JumpToUndoPosition(std::size_t position);

    // Ctrl+A: every wall (Geometry Mode), entity (Entity Mode) or sector
    // (Sector Mode) that passes the canvas filter.
    void SelectAllInMode();

    // Ctrl+D: copies of the selected entities, one grid step down and right
    // of the originals, selected in their place.
    void DuplicateSelectedEntities();

    // Texture preview / Texture View Mode access point. Textures are
    // identified by name (relative to the Textures folder), not by index.
    // Returns nullptr safely if the file is missing or unavailable - never
    // crashes on a missing texture.
    SDL_Texture* GetEditorTexture(const std::string& textureFileName);

    // Backend bridge for every thumbnail the shared inspector widgets draw.
    // ImTextureID means different things per ImGui backend - an SDL_Texture*
    // under imgui_impl_sdlrenderer3, a GLuint texture name under
    // imgui_impl_opengl3 - so nothing below may build one itself.
    //
    // The Map Editor leaves previewTextureProvider null and gets the
    // SDL_Renderer path above. The Runtime Editor points it at the live
    // IRenderer in RuntimeEditor::Start() (and clears it in Shutdown()), so
    // the exact same DrawAssetField call produces GL-valid IDs there.
    //
    // Both defined in MapEditorUI.cpp.
    extern ImTextureID (*previewTextureProvider)(const std::string& textureFileName);
    [[nodiscard]] ImTextureID GetPreviewTextureID(const std::string& textureFileName);

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

    // Drag-and-drop of level objects onto Entity/Wall/Sector script fields.
    // Sources are the hierarchy rows (all three kinds) and the canvas
    // (entities only, while one is being dragged in Entity Mode); targets
    // are the reference fields in the script inspector.
    inline constexpr const char* ENTITY_REF_PAYLOAD = "TILKY_ENTITY_REF";
    inline constexpr const char* WALL_REF_PAYLOAD = "TILKY_WALL_REF";
    inline constexpr const char* SECTOR_REF_PAYLOAD = "TILKY_SECTOR_REF";

    struct LevelObjectDragPayload {
        ID id = INVALID_ID;

        // Set when the drag started on the canvas: the entity has been
        // following the cursor since, so a successful drop has to put it
        // back where it was (see RevertCanvasEntityDrag).
        bool fromCanvas = false;
    };

    // Makes the hierarchy row just drawn draggable onto a matching
    // Entity/Wall/Sector field. Used by both the Map Editor's and the UI
    // Editor's hierarchies. Defined in MapEditorUI.cpp.
    void HierarchyRowDragSource(const char* payloadType, ID id, const std::string& label);

    // Small rounded count badge drawn SameLine after a hierarchy section
    // header. Defined in MapEditorUI.cpp.
    void DrawCountBadge(int count);

    // The type marker in front of a hierarchy row, in a fixed-width slot so
    // the names line up: `icon` (a Font Awesome glyph) when the icon font
    // loaded, otherwise the bracketed `fallback`. `color` is for the dark
    // theme and is darkened for the light one. Defined in MapEditorUI.cpp.
    void DrawHierarchyTypeIcon(const char* icon, const char* fallback, const ImVec4& color);

    // Square "clear" button for the end of a search field: an X icon when
    // the icon font loaded, otherwise a plain "x". Defined in MapEditorUI.cpp.
    bool ClearSearchButton(const char* id);

    // Hierarchy row text for both editors: "Name  (#id)" once the object
    // is named, "Sector #id" / "Wall #id" until then. Always the stable
    // ID, so it matches the inspector header. Defined in MapEditorUI.cpp.
    std::string SectorHierarchyLabel(const Sector& sector);
    std::string WallHierarchyLabel(const Wall& wall);

    // Called once per frame from the UI pass. Turns an in-progress canvas
    // entity drag into an ImGui drag source while the cursor is over a
    // panel, so it can be dropped onto a script field. Defined in
    // MapEditorInput.cpp.
    void SubmitCanvasEntityDragSource();

    // Puts every entity moved by the last canvas drag back where it was
    // when that drag started, and restores the selection the drag's press
    // replaced. Defined in MapEditorInput.cpp.
    void RevertCanvasEntityDrag();

    // The entity the inspector must keep showing while a canvas drag is in
    // progress (the press has already re-selected the dragged entity), or
    // INVALID_ID when the inspector should follow the selection as usual.
    // Defined in MapEditorInput.cpp.
    ID GetCanvasDragPinnedEntity();
    
    // Safe to call unconditionally - it no-ops if the drop didn't land on
    // the Asset Browser panel, or if the Map Editor window isn't the one
    // the drop occurred over.
    void HandleAssetBrowserFileDrop(SDL_WindowID windowID, float x, float y, const char* filePath);

    void QueueLevelLoad(const std::string& levelName);
    bool ProcessPendingLevelLoad();

    void UpdateLevels();

    void ClearManualSectorSelection();
    void CreateManualSector();

    bool SaveUserSettings();
    bool LoadUserSettings();

    bool SaveProjectSettings();
    bool LoadProjectSettings();
}