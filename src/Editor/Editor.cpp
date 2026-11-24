#include "EditorInternal.hpp"
#include "Headers/Editor/CommandPalette.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <filesystem>
#include <optional>
#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/UISystem.hpp"
#include "Headers/Editor/EditorStyle.hpp"
#include "Headers/Editor/EditorTextureCache.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Runtime/RuntimeSession.hpp"
#include "Headers/Runtime/Renderer/IRenderer.hpp"

#include <glad/glad.h>

namespace MapEditorInternal {
    int screenWidth = 1080;
    int screenHeight = 960;
}

namespace {
    ImGuiContext* imguiContext = nullptr;

    // The window's size this frame, which the panels and ImGui are drawn at
    // (screenWidth/screenHeight are the 2D canvas's).
    int windowWidth = 1080;
    int windowHeight = 960;

    // Clears the window where no panel covers it, to what the dock space's
    // own background would show.
    void ClearWindowBehindPanels() {
        const ImVec4 emptyBackground = ImGui::GetStyle().Colors[ImGuiCol_DockingEmptyBg];
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowWidth, windowHeight);
        glClearColor(emptyBackground.x, emptyBackground.y, emptyBackground.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // User settings are read from disk on the first Start() only, so values
    // changed but not yet saved survive switching to the runtime editor and
    // back.
    bool userSettingsLoaded = false;

    // False when a settings file exists but could not be read. Destroy() then
    // skips its save, so the defaults the editor fell back to never overwrite
    // that file.
    bool userSettingsSafeToSave = false;

    // Between Start() and Stop(): the 3D view (runtime editor session) runs
    // then, as a panel next to the map.
    bool editorRunning = false;

    // Middle-drag panning of the map holds the cursor where the drag
    // started, hidden, like the 3D view's middle-drag look: it can't slide
    // onto the 3D view (or out of the window) mid-pan, and comes back where
    // it was on release. The pan reads the relative motion meanwhile.
    bool panCursorLocked = false;
    float panCursorX = 0.0f;
    float panCursorY = 0.0f;
    std::optional<SDL_Rect> mouseRectBeforePan;

    // The OS window the map panel is in (the editor's, or its own when
    // pulled out), held while locked.
    SDL_Window* panWindow = nullptr;

    void SetPanCursorLocked(const bool locked) {
        if (locked == panCursorLocked) return;

        SDL_Window* window = locked ? MapEditorInternal::PanelSdlWindow(MapEditorInternal::mapPanelWindowID) : panWindow;
        if (window == nullptr) return;

        if (locked) {
            panWindow = window;
            const Vector2 inWindow = MapEditorInternal::MouseInPanelWindow(MapEditorInternal::mapPanelWindowID);
            panCursorX = inWindow.x;
            panCursorY = inWindow.y;

            const SDL_Rect* previousRect = SDL_GetWindowMouseRect(window);
            mouseRectBeforePan = previousRect != nullptr ? std::optional<SDL_Rect>{*previousRect} : std::nullopt;

            InputManager::SetRelativeMouseMode(window, true);

            const SDL_Rect lockRect = {static_cast<int>(panCursorX), static_cast<int>(panCursorY), 1, 1};
            if (!SDL_SetWindowMouseRect(window, &lockRect))
                spdlog::warn("Map pan could not fix the cursor position: {}", SDL_GetError());

            panCursorLocked = true;
            return;
        }

        if (!SDL_SetWindowMouseRect(window, mouseRectBeforePan.has_value() ? &*mouseRectBeforePan : nullptr))
            spdlog::warn("Map pan could not restore the mouse rectangle: {}", SDL_GetError());

        // Never moves the pointer over another application after focus loss.
        if (SDL_GetMouseFocus() == window) SDL_WarpMouseInWindow(window, panCursorX, panCursorY);

        InputManager::SetRelativeMouseMode(window, false);
        panCursorLocked = false;
        panWindow = nullptr;
        mouseRectBeforePan.reset();
    }

    uint64_t lastAutoSaveTime = 0;
    // The level the running auto save is writing - a failed save only marks
    // the level unsaved again if it is still the one open.
    std::string autoSavingLevel;

    // Every autoSaveInSeconds, starts a background save of the open level if
    // it has unsaved changes, and reports the result once it finishes.
    void UpdateAutoSave() {
        using namespace MapEditorInternal;

        switch (PollAutoSave()) {
            case AutoSaveStatus::Saved:
                ImGuiDrawFunctions::ShowNotification(Localisation::Get("levels.notification.auto_saved").c_str());
                break;
            case AutoSaveStatus::Failed:
                if (Editor::currentMap == autoSavingLevel) hasUnsavedChanges = true;
                ImGuiDrawFunctions::ShowNotification(Localisation::Get("levels.notification.save_failed_check_logs").c_str(), /*isError=*/true);
                break;
            case AutoSaveStatus::Running:
                return;
            case AutoSaveStatus::Idle:
                break;
        }

        if (autoSaveInSeconds <= 0.0) return;

        const uint64_t now = SDL_GetTicks();
        if (now - lastAutoSaveTime < static_cast<uint64_t>(autoSaveInSeconds * 1000.0)) return;
        lastAutoSaveTime = now;

        // A level that has never been saved has no name to save under yet.
        if (!hasUnsavedChanges || Editor::currentMap.empty()) return;

        // Cleared now: anything edited while the save runs isn't in its copy,
        // so it sets the flag again.
        if (StartAutoSave(Editor::currentMap)) {
            autoSavingLevel = Editor::currentMap;
            hasUnsavedChanges = false;
        }
    }

    // A component inspector key button that wasn't drawn last frame
    // (entity deselected, inspector closed) can't finish its binding.
    void DropStaleKeyBindListen() {
        using namespace MapEditorInternal;

        if (keyBindBeingListened != nullptr && !IsUserSettingsKeySlot(keyBindBeingListened) &&
            componentKeyBindDrawnFrame < ImGui::GetFrameCount() - 1)
            keyBindBeingListened = nullptr;
    }

    // After the frame's edits, so the 3D view draws the level as the 2D
    // editor left it. Walls, sectors and sprites are rebuilt from the level
    // every frame and floors whenever their triangles change (the renderer
    // does both), so what's left is where entities stand and any image the
    // atlas doesn't have yet - checked only after something could have set
    // one: a click, a key, an undo or redo.
    void SyncRuntimeViewWithEdits() {
        using namespace MapEditorInternal;

        Level& level = LevelManager::CurrentLevel();

        for (ComponentTransform& transform : level.transforms.components)
            transform.UpdateObjectSectorAndFloor(level.sectors);

        static std::uint64_t checkedUndoVersion = 0;

        const bool mayHaveNewTexture =
            undoHistoryVersion != checkedUndoVersion ||
            InputManager::GetAnyKeyDown() != SDL_SCANCODE_UNKNOWN ||
            InputManager::GetMouseButtonUp(SDL_BUTTON_LEFT) ||
            InputManager::GetMouseButtonUp(SDL_BUTTON_RIGHT);

        checkedUndoVersion = undoHistoryVersion;

        if (mayHaveNewTexture) canvasRenderer->RefreshTexturesIfLevelNeedsNew();
    }
}

namespace MapEditorInternal {
    void StartRuntimeView() {
        if (!editorRunning || RuntimeSession::EditorViewRunning()) return;

        Editor::PrepareLevelForRuntime();

        if (!RuntimeSession::Start(RuntimeSession::EDITOR))
            spdlog::error("The 3D view (runtime editor) failed to start; the editor runs without it");

        // Starting made the renderer's own ImGui context (Play's) current.
        ImGui::SetCurrentContext(imguiContext);
    }

