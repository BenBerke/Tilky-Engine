#include "EditorInternal.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

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

namespace MapEditorInternal {
    int screenWidth = 1080;
    int screenHeight = 960;
}

namespace {
    ImGuiContext* imguiContext = nullptr;
    bool shownOnce = false;

    // User settings are read from disk on the first Start() only, so values
    // changed but not yet saved survive switching to the runtime editor and
    // back.
    bool userSettingsLoaded = false;

    // False when a settings file exists but could not be read. Destroy() then
    // skips its save, so the defaults the editor fell back to never overwrite
    // that file.
    bool userSettingsSafeToSave = false;
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
        editingComponent = false;

        sectorBeingCreated.clear();
        pendingSectorParams = {};
        actions.clear();

        dots.clear();
        dotIDToIndex.clear();
        nextDotID = 0;

        ClearWallSelection();
        hoveredWallID = INVALID_ID;
        draggingWallGeometry = false;

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

        SDL_ShowWindow(window);

        if (!shownOnce) {
            // Later shows keep whatever size and position the user left it at.
            SDL_MaximizeWindow(window);
            shownOnce = true;
        }

        SDL_RaiseWindow(window);
    }

    bool Initialize() {
        using namespace MapEditorInternal;

        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            spdlog::critical("MapEditor SDL_Init Failed: {}", SDL_GetError());
            return false;
        }

        //const SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
        //const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(displayID);
        // if (!mode) spdlog::error("Failed to get desktop display mode", SDL_GetError());
        // else {
        //     screenWidth = mode->w;
        //     screenHeight = mode->h;
        // }

        if (SDL_CreateWindowAndRenderer(
                Localisation::Get("screen.title.level_editor").c_str(),
                screenWidth,
                screenHeight,
                // Hidden until Start() shows it; maximized on that first show.
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN,
                &window,
                &renderer
            ) == false) {
            spdlog::critical("MapEditor Window or Renderer failed", SDL_GetError());
            SDL_Quit();
            return false;
        }
        SDL_SetWindowFullscreenMode(window, nullptr);

        const fs::path iconPath = ProjectManager::GetEngineBasePath() / "LauncherAssets" / "Fox.png";
        SDL_Surface* windowIcon = IMG_Load(iconPath.string().c_str());

        if (windowIcon == nullptr) spdlog::error("Map editor failed to load window icon {}", SDL_GetError());
        else {
            if (!SDL_SetWindowIcon(window, windowIcon)) spdlog::error("Mapeditor failed to set window icon {}", SDL_GetError());
            SDL_DestroySurface(windowIcon);
        }

        if (!TTF_Init()) {
            spdlog::critical("TTF_Init failed {}", SDL_GetError());
            SDL_Quit();
            return false;
        }

        const fs::path fontPath = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

        font = TTF_OpenFont(fontPath.string().c_str(), UI_FONT_SIZE);
        TTF_SetFontKerning(font, false);

        if (!font) {
            spdlog::critical("TTF_OpenFont failed {}", SDL_GetError());
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        textEngine = TTF_CreateRendererTextEngine(renderer);

        IMGUI_CHECKVERSION();
        // Lives until Destroy(), next to the runtime's own context, so both
        // set theirs current before touching ImGui.
        imguiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(imguiContext);

        ImGuiIO &io = ImGui::GetIO();

        scriptEditorFont = io.Fonts->AddFontFromFileTTF("EngineAssets/Fonts/jetbrainsmono.ttf");

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // When you comment this out somethings break for some reason
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        return true;
    }

    void Stop() {
        using namespace MapEditorInternal;
#ifndef TILKY_STANDALONE
        LevelManager::CurrentLevel().editorCamPos = cameraPos;
#endif
        SDL_HideWindow(window);
    }

    void Update() {
        using namespace MapEditorInternal;

        ImGui::SetCurrentContext(imguiContext);

        if (ProcessPendingLevelLoad()) [[unlikely]] {
#ifndef TILKY_STANDALONE
            cameraPos = LevelManager::CurrentLevel().editorCamPos;
#endif
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            return;
        }

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        // Before anything draws, so a texture finished this frame shows up
        // this frame.
        EditorTextureCache::Update(renderer);

        SDL_SetRenderDrawColor(renderer, backgroundColor.r, backgroundColor.g, backgroundColor.b, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO &io = ImGui::GetIO();

        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        const bool keyboardBlockedByImgui = io.WantCaptureKeyboard;

        if (currentState == STATE_MAP) {
            HandleEditorInput(mouseBlockedByImGui, keyboardBlockedByImgui);

            DrawGridDots();
            DrawExistingSectors();
            DrawWalls();

            // Geometry Mode's hover/selection highlights and endpoint
            // handles. This took the place of DrawDots(), but has to be
            // drawn AFTER DrawWalls() rather than before it the way
            // DrawDots() was - it highlights the walls, so drawing it
            // first would put every highlight underneath them.
            DrawGeometryEditOverlay();

            DrawEntities();
            DrawEntityMoveGizmo();

            // Also called in Geometry Mode for the snap indicator: wall
            // and endpoint dragging obey the same grid/point snapping as
            // sector drawing, so it needs the same preview of where a
            // point will actually land.
            if (currentMode == MODE_SECTOR || currentMode == MODE_GEOMETRY) DrawSectorPreview();

            DrawEditorUI();
        }
        else if (currentState == STATE_UI) {
            HandleUIEditorInput(mouseBlockedByImGui, keyboardBlockedByImgui);

            Level& level = LevelManager::CurrentLevel();
            UISystem::UpdateAllTransforms(level, screenWidth, screenHeight);

            HandleUIEditorInput(mouseBlockedByImGui, keyboardBlockedByImgui);
            DrawUIEditorUI();
            UIEditorDraw();
        }

        ImGui::Render();

        // ImGui's SDL3 backend may stop text input because ImGuiColorTextEdit
        // is a custom widget rather than ImGui::InputText.
        if (!SDL_TextInputActive(window))
            if (!SDL_StartTextInput(window))
                spdlog::error("SDL_StartTextInput failed: {}", SDL_GetError());

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_GetWindowSize(window, &screenWidth, &screenHeight);

        SDL_RenderPresent(renderer);
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

    bool SwitchToRuntimeEditorRequested() {
        return MapEditorInternal::switchToRuntime;
    }

    void Destroy() {
        using namespace MapEditorInternal;

        if (imguiContext == nullptr) return;

        if (userSettingsSafeToSave) SaveUserSettings();
        else spdlog::warn("Not saving user settings on close: the existing file could not be loaded");

        ImGui::SetCurrentContext(imguiContext);
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(imguiContext);
        imguiContext = nullptr;

        DestroyUIPreviewFonts();

        if (font) {
            TTF_CloseFont(font);
            font = nullptr;
        }

        TTF_Quit();

        // Must run before the renderer is destroyed below - these textures
        // were created against it.
        EditorTextureCache::Destroy();

        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }

        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        quit = false;
        shutdown = false;
        SDL_Quit();
    }

    SDL_Window* GetWindow() {
        return MapEditorInternal::window;
    }
}