#include "MapEditorInternal.hpp"

namespace MapEditor {
    std::vector<std::string> maps;
    std::string currentMap;

    Vector2 playerStartPos = {0.0f, 0.0f};
    int backgroundTextureIndex = -1;
}

namespace MapEditorInternal {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    TTF_TextEngine* textEngine = nullptr;

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

    std::string currentMap;

    float objectSize = 15.0f;

    Mode currentMode = MODE_DOT;

    bool playerPlaced = false;

    std::vector<std::array<char, 256>> textureInputs;

    std::vector<Action> actions;

    int currentFloor;

    bool quit = false;
}