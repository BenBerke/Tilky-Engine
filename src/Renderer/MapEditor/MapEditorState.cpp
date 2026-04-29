#include "MapEditorInternal.hpp"

namespace MapEditor {
    std::vector<Wall> walls;
    std::vector<Sector> sectors;
    std::vector<Object> objects;
    Vector2 playerStartPos = {0.0f, 0.0f};
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

    bool editingObject = false;
    int selectedObject = -1;

    float objectSize = 10.0f;

    Mode currentMode = MODE_DOT;

    ObjectType currentObjectTypeToPlace = OBJ_PLAYER_SPAWN;
    bool playerPlaced = false;

    std::vector<std::array<char, 256>> textureInputs;

    std::vector<Action> actions;

    int currentFloor;

    bool quit = false;
}