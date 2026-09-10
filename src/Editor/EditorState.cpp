#include "EditorInternal.hpp"

namespace Editor {
    std::vector<std::string> maps;
    std::string currentMap;

    Vector3 playerStartPos = {0.0f, 0.0f, 0.0f};
    std::string backgroundTextureFileName = "";
}

namespace MapEditorInternal {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    TTF_TextEngine* textEngine = nullptr;

    ImFont* scriptEditorFont = nullptr;

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

    EditorTheme currentTheme = THEME_DARK;
    bool textureViewMode = false;

    std::vector<Action> actions;
    std::vector<GeometrySnapshot> geometrySnapshots;
    std::string lastGeometryError;

    int currentFloor;

    bool quit = false;
    bool play = false;
    bool shutdown = false;
    bool switchToRuntime = false;

    bool hasEntityInClipboard = false;
    Entity entityInClipboard;

    //-- Sector to be created variables
    std::string wallTexture;
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

    float cameraSpeed = 1.0f;
    float cameraStepSize = 50.0f;
}