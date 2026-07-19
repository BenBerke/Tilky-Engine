//
// Created by berke on 6/14/2026.
//

#ifndef TILKY_ENGINE_EDITORFUNCTIONS_H
#define TILKY_ENGINE_EDITORFUNCTIONS_H

#include <string>

#include "Headers/Math/Vector/Vector3.hpp"

class IRenderer;

namespace EditorFunctions {
    void Print(const std::string& text);
    void Print(const std::string& text, const Vector3 &color);
    void Print(const std::string& text, const Vector3 &color, float lifeTime);

    // Updates timers only. Can be called once per frame.
    void UpdateConsole(float deltaTime);

    // Draws only. Must be called after BeginFrame(), before EndFrame().
    void RenderConsole(IRenderer* renderer);

    void ClearConsole();
}
#endif //TILKY_ENGINE_EDITORFUNCTIONS_H