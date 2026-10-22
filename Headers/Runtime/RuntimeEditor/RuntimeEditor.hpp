//
// Created by berke on 6/3/2026.
//

#ifndef TILKY_ENGINE_RUNTIMEEDITOR_H
#define TILKY_ENGINE_RUNTIMEEDITOR_H

struct Level;
class IRenderer;

namespace RuntimeEditor {
    void Start(Level& level, IRenderer& renderer);
    void Update(
        Level& level,
        IRenderer& renderer,
        bool relativeMouseMod,
        bool mouseBlockedByImGui,
        bool keyboardBlockedByImGui,
        float screenWidth,
        float screenHeight
    );
    void Draw(Level& level);
    void Shutdown(Level& level);

    // The key back to the Map Editor was pressed. Reset by Start().
    bool BackToEditorRequested();

    // Save & Play was pressed and the level saved.
    bool PlayRequested();
}

#endif //TILKY_ENGINE_RUNTIMEEDITOR_H