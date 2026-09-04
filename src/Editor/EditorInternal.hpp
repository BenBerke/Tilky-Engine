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
    inline constexpr float UI_FONT_SIZE = 48.0f;
    inline constexpr float UI_TEXT_PADDING = 8.0f;

    extern int screenWidth;
    extern int screenHeight;

    extern float editorZoom;
    extern float GRID_SIZE; // fixed world-space grid/snap spacing - NOT affected by editorZoom. Adjustable at runtime via the Grid & Snapping panel.
    constexpr float MIN_EDITOR_ZOOM = 0.10f;
    constexpr float MAX_EDITOR_ZOOM = 10.00f;
    constexpr float MIN_GRID_SIZE = 1.0f;

    constexpr float ENTITY_SIZE = 10.0f;

    // Below this world-space size, a drawing tool's shape is considered
    // degenerate (zero-width rectangle, zero-radius circle, a curve
    // whose start/end coincide, etc.) and is refused rather than
    // committed. Shared between the tool logic (MapEditorGeometry.cpp)
    // and the live-preview colouring (MapEditorDrawing.cpp) so the
    // preview's valid/invalid tint always matches what a click would do.
    constexpr float MIN_DRAW_SHAPE_DIMENSION = 0.25f;

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

    enum Action {
        ACTION_CREATE_SECTOR,
        ACTION_CREATE_WALL,
        ACTION_CREATE_CORNER, // historical name - Dot lifecycle only (see AddDot; dots are internal snap anchors now, not user-facing objects).
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

    // Sector Mode's geometry-building tools. DRAWTOOL_FREEHAND covers the
    // pre-existing point-by-point chain (including its manualSectorMode
    // sub-toggle, which is unchanged); the rest are additive.
    enum DrawTool {
        DRAWTOOL_FREEHAND,
        DRAWTOOL_RECTANGLE,
        DRAWTOOL_POLYGON,
        DRAWTOOL_CIRCLE,
        DRAWTOOL_CURVE,

        DRAWTOOL_COUNT
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
        std::string wallTexture;

        std::vector<SectorFloor> floors = {
            {
                {0.0f, std::numeric_limits<uint_fast32_t>::max(), {}},
                {40.0f, std::numeric_limits<uint_fast32_t>::max(), {}}
            }
        };

        Vector3 lightValue = {255.0f, 255.0f, 255.0f};
        Vector4 wallColor = {1.0f, 1.0f, 1.0f, 1.0f};
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
        std::vector<ID> selectedWalls;
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

    extern EditorTheme currentTheme;
    extern bool textureViewMode;

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

    // Selection within the UI Editor. Independent of selectedEntity/editingEntity
    // above, which belong to the Map Editor's MODE_ENTITY and are left untouched.
    extern ID selectedUIEntityID;
    extern ID hoveredUIEntityID; // recomputed each frame in HandleUIEditorInput(); read-only elsewhere, for hover highlighting.

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
    [[nodiscard]] Vector2 ResolveFreehandPoint(const Vector2& mouseWorld);
    [[nodiscard]] Vector2 ResolveRectangleCorner(const Vector2& mouseWorld); // valid once rectangleHasFirstCorner
    [[nodiscard]] Vector2 ResolvePolygonHandle(const Vector2& mouseWorld);  // valid once polygonHasCenter
    [[nodiscard]] Vector2 ResolveCircleHandle(const Vector2& mouseWorld);   // valid once circleHasCenter
    [[nodiscard]] Vector2 ResolveCurveEnd(const Vector2& mouseWorld);       // valid during CURVE_STAGE_END

    // Point generators for the three parametric tools. Never snapped
    // vertex-by-vertex (only the control points that define them are) -
    // snapping every generated point would turn a circle into a jagged
    // mess. Winding is consistently counter-clockwise (matching world
    // space, +Y up) for all three, regardless of which corner/handle
    // the user placed first.
    [[nodiscard]] std::vector<Vector2> BuildRegularPolygon(const Vector2& center, const Vector2& handle, int sideCount);
    [[nodiscard]] std::vector<Vector2> BuildEllipse(const Vector2& center, float radiusX, float radiusY, int segments);
    [[nodiscard]] std::vector<Vector2> BuildQuadraticCurve(const Vector2& start, const Vector2& control, const Vector2& end, int subdivisions);

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

    // Undo support for geometry edits. Wall moves and deletions reuse the
    // existing whole-operation ACTION_APPLY_GEOMETRY entry rather than
    // introducing a second undo kind: capture before touching anything,
    // push only once the edit has actually changed something.
    [[nodiscard]] GeometrySnapshot CaptureGeometryUndoSnapshot();
    void PushGeometryUndoSnapshot(GeometrySnapshot snapshot);

    // Deletes every wall in the current selection as one undoable
    // operation, with a single topology rebuild at the end.
    void DeleteSelectedWalls();

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

    // Call this from SDL event loop whenever it receives
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