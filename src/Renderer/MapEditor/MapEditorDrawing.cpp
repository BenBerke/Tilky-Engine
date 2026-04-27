#include "MapEditorInternal.hpp"

#include "Headers/Engine/InputManager.hpp"

#include <cmath>

namespace MapEditorInternal {
    void DrawThickLine(SDL_Renderer* renderer, const Vector2 start, const Vector2 end, const float thickness) {
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

    void DrawFilledTriangle(const Triangle& triangle, const SDL_FColor color) {
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

        const std::vector<Triangle> previewTriangles = MapEditor::Triangulate(previewVertices);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const SDL_FColor redPreviewColor = {
            1.0f,
            0.0f,
            0.0f,
            0.30f
        };

        for (const Triangle& triangle : previewTriangles) {
            DrawFilledTriangle(triangle, redPreviewColor);
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawExistingSectors() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        int hoveredSectorIndex = -1;

        for (int i = static_cast<int>(MapEditor::sectors.size()) - 1; i >= 0; --i) {
            if (IsPointInsidePolygon(MapEditor::sectors[i].vertices, mouseWorld)) {
                hoveredSectorIndex = i;
                break;
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const SDL_FColor hoveredSectorColor = {
            1.0f,
            0.75f,
            0.0f,
            0.45f
        };

        const int totalSectors = static_cast<int>(MapEditor::sectors.size());

        for (int i = 0; i < totalSectors; ++i) {
            const float offset = static_cast<float>(i) / static_cast<float>(std::max(totalSectors, 1));

            const SDL_FColor normalSectorColor = {
                std::fmod(hoveredSectorColor.r + offset, 1.0f),
                std::fmod(hoveredSectorColor.g + offset, 1.0f),
                std::fmod(hoveredSectorColor.b + offset, 1.0f),
                hoveredSectorColor.a
            };

            const SDL_FColor sectorColor =
                i == hoveredSectorIndex && currentMode == MODE_SECTOR
                    ? hoveredSectorColor
                    : normalSectorColor;

            for (const Triangle& triangle : MapEditor::sectors[i].triangles) {
                DrawFilledTriangle(triangle, sectorColor);
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    void DrawCorners() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

        for (const Vector2& cornerWorld : placedCorners) {
            const Vector2 cornerScreen = WorldToScreen(cornerWorld, cameraPos);

            SDL_FRect dotRect = {
                cornerScreen.x - 3.0f,
                cornerScreen.y - 3.0f,
                6.0f,
                6.0f
            };

            SDL_RenderFillRect(renderer, &dotRect);
        }
    }

    void DrawWalls() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

        for (const EditorLine& line : placedLines) {
            const Vector2 startScreen = WorldToScreen(line.start, cameraPos);
            const Vector2 endScreen = WorldToScreen(line.end, cameraPos);

            DrawThickLine(renderer, startScreen, endScreen, 5.0f);
        }
    }

    void DrawObjects() {
        for (const Object& object : objects) {
            const Vector2 objectScreen = WorldToScreen(object.position, cameraPos);

            Vector3 color = {0, 0, 0};

            switch (object.type) {
                case PLAYER:
                    color = {255, 0, 0};
                    break;

                default:
                    color = {0, 0, 0};
                    break;
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

        const float leftWorld = cameraPos.x - SCREEN_WIDTH * 0.5f;
        const float rightWorld = cameraPos.x + SCREEN_WIDTH * 0.5f;
        const float bottomWorld = cameraPos.y - SCREEN_HEIGHT * 0.5f;
        const float topWorld = cameraPos.y + SCREEN_HEIGHT * 0.5f;

        const float startX = std::floor(leftWorld / GRID_SIZE) * GRID_SIZE;
        const float startY = std::floor(bottomWorld / GRID_SIZE) * GRID_SIZE;

        for (float worldX = startX; worldX <= rightWorld; worldX += GRID_SIZE) {
            for (float worldY = startY; worldY <= topWorld; worldY += GRID_SIZE) {
                const float screenX = worldX - cameraPos.x + SCREEN_WIDTH * 0.5f;
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
}