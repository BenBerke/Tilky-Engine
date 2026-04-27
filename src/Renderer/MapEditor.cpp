//
// Created by berke on 4/13/2026.
//

#include "../../Headers/Renderer/MapEditor.hpp"
#include "../../Headers/Math/Vector/Vector2Math.hpp"
#include "../../Headers/Engine/InputManager.hpp"

#include "imgui.h"

#include <algorithm>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <fstream>

#define SCREEN_WIDTH 1680
#define SCREEN_HEIGHT 960

#define FONT_SIZE 24

#define GRID_SIZE 32.0f

namespace {
    Vector2 cameraPos = {0, 0};
    std::vector<Vector2> placedCorners;

    struct EditorLine {
        Vector2 start;
        Vector2 end;
    };

    std::vector<EditorLine> placedLines;

    bool drawingLine = false;
    Vector2 lineStartWorld = {0.0f, 0.0f};

    std::vector<Vector2> sectorBeingCreated;

    bool editingSector = false;
    int selectedSector = -1;
    bool creatableSector = false;

    enum Mode {
        MODE_DOT,
        MODE_SECTOR,
        MODE_WALL,
        MODE_OBJECT,

        MODE_COUNT
    };
    Mode currentMode = MODE_DOT;

    enum ObjectType {
        PLAYER,

        OBJECT_COUNT,
    };

    struct Object {
        ObjectType type;
        Vector2 position;
    };
    std::vector<Object> objects;
    ObjectType currentObjectToPlace = PLAYER;
    bool playerPlaced = false;

    bool quit = false;
}

bool SamePoint(const Vector2 &a, const Vector2 &b) {
    return a.x == b.x && a.y == b.y;
}

bool CornerExistsAt(const Vector2 &point) {
    for (const Vector2 &placedDot: placedCorners) {
        if (SamePoint(placedDot, point)) {
            return true;
        }
    }

    return false;
}

bool IsCornerConnectedToLine(const Vector2 &point) {
    for (const EditorLine &line: placedLines) {
        if (SamePoint(line.start, point) || SamePoint(line.end, point)) {
            return true;
        }
    }

    // Optional: also protect the dot while currently drawing a line from it
    if (drawingLine && SamePoint(lineStartWorld, point)) {
        return true;
    }

    return false;
}

void DrawThickLine(SDL_Renderer *renderer, const Vector2 start, const Vector2 end, const float thickness) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;

    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0001f) {
        return;
    }

    const float normalX = -dy / length;
    const float normalY = dx / length;

    const int halfThickness = static_cast<int>(thickness * 0.5f);

    for (int i = -halfThickness; i <= halfThickness; ++i) {
        const float offsetX = normalX * static_cast<float>(i);
        const float offsetY = normalY * static_cast<float>(i);

        SDL_RenderLine(
            renderer,
            start.x + offsetX,
            start.y + offsetY,
            end.x + offsetX,
            end.y + offsetY
        );
    }
}

bool HasLineBetween(const Vector2 &a, const Vector2 &b) {
    for (const EditorLine &line: placedLines) {
        const bool sameDirection = SamePoint(line.start, a) && SamePoint(line.end, b);
        const bool oppositeDirection = SamePoint(line.start, b) && SamePoint(line.end, a);

        if (sameDirection || oppositeDirection) {
            return true;
        }
    }

    return false;
}

std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate() {
    std::vector<Vector2> result = sectorBeingCreated;

    if (result.size() >= 2 && SamePoint(result.front(), result.back())) {
        result.pop_back();
    }

    return result;
}

bool IsSectorClosed(const std::vector<Vector2> &vertices) {
    if (vertices.size() < 3) {
        return false;
    }

    // Case 1: user clicked the first point again at the end.
    if (sectorBeingCreated.size() >= 4 &&
        SamePoint(sectorBeingCreated.front(), sectorBeingCreated.back())) {
        return true;
    }

    // Case 2: selected points are connected by existing editor lines.
    for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
        const Vector2 &a = vertices[i];
        const Vector2 &b = vertices[(i + 1) % vertices.size()];

        if (!HasLineBetween(a, b)) {
            return false;
        }
    }

    return true;
}

