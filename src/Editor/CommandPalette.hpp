#pragma once

#include <functional>

#include "EditorInternal.hpp"

// Ctrl+Shift+P: a searchable list of every action of every editor - the 2D,
// runtime and UI editors' shortcuts plus the commands they register - with
// the keys each is bound to. Enter runs the highlighted one; one from
// another editor switches to that editor first.
namespace CommandPalette {
    void Open();
    [[nodiscard]] bool IsOpen();

    // An action with no key of its own (a menu item, a panel toggle).
    // Registering the same label again replaces it.
    void Register(const char* labelKey, MapEditorInternal::KeyBindScope scope, std::function<void()> run);

    // Makes the given editor the current one.
    using SwitchEditor = std::function<void(MapEditorInternal::KeyBindScope)>;

    // Draws the palette while it's open. `current` is the editor drawing it.
    void Draw(MapEditorInternal::KeyBindScope current, const SwitchEditor& switchTo);
}
