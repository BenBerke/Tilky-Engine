//
// Created by berke on 10/31/2026.
//

#ifndef TILKY_ENGINE_UIBUTTONSYSTEM_HPP
#define TILKY_ENGINE_UIBUTTONSYSTEM_HPP

#include <functional>

#include "Headers/Objects/Level.hpp"
#include "Headers/Runtime/EventSystem.hpp"

/// Runs every ComponentUIButton during Play and Standalone (LevelSystem):
/// finds the topmost button under the cursor, fires the UI Button events and
/// the scripts' callbacks, and puts each button's state look on its entity's
/// first UI Sprite (ComponentUISprite::buttonTint / buttonTexture). Only an
/// entity's first UI Button counts. Does nothing while the cursor is locked
/// or hidden: buttons then lose their hover and press without events.
namespace UIButtonSystem {
    // Calls the OnClick / OnRelease / OnHoverEnter / OnHoverExit callback of
    // `entityID`'s scripts.
    using CallbackDispatcher = std::function<void(ID entityID, EventSystem::UIButtonEvent kind)>;

    // Clears every button's hover and press and shows its normal (or
    // disabled) look. Call once the level's scripts have started.
    void Start(Level& level);

    // Before the scripts' Update, so they see this frame's hover and press.
    // Uses this frame's mouse input; works while the game is paused.
    void Update(Level& level, const CallbackDispatcher& dispatch);

    // Puts the button's current state look on its entity's first UI Sprite.
    // For changes made outside Update (Lua setting interactable or a colour).
    void ApplyLook(Level& level, const ComponentUIButton& button);

    // The colour and texture the button shows in its current state.
    [[nodiscard]] const Vector4& StateColor(const ComponentUIButton& button);
    [[nodiscard]] const std::string& StateTexture(const ComponentUIButton& button);
}

#endif //TILKY_ENGINE_UIBUTTONSYSTEM_HPP
