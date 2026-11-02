//
// Created by berke on 5/22/2026.
//

#ifndef TILKY_ENGINE_UISYSTEM_HPP
#define TILKY_ENGINE_UISYSTEM_HPP

#include <algorithm>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Objects/Components.hpp"
#include "Objects/Level.hpp"

namespace UISystem {
    constexpr float DEFAULT_UI_SIZE = 5.0f;

    void UpdateTransform(ComponentUITransform& transform, float screenWidth, float screenHeight);
    void UpdateAllTransforms(Level& level, float screenWidth, float screenHeight);

    // Calls sprite(ui sprite) bottom first: lowest Draw Order first, across
    // all UI. With equal Draw Orders it's storage order, which is layer order
    // (UI_LEVEL_DRAW_LAYER: the UI layouts, then the level's own UI):
    // UILayouts::SortUIDrawOrder keeps the storages sorted by layer, and
    // components added since go at the end, in the level's own top layer.
    template<typename LevelType, typename SpriteFn>
    void ForEachSpriteInDrawOrder(LevelType& level, SpriteFn&& sprite) {
        auto& sprites = level.ui_sprites.components;

        const auto lower = [](const auto& a, const auto& b) { return a.drawOrder < b.drawOrder; };

        // Almost every frame of almost every game: nothing to reorder.
        if (std::is_sorted(sprites.begin(), sprites.end(), lower)) {
            for (auto& each : sprites) sprite(each);
            return;
        }

        std::vector<decltype(&sprites[0])> ordered;
        ordered.reserve(sprites.size());
        for (auto& each : sprites) ordered.push_back(&each);

        std::stable_sort(ordered.begin(), ordered.end(), [&lower](const auto* a, const auto* b) { return lower(*a, *b); });

        for (const auto each : ordered) sprite(*each);
    }

    // Calls sprite(ui sprite) and text(ui text) in the order the UI draws:
    // every Sprite, then every Text, so text is always on top of any image.
    // Texts draw in storage order, which is layer order as above.
    template<typename LevelType, typename SpriteFn, typename TextFn>
    void ForEachInDrawOrder(LevelType& level, SpriteFn&& sprite, TextFn&& text) {
        ForEachSpriteInDrawOrder(level, sprite);
        for (auto& each : level.ui_texts.components) text(each);
    }
}

#endif //TILKY_ENGINE_UISYSTEM_HPP