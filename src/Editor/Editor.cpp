#include "EditorInternal.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/UISystem.hpp"
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
    bool LoadUserSettings() {
        const std::string version = ProjectManager::GetProjectEngineVersion();
        if (version.empty()) {
            spdlog::error("Can't find engine version, unable to load engine data");
            return false;
        }
        const fs::path settingsPath = ProjectManager::GetUserSettingsPath();
        if (!fs::exists(settingsPath)) {
            spdlog::error("User settings file doesn't exist: {}. Using defaults.", settingsPath.string());
            return false;
        }
        try {
            std::ifstream input(settingsPath, std::ios::binary);

            if (!input) spdlog::error("Could not open user settings file {}", settingsPath.string());

            const nlohmann::json settings = nlohmann::json::from_bson(input);

            if (!settings.is_object()) {
                spdlog::error("User settings root is not an object");
                return false;
            }

            const int formatVersion = settings.value("formatVersion", 0);

            if (formatVersion != 1) {
                spdlog::error("Unsupported user settings format version: {}", formatVersion);
                return false;
            }
            const nlohmann::json &colors = settings.at("colors");

            auto LoadColor3 = [&](const char *key) -> Vector3 {
                return {
                    colors.at(key).at(0).get<float>(),
                    colors.at(key).at(1).get<float>(),
                    colors.at(key).at(2).get<float>()
                };
            };

            auto LoadColor4 = [&](const char *key) -> Vector4 {
                return {
                    colors.at(key).at(0).get<float>(),
                    colors.at(key).at(1).get<float>(),
                    colors.at(key).at(2).get<float>(),
                    colors.at(key).at(3).get<float>()
                };
            };

            using namespace MapEditorInternal;
            normalEntityColor = LoadColor3("normalEntityColor");
            highlightedEntityColor = LoadColor3("highlightedEntityColor");
            spriteEntityColor = LoadColor3("spriteEntityColor");
            normalWallColor = LoadColor3("normalWallColor");
            highlightedWallColor = LoadColor3("highlightedWallColor");
            hoveredSectorColor = LoadColor3("hoveredSectorColor");
            highlightedSectorColor = LoadColor3("highlightedSectorColor");
            snapIndicatorColor = LoadColor3("snapIndicatorColor");
            kValidLineColor = LoadColor3("validLineColor");
            kInvalidLineColor = LoadColor3("invalidLineColor");
            kAnchorColor = LoadColor3("anchorColor");
            kValidFillColor = LoadColor4("validFillColor");
            kInvalidFillColor = LoadColor4("invalidFillColor");
            normalHandleColor = LoadColor3("normalHandleColor");
            highlightedHandleColor = LoadColor3("highlightedHandleColor");
            handleOutlineColor = LoadColor3("handleOutlineColor");
            themeTextColor = LoadColor3("themeTextColor");
            gridColor = LoadColor3("gridColor");
            backgroundColor = LoadColor3("backgroundColor");
        }
        catch (std::exception& e) {
            spdlog::error("Error while loading user settings {}", e.what());
            return false;
        }

        return true;
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

        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            spdlog::critical("MapEditor SDL_Init Failed: {}", SDL_GetError());
            return;
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
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED,
                &window,
                &renderer
            ) == false) {
            spdlog::critical("MapEditor Window or Renderer failed", SDL_GetError());
            SDL_Quit();
            return;
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
            return;
        }

        const fs::path fontPath = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

        font = TTF_OpenFont(fontPath.string().c_str(), UI_FONT_SIZE);
        TTF_SetFontKerning(font, false);

        if (!font) {
            spdlog::critical("TTF_OpenFont failed {}", SDL_GetError());
            TTF_Quit();
            SDL_Quit();
            return;
        }

        textEngine = TTF_CreateRendererTextEngine(renderer);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();

        scriptEditorFont = io.Fonts->AddFontFromFileTTF("EngineAssets/Fonts/jetbrainsmono.ttf");

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // When you comment this out somethings break for some reason
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(),18.0f);
        ApplyEditorTheme(currentTheme);

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        UpdateLevels();

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        if (!LoadUserSettings()) spdlog::error("Unable to load user settings");
    }

    void Update() {
        using namespace MapEditorInternal;

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
#ifndef TILKY_STANDALONE
        LevelManager::CurrentLevel().editorCamPos = cameraPos;
#endif
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

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