#include "MapEditorInternal.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

#include "Headers/Objects/Entity.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Map/LevelManager.hpp"


namespace MapEditor {
    std::vector<Level> levels;
    EntityID currentLevels = 0;

    void Start() {
        using namespace MapEditorInternal;

        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init Error: %s\n", SDL_GetError());
            return;
        }

        if (SDL_CreateWindowAndRenderer(
                "Wolfy Level Editor",
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                &window,
                &renderer
            ) == false) {
            SDL_Log("Window/Renderer Error: %s\n", SDL_GetError());
            SDL_Quit();
            return;
        }

        if (!TTF_Init()) {
            SDL_Log("TTF_INIT failed: %s\n", SDL_GetError());
            SDL_Quit();
            return;
        }

        font = TTF_OpenFont("../Assets/Fonts/arial.ttf", FONT_SIZE);

        if (!font) {
            SDL_Log("TTF_OpenFont failed: %s\n", SDL_GetError());
            TTF_Quit();
            SDL_Quit();
            return;
        }

        textEngine = TTF_CreateRendererTextEngine(renderer);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        UpdateLevels();
    }

    void Update() {
        using namespace MapEditorInternal;

        if (!LevelManager::HasCurrentLevel()) {
            LevelManager::loadedLevels.emplace_back();
            LevelManager::currentLevelIndex = 0;
        }

        Level &level = LevelManager::CurrentLevel();

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO &io = ImGui::GetIO();

        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        const bool keyboardBlockedByImgui = io.WantCaptureKeyboard;

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

            const Vector2 toObject = transform->position - wall.start;

            float t =
                    (toObject.x * wallVector.x + toObject.y * wallVector.y) /
                    wallLengthSq;

            t = std::clamp(t, 0.0f, 1.0f);

            decal.wallT = t;
            decal.wallOffset = std::sqrt(wallLengthSq) * decal.wallT;

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
        DrawCorners();
        DrawWalls();
        DrawObjects();

        if (currentMode == MODE_SECTOR) {
            DrawSectorPreview();
        }

        DrawEditorUI();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }

    bool QuitRequested() {
        return MapEditorInternal::quit;
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

        SDL_Quit();
    }
}
