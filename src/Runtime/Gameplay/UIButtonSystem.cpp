//
// Created by berke on 10/31/2026.
//

#include "Headers/Runtime/Gameplay/UIButtonSystem.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/UISystem.hpp"

namespace {
    using UIButtonEvent = EventSystem::UIButtonEvent;

    // Whether the last Update had a free cursor, so losing it clears the
    // buttons once instead of every frame.
    bool wasActive = false;

    // The entity's first UI Button is the one that works.
    bool IsFirstOnOwner(const Level& level, const ComponentUIButton& button) {
        return level.ui_buttons.Get(button.ownerID) == &button;
    }

    // The mouse in the window's pixels (what UI Transforms are resolved in)
    // and the window's pixel size. nullopt when the mouse isn't over a window.
    struct MouseInPixels {
        Vector2 position;
        Vector2 screenSize;
    };

    std::optional<MouseInPixels> GetMouseInPixels() {
        SDL_Window* window = SDL_GetMouseFocus();
        if (window == nullptr) return std::nullopt;

        int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
        if (!SDL_GetWindowSize(window, &width, &height) || !SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight))
            return std::nullopt;
        if (width <= 0 || height <= 0) return std::nullopt;

        const Vector2 mouse = InputManager::GetMousePosition();
        return MouseInPixels{
            {mouse.x * static_cast<float>(pixelWidth) / static_cast<float>(width),
             mouse.y * static_cast<float>(pixelHeight) / static_cast<float>(height)},
            {static_cast<float>(pixelWidth), static_cast<float>(pixelHeight)}
        };
    }

    // Inside the rectangle the renderer draws: turned by `rotation` degrees
    // around its centre.
    bool PointInside(const Vector2& point, const ComponentUITransform& transform) {
        const Vector2 size = transform.resolvedSize;
        const Vector2 center = {
            transform.resolvedPosition.x + size.x * 0.5f,
            transform.resolvedPosition.y + size.y * 0.5f
        };

        const float radians = -transform.rotation * Constants::DegToRad;
        const float cosA = std::cos(radians);
        const float sinA = std::sin(radians);
        const Vector2 offset = {point.x - center.x, point.y - center.y};
        const Vector2 local = {offset.x * cosA - offset.y * sinA, offset.x * sinA + offset.y * cosA};

        const float halfX = std::fabs(size.x) * 0.5f;
        const float halfY = std::fabs(size.y) * 0.5f;
        return local.x >= -halfX && local.x <= halfX && local.y >= -halfY && local.y <= halfY;
    }

    // How high each UI entity is drawn: Texts over Sprites, and later
    // components over earlier ones (OpenGL::Update's UI order). 0 = draws nothing.
    std::unordered_map<ID, size_t> DrawRanks(const Level& level) {
        std::unordered_map<ID, size_t> ranks;

        const size_t spriteCount = level.ui_sprites.components.size();
        for (size_t i = 0; i < spriteCount; ++i) {
            size_t& rank = ranks[level.ui_sprites.components[i].ownerID];
            rank = std::max(rank, i + 1);
        }
        for (size_t i = 0; i < level.ui_texts.components.size(); ++i) {
            size_t& rank = ranks[level.ui_texts.components[i].ownerID];
            rank = std::max(rank, spriteCount + i + 1);
        }
        return ranks;
    }

    // The topmost enabled button under the mouse, interactable or not.
    std::optional<ComponentInstanceID> TopmostButtonAt(Level& level, const MouseInPixels& mouse) {
        const std::unordered_map<ID, size_t> ranks = DrawRanks(level);

        std::optional<ComponentInstanceID> top;
        size_t topRank = 0;

        for (const ComponentUIButton& button : level.ui_buttons.components) {
            if (!button.ownerEnabled || !IsFirstOnOwner(level, button)) continue;

            ComponentUITransform* transform = level.ui_transforms.Get(button.ownerID);
            if (transform == nullptr) continue;

            // Resolved here too: the renderer only does it after this frame's update.
            UISystem::UpdateTransform(*transform, mouse.screenSize.x, mouse.screenSize.y);
            if (!PointInside(mouse.position, *transform)) continue;

            const auto rank = ranks.find(button.ownerID);
            const size_t buttonRank = rank != ranks.end() ? rank->second : 0;

            // A tie goes to the later button, like a later sprite drawing on top.
            if (!top || buttonRank >= topRank) {
                top = button.instanceID;
                topRank = buttonRank;
            }
        }

        return top;
    }

    // Every working button (the first on its entity), by instance: callbacks
    // can add or remove components, so they are looked up again each time.
    std::vector<ComponentInstanceID> WorkingButtons(const Level& level) {
        std::vector<ComponentInstanceID> buttons;
        for (const ComponentUIButton& button : level.ui_buttons.components)
            if (IsFirstOnOwner(level, button)) buttons.push_back(button.instanceID);
        return buttons;
    }

    void ApplyAllLooks(Level& level) {
        for (const ComponentUIButton& button : level.ui_buttons.components) UIButtonSystem::ApplyLook(level, button);
    }

    // Drops every hover and press without events (cursor locked or hidden).
    void ClearAll(Level& level) {
        for (ComponentUIButton& button : level.ui_buttons.components) {
            button.hovered = false;
            button.pressed = false;
        }
        ApplyAllLooks(level);
    }
}