    void StopRuntimeView() {
        if (!RuntimeSession::EditorViewRunning()) return;

        // Writes where its camera is into the level, as it always did.
        canvasRenderer->MakeCurrent();
        RuntimeSession::Shutdown();
        ImGui::SetCurrentContext(imguiContext);
    }
}

namespace Editor {
    std::vector<Level> levels;
    ID currentLevels = 0;

    void Start() {
        using namespace MapEditorInternal;

        quit = false;
        play = false;
        shutdown = false;

        editingSector = false;
        selectedSectorID = INVALID_ID;

        editingEntity = false;

        sectorBeingCreated.clear();
        pendingSectorParams = {};

        // The undo history is kept: Play puts the level back as it was, and
        // the runtime editor records its edits in the same history.

        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;

        ClearWallSelection();
        hoveredWallID = INVALID_ID;
        hoveredEntityID = INVALID_ID;
        hoveredSectorID = INVALID_ID;
        draggingWallGeometry = false;

        ClearLinedefSelection();
        hoveredLinedefID = INVALID_ID;
        draggingLinedefGeometry = false;
        drawingLinedef = false;

        ImGui::SetCurrentContext(imguiContext);

        {
            // The runtime editor drives these on this context while its 3D
            // camera looks or drags (it shares the editor's context); the 2D
            // editor never sets them.
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
        }

        UpdateLevelsIfChanged();

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        if (!userSettingsLoaded) {
            userSettingsLoaded = true;

            if (LoadUserSettings()) userSettingsSafeToSave = true;
            else {
                spdlog::error("Unable to load user settings");

                // No file yet just means first run - saving the defaults is fine.
                userSettingsSafeToSave = !std::filesystem::exists(ProjectManager::GetUserSettingsPath());
            }
        }
        if (!LoadProjectSettings()) spdlog::error("Unable to load project settings");

        // The window Play and the runtime editor use too. Showing it makes
        // the renderer's context and ImGui context current (and maximizes it
        // the first time), so the editor's ImGui context is set again after.
        canvasRenderer->SetWindowVisible(true);
        ImGui::SetCurrentContext(imguiContext);

        SDL_SetWindowTitle(window, Localisation::Get("screen.title.level_editor").c_str());

        // The 3D view next to the map. Its session sets the window's VSync
        // from the setting, which the whole editor follows now that it draws
        // the 3D view every frame.
        editorRunning = true;
        StartRuntimeView();
    }

