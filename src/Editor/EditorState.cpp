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
    float GRID_SIZE = 32.0f;

    Vector2 cameraPos = {0.0f, 0.0f};

    std::vector<Dot> dots;
    ID nextDotID = 0;
    std::unordered_map<ID, int> dotIDToIndex;
    ID selectedDotID = INVALID_ID;

    std::vector<Vector2> sectorBeingCreated;
    PendingSectorParams pendingSectorParams;

    bool editingSector = false;
    ID selectedSectorID = INVALID_ID;

    bool editingComponent = false;
    bool editingEntity = false;
    Entity selectedEntity;

    std::string currentMap;

    float entitySize = 15.0f;

    Mode currentMode = MODE_DOT;
    State currentState = STATE_MAP;

    EditorTheme currentTheme = THEME_DARK;
    bool textureViewMode = false;

    bool playerPlaced = false;

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

    uint_fast32_t wallColor = std::numeric_limits<uint_fast32_t>::max();
    uint_fast32_t ceilColor = std::numeric_limits<uint_fast32_t>::max();
    uint_fast32_t floorColor = std::numeric_limits<uint_fast32_t>::max();

    bool manualSectorMode = false;
    std::vector<Vector2> manualSectorDots;

    // Dot Mode — Wall inspector (right-click select)
    bool editingWall = false;
    ID selectedWallID = INVALID_ID;

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
}