void AddSectorSelectionPoint(const Vector2 &point) {
    // Allow clicking the first point again to close the shape.
    if (sectorBeingCreated.size() >= 3 && SamePoint(point, sectorBeingCreated.front())) {
        sectorBeingCreated.push_back(point);
        return;
    }

    // Ignore duplicate points elsewhere.
    for (const Vector2 &existingPoint: sectorBeingCreated) {
        if (SamePoint(existingPoint, point)) {
            return;
        }
    }

    sectorBeingCreated.push_back(point);
}

Vector2 ScreenToWorld(const Vector2 &screenPos, const Vector2 &cameraPos) {
    return {
        screenPos.x - SCREEN_WIDTH * 0.5f + cameraPos.x,
        SCREEN_HEIGHT * 0.5f - screenPos.y + cameraPos.y
    };
}

Vector2 WorldToScreen(const Vector2 &worldPos, const Vector2 &cameraPos) {
    return {
        worldPos.x - cameraPos.x + SCREEN_WIDTH * 0.5f,
        SCREEN_HEIGHT * 0.5f - (worldPos.y - cameraPos.y)
    };
}

Vector2 SnapToGrid(const Vector2 &worldPos) {
    return {
        std::round(worldPos.x / GRID_SIZE) * GRID_SIZE,
        std::round(worldPos.y / GRID_SIZE) * GRID_SIZE
    };
}

bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point) {
    bool inside = false;

    for (int i = 0, j = static_cast<int>(polygon.size()) - 1;
         i < static_cast<int>(polygon.size());
         j = i++) {

        const Vector2& a = polygon[i];
        const Vector2& b = polygon[j];

        const bool crossesY = (a.y > point.y) != (b.y > point.y);

        if (crossesY) {
            const float intersectX =
                (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;

            if (point.x < intersectX) {
                inside = !inside;
            }
        }
         }

    return inside;
}

namespace MapEditor {
    //region Triangulation
    static constexpr float EPSILON = 0.00001f;

    // Cross product of the triangle/turn A -> B -> C
    float crossAtPoint(const Vector2 a, const Vector2 b, const Vector2 c) {
        return Vector2Math::Cross(b - a, c - a);
    }

    float polygonArea(const std::vector<Vector2> &vertices) {
        float area = 0.0f;

        for (int i = 0; i < vertices.size(); ++i) {
            int next = (i + 1) % vertices.size();

            area += Vector2Math::Cross(vertices[i], vertices[next]);
        }

        return area * 0.5f;
    }

    // Check if point P is inside triangle A, B, C
    bool isInsideTriangle(const Vector2 a, const Vector2 b, const Vector2 c, const Vector2 p) {
        const float cp1 = crossAtPoint(a, b, p);
        const float cp2 = crossAtPoint(b, c, p);
        const float cp3 = crossAtPoint(c, a, p);

        return cp1 >= -EPSILON &&
               cp2 >= -EPSILON &&
               cp3 >= -EPSILON;
    }

    bool isEar(const std::vector<Vector2> &vertices, const int prev, const int curr, const int next) {
        const Vector2 a = vertices[prev];
        const Vector2 b = vertices[curr];
        const Vector2 c = vertices[next];

        // Must be convex.
        // This assumes vertices are in counter-clockwise order.
        if (crossAtPoint(a, b, c) <= EPSILON) {
            return false;
        }

        // No other vertex should be inside this triangle.
        for (int i = 0; i < vertices.size(); ++i) {
            if (i == prev || i == curr || i == next) {
                continue;
            }

            if (isInsideTriangle(a, b, c, vertices[i])) {
                return false;
            }
        }

        return true;
    }

    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices) {
        std::vector<Triangle> triangles;

        if (vertices.size() < 3) {
            return triangles;
        }

        // Ear clipping below assumes counter-clockwise vertices.
        // If the polygon is clockwise, reverse it first.
        if (polygonArea(vertices) < 0.0f) {
            std::ranges::reverse(vertices);
        }

        while (vertices.size() > 3) {
            bool ear_found = false;

            for (int i = 0; i < vertices.size(); ++i) {
                int prev = (i == 0) ? vertices.size() - 1 : i - 1;
                int next = (i == vertices.size() - 1) ? 0 : i + 1;

                if (isEar(vertices, prev, i, next)) {
                    triangles.push_back({
                        vertices[prev],
                        vertices[i],
                        vertices[next]
                    });

                    vertices.erase(vertices.begin() + i);

                    ear_found = true;
                    break;
                }
            }

            if (!ear_found) {
                // Usually means polygon is self-intersecting,
                // has duplicate points, or has another invalid shape issue.
                break;
            }
        }

        if (vertices.size() == 3) {
            if (crossAtPoint(vertices[0], vertices[1], vertices[2]) > EPSILON) {
                triangles.push_back({
                    vertices[0],
                    vertices[1],
                    vertices[2]
                });
            }
        }

        return triangles;
    }

    //endregion

    void AddWall(const Wall &wall) {
        walls.push_back(wall);
    }
    void AddSector(const Sector &sector) {
        sectors.push_back(sector);
    }

    void CreateSector(
    const std::vector<Vector2>& vertices,
    const float ceilHeight,
    const float floorHeight,
    const Vector3 ceilColor,
    const Vector3 floorColor,
    const int ceilTextureIndex,
    const int floorTextureIndex
) {
        Sector newSector = {
            vertices,
            {},
            ceilHeight,
            floorHeight,
            ceilColor,
            floorColor,
            ceilTextureIndex,
            floorTextureIndex
        };

        newSector.triangles = Triangulate(newSector.vertices);

        AddSector(newSector);
    }

    void TriangulateSectors() {
        for (Sector &sector: sectors) {
            sector.triangles.clear();

            if (sector.vertices.size() < 3) continue;
            sector.triangles = Triangulate(sector.vertices);
        }
    }

    //region Render Functions

    void DrawFilledTriangle(const Triangle &triangle, const SDL_FColor color) {
        const Vector2 a = WorldToScreen(triangle.a, cameraPos);
        const Vector2 b = WorldToScreen(triangle.b, cameraPos);
        const Vector2 c = WorldToScreen(triangle.c, cameraPos);

        SDL_Vertex vertices[3];

        vertices[0].position = {a.x, a.y};
        vertices[0].color = color;
        vertices[0].tex_coord = {0.0f, 0.0f};

        vertices[1].position = {b.x, b.y};
        vertices[1].color = color;
        vertices[1].tex_coord = {0.0f, 0.0f};

        vertices[2].position = {c.x, c.y};
        vertices[2].color = color;
        vertices[2].tex_coord = {0.0f, 0.0f};

        SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0);
    }

    void DrawSectorPreview() {
        const std::vector<Vector2> previewVertices = GetSectorVerticesWithoutClosingDuplicate();

        if (previewVertices.size() < 3) {
            return;
        }

        const std::vector<Triangle> previewTriangles = Triangulate(previewVertices);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const SDL_FColor redPreviewColor = {
            1.0f,
            0.0f,
            0.0f,
            0.30f
        };

        for (const Triangle &triangle: previewTriangles) {
            DrawFilledTriangle(triangle, redPreviewColor);
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawExistingSectors() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        int hoveredSectorIndex = -1;

        for (int i = static_cast<int>(sectors.size()) - 1; i >= 0; --i) {
            if (IsPointInsidePolygon(sectors[i].vertices, mouseWorld)) {
                hoveredSectorIndex = i;
                break;
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const SDL_FColor normalSectorColor = {
            0.0f,
            0.3f,
            1.0f,
            0.25f
        };

        const SDL_FColor hoveredSectorColor = {
            1.0f,
            0.75f,
            0.0f,
            0.45f
        };

        const int totalSectors = static_cast<int>(sectors.size());
        for (int i = 0; i < totalSectors; ++i) {
            const float offset = static_cast<float>(i) / totalSectors;
            const SDL_FColor thisSectorColor = {std::fmod((hoveredSectorColor.r + offset),1.0f), std::fmod((hoveredSectorColor.g + offset),1.0f), std::fmod((hoveredSectorColor.b + offset),1.0f), hoveredSectorColor.a};
            const SDL_FColor sectorColor =
                i == hoveredSectorIndex && currentMode == MODE_SECTOR ? hoveredSectorColor : thisSectorColor;

            for (const Triangle& triangle : sectors[i].triangles) {
                DrawFilledTriangle(triangle, sectorColor);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawCorners() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        for (const Vector2 &dotWorld: placedCorners) {
            Vector2 dotScreen = WorldToScreen(dotWorld, cameraPos);

            SDL_FRect dotRect = {
                dotScreen.x - 3.0f,
                dotScreen.y - 3.0f,
                6.0f,
                6.0f
            };
            SDL_RenderFillRect(renderer, &dotRect);
        }
    }

    void DrawWalls() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        for (const EditorLine &line: placedLines) {
            const Vector2 startScreen = WorldToScreen(line.start, cameraPos);
            const Vector2 endScreen = WorldToScreen(line.end, cameraPos);

            DrawThickLine(renderer, startScreen, endScreen, 5.0f);
        }
    }

    void DrawObjects() {
        for (const Object &object : objects) {
            const Vector2 objectScreen = WorldToScreen(object.position, cameraPos);

            Vector3 color = {0, 0, 0};
            switch (object.type) {
                case PLAYER: color = {255, 0, 0,}; break;
                default: color = {0, 0, 0, }; break;
            }

            SDL_FRect dotRect = {
                objectScreen.x - 3.0f,
                objectScreen.y - 3.0f,
                15.0f,
                15.0f
            };
            SDL_SetRenderDrawColor(renderer, color.x, color.y, color.z, 255);
            SDL_RenderFillRect(renderer, &dotRect);
        }
    }

    void DrawGridDots() {
        constexpr float dotSize = 3.0f;

        SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);

        // Work out visible world area
        const float leftWorld = cameraPos.x - SCREEN_WIDTH * 0.5f;
        const float rightWorld = cameraPos.x + SCREEN_WIDTH * 0.5f;
        const float bottomWorld = cameraPos.y - SCREEN_HEIGHT * 0.5f;
        const float topWorld = cameraPos.y + SCREEN_HEIGHT * 0.5f;

        // Snap the first dot to the grid
        const float startX = std::floor(leftWorld / GRID_SIZE) * GRID_SIZE;
        const float startY = std::floor(bottomWorld / GRID_SIZE) * GRID_SIZE;

        for (float worldX = startX; worldX <= rightWorld; worldX += GRID_SIZE) {
            for (float worldY = startY; worldY <= topWorld; worldY += GRID_SIZE) {
                // Convert world position to screen position
                const float screenX = (worldX - cameraPos.x) + SCREEN_WIDTH * 0.5f;
                const float screenY = SCREEN_HEIGHT * 0.5f - (worldY - cameraPos.y);

                SDL_FRect dotRect = {
                    screenX - dotSize * 0.5f,
                    screenY - dotSize * 0.5f,
                    dotSize,
                    dotSize
                };

                SDL_RenderFillRect(renderer, &dotRect);
            }
        }
    }

    void FinishSectorSelection() {
        const std::vector<Vector2> finalVertices = GetSectorVerticesWithoutClosingDuplicate();

        if (finalVertices.size() < 3) {
            SDL_Log("Sector cancelled: fewer than 3 points");
            sectorBeingCreated.clear();
            return;
        }

        if (!IsSectorClosed(finalVertices)) {
            SDL_Log("Sector cancelled: shape is not closed");
            sectorBeingCreated.clear();
            return;
        }

        CreateSector(
            finalVertices,
            40.0f, // ceiling height
            0.0f, // floor height
            {255.0f, 255.0f, 255.0f}, // ceiling colour
            {255.0f, 255.0f, 255.0f}, // floor colour
            -1, // ceiling texture
            -1 // floor texture
        );

        SDL_Log("Sector created with %d vertices", static_cast<int>(finalVertices.size()));

        sectorBeingCreated.clear();
    }

    //endregion
    void Start() {
        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init Error: %s\n", SDL_GetError());
            return;
        }
        if (SDL_CreateWindowAndRenderer("Game of Life", SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window, &renderer) == false) {
            SDL_Log("Window/Renderer Error: %s\n", SDL_GetError());
            SDL_Quit();
            return;
        }

        if (!TTF_Init()) {
            SDL_Log("TTF_INIT failed: %s\n", SDL_GetError());
            SDL_Quit();
            return;
        }
        font = TTF_OpenFont("../Assets/Fonts/arial.ttf", FONT_SIZE);
        if (!font) {
            SDL_Log("TTF_OpenFont failed: %s\n", SDL_GetError());
            TTF_Quit();
            SDL_Quit();
            return;
        }

        textEngine = TTF_CreateRendererTextEngine(renderer);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);
    }

    // static TTF_Text* controls = TTF_CreateText(textEngine, font, "Left / Right arrow to step backwards / forwards. Space to auto step", 0);

    const char* GetModeName(const Mode mode) {
        switch (mode) {
            case MODE_DOT:
                return "Dot Mode";

            case MODE_WALL:
                return "Wall Mode";

            case MODE_SECTOR:
                return "Sector Mode";

            case MODE_OBJECT:
                return "Object Mode";

            default:
                return "Unknown Mode";
        }
    }

    void MoveMode() {
        currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    }

    bool SaveAndQuit() {
        if (!playerPlaced) {
            SDL_Log("Can not play without a player");
            return false;
        }
        json levelData;
        levelData["objects"] = json::array();
        levelData["walls"] = json::array();
        levelData["sectors"] = json::array();
        for (int i = 0; i < objects.size(); i++) {
            json jsonObj = {
                {"id", i},
                {"type", static_cast<int>(objects[i].type)},
                {"position", {objects[i].position.x, objects[i].position.y}},
            };
            levelData["objects"].push_back(jsonObj);
        }
        for (int i = 0; i < walls.size(); i++) {
            json jsonObj = {
                {"id", i},
                    {"start", {walls[i].start.x, walls[i].start.y}},
                        {"end", {walls[i].end.x, walls[i].end.y}},
            };
            levelData["walls"].push_back(jsonObj);
        }
        for (int i = 0; i < sectors.size(); i++) {
            json cornerArray = json::array();
            for (const auto& point : sectors[i].vertices) {
                cornerArray.push_back({
                    {"x", point.x},
                    {"y", point.y}
                });
            }

            json jsonObj = {
                {"id", i},
                {"corners", cornerArray}
            };

            levelData["sectors"].push_back(jsonObj);
        }

        std::ofstream file("../Assets/Levels/test_level.json");
        if (!file.is_open()) {
            SDL_Log("Failed to open test_level.json");
            return false;
        }
        file << levelData.dump(4);
        file.close();

        SDL_Log("Level created successfully");
        return true;
    }

    void Update() {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO& io = ImGui::GetIO();
        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        const bool keyboardBlockedByImgui = io.WantCaptureKeyboard;

        if (!mouseBlockedByImGui) {
            if (InputManager::GetMouseButton(SDL_BUTTON_MIDDLE)) {
                const Vector2 mouseDelta = InputManager::GetMouseDelta();

                cameraPos.x -= mouseDelta.x;
                cameraPos.y += mouseDelta.y;
            }
            else if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (currentMode == MODE_SECTOR) {
                    if (CornerExistsAt(snappedWorld)) {
                        AddSectorSelectionPoint(snappedWorld);
                        creatableSector = sectorBeingCreated.size() >= 3;
                    }
                    else {
                        for (int i = static_cast<int>(sectors.size()) - 1; i >= 0; --i) {
                            if (IsPointInsidePolygon(sectors[i].vertices, mouseWorld)) {
                                editingSector = !editingSector;
                                selectedSector = i;
                                break;
                            }
                        }
                    }
                }
                else if (currentMode == MODE_DOT) {
                    bool dotAlreadyExists = false;

                    for (int i = 0; i < static_cast<int>(placedCorners.size()); ++i) {
                        if (SamePoint(placedCorners[i], snappedWorld)) {
                            dotAlreadyExists = true;

                            if (IsCornerConnectedToLine(snappedWorld)) break;
                            placedCorners.erase(placedCorners.begin() + i);
                            break;
                        }
                    }

                    if (!dotAlreadyExists) {
                        placedCorners.push_back(snappedWorld);
                    }
                }
                else if (currentMode == MODE_OBJECT) {
                    if (currentObjectToPlace == PLAYER) {
                        if (playerPlaced) {
                            for (Object& object : objects) if (object.type == PLAYER) object.position = mouseWorld;
                        }
                        else {
                            Object player = {PLAYER, mouseWorld};
                            playerPlaced = true;
                            objects.push_back(player);
                        }
                    }
                }
            }
            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) && currentMode == MODE_WALL) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (CornerExistsAt(snappedWorld)) {
                    drawingLine = true;
                    lineStartWorld = snappedWorld;
                }
            }

            if (InputManager::GetMouseButton(SDL_BUTTON_LEFT) && drawingLine &&currentMode == MODE_WALL) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                const Vector2 startScreen = WorldToScreen(lineStartWorld, cameraPos);
                const Vector2 endScreen = WorldToScreen(snappedWorld, cameraPos);

                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                DrawThickLine(renderer, startScreen, endScreen, 5.0f);
            }

            if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT) && drawingLine && currentMode == MODE_WALL) {
                const Vector2 mouseScreen = InputManager::GetMousePosition();
                const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
                const Vector2 snappedWorld = SnapToGrid(mouseWorld);

                if (CornerExistsAt(snappedWorld) && !SamePoint(lineStartWorld, snappedWorld)) {
                    placedLines.push_back({
                        lineStartWorld,
                        snappedWorld
                    });
                    const Wall newWall = {
                        lineStartWorld,
                        snappedWorld,
                        {0, 0, 0, 255},
                    };
                    walls.push_back(newWall);
                }

                drawingLine = false;
            }
        }

        if (!keyboardBlockedByImgui) {
            if (InputManager::GetKeyDown(SDL_SCANCODE_Q)) MoveMode();
        }

        DrawGridDots();
        DrawExistingSectors();
        DrawCorners();
        DrawWalls();
        DrawObjects();
        if (currentMode == MODE_SECTOR) DrawSectorPreview();

        // TTF_DrawRendererText(controls, 15.0f, 50);

        ImGui::Begin("Editor");

        if (ImGui::Button("Mode")) {
            const Mode previousMode = currentMode;

            MoveMode();

            if (previousMode == MODE_SECTOR) {
                FinishSectorSelection();
                creatableSector = false;
            }

            if (currentMode == MODE_SECTOR) {
                sectorBeingCreated.clear();
                creatableSector = false;
            }
        }

        ImGui::Text("%s", GetModeName(currentMode));

        ImGui::Text("\nDots: %d", static_cast<int>(placedCorners.size()));
        ImGui::Text("Lines: %d", static_cast<int>(placedLines.size()));
        ImGui::Text("Sectors: %d", static_cast<int>(sectors.size()));

        if (editingSector && currentMode == MODE_SECTOR) {
            ImGui::Begin("test", &editingSector);

            float ceilHeight = sectors[selectedSector].ceilingHeight;
            float floorHeight = sectors[selectedSector].floorHeight;

            ImGui::InputFloat("Ceil Height ", &ceilHeight);
            ImGui::InputFloat("Floor Height ", &floorHeight);

            sectors[selectedSector].ceilingHeight = ceilHeight;
            sectors[selectedSector].floorHeight = floorHeight;

            if (ImGui::Button("Close")) editingSector = false;

            ImGui::End();
        }

        if (creatableSector) {
            if (ImGui::Button("Create Sector")) {
                if (sectorBeingCreated.size() >= 3) {
                    // If the sector is not already closed, close it by adding
                    // the first point to the end.
                    if (!SamePoint(sectorBeingCreated.front(), sectorBeingCreated.back())) {
                        sectorBeingCreated.push_back(sectorBeingCreated.front());
                    }

                    FinishSectorSelection();

                    creatableSector = false;
                }
            }
        }

        if (ImGui::Button("Save & Play")) {
            if (SaveAndQuit()){
                quit = true;
            }
        }

        ImGui::End();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    bool QuitRequested() {
        return quit;
    }

    void Destroy() {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool LoadLevel(const std::string& level) {
        walls.clear();
        sectors.clear();
        objects.clear();
        placedLines.clear();
        placedCorners.clear();

        playerPlaced = false;
        playerStartPos = {0.0f, 0.0f};
        std::ifstream file(level);

        if (!file.is_open()) {
            SDL_Log("Couldn't open level file");
            return false;
        }

        json levelData;
        try {
            file >> levelData;
        }
        catch (const std::exception& e) {
            SDL_Log("Failed to parse level JSON: %s", e.what());
            return false;
        }
        file.close();

        if (levelData.contains("objects")) {
            for (const json& objectJson : levelData["objects"]) {
                Object object;

                object.type = static_cast<ObjectType>(objectJson["type"].get<int>());
                object.position = {
                objectJson["position"][0].get<float>(),
                objectJson["position"][1].get<float>(),
                };

                objects.push_back(object);
                if (object.type == PLAYER) {
                    playerPlaced = true;
                    playerStartPos = object.position;
                }
            }
        }
        if (levelData.contains("walls")) {
            for (const json& wallJson : levelData["walls"]) {
                Vector2 start = {
                    wallJson["start"][0].get<float>(),
                    wallJson["start"][1].get<float>(),
                };
                Vector2 end = {
                    wallJson["end"][0].get<float>(),
                    wallJson["end"][1].get<float>(),
                };
                Wall wall {
                    start, end,
                    {0, 0, 0, 255},
                };

                walls.push_back(wall);
                placedLines.push_back({start,end});
            }
        }

        if (levelData.contains("sectors")) {
            for (const json& sectorJson : levelData["sectors"]) {
                std::vector<Vector2> corners;

                for (const json& cornerJson : sectorJson["corners"]) {
                    corners.push_back({
                        cornerJson["x"].get<float>(),
                        cornerJson["y"].get<float>()
                    });
                }

                Sector sector = {
                    corners,
                    {},
                    sectorJson.value("ceilingHeight", 40.0f),
                    sectorJson.value("floorHeight", 0.0f),
                    {255.0f, 255.0f, 255.0f},
                    {255.0f, 255.0f, 255.0f},
                    -1,
                    -1
                };

                sector.triangles = Triangulate(sector.vertices);

                sectors.push_back(sector);
            }
        }

        SDL_Log("Level loaded successfully");
        return true;
    }
}
