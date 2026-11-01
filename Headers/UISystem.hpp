//
// Created by berke on 5/22/2026.
//

#ifndef TILKY_ENGINE_UISYSTEM_HPP
#define TILKY_ENGINE_UISYSTEM_HPP

#include "Headers/Math/Vector/Vector2.hpp"
#include "Objects/Components.hpp"
#include "Objects/Level.hpp"

namespace UISystem {
    constexpr float DEFAULT_UI_SIZE = 5.0f;

    void UpdateTransform(ComponentUITransform& transform, float screenWidth, float screenHeight);
    void UpdateAllTransforms(Level& level, float screenWidth, float screenHeight);

    // Calls sprite(ui sprite) and text(ui text) in the order the UI draws:
    // layer by layer (UI_LEVEL_DRAW_LAYER: the UI layouts, then the level's
    // own UI), each layer's Sprites before its Texts, in storage order.
    // UILayouts::SortUIDrawOrder keeps the storages sorted by layer;
    // components added since go at the end, in the level's own top layer.
    template<typename LevelType, typename SpriteFn, typename TextFn>
    void ForEachInDrawOrder(LevelType& level, SpriteFn&& sprite, TextFn&& text) {
        auto& sprites = level.ui_sprites.components;
        auto& texts = level.ui_texts.components;

        size_t i = 0;
        size_t j = 0;
        while (i < sprites.size() || j < texts.size()) {
            const std::uint32_t layer = std::min(i < sprites.size() ? sprites[i].drawLayer : UI_LEVEL_DRAW_LAYER,
                                                 j < texts.size() ? texts[j].drawLayer : UI_LEVEL_DRAW_LAYER);

            while (i < sprites.size() && sprites[i].drawLayer == layer) sprite(sprites[i++]);
            while (j < texts.size() && texts[j].drawLayer == layer) text(texts[j++]);
        }
    }
}

#endif //TILKY_ENGINE_UISYSTEM_HPP