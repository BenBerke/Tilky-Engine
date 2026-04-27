#pragma once

#include "Headers/Renderer/MapEditor.hpp"

#include <array>
#include <vector>

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace MapEditorInternal {
    constexpr int SCREEN_WIDTH = 1680;
    constexpr int SCREEN_HEIGHT = 960;
    constexpr int FONT_SIZE = 24;
    constexpr float GRID_SIZE = 32.0f;

    struct EditorLine {
        Vector2 start;
        Vector2 end;
    };

    enum Mode {
        MODE_DOT,
        MODE_SECTOR,
        MODE_WALL,
        MODE_OBJECT,

        MODE_COUNT
    };

    enum ObjectType {
        PLAYER,

        OBJECT_COUNT,
    };

    struct Object {
        ObjectType type;
        Vector2 position;
    };

    extern SDL_Window* window;
    extern SDL_Renderer* renderer;
    extern TTF_Font* font;
    extern TTF_TextEngine* textEngine;

    extern Vector2 cameraPos;
    extern std::vector<Vector2> placedCorners;
    extern std::vector<EditorLine> placedLines;

    extern bool drawingLine;
    extern Vector2 lineStartWorld;

    extern std::vector<Vector2> sectorBeingCreated;

    extern bool editingSector;
    extern int selectedSector;
    extern bool creatableSector;

    extern Mode currentMode;

    extern std::vector<Object> objects;
    extern ObjectType currentObjectToPlace;
    extern bool playerPlaced;

    extern bool quit;

    bool SamePoint(const Vector2& a, const Vector2& b);
    bool CornerExistsAt(const Vector2& point);
    bool IsCornerConnectedToLine(const Vector2& point);
    bool HasLineBetween(const Vector2& a, const Vector2& b);

    std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate();
    bool IsSectorClosed(const std::vector<Vector2>& vertices);
    void AddSectorSelectionPoint(const Vector2& point);
    void FinishSectorSelection();

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos);
    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos);
    Vector2 SnapToGrid(const Vector2& worldPos);

    bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point);

    void DrawThickLine(SDL_Renderer* renderer, Vector2 start, Vector2 end, float thickness);
    void DrawFilledTriangle(const Triangle& triangle, SDL_FColor color);
    void DrawSectorPreview();
    void DrawExistingSectors();
    void DrawCorners();
    void DrawWalls();
    void DrawObjects();
    void DrawGridDots();

    void HandleEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);
    void DrawEditorUI();

    const char* GetModeName(Mode mode);
    void MoveMode();

    bool SaveAndQuit();

    extern std::vector<std::array<char, 256>> textureInputs;
}