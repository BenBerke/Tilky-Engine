#include "EditorInternal.hpp"

#include <string_view>

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

namespace Editor {
    std::vector<std::string> maps;
    std::string currentMap;

    Vector3 playerStartPos = {0.0f, 0.0f, 0.0f};
}

namespace MapEditorInternal {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    TTF_Font* labelFont = nullptr;
    TTF_TextEngine* textEngine = nullptr;

    ImFont* scriptEditorFont = nullptr;
    ImFont* editorIconFont = nullptr;

    float editorZoom = 1.0f;
    float GRID_SIZE = DEFAULT_GRID_SIZE; // fixed world-space grid/snap spacing; adjustable at runtime via the Grid & Snapping panel (see DrawEditorUI)

    Vector2 cameraPos = {0.0f, 0.0f};

    // Internal snap anchors only - nothing user-facing creates, selects
    // or lists dots since Dot Mode became Geometry Mode.
    std::vector<Dot> dots;
    ID nextDotID = 0;
    std::unordered_map<ID, int> dotIDToIndex;

    std::vector<Vector2> sectorBeingCreated;
    PendingSectorParams pendingSectorParams;

    bool editingSector = false;
    ID selectedSectorID = INVALID_ID;

    bool editingEntity = false;
    Entity selectedEntity;

    std::string currentMap;

    Mode currentMode = MODE_GEOMETRY;
    State currentState = STATE_MAP;

    TextureViewMode textureViewMode = TextureViewMode::Off;

    // Here rather than next to UndoLastAction: level loading clears them,
    // and that code is in the standalone build too.
    std::vector<UndoStep> undoSteps;
    std::vector<UndoStep> redoSteps;
    std::uint64_t undoHistoryVersion = 0;

    void ClearUndoHistory() {
        undoSteps.clear();
        redoSteps.clear();
        ++undoHistoryVersion;
    }

    std::string HistoryLabel(const char* labelKey, const int count) {
        std::string label = Localisation::Get(labelKey);
        if (count > 1) label += " (" + std::to_string(count) + ")";
        return label;
    }
    std::string lastGeometryError;

    int currentFloor;

    bool quit = false;
    bool play = false;
    bool shutdown = false;
    bool switchToRuntime = false;

    std::vector<ID> entityClipboard;

    //-- Sector to be created variables
    std::string wallTopTexture;
    std::string wallBottomTexture;
    std::string ceilTexture;
    std::string floorTexture;

    float floorHeight = .0f;
    float ceilHeight = 40.0f;

    Vector3 lightValue = {255.0f, 255.0f, 255.0f};

    Vector4 wallColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 ceilColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 floorColor = {1.0f, 1.0f, 1.0f, 1.0f};

    bool manualSectorMode = false;
    std::vector<Vector2> manualSectorDots;

    // Sector Mode — drawing tools
    DrawTool currentDrawTool = DRAWTOOL_FREEHAND;

    bool gridSnapEnabled = true;
    bool vertexSnapEnabled = true;

    bool rectangleHasFirstCorner = false;
    Vector2 rectangleFirstCorner = {0.0f, 0.0f};

    bool polygonHasCenter = false;
    Vector2 polygonCenter = {0.0f, 0.0f};
    int polygonSideCount = 6; // hexagon by default

    bool circleHasCenter = false;
    Vector2 circleCenter = {0.0f, 0.0f};
    int circleSegments = 24;

    CurveStage curveStage = CURVE_STAGE_START;
    Vector2 curveStart = {0.0f, 0.0f};
    Vector2 curveEnd = {0.0f, 0.0f};
    int curveSubdivisions = 12;

    bool staircaseHasFirstCorner = false;
    Vector2 staircaseFirstCorner = {0.0f, 0.0f};

    StaircaseCalculationMode staircaseCalcMode = STAIRCASE_CALC_STEP_HEIGHT;
    StaircaseDirection staircaseDirection = STAIRCASE_DIR_HORIZONTAL;
    bool staircaseRaiseCeiling = true;
    bool staircaseReverseRise = false;

    float staircaseStepHeight = 8.0f;
    float staircaseStepLength = 32.0f;
    float staircaseTargetFloorHeight = 40.0f;
    int staircaseStepCount = 4;

    // Geometry / Wall Edit Mode — selection, hover and drag state
    bool editingWall = false;
    ID selectedWallID = INVALID_ID;
    ID hoveredWallID = INVALID_ID;
    ID hoveredEntityID = INVALID_ID;
    ID hoveredSectorID = INVALID_ID;
    bool draggingWallGeometry = false;