    bool Initialize() {
        using namespace MapEditorInternal;

        // The editors draw in the renderer's window, with its GL context.
        canvasRenderer = RuntimeSession::GetRenderer();
        if (canvasRenderer == nullptr) {
            spdlog::critical("The editor needs the renderer's window; RuntimeSession::Initialize() failed or hasn't run");
            return false;
        }

        canvasRenderer->MakeCurrent();
        window = canvasRenderer->GetWindow();
        SDL_GLContext glContext = SDL_GL_GetCurrentContext();

        if (window == nullptr || glContext == nullptr) {
            spdlog::critical("The renderer has no window or GL context for the editor");
            return false;
        }

        const fs::path iconPath = ProjectManager::GetEngineBasePath() / "LauncherAssets" / "Fox.png";
        SDL_Surface* windowIcon = IMG_Load(iconPath.string().c_str());

        if (windowIcon == nullptr) spdlog::error("Map editor failed to load window icon {}", SDL_GetError());
        else {
            if (!SDL_SetWindowIcon(window, windowIcon)) spdlog::error("Mapeditor failed to set window icon {}", SDL_GetError());
            SDL_DestroySurface(windowIcon);
        }

        EditorTextureCache::SetRenderer(canvasRenderer);

        const fs::path fontPath = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

        IMGUI_CHECKVERSION();
        // Lives until Destroy(), next to the runtime's own context on the same
        // window, so both set theirs current before touching ImGui.
        imguiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(imguiContext);

        ImGuiIO &io = ImGui::GetIO();

        scriptEditorFont = io.Fonts->AddFontFromFileTTF("EngineAssets/Fonts/jetbrainsmono.ttf");

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // When you comment this out somethings break for some reason
        // Panels can be pulled out of the editor window, Unity style: each
        // becomes an OS window of its own (drawn at the end of Update()),
        // without a task bar button. ImGui positions are then desktop
        // positions - see MapEditorInternal::EditorMousePosition.
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigViewportsNoTaskBarIcon = true;

        // The editor's own text font. The script editor's monospace font is
        // added first, so without this it would be the default for the whole UI.
        if (ImFont* uiFont = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 18.0f))
            io.FontDefault = uiFont;

        // Toolbar icons. Loaded on its own (not merged into the text font) so
        // only the toolbar pushes it.
        editorIconFont = nullptr;
        const fs::path iconFontPath = ProjectManager::FindAssetPath("EngineAssets/Fonts/FontAwesome6-Solid.otf");
        if (std::error_code ec; fs::exists(iconFontPath, ec)) editorIconFont = io.Fonts->AddFontFromFileTTF(iconFontPath.string().c_str(), 16.0f);
        else spdlog::warn("Icon font not found at {}, the toolbar will use text labels", iconFontPath.string());
        // The saved design replaces this once LoadUserSettings() in Start() runs.
        EditorStyle::Apply(EditorStyle::ActiveDesign());

        ImGui_ImplSDL3_InitForOpenGL(window, glContext);
        ImGui_ImplOpenGL3_Init("#version 430");

        return true;
    }

