// The map editor canvas. It is drawn by the renderer (OpenGL, in the window
// the game and the runtime editor use) from the Editor2D::Frame that
// MapEditorFrame.cpp builds; what's here is the part drawn with ImGui over
// it - the height views' legend and the comment icons.

#include "../EditorInternal.hpp"

#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Editor/Comments.hpp"
#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/Runtime/Renderer/IRenderer.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>

namespace MapEditorInternal {
    void DrawMapCanvas() {
        if (canvasRenderer == nullptr) return;

        // Kept between frames so its buffers are reused.
        static Editor2D::Frame frame;
        BuildMapEditorFrame(frame);
        canvasRenderer->DrawEditor2D(frame);
    }

    // ---- View modes ----------------------------------------------------------
    // Named, not anonymous: unity builds put several of these files together.
    namespace ViewModeLegend {
        constexpr float VIEW_MODE_FILL_ALPHA = 0.75f;

        // The lowest and highest value the current height view shows, over
        // every sector, so the colours spread across what the level uses.
        struct ViewRange {
            float low = 0.0f;
            float high = 0.0f;
        };

        // How tall the level's player is: the collider of its first Player
        // Controller entity. 0 when there is none.
        float PlayerHeight(const Level& level) {
            for (const ComponentPlayerController& controller : level.playerControllers.components) {
                const ComponentTransform* transform = level.transforms.Get(controller.ownerID);
                const ComponentCollider* collider = level.colliders.Get(controller.ownerID);

                if (transform != nullptr && collider != nullptr) return collider->Height(*transform);
            }

            return 0.0f;
        }

        // The value a height view shows for `sector`, from its lowest floor.
        std::optional<float> ViewModeValue(const Sector& sector) {
            if (sector.floors.empty()) return std::nullopt;

            const SectorFloor& floor = sector.floors.front();

            switch (textureViewMode) {
                case TextureViewMode::FloorHeight: return floor.floor.height;
                case TextureViewMode::CeilingHeight: return floor.ceiling.height;
                case TextureViewMode::RoomHeight: return floor.ceiling.height - floor.floor.height;
                default: return std::nullopt;
            }
        }

        ViewRange ComputeViewRange(const Level& level) {
            ViewRange range;
            bool any = false;

            for (const Sector& sector : level.sectors) {
                const std::optional<float> value = ViewModeValue(sector);
                if (!value.has_value()) continue;

                range.low = any ? std::min(range.low, *value) : *value;
                range.high = any ? std::max(range.high, *value) : *value;
                any = true;
            }

            return range;
        }

        // Blue at 0 through cyan, green and yellow to red at 1.
        SDL_FColor HeatColor(const float t, const float alpha) {
            constexpr SDL_FColor STOPS[] = {
                {0.15f, 0.30f, 1.00f, 1.0f},
                {0.10f, 0.85f, 0.95f, 1.0f},
                {0.20f, 0.85f, 0.25f, 1.0f},
                {0.95f, 0.90f, 0.15f, 1.0f},
                {1.00f, 0.35f, 0.10f, 1.0f},
            };
            constexpr int LAST = static_cast<int>(std::size(STOPS)) - 1;

            const float scaled = std::clamp(t, 0.0f, 1.0f) * LAST;
            const int index = std::min(static_cast<int>(scaled), LAST - 1);
            const float fraction = scaled - static_cast<float>(index);

            const SDL_FColor& a = STOPS[index];
            const SDL_FColor& b = STOPS[index + 1];

            return {a.r + (b.r - a.r) * fraction, a.g + (b.g - a.g) * fraction, a.b + (b.b - a.b) * fraction, alpha};
        }

        constexpr SDL_FColor TOO_LOW_COLOR = {0.85f, 0.05f, 0.05f, VIEW_MODE_FILL_ALPHA};

    }

    void DrawViewModeLegend(ImDrawList* drawList) {
        using namespace ViewModeLegend;

        if (textureViewMode != TextureViewMode::FloorHeight &&
            textureViewMode != TextureViewMode::CeilingHeight &&
            textureViewMode != TextureViewMode::RoomHeight) return;

        const Level& level = LevelManager::CurrentLevel();
        const ViewRange range = ComputeViewRange(level);

        constexpr float BAR_WIDTH = 180.0f;
        constexpr float BAR_HEIGHT = 12.0f;
        constexpr float MARGIN = 16.0f;
        constexpr int SEGMENTS = 32;

        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 barMin = {mapPanelOrigin.x + MARGIN,
                               mapPanelOrigin.y + static_cast<float>(screenHeight) - MARGIN - BAR_HEIGHT - lineHeight - 4.0f};

        const bool roomHeight = textureViewMode == TextureViewMode::RoomHeight;

        for (int i = 0; i < SEGMENTS; ++i) {
            const float t = static_cast<float>(i) / (SEGMENTS - 1);
            const SDL_FColor color = HeatColor(roomHeight ? 0.5f - t * 0.5f : t, 1.0f);

            drawList->AddRectFilled(
                {barMin.x + BAR_WIDTH * i / SEGMENTS, barMin.y},
                {barMin.x + BAR_WIDTH * (i + 1) / SEGMENTS, barMin.y + BAR_HEIGHT},
                ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, 1.0f}));
        }

        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        char text[64];

        std::snprintf(text, sizeof(text), "%g", range.low);
        drawList->AddText({barMin.x, barMin.y + BAR_HEIGHT + 2.0f}, textColor, text);

        std::snprintf(text, sizeof(text), "%g", range.high);
        drawList->AddText({barMin.x + BAR_WIDTH - ImGui::CalcTextSize(text).x, barMin.y + BAR_HEIGHT + 2.0f}, textColor, text);

        if (roomHeight) {
            const float playerHeight = PlayerHeight(level);

            if (playerHeight > 0.0f) {
                const ImVec2 swatchMin = {barMin.x + BAR_WIDTH + 12.0f, barMin.y};
                drawList->AddRectFilled(swatchMin, {swatchMin.x + BAR_HEIGHT, swatchMin.y + BAR_HEIGHT},
                    ImGui::ColorConvertFloat4ToU32({TOO_LOW_COLOR.r, TOO_LOW_COLOR.g, TOO_LOW_COLOR.b, 1.0f}));

                std::snprintf(text, sizeof(text), Localisation::Get("editor.view_mode_legend.too_low").c_str(), playerHeight);
                drawList->AddText({swatchMin.x + BAR_HEIGHT + 6.0f, swatchMin.y - 1.0f}, textColor, text);
            }
        }
    }

    void DrawCommentIcons(ImDrawList* drawList) {
        // In window coordinates, as Comments compares them with the mouse.
        Comments::DrawIcons(LevelManager::CurrentLevel(), [](const Vector3& world, Vector2& screen) {
            screen = WorldToScreen({world.x, world.z}, cameraPos);
            screen.x += mapPanelOrigin.x;
            screen.y += mapPanelOrigin.y;
            return true;
        }, mapMouseFree, drawList);
    }
}