    // Linedef Mode — selection, hover, drag and drawing state
    bool linedefMode = false;
    std::vector<ID> selectedLinedefs = {};
    ID selectedLinedefID = INVALID_ID;
    bool editingLinedef = false;
    ID hoveredLinedefID = INVALID_ID;
    bool draggingLinedefGeometry = false;
    bool drawingLinedef = false;
    Vector2 linedefDrawStart = {0.0f, 0.0f};

    // =========================================================================
    //  UI Editor — shared state (see EditorInternal.hpp for the rationale)
    // =========================================================================

    ID selectedUIEntityID = INVALID_ID;
    ID hoveredUIEntityID = INVALID_ID;

    // {0,0} until the first DrawUIEditorUI() call centres it on the actual
    // current screen (screenWidth/screenHeight aren't known yet at static
    // init time) - see the one-time centring check at the top of that
    // function.
    Vector2 uiCanvasPan = {0.0f, 0.0f};
    float uiCanvasZoom = 1.0f;

    bool showUIGrid = false;
    bool showUICenterLines = true;
    bool showUISafeArea = false;

    std::vector<ID> selectedUIEntities = {};

    bool uiSnapEnabled = true;
    bool uiSnapToElements = true;

    std::vector<ID> selectedEntities = {};
    std::vector<ID> selectedSectors = {};
    std::vector<ID> selectedWalls = {};

    // User Settings
    Vector3 normalEntityColor = {190, 190, 190};
    Vector3 highlightedEntityColor = {20, 20, 220};
    Vector3 spriteEntityColor = {100, 200, 100};

    Vector3 normalWallColor = {205, 205, 205};
    Vector3 portalWallColor = {150, 150, 150};
    Vector3 highlightedWallColor = {80, 220, 255};

    Vector3 linedefColor = {135, 206, 250};
    float linedefThickness = DEFAULT_LINEDEF_THICKNESS;
    Vector3 linedefEndpointColor = {135, 206, 250};
    int linedefEndpointSize = DEFAULT_LINEDEF_ENDPOINT_SIZE;

    Vector3 hoveredSectorColor = {250, 250, 100};
    Vector3 highlightedSectorColor = {40, 40, 220};

    Vector3 snapIndicatorColor = {220, 255, 255};

    Vector3 eventLinkColor = {255, 0, 0};
    float eventLinkThickness = DEFAULT_EVENT_LINK_THICKNESS;

    Vector3 kValidLineColor = {255, 220, 80};
    Vector3 kInvalidLineColor = {230, 70, 70};
    Vector3 kAnchorColor = {80, 220, 255};
    Vector4 kValidFillColor = {1.0f, 0.863f, 0.314f, 0.28f};
    Vector4 kInvalidFillColor = {0.90f, 0.27f, 0.27f, 0.28f};

    Vector3 themeTextColor = {255, 255, 255};
    Vector4 gridColor = {1.0f, 1.0f, 1.0f, 0.5f};

    Vector3 backgroundColor = {45, 45, 45};

    Vector3 normalHandleColor = {80, 220, 255};
    Vector3 highlightedHandleColor = {255, 255, 255};
    Vector3 handleOutlineColor = {20, 20, 20};

    Vector3 gizmoXColor = {230, 60, 60};
    Vector3 gizmoYColor = {80, 200, 80};
    Vector3 gizmoZColor = {60, 110, 240};

    // has to be an int because Imgui doesnt support chars
    int filteredAlpha = 60;

    float cameraSpeed = 1.0f;
    float cameraStepSize = 50.0f;

    float runtimeCameraFov = DEFAULT_RUNTIME_CAMERA_FOV;

    bool rememberCameraPosition = true;

    bool hoverSectorsInEveryMode = true;

    float textureFieldSize = DEFAULT_TEXTURE_FIELD_SIZE;

    bool hierarchyVisible = true;
    bool editorPanelVisible = true;
    bool worldSettingsVisible = true;
    bool assetBrowserVisible = true;
    bool historyVisible = false;
    bool runtimeHistoryVisible = false;
    bool tagExplorerVisible = false;
    bool runtimeTagExplorerVisible = false;
    bool commentsVisible = false;
    bool runtimeCommentsVisible = false;
    EntityMarkerMode entityMarkerMode = EntityMarkerMode::Always;
    std::set<std::string> collapsedComponentTypes;

