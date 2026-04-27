#include "MapEditorInternal.hpp"

namespace MapEditor {
    std::vector<Wall> walls;
    std::vector<Sector> sectors;
    Vector2 playerStartPos = {0.0f, 0.0f};
}

namespace MapEditorInternal {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    TTF_TextEngine* textEngine = nullptr;

    Vector2 cameraPos = {0.0f, 0.0f};
    std::vector<Vector2> placedCorners;
    std::vector<EditorLine> placedLines;

    bool drawingLine = false;
    Vector2 lineStartWorld = {0.0f, 0.0f};

    std::vector<Vector2> sectorBeingCreated;

    bool editingSector = false;
    int selectedSector = -1;
    bool creatableSector = false;

    Mode currentMode = MODE_DOT;

    std::vector<Object> objects;
    ObjectType currentObjectToPlace = PLAYER;
    bool playerPlaced = false;

    std::vector<std::array<char, 256>> textureInputs;

    bool quit = false;
}