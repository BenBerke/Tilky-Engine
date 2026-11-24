//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_RUNTIMESESSION_H
#define TILKY_ENGINE_RUNTIMESESSION_H

#include <string>

class IRenderer;

/// Responsible for anything that requires rendering the map.

namespace RuntimeSession {
    enum RuntimeType {
        STANDALONE,
        EDITOR,
        PLAY
    };

#ifdef TILKY_STANDALONE
    bool Start(const std::string& windowName);
#else
    // Once at engine start: the hidden game window, its GL/ImGui contexts,
    // the texture atlas and the OpenAL device. All of it lives until Destroy().
    bool Initialize(const std::string& windowName);
    // Once at engine exit, before Editor::Destroy() (which calls SDL_Quit).
    void Destroy();

    // Shows the game window and starts a Play / runtime editor session.
    bool Start(RuntimeType runtimeType);

    // A runtime editor (EDITOR) session runs for as long as the 2D editor
    // does, inside its frame and ImGui context: the Scene panel next to the
    // Map panel. Each frame, in order:
    // RenderEditorView() before the editor's ImGui::NewFrame (game time, the
    // 3D view into the Scene panel's texture), DrawEditorUI() inside the
    // ImGui frame, FinishEditorFrame() once ImGui was rendered and before the
    // editor swaps the window (overlays, then the runtime editor's input;
    // `keyboardElsewhere`: the keys are the Map panel's this frame).
    [[nodiscard]] bool EditorViewRunning();
    void RenderEditorView();
    void DrawEditorUI();
    void FinishEditorFrame(bool keyboardElsewhere);
#endif

    // Play (and the standalone game) each frame.
    void Update();
    // Ends the session. In the engine the window is kept and stays visible
    // until HideWindow(), so the editor window can open first.
    void Shutdown();
#ifndef TILKY_STANDALONE
    void HideWindow();

    // The renderer and its window, made by Initialize(). The map and UI
    // editors draw in the same window. nullptr if Initialize() failed.
    [[nodiscard]] IRenderer* GetRenderer();

    // The game window's VSync (1 on, -1 adaptive, 0 off), from the editor's
    // Engine Settings. Applied at once (the editor draws in that window too)
    // and again when a session starts.
    void SetVSyncMode(int mode);
    [[nodiscard]] int GetVSyncMode();
#endif
}

#endif // TILKY_ENGINE_RUNTIMESESSION_H