    void Stop() {
        using namespace MapEditorInternal;

        // Before Play: the 3D view's session ends first, and a map pan lets
        // the cursor go.
        SetPanCursorLocked(false);
        StopRuntimeView();

        // Panels pulled out of the window would stay up, frozen, through
        // Play; they come back with the editor's next frame.
        // (Not DestroyPlatformWindows(): that also ends the editor window's
        // own platform data, which ImGui needs until Destroy().)
        if (imguiContext != nullptr) {
            ImGui::SetCurrentContext(imguiContext);
            ImGuiContext& g = *GImGui;
            for (ImGuiViewport* viewport : g.Viewports)
                if (viewport != ImGui::GetMainViewport() && viewport->PlatformWindowCreated)
                    ImGui::DestroyPlatformWindow(static_cast<ImGuiViewportP*>(viewport));
        }
        editorRunning = false;

#ifndef TILKY_STANDALONE
        LevelManager::CurrentLevel().editorCamPos = cameraPos;
#endif

        // ImGui only saves the layout a few seconds after a change; write any
        // pending one now so it isn't lost while the editor is stopped. Not
        // before the first frame has read the file, or it would be emptied.
        ImGui::SetCurrentContext(imguiContext);
        const ImGuiIO& io = ImGui::GetIO();
        if (io.IniFilename != nullptr && GImGui->SettingsLoaded) ImGui::SaveIniSettingsToDisk(io.IniFilename);

        // Play and the runtime editor go on in this window, under its own name.
        SDL_SetWindowTitle(window, Localisation::Get("screen.title.engine").c_str());
    }

    void Update() {
        using namespace MapEditorInternal;

        canvasRenderer->MakeCurrent();
        ImGui::SetCurrentContext(imguiContext);

        SDL_GetWindowSize(window, &windowWidth, &windowHeight);

        // The map canvas is the Map panel's size as last drawn (the
        // window's before that); the UI editor's canvas is the window.
        if (currentState == STATE_MAP && mapPanelSize.x >= 1.0f && mapPanelSize.y >= 1.0f) {
            screenWidth = static_cast<int>(mapPanelSize.x);
            screenHeight = static_cast<int>(mapPanelSize.y);
        }
        else {
            screenWidth = windowWidth;
            screenHeight = windowHeight;
        }

        // Only the Map panel, drawn in the map editor, says the mouse is on
        // the canvas; a value left from before a switch must not linger.
        if (currentState != STATE_MAP) mapMouseFree = false;

        // The 3D view (Scene panel) is drawn with the map, not in the UI
        // editor, which fills the window.
        const bool sceneRunning = currentState == STATE_MAP && RuntimeSession::EditorViewRunning();

        if (ProcessPendingLevelLoad()) [[unlikely]] {
#ifndef TILKY_STANDALONE
            cameraPos = LevelManager::CurrentLevel().editorCamPos;
#endif
            glViewport(0, 0, windowWidth, windowHeight);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            canvasRenderer->EndFrame();
            return;
        }

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        // Before anything draws, so a texture finished this frame shows up
        // this frame.
        EditorTextureCache::Update();

        // Before the editor's ImGui frame: into the Scene panel's texture.
        if (sceneRunning) RuntimeSession::RenderEditorView();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO &io = ImGui::GetIO();

        DropStaleKeyBindListen();
        UpdateKeyboardPanel(sceneRunning);

        // The map canvas is an ImGui panel (Map), so ImGui always wants the
        // mouse over it; whether the canvas itself has the mouse is asked
        // there. The UI editor's canvas is still the window's middle.
        const bool mouseBlockedByImGui = currentState == STATE_MAP ? !mapMouseFree : io.WantCaptureMouse;
        // Only a text field being typed into blocks the shortcuts.
        // WantCaptureKeyboard is true whenever any ImGui window has focus
        // (keyboard nav is on), so clicking a panel used to lock every
        // shortcut until the canvas was clicked again.
        // The command palette has the keyboard while it's open, typing or not.
        const bool keyboardBlockedByImgui = io.WantTextInput || keyBindBeingListened != nullptr || CommandPalette::IsOpen();

        // Held from the frame the middle button pans the map (the canvas has
        // the mouse) until it goes up; off in the UI editor.
        SetPanCursorLocked(currentState == STATE_MAP && InputManager::GetMouseButton(SDL_BUTTON_MIDDLE) &&
                           (panCursorLocked || !mouseBlockedByImGui));

        if (currentState == STATE_MAP) {
            // The keys are the Map panel's or the 3D view's, never both.
            const bool mapKeyboardBlocked = keyboardBlockedByImgui || keyboardPanel != EditorPanel::Map;

            // ImGui only drops window focus on a left-click on empty space,
            // but the canvas picks with right-click too. A panel left focused
            // (the asset browser) would keep Ctrl+C/V from the canvas.
            if (!mouseBlockedByImGui && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
                ImGui::SetWindowFocus(nullptr);

            const bool teleporterPickTookMouse = HandleTeleporterPick(mouseBlockedByImGui, mapKeyboardBlocked);
            const bool gatewayPickTookMouse = HandleGatewayPick(mouseBlockedByImGui, mapKeyboardBlocked);
            HandleEditorInput(mouseBlockedByImGui || teleporterPickTookMouse || gatewayPickTookMouse, mapKeyboardBlocked);
            HandleEditorShortcuts(mapKeyboardBlocked);

            // After everything that moves entities, so a snapped one is
            // drawn back on its wall.
            if (WallSnap::Enforce(LevelManager::CurrentLevel())) MarkUnsavedChanges();

            // After the input, so what it changed is drawn this frame: into
            // the Map panel's texture, which DrawEditorUI shows with the
            // legend and comment icons on top.
            canvasRenderer->BeginPanelTarget(IRenderer::PanelTarget::Map, screenWidth, screenHeight);
            DrawMapCanvas();
            canvasRenderer->EndPanelTarget();
            ClearWindowBehindPanels();

            DrawEditorUI();

            // After the 2D panels, into the same dock space: the Scene panel,
            // its toolbar and the runtime editor's inspectors.
            if (sceneRunning) RuntimeSession::DrawEditorUI();
        }
        else if (currentState == STATE_UI) {
            HandleUIEditorShortcuts(keyboardBlockedByImgui);

            {
                // The canvas edits the UI layout the editor has open, if any.
                UIEditedLevelScope editedScope;
                HandleUIEditorInput(mouseBlockedByImGui, keyboardBlockedByImgui);

                // Again after the input, so a drag made this frame is drawn where
                // it now is.
                Level& level = LevelManager::CurrentLevel();
                UISystem::UpdateAllTransforms(level, screenWidth, screenHeight);
            }

            // Both pick the edited layout themselves.
            DrawUIEditorUI();
            UIEditorDraw();
        }

        ImGui::Render();

        // ImGui's SDL3 backend may stop text input because ImGuiColorTextEdit
        // is a custom widget rather than ImGui::InputText.
        if (!SDL_TextInputActive(window))
            if (!SDL_StartTextInput(window))
                spdlog::error("SDL_StartTextInput failed: {}", SDL_GetError());

        glViewport(0, 0, windowWidth, windowHeight);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // The panels pulled out of the window, each into its own OS window
        // (its GL context shares textures with this one); then this window's
        // context is current again.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
            SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(currentWindow, currentContext);
        }

        if (sceneRunning) {
            // Its overlays, then its input (as it always ran: after drawing).
            RuntimeSession::FinishEditorFrame(keyboardPanel != EditorPanel::Scene || keyBindBeingListened != nullptr);
            SyncRuntimeViewWithEdits();
        }

        canvasRenderer->EndFrame();

        UpdateAutoSave();
    }

