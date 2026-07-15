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
#include "Headers/Objects/Entity.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

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
        selectedDotID = INVALID_ID;

        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            spdlog::critical("MapEditor SDL_Init Failed: {}", SDL_GetError());
            return;
        }

        if (SDL_CreateWindowAndRenderer(
                Localisation::Get("screen.title.level_editor").c_str(),
                screenWidth,
                screenHeight,
                SDL_WINDOW_RESIZABLE,
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

        if (windowIcon == nullptr)
            spdlog::error("Map editor failed to load window icon {}", SDL_GetError());
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

        font = TTF_OpenFont(fontPath.string().c_str(), FONT_SIZE);

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

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        io.Fonts->AddFontFromFileTTF(
            fontPath.string().c_str(),
            18.0f
        );

        // Feature #2: theme is now switchable at runtime; this just applies
        // whichever theme is currently selected (defaults to dark, matching
        // the previous hardcoded behavior).
        ApplyEditorTheme(currentTheme);

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        UpdateLevels();

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        EditorTextureCache::RefreshLevelTexturesFromFolder();
        RefreshLevelSoundsFromFolder();
        EditorTextureCache::Load(renderer, LevelManager::CurrentLevel());
    }

    void Update() {
        using namespace MapEditorInternal;

        if (ProcessPendingLevelLoad()) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            return;
        }

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        Level &level = LevelManager::CurrentLevel();

        if (currentTheme == THEME_DARK)
            SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        else if (currentTheme == THEME_LIGHT)
            SDL_SetRenderDrawColor(renderer, 215, 215, 215, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO &io = ImGui::GetIO();

        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        const bool keyboardBlockedByImgui = io.WantCaptureKeyboard;

        if (currentState == STATE_MAP) {
            // Update decals so they always stick to the wall they are attached to
            HandleEditorInput(mouseBlockedByImGui, keyboardBlockedByImgui);
            for (ComponentDecal &decal: level.decals.components) {
                ComponentTransform *transform = level.transforms.Get(decal.ownerID);

                if (transform == nullptr) {
                    continue;
                }

                if (decal.wallIndex < 0 ||
                    decal.wallIndex >= static_cast<int>(level.walls.size())) {
                    continue;
                    }

                const Wall &wall = level.walls[decal.wallIndex];

                const Vector2 wallVector = wall.end - wall.start;

                const float wallLengthSq =
                        wallVector.x * wallVector.x +
                        wallVector.y * wallVector.y;

                if (wallLengthSq <= 0.0001f) {
                    continue;
                }

                const Vector2 toObject = Vector2{transform->position.x, transform->position.y} - wall.start;

                float t = (toObject.x * wallVector.x + toObject.y * wallVector.y) / wallLengthSq;

                t = std::clamp(t, 0.0f, 1.0f);

                if (!editingComponent) {
                    decal.wallT = t;
                    decal.horizontalPos = std::sqrt(wallLengthSq) * decal.wallT;
                }
                auto lerp = [](const float a, const float b, const float t) -> float {
                    return (1.0f - t) * a + t * b;
                };

                transform->position = {
                    lerp(wall.start.x, wall.end.x, decal.wallT),
                    lerp(wall.start.y, wall.end.y, decal.wallT)
                };
            }

            DrawGridDots();
            DrawExistingSectors();
            DrawDots();
            DrawWalls();
            DrawEntities();

            if (currentMode == MODE_SECTOR) {
                DrawSectorPreview();
            }

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

        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        if (font) {
            TTF_CloseFont(font);
            font = nullptr;
        }

        TTF_Quit();

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
}