#include "MapEditorInternal.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

#include <filesystem>

namespace MapEditor {
    std::vector<std::string> maps;
    std::string currentMap = "test_level";

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

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        UpdateLevels();
    }

    void Update() {
        using namespace MapEditorInternal;

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO& io = ImGui::GetIO();

        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        const bool keyboardBlockedByImgui = io.WantCaptureKeyboard;

        HandleEditorInput(mouseBlockedByImGui, keyboardBlockedByImgui);

        for (Object& object : objects) {
            if (object.type != OBJ_DECAL || object.wallIndex == -1) {
                continue;
            }

            if (object.wallIndex < 0 || object.wallIndex >= static_cast<int>(walls.size())) {
                continue;
            }

            const Wall& wall = walls[object.wallIndex];

            const Vector2 wallVector = wall.end - wall.start;

            const float wallLengthSq =
                wallVector.x * wallVector.x +
                wallVector.y * wallVector.y;

            if (wallLengthSq <= 0.0001f) {
                continue;
            }

            const Vector2 toObject = object.position - wall.start;

            float t =
                (toObject.x * wallVector.x + toObject.y * wallVector.y) /
                wallLengthSq;

            t = std::clamp(t, 0.0f, 1.0f);

            object.wallT = t;
            object.wallOffset = std::sqrt(wallLengthSq) * object.wallT;

            auto lerp = [](const float a, const float b, const float t) -> float {
                return (1.0f - t) * a + t * b;
            };

            object.position = {
                lerp(wall.start.x, wall.end.x, object.wallT),
                lerp(wall.start.y, wall.end.y, object.wallT)
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