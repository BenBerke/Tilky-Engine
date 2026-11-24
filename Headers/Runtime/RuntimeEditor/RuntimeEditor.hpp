//
// Created by berke on 6/3/2026.
//

#ifndef TILKY_ENGINE_RUNTIMEEDITOR_H
#define TILKY_ENGINE_RUNTIMEEDITOR_H

#include "Headers/Math/Vector/Vector3.hpp"

#include <string>

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

    // The Scene panel's size in framebuffer pixels as last drawn, which the
    // 3D view is rendered at (0 x 0 before it first was).
    void ScenePixelSize(int& width, int& height);

    // The mouse is on the Scene panel's image with nothing of ImGui's in the
    // way: what the runtime editor's clicks and keys under the mouse need.
    [[nodiscard]] bool SceneMouseFree();

    // Puts a texture on the selected walls, floors/ceilings (the ones picked
    // in the 3D view) or entity sprites, as one undo step: the Asset
    // Browser's Ctrl+click while the 3D view has the keys.
    bool AssignTextureToSelection(const std::string& reference);

    // Puts the 3D view's camera there (its rotation is kept).
    void MoveCameraTo(Vector3 position);
}

#endif //TILKY_ENGINE_RUNTIMEEDITOR_H