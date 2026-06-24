#pragma once

#include "../../Headers/Editor/Editor.hpp"

#include <string>
#include <unordered_map>
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

    // NOTE: MODE_WALL has been removed (feature #4). Walls are still real
    // internal geometry (Wall struct / level.walls are untouched) but the
    // user no longer enters a dedicated mode to draw them by hand - Sector
    // Mode creates/reuses walls automatically as part of the chain workflow.
    enum Mode {
        MODE_DOT,
        MODE_SECTOR,
        MODE_ENTITY,

        MODE_COUNT
    };

    enum Action {
        ACTION_CREATE_SECTOR,
        ACTION_CREATE_WALL,
        ACTION_CREATE_CORNER, // historical name - now fires for Dot placement/undo.
        ACTION_CREATE_OBJECT,
    };

    enum State {
        STATE_MAP,
        STATE_UI,

        STATE_COUNT,
    };

    enum EditorTheme {
        THEME_DARK,
        THEME_LIGHT
    };

    // A Dot (feature #6/#7/#10) is a free-floating, off-grid-capable editor
    // reference point with a stable ID. It is editor-session data (kept in
    // MapEditorInternal, NOT in Level/LevelSerialization - see NOTES.md for
    // why) used as a visual aid and as a snap target for sector chains.
    struct Dot {
        ID id = INVALID_ID;
        Vector2 position{};
    };

    // Snapshot of the sector-creation parameters from the Editor menu, taken
    // the instant a chain starts (feature #5: "use the values currently
    // picked in the Editor menu before the chain started").
    struct PendingSectorParams {
        int wallTextureIndex = -1;
        int ceilTextureIndex = -1;
        int floorTextureIndex = -1;

        float floorHeight = 0.0f;
        float ceilHeight = 0.0f;
        float lightValue = 255.0f;

        Vector3 wallColor{};
        Vector3 ceilColor{};
        Vector3 floorColor{};
    };

    // Internal variables do not touch
    extern std::vector<Action> actions;

    extern SDL_Window* window;
    extern SDL_Renderer* renderer;
    extern TTF_Font* font;
    extern TTF_TextEngine* textEngine;

    // --

    extern Vector2 cameraPos;

    // Dots (feature #6/#7/#10). Replaces the old grid-only `placedCorners`.
    extern std::vector<Dot> dots;
    extern ID nextDotID;
    extern std::unordered_map<ID, int> dotIDToIndex;
    extern ID selectedDotID;

    // Sector creation chain (feature #5).
    extern std::vector<Vector2> sectorBeingCreated;
    extern PendingSectorParams pendingSectorParams;

    extern bool editingSector;
    extern ID selectedSectorID; // stable ID, NOT a vector index (feature #11)

    extern bool editingComponent;
    extern bool editingEntity;
    extern Entity selectedEntity;
    extern float entitySize;

    extern std::string currentMap;

    extern Mode currentMode;
    extern State currentState;

    extern EditorTheme currentTheme;  // feature #2
    extern bool textureViewMode;      // feature #9

    extern bool playerPlaced;

    extern int currentFloor;

    extern bool quit;
    extern bool play;
    extern bool shutdown;
    extern bool switchToRuntime;

    extern bool hasEntityInClipboard;
    extern Entity entityInClipboard;

    //-- Sector to be created variables
    extern int wallTextureIndex;
    extern int ceilTextureIndex;
    extern int floorTextureIndex;

    extern float floorHeight;
    extern float ceilHeight;

    extern float lightValue;

    extern Vector3 wallColor;
    extern Vector3 ceilColor;
    extern Vector3 floorColor;

    [[nodiscard]] bool SamePoint(const Vector2& a, const Vector2& b);
    [[nodiscard]] bool WithinRadius(const Vector2& a, const Vector2& b, float radius);
    [[nodiscard]] Entity* EntityAt(const Vector2& mouseClick);
    [[nodiscard]] bool HasLineBetween(const Vector2& a, const Vector2& b);

    // Automatically strips a trailing duplicate-of-front vertex if one is
    // ever present (defensive; the new chain flow never adds one - see
    // NOTES.md). Used by the preview renderer.
    std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate();
    bool IsSectorClosed(const std::vector<Vector2>& vertices);
    void AddSectorSelectionPoint(const Vector2& point);

    // Sector chain workflow (feature #5).
    void TrySectorChainClick(const Vector2& resolvedPoint);
    void FinishSectorSelection();
    void CancelSectorChain();

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos);
    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos);
    Vector2 SnapToGrid(const Vector2& worldPos);

    // Snapping (feature #7): nearest of {dots, wall starts, wall ends} within
    // radius, else grid snap.
    Vector2 ResolveSnapPoint(const Vector2& mouseWorld);

    [[nodiscard]] bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point);

    float GetActiveGridSize();

    void DrawThickLine(SDL_Renderer* renderer, Vector2 start, Vector2 end, float thickness);
    void DrawFilledTriangle(const Triangle& triangle, SDL_FColor color);
    void DrawFilledTriangleTextured(const Triangle& triangle, SDL_Texture* texture, SDL_FColor tint);
    void DrawSectorPreview();
    void DrawSnapIndicator();
    void DrawExistingSectors();
    void DrawDots(); // renamed from DrawCorners (feature #6)
    void DrawWalls();
    void DrawEntities();
    void DrawGridDots();

    void HandleEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);
    void DrawEditorUI();

    void DrawUIImages();
    void DrawEntities_UI();
    void DrawUIEditorUI();
    void HandleUIEditorInput(bool mouseBlockedByImGui, bool keyboardBlockedByImgui);

    void ChangeMode();
    void ApplyEditorTheme(EditorTheme theme); // feature #2

    bool Save(const std::string& saveTo);

    float DistancePointToSegmentSq(const Vector2& point, const Vector2& a, const Vector2& b);
    int GetWallAtPoint(const Vector2& worldPoint);

    // Dot lifecycle (feature #6/#10/#11).
    void AddDot(const Vector2& position);
    void RemoveDot(ID dotID);

    // Sector deletion with full ID-safety cleanup (feature #11).
    void DeleteSector(ID sectorID);

    // Texture preview / Texture View Mode access point (feature #8/#9).
    // Returns nullptr safely if the index is invalid or unavailable - never
    // crashes on a missing texture. See NOTES.md for the one assumption this
    // makes about EditorTextureCache's interface.
    SDL_Texture* GetEditorTexture(int textureIndex);

    void QueueLevelLoad(const std::string& levelName);
    bool ProcessPendingLevelLoad();

    void UpdateLevels();
}