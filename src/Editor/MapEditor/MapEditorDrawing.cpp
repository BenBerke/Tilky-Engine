#include "../EditorInternal.hpp"

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Editor/EditorTextureCache.hpp"

#include <cmath>
#include <spdlog/spdlog.h>

#include "Headers/Math/Geometry/Geometry.hpp"

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

        // walls should the be the opposite of the theme
        if (currentTheme == THEME_DARK)
        SDL_SetRenderDrawColor(renderer, 205, 205, 205, 255);
        else if (currentTheme == THEME_LIGHT)
            SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);

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

    // Feature #9: textured variant used by Texture View Mode. UVs are a
    // simple fixed-scale planar projection of world position - good enough
    // for an editor preview without needing per-sector UV authoring.
    void DrawFilledTriangleTextured(const Triangle& triangle, SDL_Texture* texture, const SDL_FColor tint) {
        if (texture == nullptr) {
            return;
        }

        constexpr float uvScale = 1.0f / 64.0f;

        const Vector2 a = WorldToScreen(triangle.a, cameraPos);
        const Vector2 b = WorldToScreen(triangle.b, cameraPos);
        const Vector2 c = WorldToScreen(triangle.c, cameraPos);

        SDL_Vertex vertices[3];

        vertices[0].position = {a.x, a.y};
        vertices[0].color = tint;
        vertices[0].tex_coord = {triangle.a.x * uvScale, triangle.a.y * uvScale};

        vertices[1].position = {b.x, b.y};
        vertices[1].color = tint;
        vertices[1].tex_coord = {triangle.b.x * uvScale, triangle.b.y * uvScale};

        vertices[2].position = {c.x, c.y};
        vertices[2].color = tint;
        vertices[2].tex_coord = {triangle.c.x * uvScale, triangle.c.y * uvScale};

        SDL_RenderGeometry(renderer, texture, vertices, 3, nullptr, 0);
    }

    // Feature #8/#9: single chokepoint for "give me the SDL_Texture* for
    // this level-local texture index, or nullptr if unavailable". Never
    // crashes on -1/out-of-range/missing textures.
    //
    // ASSUMPTION (flagged in NOTES.md): EditorTextureCache exposes an
    // accessor returning the already-loaded SDL_Texture* for an index -
    // EditorTextureCache::Load(renderer, level) is already called in
    // Editor::Start(), so the cache almost certainly owns live SDL_Texture*
    // handles already; this just assumes the accessor's name. If the real
    // one differs, this is the only function that needs to change.
    SDL_Texture* GetEditorTexture(const int textureIndex) {
        if (textureIndex < 0) {
            return nullptr;
        }

        return EditorTextureCache::Get(textureIndex);
    }

    void DrawSnapIndicator() {
        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
        const Vector2 snapped = ResolveSnapPoint(mouseWorld);
        const Vector2 screenPos = WorldToScreen(snapped, cameraPos);

        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);

        constexpr float radius = 5.0f;

        const SDL_FRect ring = {
            screenPos.x - radius,
            screenPos.y - radius,
            radius * 2.0f,
            radius * 2.0f
        };

        SDL_RenderRect(renderer, &ring);
    }

    // Feature #5: draws already-placed chain edges, a rubber-band preview
    // edge to the resolved snap point, a translucent fill once there are
    // enough points, and the snap indicator itself.
    void DrawSectorPreview() {
        if (sectorBeingCreated.empty()) {
            DrawSnapIndicator();
            return;
        }

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);
        const Vector2 snapped = ResolveSnapPoint(mouseWorld);

        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);

        for (std::size_t i = 0; i + 1 < sectorBeingCreated.size(); ++i) {
            const Vector2 a = WorldToScreen(sectorBeingCreated[i], cameraPos);
            const Vector2 b = WorldToScreen(sectorBeingCreated[i + 1], cameraPos);
            DrawThickLine(renderer, a, b, 3.0f);
        }

        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 160);

        const Vector2 lastScreen = WorldToScreen(sectorBeingCreated.back(), cameraPos);
        const Vector2 previewScreen = WorldToScreen(snapped, cameraPos);
        DrawThickLine(renderer, lastScreen, previewScreen, 2.0f);

        const std::vector<Vector2> previewVertices = GetSectorVerticesWithoutClosingDuplicate();

        if (previewVertices.size() >= 3) {
            const std::vector<Triangle> previewTriangles = Geometry::Triangulate(previewVertices);

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

            const SDL_FColor redPreviewColor = {1.0f, 0.0f, 0.0f, 0.30f};

            for (const Triangle& triangle : previewTriangles) {
                DrawFilledTriangle(triangle, redPreviewColor);
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        DrawSnapIndicator();
    }

    void DrawExistingSectors() {
        const Level& level = LevelManager::CurrentLevel();

        const Vector2 mouseScreen = InputManager::GetMousePosition();
        const Vector2 mouseWorld = ScreenToWorld(mouseScreen, cameraPos);

        int hoveredSectorIndex = -1;

        for (int i = static_cast<int>(level.sectors.size()) - 1; i >= 0; --i) {
            if (IsPointInsidePolygon(level.sectors[i].vertices, mouseWorld)) {
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

        const int totalSectors = static_cast<int>(level.sectors.size());

        auto HSVtoRGB = [](const float h, const float s, const float v) -> SDL_FColor {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            const int i = floor(h * 6);
            const float f = h * 6 - i;
            const float p = v * (1 - s);
            const float q = v * (1 - f * s);
            const float t = v * (1 - (1 - f) * s);
            switch (i % 6) {
                case 0: r = v, g = t, b = p; break;
                case 1: r = q, g = v, b = p; break;
                case 2: r = p, g = v, b = t; break;
                case 3: r = p, g = q, b = v; break;
                case 4: r = t, g = p, b = v; break;
                case 5: r = v, g = p, b = q; break;
                default: break;
            }
            return { r, g, b, .55f};
        };

        for (int i = 0; i < totalSectors; ++i) {
            const Sector& sector = level.sectors[i];

            const float hue = std::fmod(static_cast<float>(i) * 0.618033988749895f, 1.0f);
            const SDL_FColor normalSectorColor = HSVtoRGB(hue, 0.7f, 0.9f);

            const SDL_FColor sectorColor =
                i == hoveredSectorIndex && currentMode == MODE_SECTOR
                    ? hoveredSectorColor
                    : normalSectorColor;

            // Feature #9: Texture View Mode. Falls back safely to the normal
            // editor color/white-box fill whenever the floor texture is -1
            // or unavailable, so a missing texture can never break the view.
            SDL_Texture* floorTexture = nullptr;

            if (textureViewMode && sector.floorTextureIndex != -1) {
                floorTexture = GetEditorTexture(sector.floorTextureIndex);
            }

            for (const Triangle& triangle : sector.triangles) {
                if (floorTexture != nullptr) {
                    DrawFilledTriangleTextured(triangle, floorTexture, {1.0f, 1.0f, 1.0f, 1.0f});
                }
                else {
                    DrawFilledTriangle(triangle, sectorColor);
                }
            }
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        // Feature #10/#11: outline whichever sector is currently selected
        // (e.g. via the Hierarchy panel), independent of mouse hover.
        if (selectedSectorID != INVALID_ID) {
            const Sector* selected = MapQueries::GetSectorByID(level, selectedSectorID);

            if (selected != nullptr && !selected->vertices.empty()) {
                SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);

                const int n = static_cast<int>(selected->vertices.size());

                for (int i = 0; i < n; ++i) {
                    const Vector2 a = WorldToScreen(selected->vertices[i], cameraPos);
                    const Vector2 b = WorldToScreen(selected->vertices[(i + 1) % n], cameraPos);
                    DrawThickLine(renderer, a, b, 3.0f);
                }
            }
        }
    }

    // Renamed from DrawCorners (feature #6): dots replace the old grid-only
    // "placedCorners" concept and are now ID-stable, off-grid-capable points.
    void DrawDots() {
        if (currentTheme == THEME_DARK)
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        else if (currentTheme == THEME_LIGHT)
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

        for (const Dot& dot : dots) {
            const Vector2 screenPos = WorldToScreen(dot.position, cameraPos);

            SDL_FRect dotRect = {
                screenPos.x - 3.0f,
                screenPos.y - 3.0f,
                6.0f,
                6.0f
            };

            SDL_RenderFillRect(renderer, &dotRect);
        }

        if (selectedDotID != INVALID_ID) {
            const auto it = dotIDToIndex.find(selectedDotID);

            if (it != dotIDToIndex.end() && it->second >= 0 && it->second < static_cast<int>(dots.size())) {
                const Vector2 screenPos = WorldToScreen(dots[it->second].position, cameraPos);

                SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);

                const SDL_FRect highlightRect = {
                    screenPos.x - 6.0f,
                    screenPos.y - 6.0f,
                    12.0f,
                    12.0f
                };

                SDL_RenderRect(renderer, &highlightRect);
            }
        }
    }

    void DrawWalls() {
        const Level& level = LevelManager::CurrentLevel();

        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);

        for (const Wall& wall : level.walls) {
            const Vector2 startScreen = WorldToScreen(wall.start, cameraPos);
            const Vector2 endScreen = WorldToScreen(wall.end, cameraPos);

            DrawThickLine(renderer, startScreen, endScreen, 5.0f);
        }
    }

    void DrawEntities() {
        Level& level = LevelManager::CurrentLevel();

        for (const Entity& entity : level.entities) {
            const ComponentTransform* transform = level.transforms.Get(entity.id);

            if (transform == nullptr) continue;

            const Vector2 screenPos = WorldToScreen({transform->position.x, transform->position.y}, cameraPos);

            const float screenEntitySize = entitySize * editorZoom;

            SDL_FRect rect = {
                screenPos.x - screenEntitySize * 0.5f,
                screenPos.y - screenEntitySize * 0.5f,
                screenEntitySize,
                screenEntitySize
            };

            // Feature #9: Texture View Mode for entities with a sprite.
            // Falls back to the existing color-block rendering whenever the
            // sprite has no texture assigned or the texture isn't available.
            const ComponentSprite* sprite = level.sprites.Get(entity.id);
            SDL_Texture* spriteTexture = nullptr;

            if (textureViewMode && sprite != nullptr && sprite->textureIndex != -1) {
                spriteTexture = GetEditorTexture(sprite->textureIndex);
            }

            if (spriteTexture != nullptr) {
                SDL_RenderTexture(renderer, spriteTexture, nullptr, &rect);
            }
            else {
                if (sprite != nullptr) SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
                else if (level.decals.Get(entity.id) != nullptr) SDL_SetRenderDrawColor(renderer, 255, 120, 120, 255);
                else SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

                SDL_RenderFillRect(renderer, &rect);
            }

            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderRect(renderer, &rect);
        }
    }

    void DrawGridDots() {
        constexpr float dotSize = 3.0f;

        if (currentTheme == THEME_DARK)
            SDL_SetRenderDrawColor(renderer, 225, 225, 225, 255);
        else if (currentTheme == THEME_LIGHT)
            SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);

        const float activeGridSize = GetActiveGridSize();

        const float visibleHalfWidthWorld = (screenWidth * 0.5f) / editorZoom;
        const float visibleHalfHeightWorld = (screenHeight * 0.5f) / editorZoom;

        const float leftWorld = cameraPos.x - visibleHalfWidthWorld;
        const float rightWorld = cameraPos.x + visibleHalfWidthWorld;
        const float bottomWorld = cameraPos.y - visibleHalfHeightWorld;
        const float topWorld = cameraPos.y + visibleHalfHeightWorld;

        const float startX = std::floor(leftWorld / activeGridSize) * activeGridSize;
        const float startY = std::floor(bottomWorld / activeGridSize) * activeGridSize;

        for (float worldX = startX; worldX <= rightWorld; worldX += activeGridSize) {
            for (float worldY = startY; worldY <= topWorld; worldY += activeGridSize) {
                const Vector2 screenPos = WorldToScreen({worldX, worldY}, cameraPos);

                SDL_FRect dotRect = {
                    screenPos.x - dotSize * 0.5f,
                    screenPos.y - dotSize * 0.5f,
                    dotSize,
                    dotSize
                };

                SDL_RenderFillRect(renderer, &dotRect);
            }
        }
    }
}