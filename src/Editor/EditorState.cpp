#include "EditorInternal.hpp"

namespace Editor {
    std::vector<std::string> maps;
    std::string currentMap;

    Vector3 playerStartPos = {0.0f, 0.0f, 0.0f};
    int backgroundTextureIndex = -1;
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
    std::vector<Vector2> placedCorners;

    bool drawingLine = false;
    Vector2 lineStartWorld = {0.0f, 0.0f};

    std::vector<Vector2> sectorBeingCreated;

    bool editingSector = false;
    int selectedSector = -1;
    bool creatableSector = false;

    bool editingWall = false;
    int selectedWall = -1;

    bool editingComponent = false;
    bool editingEntity = false;
    Entity selectedEntity;

    std::string currentMap;

    float entitySize = 15.0f;

    Mode currentMode = MODE_DOT;
    State currentState = STATE_MAP;

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
    int wallTextureIndex = 1;
    int ceilTextureIndex = 1;
    int floorTextureIndex = 1;

    float floorHeight = 40.0f;
    float ceilHeight = 0.0f;

    float lightValue = 255.0f;

    Vector3 wallColor = {255.0f, 255.0f, 255.0f};
    Vector3 ceilColor = {255.0f, 255.0f, 255.0f};
    Vector3 floorColor = {255.0f, 255.0f, 255.0f};

}