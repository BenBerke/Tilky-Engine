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

    int screenWidth = 1080;
    int screenHeight = 960;

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

    float lightValue = 255.0f;

    Vector3 wallColor = {255.0f, 255.0f, 255.0f};
    Vector3 ceilColor = {255.0f, 255.0f, 255.0f};
    Vector3 floorColor = {255.0f, 255.0f, 255.0f};

    bool manualSectorMode = false;
    std::vector<Vector2> manualSectorDots;

    // Dot Mode — Wall inspector (right-click select)
    bool editingWall = false;
    ID selectedWallID = INVALID_ID;
}