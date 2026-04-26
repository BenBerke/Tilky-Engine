//
// Created by berke on 4/13/2026.
//

#include "../../Headers/Renderer/MapEditor.h"
#include "../../Headers/Math/Vector/Vector2Math.h"

#include <algorithm>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

#define SCREEN_WIDTH 1680
#define SCREEN_HEIGHT 960

#define FONT_SIZE 24

namespace MapEditor {

//region Triangulation
    static constexpr float EPSILON = 0.00001f;

    // Cross product of the triangle/turn A -> B -> C
    float crossAtPoint(const Vector2 a, const Vector2 b, const Vector2 c) {
        return Vector2Math::Cross(b - a, c - a);
    }

    float polygonArea(const std::vector<Vector2>& vertices) {
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

    bool isEar(const std::vector<Vector2>& vertices, const int prev, const int curr, const int next) {
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

    std::vector<Triangle> triangulate(std::vector<Vector2> vertices) {
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

    void TriangulateSectors() {
        for (Sector& sector : sectors) {
            sector.triangles.clear();

            if (sector.vertices.size() < 3) continue;
            sector.triangles = triangulate(sector.vertices);
        }
    }

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
    }
   // static TTF_Text* controls = TTF_CreateText(textEngine, font, "Left / Right arrow to step backwards / forwards. Space to auto step", 0);

    void Update() {
        SDL_SetRenderDrawColor(renderer,255, 255, 255, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer,9, 1, 9, 255);
       // TTF_DrawRendererText(controls, 15.0f, 50);

        SDL_RenderPresent(renderer);
    }

    void Destroy() {
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
}
