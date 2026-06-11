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
        float screenWidth,
        float screenHeight
    );
    void Draw(Level& level);
    void Shutdown(const Level& level);
}

#endif //TILKY_ENGINE_RUNTIMEEDITOR_H