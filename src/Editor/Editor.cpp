#include "EditorInternal.hpp"
#include "Headers/Editor/CommandPalette.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <filesystem>
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

    // User settings are read from disk on the first Start() only, so values
    // changed but not yet saved survive switching to the runtime editor and
    // back.
    bool userSettingsLoaded = false;

    // False when a settings file exists but could not be read. Destroy() then
    // skips its save, so the defaults the editor fell back to never overwrite
    // that file.
    bool userSettingsSafeToSave = false;

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
}

namespace Editor {
    std::vector<Level> levels;
    ID currentLevels = 0;

    void Start() {
        using namespace MapEditorInternal;

        quit = false;
        play = false;
        shutdown = false;
        switchToRuntime = false;

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

        UpdateLevels();

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

        // The editor drew without VSync on its own SDL_Renderer; the VSync
        // setting is for Play and the runtime editor (they set it each session).
        canvasRenderer->SetVSync(0);
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
        // No ImGuiConfigFlags_ViewportsEnable: the SDL_Renderer backend the
        // editor used couldn't put panels in windows of their own, so they
        // always stayed inside the editor window. The OpenGL backend can, but
        // only with platform windows managed every frame; they stay inside.

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

        // The canvas is drawn at the window's size this frame.
        SDL_GetWindowSize(window, &screenWidth, &screenHeight);

        if (ProcessPendingLevelLoad()) [[unlikely]] {
#ifndef TILKY_STANDALONE
            cameraPos = LevelManager::CurrentLevel().editorCamPos;
#endif
            glViewport(0, 0, screenWidth, screenHeight);
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO &io = ImGui::GetIO();

        // A component inspector key button that wasn't drawn last frame
        // (entity deselected, inspector closed) can't finish its binding.
        if (keyBindBeingListened != nullptr && !IsUserSettingsKeySlot(keyBindBeingListened) &&
            componentKeyBindDrawnFrame < ImGui::GetFrameCount() - 1)
            keyBindBeingListened = nullptr;

        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        // Only a text field being typed into blocks the shortcuts.
        // WantCaptureKeyboard is true whenever any ImGui window has focus
        // (keyboard nav is on), so clicking a panel used to lock every
        // shortcut until the canvas was clicked again.
        // The command palette has the keyboard while it's open, typing or not.
        const bool keyboardBlockedByImgui = io.WantTextInput || keyBindBeingListened != nullptr || CommandPalette::IsOpen();

        if (currentState == STATE_MAP) {
            // ImGui only drops window focus on a left-click on empty space,
            // but the canvas picks with right-click too. A panel left focused
            // (the asset browser) would keep Ctrl+C/V from the canvas.
            if (!mouseBlockedByImGui && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
                ImGui::SetWindowFocus(nullptr);

            const bool teleporterPickTookMouse = HandleTeleporterPick(mouseBlockedByImGui, keyboardBlockedByImgui);
            const bool gatewayPickTookMouse = HandleGatewayPick(mouseBlockedByImGui, keyboardBlockedByImgui);
            HandleEditorInput(mouseBlockedByImGui || teleporterPickTookMouse || gatewayPickTookMouse, keyboardBlockedByImgui);
            HandleEditorShortcuts(keyboardBlockedByImgui);

            // After everything that moves entities, so a snapped one is
            // drawn back on its wall.
            if (WallSnap::Enforce(LevelManager::CurrentLevel())) MarkUnsavedChanges();

            // After the input, so what it changed is drawn this frame. Drawn
            // now; ImGui (the legend, comment icons, panels) goes on top.
            DrawMapCanvas();

            DrawViewModeLegend();
            DrawCommentIcons();

            DrawEditorUI();
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

        glViewport(0, 0, screenWidth, screenHeight);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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

    bool SwitchToRuntimeEditorRequested() {
        return MapEditorInternal::switchToRuntime;
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