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
    TTF_TextEngine* textEngine = nullptr;

    ImFont* scriptEditorFont = nullptr;
    ImFont* editorIconFont = nullptr;

    float editorZoom = 1.0f;
    float GRID_SIZE = 12.0f; // fixed world-space grid/snap spacing; adjustable at runtime via the Grid & Snapping panel (see DrawEditorUI)

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

    bool editingComponent = false;
    bool editingEntity = false;
    Entity selectedEntity;

    std::string currentMap;

    Mode currentMode = MODE_GEOMETRY;
    State currentState = STATE_MAP;

    bool textureViewMode = false;

    std::vector<Action> actions;
    std::vector<GeometrySnapshot> geometrySnapshots;
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
    bool draggingWallGeometry = false;

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

    std::vector<ID> selectedEntities = {};
    std::vector<ID> selectedSectors = {};
    std::vector<ID> selectedWalls = {};

    // User Settings
    Vector3 normalEntityColor = {190, 190, 190};
    Vector3 highlightedEntityColor = {20, 20, 220};
    Vector3 spriteEntityColor = {100, 200, 100};

    Vector3 normalWallColor = {205, 205, 205};
    Vector3 highlightedWallColor = {15, 15, 200};

    Vector3 hoveredSectorColor = {250, 250, 100};
    Vector3 highlightedSectorColor = {40, 40, 220};

    Vector3 snapIndicatorColor = {220, 255, 255};

    Vector3 kValidLineColor = {255, 220, 80};
    Vector3 kInvalidLineColor = {230, 70, 70};
    Vector3 kAnchorColor = {80, 220, 255};
    Vector4 kValidFillColor = {1.0f, 0.863f, 0.314f, 0.28f};
    Vector4 kInvalidFillColor = {0.90f, 0.27f, 0.27f, 0.28f};

    Vector3 themeTextColor = {255, 255, 255};
    Vector3 gridColor = {255, 255, 255};

    Vector3 backgroundColor = {45, 45, 45};

    Vector3 normalHandleColor = {80, 220, 255};
    Vector3 highlightedHandleColor = {255, 255, 255};
    Vector3 handleOutlineColor = {20, 20, 20};

    Vector3 gizmoXColor = {230, 60, 60};
    Vector3 gizmoYColor = {80, 200, 80};
    Vector3 gizmoZColor = {60, 110, 240};

    float cameraSpeed = 1.0f;
    float cameraStepSize = 50.0f;

    float runtimeCameraFov = DEFAULT_RUNTIME_CAMERA_FOV;

    bool hierarchyVisible = true;
    bool editorPanelVisible = true;
    bool worldSettingsVisible = true;
    bool assetBrowserVisible = true;

    // In EditorKeyBind order.
    const EditorKeyBindInfo KEY_BIND_INFO[KEYBIND_COUNT] = {
        {"pan_up",           "editor.key.pan_up",           "editor.key_group.camera",    {SDL_SCANCODE_W}},
        {"pan_down",         "editor.key.pan_down",         "editor.key_group.camera",    {SDL_SCANCODE_S}},
        {"pan_left",         "editor.key.pan_left",         "editor.key_group.camera",    {SDL_SCANCODE_A}},
        {"pan_right",        "editor.key.pan_right",        "editor.key_group.camera",    {SDL_SCANCODE_D}},
        {"step_up",          "editor.key.step_up",          "editor.key_group.camera",    {SDL_SCANCODE_UP}},
        {"step_down",        "editor.key.step_down",        "editor.key_group.camera",    {SDL_SCANCODE_DOWN}},
        {"step_left",        "editor.key.step_left",        "editor.key_group.camera",    {SDL_SCANCODE_LEFT}},
        {"step_right",       "editor.key.step_right",       "editor.key_group.camera",    {SDL_SCANCODE_RIGHT}},
        {"zoom_in",          "editor.key.zoom_in",          "editor.key_group.camera",    {SDL_SCANCODE_EQUALS, SDL_SCANCODE_KP_PLUS}},
        {"zoom_out",         "editor.key.zoom_out",         "editor.key_group.camera",    {SDL_SCANCODE_MINUS, SDL_SCANCODE_KP_MINUS}},
        {"focus_selection",  "editor.key.focus_selection",  "editor.key_group.camera",    {SDL_SCANCODE_F}},

        {"cycle_mode",       "editor.key.cycle_mode",       "editor.key_group.modes",     {SDL_SCANCODE_Q}},
        {"geometry_mode",    "editor.key.geometry_mode",    "editor.key_group.modes",     {SDL_SCANCODE_G, SDL_SCANCODE_V}},
        {"entity_mode",      "editor.key.entity_mode",      "editor.key_group.modes",     {SDL_SCANCODE_E, SDL_SCANCODE_T}},
        {"sector_mode",      "editor.key.sector_mode",      "editor.key_group.modes",     {SDL_SCANCODE_R}},

        {"place_entity",     "editor.key.place_entity",     "editor.key_group.editing",   {SDL_SCANCODE_E}},
        {"delete",           "editor.key.delete",           "editor.key_group.editing",   {SDL_SCANCODE_DELETE}},

        {"finish_shape",     "editor.key.finish_shape",     "editor.key_group.drawing",   {SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE}},
        {"undo_point",       "editor.key.undo_point",       "editor.key_group.drawing",   {SDL_SCANCODE_BACKSPACE}},
        {"fewer_sides",      "editor.key.fewer_sides",      "editor.key_group.drawing",   {SDL_SCANCODE_LEFTBRACKET}},
        {"more_sides",       "editor.key.more_sides",       "editor.key_group.drawing",   {SDL_SCANCODE_RIGHTBRACKET}},
        {"flip_staircase",   "editor.key.flip_staircase",   "editor.key_group.drawing",   {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},
        {"tool_freehand",    "editor.key.tool_freehand",    "editor.key_group.drawing",   {SDL_SCANCODE_1}},
        {"tool_rectangle",   "editor.key.tool_rectangle",   "editor.key_group.drawing",   {SDL_SCANCODE_2}},
        {"tool_polygon",     "editor.key.tool_polygon",     "editor.key_group.drawing",   {SDL_SCANCODE_3}},
        {"tool_circle",      "editor.key.tool_circle",      "editor.key_group.drawing",   {SDL_SCANCODE_4}},
        {"tool_curve",       "editor.key.tool_curve",       "editor.key_group.drawing",   {SDL_SCANCODE_5}},
        {"tool_staircase",   "editor.key.tool_staircase",   "editor.key_group.drawing",   {SDL_SCANCODE_6}},

        {"disable_snap",     "editor.key.disable_snap",     "editor.key_group.modifiers", {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"multi_select",     "editor.key.multi_select",     "editor.key_group.modifiers", {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"range_select",     "editor.key.range_select",     "editor.key_group.modifiers", {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
        {"constrain_angle",  "editor.key.constrain_angle",  "editor.key_group.modifiers", {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
        {"split_wall",       "editor.key.split_wall",       "editor.key_group.modifiers", {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT}},

        {"rt_fly_forward",      "editor.key.rt_fly_forward",      "editor.key_group.runtime_camera",  {SDL_SCANCODE_W}},
        {"rt_fly_back",         "editor.key.rt_fly_back",         "editor.key_group.runtime_camera",  {SDL_SCANCODE_S}},
        {"rt_fly_left",         "editor.key.rt_fly_left",         "editor.key_group.runtime_camera",  {SDL_SCANCODE_A}},
        {"rt_fly_right",        "editor.key.rt_fly_right",        "editor.key_group.runtime_camera",  {SDL_SCANCODE_D}},
        {"rt_fly_up",           "editor.key.rt_fly_up",           "editor.key_group.runtime_camera",  {SDL_SCANCODE_SPACE}},
        {"rt_fly_down",         "editor.key.rt_fly_down",         "editor.key_group.runtime_camera",  {SDL_SCANCODE_LCTRL}},
        {"rt_speed_up",         "editor.key.rt_speed_up",         "editor.key_group.runtime_camera",  {SDL_SCANCODE_Q}},
        {"rt_speed_down",       "editor.key.rt_speed_down",       "editor.key_group.runtime_camera",  {SDL_SCANCODE_E}},
        {"rt_reset_speed",      "editor.key.rt_reset_speed",      "editor.key_group.runtime_camera",  {SDL_SCANCODE_C}},

        {"rt_move_tool",        "editor.key.rt_move_tool",        "editor.key_group.runtime_editing", {SDL_SCANCODE_1}},
        {"rt_rotate_tool",      "editor.key.rt_rotate_tool",      "editor.key_group.runtime_editing", {SDL_SCANCODE_2}},
        {"rt_disable_snap",     "editor.key.rt_disable_snap",     "editor.key_group.runtime_editing", {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}},
        {"rt_add_to_selection", "editor.key.rt_add_to_selection", "editor.key_group.runtime_editing", {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}},
        {"rt_texture_x_only",   "editor.key.rt_texture_x_only",   "editor.key_group.runtime_editing", {SDL_SCANCODE_X}},
        {"rt_texture_y_only",   "editor.key.rt_texture_y_only",   "editor.key_group.runtime_editing", {SDL_SCANCODE_Y}},
        {"rt_flip_texture_x",   "editor.key.rt_flip_texture_x",   "editor.key_group.runtime_editing", {SDL_SCANCODE_X}},
        {"rt_flip_texture_y",   "editor.key.rt_flip_texture_y",   "editor.key_group.runtime_editing", {SDL_SCANCODE_Y}},
    };

    SDL_Scancode keyBindings[KEYBIND_COUNT][KEYBIND_SLOTS] = {};

    namespace {
        // keyBindings starts as a copy of the defaults.
        const bool keyBindingsInitialized = [] {
            for (int bind = 0; bind < KEYBIND_COUNT; ++bind)
                for (int slot = 0; slot < KEYBIND_SLOTS; ++slot)
                    keyBindings[bind][slot] = KEY_BIND_INFO[bind].defaults[slot];
            return true;
        }();
    }

    bool GetKeyBindDown(const EditorKeyBind bind) {
        for (const SDL_Scancode key : keyBindings[bind])
            if (key != SDL_SCANCODE_UNKNOWN && InputManager::GetKeyDown(key)) return true;
        return false;
    }

    bool GetKeyBind(const EditorKeyBind bind) {
        for (const SDL_Scancode key : keyBindings[bind])
            if (key != SDL_SCANCODE_UNKNOWN && InputManager::GetKey(key)) return true;
        return false;
    }

    std::string KeyBindName(const EditorKeyBind bind) {
        for (const SDL_Scancode key : keyBindings[bind])
            if (key != SDL_SCANCODE_UNKNOWN) return InputManager::ScancodeToString(key);

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

            if (bind < KEYBIND_COUNT) result += KeyBindName(static_cast<EditorKeyBind>(bind));
            else result.append(text, open, close + 1 - open);

            position = close + 1;
        }
    }

    SDL_Scancode* keyBindBeingListened = nullptr;
}