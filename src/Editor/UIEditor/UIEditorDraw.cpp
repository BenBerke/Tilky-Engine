// The UI editor canvas. It is drawn by the renderer (OpenGL, in the window
// the game and the runtime editor use) from the Editor2D::Frame that
// UIEditorFrame.cpp builds - the picture follows the game's UI math (see the
// notes there). This file only reads; it changes no values.

#include "src/Editor/EditorInternal.hpp"

#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/Runtime/Renderer/IRenderer.hpp"

namespace MapEditorInternal {
    void UIEditorDraw() {
        if (canvasRenderer == nullptr) return;

        // Kept between frames so its buffers are reused.
        static Editor2D::Frame frame;
        BuildUIEditorFrame(frame);
        canvasRenderer->DrawEditor2D(frame);
    }
}