    // In EditorKeyBind order.
    const EditorKeyBindInfo KEY_BIND_INFO[KEYBIND_COUNT] = {
        {"pan_up",           "editor.key.pan_up",           "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_W}},
        {"pan_down",         "editor.key.pan_down",         "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_S}},
        {"pan_left",         "editor.key.pan_left",         "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_A}},
        {"pan_right",        "editor.key.pan_right",        "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_D}},
        {"step_up",          "editor.key.step_up",          "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_UP}},
        {"step_down",        "editor.key.step_down",        "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_DOWN}},
        {"step_left",        "editor.key.step_left",        "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_LEFT}},
        {"step_right",       "editor.key.step_right",       "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_RIGHT}},
        {"zoom_in",          "editor.key.zoom_in",          "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_EQUALS, SDL_SCANCODE_KP_PLUS}},
        {"zoom_out",         "editor.key.zoom_out",         "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_MINUS, SDL_SCANCODE_KP_MINUS}},
        {"focus_selection",  "editor.key.focus_selection",  "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_F}},
        {"frame_map",        "editor.key.frame_map",        "editor.key_group.camera",     KEYSCOPE_MAP, {SDL_SCANCODE_J}},

        {"cycle_mode",       "editor.key.cycle_mode",       "editor.key_group.modes",      KEYSCOPE_MAP, {SDL_SCANCODE_Q}},
        {"geometry_mode",    "editor.key.geometry_mode",    "editor.key_group.modes",      KEYSCOPE_MAP, {SDL_SCANCODE_G}},
        {"entity_mode",      "editor.key.entity_mode",      "editor.key_group.modes",      KEYSCOPE_MAP, {SDL_SCANCODE_E, SDL_SCANCODE_T}},
        {"sector_mode",      "editor.key.sector_mode",      "editor.key_group.modes",      KEYSCOPE_MAP, {SDL_SCANCODE_R}},
        {"toggle_ui_editor", "editor.key.toggle_ui_editor", "editor.key_group.modes",      KEYSCOPE_MAP, {SDL_SCANCODE_U}},
        {"toggle_runtime_editor", "editor.key.toggle_runtime_editor", "editor.key_group.modes", KEYSCOPE_MAP, {SDL_SCANCODE_V}},
        {"toggle_texture_view", "editor.key.toggle_texture_view", "editor.key_group.modes", KEYSCOPE_MAP, {SDL_SCANCODE_M}},
        {"save_and_play",    "editor.key.save_and_play",    "editor.key_group.modes",      KEYSCOPE_MAP, {SDL_SCANCODE_P}},

        {"place_entity",     "editor.key.place_entity",     "editor.key_group.editing",    KEYSCOPE_MAP, {SDL_SCANCODE_E}},
        {"delete",           "editor.key.delete",           "editor.key_group.editing",    KEYSCOPE_MAP, {SDL_SCANCODE_DELETE}},
        {"flip_linedef",     "editor.key.flip_linedef",     "editor.key_group.editing",    KEYSCOPE_MAP, {SDL_SCANCODE_H}},
        {"point_at_cursor",  "editor.key.point_at_cursor",  "editor.key_group.editing",    KEYSCOPE_MAP, {SDL_SCANCODE_I}},
        {"raise_floor_to_neighbour",   "editor.key.raise_floor_to_neighbour",   "editor.key_group.editing", KEYSCOPE_MAP, {SDL_SCANCODE_PAGEUP}},
        {"lower_floor_to_neighbour",   "editor.key.lower_floor_to_neighbour",   "editor.key_group.editing", KEYSCOPE_MAP, {SDL_SCANCODE_PAGEDOWN}},
        {"raise_ceiling_to_neighbour", "editor.key.raise_ceiling_to_neighbour", "editor.key_group.editing", KEYSCOPE_MAP, {SDL_SCANCODE_HOME}},
        {"lower_ceiling_to_neighbour", "editor.key.lower_ceiling_to_neighbour", "editor.key_group.editing", KEYSCOPE_MAP, {SDL_SCANCODE_END}},
        {"match_brightness", "editor.key.match_brightness", "editor.key_group.editing",    KEYSCOPE_MAP, {SDL_SCANCODE_Y}},
        {"add_comment",      "editor.key.add_comment",      "editor.key_group.editing",    KEYSCOPE_MAP, {SDL_SCANCODE_C}},

        {"finish_shape",     "editor.key.finish_shape",     "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE}},
        {"undo_point",       "editor.key.undo_point",       "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_BACKSPACE}},
        {"fewer_sides",      "editor.key.fewer_sides",      "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_LEFTBRACKET}},
        {"more_sides",       "editor.key.more_sides",       "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_RIGHTBRACKET}},
        {"flip_staircase",   "editor.key.flip_staircase",   "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"tool_freehand",    "editor.key.tool_freehand",    "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_1}},
        {"tool_rectangle",   "editor.key.tool_rectangle",   "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_2}},
        {"tool_polygon",     "editor.key.tool_polygon",     "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_3}},
        {"tool_circle",      "editor.key.tool_circle",      "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_4}},
        {"tool_curve",       "editor.key.tool_curve",       "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_5}},
        {"tool_staircase",   "editor.key.tool_staircase",   "editor.key_group.drawing",    KEYSCOPE_MAP, {SDL_SCANCODE_6}},

        {"disable_snap",     "editor.key.disable_snap",     "editor.key_group.modifiers",  KEYSCOPE_MAP, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"multi_select",     "editor.key.multi_select",     "editor.key_group.modifiers",  KEYSCOPE_MAP, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"range_select",     "editor.key.range_select",     "editor.key_group.modifiers",  KEYSCOPE_MAP, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
        {"constrain_angle",  "editor.key.constrain_angle",  "editor.key_group.modifiers",  KEYSCOPE_MAP, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
        {"split_wall",       "editor.key.split_wall",       "editor.key_group.modifiers",  KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"paint_select",     "editor.key.paint_select",     "editor.key_group.modifiers",  KEYSCOPE_MAP, {SDL_SCANCODE_B}},

        {"assign_group_modifier", "editor.key.assign_group_modifier", "editor.key_group.modifiers", KEYSCOPE_MAP, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},

        {"toggle_grid_snap", "editor.key.toggle_grid_snap", "editor.key_group.grid",       KEYSCOPE_MAP, {SDL_SCANCODE_N}},
        {"grid_smaller",     "editor.key.grid_smaller",     "editor.key_group.grid",       KEYSCOPE_MAP, {SDL_SCANCODE_COMMA}},
        {"grid_larger",      "editor.key.grid_larger",      "editor.key_group.grid",       KEYSCOPE_MAP, {SDL_SCANCODE_PERIOD}},
        {"grid_origin_here", "editor.key.grid_origin_here", "editor.key_group.grid",       KEYSCOPE_MAP, {SDL_SCANCODE_O}},
        {"align_grid_to_wall", "editor.key.align_grid_to_wall", "editor.key_group.grid",   KEYSCOPE_MAP, {SDL_SCANCODE_K}},
        {"reset_grid",       "editor.key.reset_grid",       "editor.key_group.grid",       KEYSCOPE_MAP, {SDL_SCANCODE_UNKNOWN}},

        {"rt_fly_forward",      "editor.key.rt_fly_forward",      "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_W}},
        {"rt_fly_back",         "editor.key.rt_fly_back",         "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_S}},
        {"rt_fly_left",         "editor.key.rt_fly_left",         "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_A}},
        {"rt_fly_right",        "editor.key.rt_fly_right",        "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_D}},
        {"rt_fly_up",           "editor.key.rt_fly_up",           "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_SPACE}},
        {"rt_fly_down",         "editor.key.rt_fly_down",         "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_LSHIFT}},
        {"rt_speed_up",         "editor.key.rt_speed_up",         "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_N}},
        {"rt_speed_down",       "editor.key.rt_speed_down",       "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_B}},
        {"rt_reset_speed",      "editor.key.rt_reset_speed",      "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_COMMA}},
        {"rt_gravity_mode",     "editor.key.rt_gravity_mode",     "editor.key_group.runtime_camera",   KEYSCOPE_RUNTIME, {SDL_SCANCODE_G}},

        {"rt_move_tool",        "editor.key.rt_move_tool",        "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_1}},
        {"rt_rotate_tool",      "editor.key.rt_rotate_tool",      "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_2}},
        {"rt_enable_snap",     "editor.key.rt_enable_snap",     "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"rt_add_to_selection", "editor.key.rt_add_to_selection", "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"rt_texture_x_only",   "editor.key.rt_texture_x_only",   "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_X}},
        {"rt_texture_y_only",   "editor.key.rt_texture_y_only",   "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_Y}},
        {"rt_flip_texture_x",   "editor.key.rt_flip_texture_x",   "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_X}},
        {"rt_flip_texture_y",   "editor.key.rt_flip_texture_y",   "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_Y}},
        {"rt_previous_texture", "editor.key.rt_previous_texture", "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_LEFTBRACKET}},
        {"rt_next_texture",     "editor.key.rt_next_texture",     "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_RIGHTBRACKET}},
        {"rt_fullbright",       "editor.key.rt_fullbright",       "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_L}},
        {"rt_focus_selection",  "editor.key.rt_focus_selection",  "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_F}},
        {"rt_toggle_entity_markers", "editor.key.rt_toggle_entity_markers", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_M}},
        {"rt_place_entity",     "editor.key.rt_place_entity",     "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_E}},
        {"rt_point_at_cursor",  "editor.key.rt_point_at_cursor",  "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_I}},
        {"rt_raise_to_neighbour", "editor.key.rt_raise_to_neighbour", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_PAGEUP}},
        {"rt_lower_to_neighbour", "editor.key.rt_lower_to_neighbour", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_PAGEDOWN}},
        {"rt_match_brightness", "editor.key.rt_match_brightness", "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_J}},
        {"rt_add_comment",      "editor.key.rt_add_comment",      "editor.key_group.runtime_editing",  KEYSCOPE_RUNTIME, {SDL_SCANCODE_C}},
        {"rt_slope_between_neighbours", "editor.key.rt_slope_between_neighbours", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_K}},
        {"rt_select_by_texture", "editor.key.rt_select_by_texture", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"rt_select_connected", "editor.key.rt_select_connected", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_Z}},
        {"rt_connected_by_height", "editor.key.rt_connected_by_height", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"rt_fine_scroll",      "editor.key.rt_fine_scroll",      "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"rt_brightness_scroll", "editor.key.rt_brightness_scroll", "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"rt_back_to_editor",   "editor.key.rt_back_to_editor",   "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_V}},
        {"rt_save_and_play",    "editor.key.rt_save_and_play",    "editor.key_group.runtime_editing", KEYSCOPE_RUNTIME, {SDL_SCANCODE_P}},

        {"ui_nudge_up",         "editor.key.ui_nudge_up",         "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_UP}},
        {"ui_nudge_down",       "editor.key.ui_nudge_down",       "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_DOWN}},
        {"ui_nudge_left",       "editor.key.ui_nudge_left",       "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_LEFT}},
        {"ui_nudge_right",      "editor.key.ui_nudge_right",      "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_RIGHT}},
        {"ui_big_nudge",        "editor.key.ui_big_nudge",        "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
        {"ui_delete",           "editor.key.ui_delete",           "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_DELETE, SDL_SCANCODE_BACKSPACE}},
        {"ui_focus_selection",  "editor.key.ui_focus_selection",  "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_F}},
        {"ui_zoom_in",          "editor.key.ui_zoom_in",          "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_EQUALS, SDL_SCANCODE_KP_PLUS}},
        {"ui_zoom_out",         "editor.key.ui_zoom_out",         "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_MINUS, SDL_SCANCODE_KP_MINUS}},
        {"ui_toggle_snap",      "editor.key.ui_toggle_snap",      "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_N}},
        {"ui_disable_snap",     "editor.key.ui_disable_snap",     "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"ui_add_to_selection", "editor.key.ui_add_to_selection", "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL}},
        {"ui_keep_aspect",      "editor.key.ui_keep_aspect",      "editor.key_group.ui_editor", KEYSCOPE_UI, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
    };

    SDL_Scancode keyBindings[KEYBIND_COUNT][KEYBIND_SLOTS] = {};

    // In EditorCombo order. Set B is the same shortcut on the right-hand
    // modifier.
    const EditorComboInfo COMBO_INFO[COMBO_COUNT] = {
        {"undo",             "editor.combo.undo",             "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z, SDL_SCANCODE_RCTRL, SDL_SCANCODE_Z}},
        {"redo",             "editor.combo.redo",             "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Y, SDL_SCANCODE_RCTRL, SDL_SCANCODE_Y}},
        {"copy_entities",    "editor.combo.copy_entities",    "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_C, SDL_SCANCODE_RCTRL, SDL_SCANCODE_C}},
        {"paste_entities",   "editor.combo.paste_entities",   "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_V, SDL_SCANCODE_RCTRL, SDL_SCANCODE_V}},
        {"select_all",       "editor.combo.select_all",       "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_A, SDL_SCANCODE_RCTRL, SDL_SCANCODE_A}},
        {"duplicate",        "editor.combo.duplicate",        "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_D, SDL_SCANCODE_RCTRL, SDL_SCANCODE_D}},
        {"save",             "editor.combo.save",             "editor.key_group.shortcuts",         KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_S, SDL_SCANCODE_RCTRL, SDL_SCANCODE_S}},

        {"rt_undo",          "editor.combo.rt_undo",          "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z, SDL_SCANCODE_RCTRL, SDL_SCANCODE_Z}},
        {"rt_redo",          "editor.combo.rt_redo",          "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Y, SDL_SCANCODE_RCTRL, SDL_SCANCODE_Y}},
        {"rt_copy_texture",  "editor.combo.rt_copy_texture",  "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_C, SDL_SCANCODE_RCTRL, SDL_SCANCODE_C}},
        {"rt_copy_texture_placement", "editor.combo.rt_copy_texture_placement", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_C, SDL_SCANCODE_RALT, SDL_SCANCODE_C}},
        {"rt_paste_texture", "editor.combo.rt_paste_texture", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_V, SDL_SCANCODE_RCTRL, SDL_SCANCODE_V}},
        {"rt_paste_texture_placement", "editor.combo.rt_paste_texture_placement", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_V, SDL_SCANCODE_RALT, SDL_SCANCODE_V}},
        {"rt_copy_properties", "editor.combo.rt_copy_properties", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_R, SDL_SCANCODE_RCTRL, SDL_SCANCODE_R}},
        {"rt_paste_properties", "editor.combo.rt_paste_properties", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_F, SDL_SCANCODE_RCTRL, SDL_SCANCODE_F}},
        {"rt_reset_placement", "editor.combo.rt_reset_placement", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_R, SDL_SCANCODE_RALT, SDL_SCANCODE_R}},
        {"rt_align_textures", "editor.combo.rt_align_textures", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_G, SDL_SCANCODE_RCTRL, SDL_SCANCODE_G}},
        {"rt_fit_textures",  "editor.combo.rt_fit_textures",  "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_G, SDL_SCANCODE_RALT, SDL_SCANCODE_G}},
        {"rt_align_textures_x", "editor.combo.rt_align_textures_x", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_X, SDL_SCANCODE_RALT, SDL_SCANCODE_X}},
        {"rt_align_textures_y", "editor.combo.rt_align_textures_y", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_Y, SDL_SCANCODE_RALT, SDL_SCANCODE_Y}},
        {"rt_flood_fill_texture", "editor.combo.rt_flood_fill_texture", "editor.key_group.runtime_shortcuts", KEYSCOPE_RUNTIME, {SDL_SCANCODE_LALT, SDL_SCANCODE_F, SDL_SCANCODE_RALT, SDL_SCANCODE_F}},

        {"select_group_1", "editor.combo.select_group_1", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_1, SDL_SCANCODE_RALT, SDL_SCANCODE_1}},
        {"select_group_2", "editor.combo.select_group_2", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_2, SDL_SCANCODE_RALT, SDL_SCANCODE_2}},
        {"select_group_3", "editor.combo.select_group_3", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_3, SDL_SCANCODE_RALT, SDL_SCANCODE_3}},
        {"select_group_4", "editor.combo.select_group_4", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_4, SDL_SCANCODE_RALT, SDL_SCANCODE_4}},
        {"select_group_5", "editor.combo.select_group_5", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_5, SDL_SCANCODE_RALT, SDL_SCANCODE_5}},
        {"select_group_6", "editor.combo.select_group_6", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_6, SDL_SCANCODE_RALT, SDL_SCANCODE_6}},
        {"select_group_7", "editor.combo.select_group_7", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_7, SDL_SCANCODE_RALT, SDL_SCANCODE_7}},
        {"select_group_8", "editor.combo.select_group_8", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_8, SDL_SCANCODE_RALT, SDL_SCANCODE_8}},
        {"select_group_9", "editor.combo.select_group_9", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_9, SDL_SCANCODE_RALT, SDL_SCANCODE_9}},
        {"select_group_10", "editor.combo.select_group_10", "editor.key_group.selection_groups", KEYSCOPE_MAP, {SDL_SCANCODE_LALT, SDL_SCANCODE_0, SDL_SCANCODE_RALT, SDL_SCANCODE_0}},

        {"command_palette",  "editor.combo.command_palette",  "editor.key_group.all_editors",        KEYSCOPE_MAP,     {SDL_SCANCODE_LCTRL, SDL_SCANCODE_P, SDL_SCANCODE_RCTRL, SDL_SCANCODE_P}, true},

        {"ui_undo",          "editor.combo.ui_undo",          "editor.key_group.ui_shortcuts",      KEYSCOPE_UI,      {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z, SDL_SCANCODE_RCTRL, SDL_SCANCODE_Z}},
        {"ui_redo",          "editor.combo.ui_redo",          "editor.key_group.ui_shortcuts",      KEYSCOPE_UI,      {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Y, SDL_SCANCODE_RCTRL, SDL_SCANCODE_Y}},
        {"ui_duplicate",     "editor.combo.ui_duplicate",     "editor.key_group.ui_shortcuts",      KEYSCOPE_UI,      {SDL_SCANCODE_LCTRL, SDL_SCANCODE_D, SDL_SCANCODE_RCTRL, SDL_SCANCODE_D}},
        {"ui_select_all",    "editor.combo.ui_select_all",    "editor.key_group.ui_shortcuts",      KEYSCOPE_UI,      {SDL_SCANCODE_LCTRL, SDL_SCANCODE_A, SDL_SCANCODE_RCTRL, SDL_SCANCODE_A}},
    };

    SDL_Scancode comboBindings[COMBO_COUNT][COMBO_SLOTS] = {};

    const RuntimeStepInfo RUNTIME_STEP_INFO[RT_STEP_COUNT] = {
        {"height",              "runtime_editor.step.height",              1.0f,   0.01f,   64.0f, "%.2f"},
        {"fine_height",         "runtime_editor.step.fine_height",         0.1f,   0.01f,   64.0f, "%.2f"},
        {"texture_scale",       "runtime_editor.step.texture_scale",       0.01f,  0.0001f, 1.0f,  "%.4f"},
        {"fine_texture_scale",  "runtime_editor.step.fine_texture_scale",  0.001f, 0.0001f, 1.0f,  "%.4f"},
        {"brightness",          "runtime_editor.step.brightness",          8.0f,   0.1f,    64.0f, "%.1f"},
        {"fine_brightness",     "runtime_editor.step.fine_brightness",     0.8f,   0.1f,    64.0f, "%.1f"},
        {"texture_drag",        "runtime_editor.step.texture_drag",        0.08f,  0.001f,  1.0f,  "%.3f"},
    };

    float runtimeSteps[RT_STEP_COUNT] = {};

    bool runtimeGravityMode = false;
    float runtimeEyeHeight = DEFAULT_RUNTIME_EYE_HEIGHT;

    namespace {
        // keyBindings and comboBindings start as copies of the defaults.
        const bool keyBindingsInitialized = [] {
            for (int bind = 0; bind < KEYBIND_COUNT; ++bind)
                for (int slot = 0; slot < KEYBIND_SLOTS; ++slot)
                    keyBindings[bind][slot] = KEY_BIND_INFO[bind].defaults[slot];

            for (int combo = 0; combo < COMBO_COUNT; ++combo)
                for (int slot = 0; slot < COMBO_SLOTS; ++slot)
                    comboBindings[combo][slot] = COMBO_INFO[combo].defaults[slot];

            for (int step = 0; step < RT_STEP_COUNT; ++step) runtimeSteps[step] = RUNTIME_STEP_INFO[step].defaultValue;

            return true;
        }();

        // The 2D editor also uses the runtime editor's texture copy/paste
        // combos, so their keys are taken there too: Alt+V mustn't also
        // press V (open the runtime editor). The selection group combos are
        // the runtime editor's too: Alt+1 mustn't also pick the Move tool.
        bool ComboUsedInScope(const int combo, const KeyBindScope scope) {
            if (COMBO_INFO[combo].scope == scope) return true;
            if (combo >= COMBO_SELECT_GROUP_1 && combo <= COMBO_SELECT_GROUP_LAST) return scope == KEYSCOPE_RUNTIME;
            if (combo == COMBO_COMMAND_PALETTE) return true;
            if (scope != KEYSCOPE_MAP) return false;

            switch (combo) {
                case COMBO_RT_COPY_TEXTURE:
                case COMBO_RT_COPY_TEXTURE_PLACEMENT:
                case COMBO_RT_PASTE_TEXTURE:
                case COMBO_RT_PASTE_TEXTURE_PLACEMENT:
                    return true;
                default:
                    return false;
            }
        }

        // `key` is the second key of a combo used in `scope` whose first key
        // is held right now.
        bool ShiftHeld() {
            return InputManager::GetKey(SDL_SCANCODE_LSHIFT) || InputManager::GetKey(SDL_SCANCODE_RSHIFT);
        }

        bool IsKeyTakenByCombo(const SDL_Scancode key, const KeyBindScope scope) {
            for (int combo = 0; combo < COMBO_COUNT; ++combo) {
                if (!ComboUsedInScope(combo, scope)) continue;
                if (COMBO_INFO[combo].needsShift && !ShiftHeld()) continue;

                for (int set = 0; set < COMBO_SLOTS; set += 2) {
                    const SDL_Scancode first = comboBindings[combo][set];
                    const SDL_Scancode second = comboBindings[combo][set + 1];

                    if (second == key && first != SDL_SCANCODE_UNKNOWN && InputManager::GetKey(first)) return true;
                }
            }

            return false;
        }
    }

    namespace {
        // What the command palette pressed, and until when it waits to be
        // asked for (SDL ticks).
        constexpr Uint64 PALETTE_PRESS_TIMEOUT_MS = 2000;

        int pressedKeyBind = KEYBIND_COUNT;
        int pressedCombo = COMBO_COUNT;
        Uint64 pressExpiresAt = 0;

        // True once for a pending press of `index` in `pressed`.
        bool TakePress(int& pressed, const int index, const int none) {
            if (pressed != index) return false;

            pressed = none;
            return SDL_GetTicks() <= pressExpiresAt;
        }
    }

    void PressKeyBind(const EditorKeyBind bind) {
        pressedKeyBind = bind;
        pressedCombo = COMBO_COUNT;
        pressExpiresAt = SDL_GetTicks() + PALETTE_PRESS_TIMEOUT_MS;
    }

    void PressCombo(const EditorCombo combo) {
        pressedCombo = combo;
        pressedKeyBind = KEYBIND_COUNT;
        pressExpiresAt = SDL_GetTicks() + PALETTE_PRESS_TIMEOUT_MS;
    }

    bool GetKeyBindDown(const EditorKeyBind bind) {
        if (TakePress(pressedKeyBind, bind, KEYBIND_COUNT)) return true;

        for (const SDL_Scancode key : keyBindings[bind])
            if (key != SDL_SCANCODE_UNKNOWN && InputManager::GetKeyDown(key) && !IsKeyTakenByCombo(key, KEY_BIND_INFO[bind].scope)) return true;
        return false;
    }

    bool GetKeyBind(const EditorKeyBind bind) {
        for (const SDL_Scancode key : keyBindings[bind])
            if (key != SDL_SCANCODE_UNKNOWN && InputManager::GetKey(key) && !IsKeyTakenByCombo(key, KEY_BIND_INFO[bind].scope)) return true;
        return false;
    }

    bool GetComboDown(const EditorCombo combo) {
        if (TakePress(pressedCombo, combo, COMBO_COUNT)) return true;
        if (COMBO_INFO[combo].needsShift && !ShiftHeld()) return false;

        for (int set = 0; set < COMBO_SLOTS; set += 2) {
            const SDL_Scancode first = comboBindings[combo][set];
            const SDL_Scancode second = comboBindings[combo][set + 1];

            if (second == SDL_SCANCODE_UNKNOWN || !InputManager::GetKeyDown(second)) continue;
            if (first == SDL_SCANCODE_UNKNOWN || InputManager::GetKey(first)) return true;
        }

        return false;
    }

    std::string KeyBindName(const EditorKeyBind bind) {
        for (const SDL_Scancode key : keyBindings[bind])
            if (key != SDL_SCANCODE_UNKNOWN) return InputManager::ScancodeToString(key);

        return Localisation::Get("editor.user_settings.key_none");
    }

    std::string ComboName(const EditorCombo combo) {
        for (int set = 0; set < COMBO_SLOTS; set += 2) {
            const SDL_Scancode first = comboBindings[combo][set];
            const SDL_Scancode second = comboBindings[combo][set + 1];

            if (second == SDL_SCANCODE_UNKNOWN) continue;

            const std::string shift = COMBO_INFO[combo].needsShift ? Localisation::Get("editor.user_settings.key_shift") + "+" : std::string();

            if (first == SDL_SCANCODE_UNKNOWN) return shift + InputManager::ScancodeToString(second);
            return InputManager::ScancodeToString(first) + "+" + shift + InputManager::ScancodeToString(second);
        }

        return Localisation::Get("editor.user_settings.key_none");
    }

    std::string FormatKeyBinds(const std::string& text) {
        static constexpr std::string_view OPEN = "{key:";

        std::string result;
        result.reserve(text.size());

        std::size_t position = 0;

        while (true) {
            const std::size_t open = text.find(OPEN, position);
            const std::size_t close = open == std::string::npos ? std::string::npos : text.find('}', open);

            if (close == std::string::npos) {
                result.append(text, position, std::string::npos);
                return result;
            }

            result.append(text, position, open - position);

            const std::string_view name(text.data() + open + OPEN.size(), close - open - OPEN.size());
            int bind = 0;
            while (bind < KEYBIND_COUNT && name != KEY_BIND_INFO[bind].saveName) ++bind;

            int combo = 0;
            if (bind == KEYBIND_COUNT)
                while (combo < COMBO_COUNT && name != COMBO_INFO[combo].saveName) ++combo;

            if (bind < KEYBIND_COUNT) result += KeyBindName(static_cast<EditorKeyBind>(bind));
            else if (combo < COMBO_COUNT) result += ComboName(static_cast<EditorCombo>(combo));
            else result.append(text, open, close + 1 - open);

            position = close + 1;
        }
    }

    SDL_Scancode* keyBindBeingListened = nullptr;

    double autoSaveInSeconds = 60.0;
    bool hasUnsavedChanges = false;
}