namespace UIButtonSystem {
    const Vector4& StateColor(const ComponentUIButton& button) {
        if (!button.interactable) return button.disabledColor;
        if (button.pressed && button.hovered) return button.pressedColor;
        if (button.hovered) return button.hoverColor;
        return button.normalColor;
    }

    const std::string& StateTexture(const ComponentUIButton& button) {
        if (!button.interactable) return button.disabledTexture;
        if (button.pressed && button.hovered) return button.pressedTexture;
        if (button.hovered) return button.hoverTexture;
        return button.normalTexture;
    }

    void ApplyLook(Level& level, const ComponentUIButton& button) {
        if (!IsFirstOnOwner(level, button)) return;

        ComponentUISprite* sprite = level.ui_sprites.Get(button.ownerID);
        if (sprite == nullptr) return;

        sprite->buttonTint = StateColor(button);
        sprite->buttonTexture = StateTexture(button);
    }

    void Start(Level& level) {
        wasActive = false;
        ClearAll(level);
    }

    void Update(Level& level, const CallbackDispatcher& dispatch) {
        if (level.ui_buttons.components.empty()) return;

        const bool cursorFree = InputManager::GetCursorLockMode() != InputManager::CursorLockMode::Locked &&
                                InputManager::IsCursorVisible();

        // Buttons only work with a cursor to point with.
        if (!cursorFree) {
            if (wasActive) ClearAll(level);
            wasActive = false;
            return;
        }
        wasActive = true;

        const std::optional<MouseInPixels> mouse = GetMouseInPixels();
        const std::optional<ComponentInstanceID> top = mouse ? TopmostButtonAt(level, *mouse) : std::nullopt;

        // A button that isn't interactable still blocks the ones under it.
        std::optional<ComponentInstanceID> hoveredID;
        if (top) {
            const ComponentUIButton* topButton = level.ui_buttons.GetInstance(*top);
            if (topButton != nullptr && topButton->interactable) hoveredID = top;
        }

        const std::vector<ComponentInstanceID> buttons = WorkingButtons(level);

        // Set every flag first, then fire: the callbacks see the new state.
        std::vector<std::pair<ComponentInstanceID, UIButtonEvent>> toFire;

        for (const ComponentInstanceID instanceID : buttons) {
            ComponentUIButton* button = level.ui_buttons.GetInstance(instanceID);
            if (button == nullptr) continue;

            // Disabled or not interactable: drop hover and press without events.
            if (!button->ownerEnabled || !button->interactable) {
                button->hovered = false;
                button->pressed = false;
                continue;
            }

            const bool hovered = hoveredID == instanceID;
            if (hovered == button->hovered) continue;

            button->hovered = hovered;
            toFire.emplace_back(instanceID, hovered ? UIButtonEvent::HOVER_ENTER : UIButtonEvent::HOVER_EXIT);
        }

        // Exits before enters, so moving from one button to the next reads in order.
        std::ranges::stable_partition(toFire, [](const auto& entry) { return entry.second == UIButtonEvent::HOVER_EXIT; });

        if (hoveredID && InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT)) {
            if (ComponentUIButton* button = level.ui_buttons.GetInstance(*hoveredID)) {
                button->pressed = true;
                toFire.emplace_back(*hoveredID, UIButtonEvent::CLICK);
            }
        }

        // The release goes to the button that was clicked, wherever the cursor is.
        if (InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT)) {
            for (const ComponentInstanceID instanceID : buttons) {
                ComponentUIButton* button = level.ui_buttons.GetInstance(instanceID);
                if (button == nullptr || !button->pressed) continue;

                button->pressed = false;
                toFire.emplace_back(instanceID, UIButtonEvent::RELEASE);
            }
        }

        ApplyAllLooks(level);

        for (const auto& [instanceID, kind] : toFire) {
            const ComponentUIButton* button = level.ui_buttons.GetInstance(instanceID);
            if (button == nullptr) continue; // removed by an earlier callback

            const ID ownerID = button->ownerID;
            if (dispatch) dispatch(ownerID, kind);
            EventSystem::FireUIButtonEvent(level, ownerID, kind);
        }

        // The callbacks may have changed interactable, a colour, or added a sprite.
        ApplyAllLooks(level);
    }
}
