#pragma once

#include "../../Headers/Editor/Editor.hpp"

#include <vector>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace MapEditorInternal {
    extern int screenWidth;
    extern int screenHeight;
    constexpr int FONT_SIZE = 24;

    extern float editorZoom;
    extern float GRID_SIZE;
    constexpr float MIN_EDITOR_ZOOM = 0.20f;
    constexpr float MAX_EDITOR_ZOOM = 5.00f;

    constexpr float ENTITY_SIZE = 32.0f;

    enum Mode {
        MODE_DOT,
        MODE_SECTOR,
        MODE_WALL,
        MODE_ENTITY,

        MODE_COUNT
    };

    enum Action {
        ACTION_CREATE_SECTOR,
        ACTION_CREATE_WALL,
        ACTION_CREATE_CORNER,
        ACTION_CREATE_OBJECT,
    };

    enum State {
        STATE_MAP,
        STATE_UI,

        STATE_COUNT,
    };

    // Internal variables do not touch
    extern std::vector<Action> actions;

    extern SDL_Window* window;
    extern SDL_Renderer* renderer;
    extern TTF_Font* font;
    extern TTF_TextEngine* textEngine;

    // --

    extern Vector2 cameraPos;
    extern std::vector<Vector2> placedCorners;

    extern bool drawingLine;
    extern Vector2 lineStartWorld;

    extern std::vector<Vector2> sectorBeingCreated;

    extern bool editingSector;
    extern int selectedSector;
    extern bool creatableSector;

    extern bool editingWall;
    extern int selectedWall;

    extern bool editingComponent;
    extern bool editingEntity;
    extern Entity selectedEntity;
    extern float entitySize;

    extern std::string currentMap;

    extern Mode currentMode;
    extern State currentState;

    extern bool playerPlaced;

    extern int currentFloor;

    extern bool quit;
    extern bool play;
    extern bool shutdown;
    extern bool switchToRuntime;

    [[nodiscard]] bool SamePoint(const Vector2& a, const Vector2& b);
    [[nodiscard]] bool WithinRadius(const Vector2& a, const Vector2& b, const float radius);
    [[nodiscard]] Entity* EntityAt(const Vector2& mouseClick);
    [[nodiscard]] bool CornerExistsAt(const Vector2& point);
    [[nodiscard]] bool IsCornerConnectedToLine(const Vector2& point);
    [[nodiscard]] bool HasLineBetween(const Vector2& a, const Vector2& b);

    // Automatically if a sector's first and last vertices aren't the same
    std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate();
    bool IsSectorClosed(const std::vector<Vector2>& vertices);
    void AddSectorSelectionPoint(const Vector2& point);
    void FinishSectorSelection();

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos);
    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos);
    Vector2 SnapToGrid(const Vector2& worldPos);

    [[nodiscard]] bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point);

    float GetActiveGridSize();

    void DrawThickLine(SDL_Renderer* renderer, Vector2 start, Vector2 end, float thickness);
    void DrawFilledTriangle(const Triangle& triangle, SDL_FColor color);
    void DrawSectorPreview();
    void DrawExistingSectors();
    void DrawCorners();
    void DrawWalls();
    void DrawEntities();
    void DrawGridDots();

    void HandleEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);
    void DrawEditorUI();

    void DrawUIImages();
    void DrawEntities_UI();
    void DrawUIEditorUI();
    void HandleUIEditorInput(const bool mouseBlockedByImGui, const bool keyboardBlockedByImgui);

    void ChangeMode();

    bool Save(const std::string& saveTo);

    float DistancePointToSegmentSq(const Vector2& point, const Vector2& a, const Vector2& b);
    int GetWallAtPoint(const Vector2& worldPoint);

    void QueueLevelLoad(const std::string& levelName);
    bool ProcessPendingLevelLoad();

    void UpdateLevels();
}