    bool QuitRequested() {
        return MapEditorInternal::quit || InputManager::QuitRequested();
    }

    bool PlayRequested() {
        return MapEditorInternal::quit || MapEditorInternal::play;
    }

    bool ShutdownRequested() {
        return MapEditorInternal::shutdown || InputManager::QuitRequested();
    }

    bool HoldShutdownForExport() {
        return MapEditorInternal::HoldQuitForGameExport();
    }

    void Destroy() {
        using namespace MapEditorInternal;

        WaitForAutoSave();
        StopGameExportForExit();

        if (imguiContext == nullptr) return;

        if (userSettingsSafeToSave) SaveUserSettings();
        else spdlog::warn("Not saving user settings on close: the existing file could not be loaded");

        // The backend frees its GL objects, so the renderer's context must
        // be current: Destroy() runs before RuntimeSession::Destroy().
        if (canvasRenderer != nullptr) canvasRenderer->MakeCurrent();
        ImGui::SetCurrentContext(imguiContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(imguiContext);
        imguiContext = nullptr;

        // The textures themselves belong to the renderer.
        EditorTextureCache::Destroy();

        // The window and renderer are the runtime's; RuntimeSession::Destroy() ends them.
        window = nullptr;
        canvasRenderer = nullptr;

        quit = false;
        shutdown = false;
    }

    SDL_Window* GetWindow() {
        return MapEditorInternal::window;
    }
}