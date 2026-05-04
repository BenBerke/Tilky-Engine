//
// Created by berke on 5/4/2026.
//
#include "Headers/Launcher/LauncherApp.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

#include <iostream>
#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

namespace {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    bool quitRequested = false;

    void PutSpace(const int n) {
        for (int i = 0; i < n; i++) {
            ImGui::Spacing();
        }
    }
}

namespace LauncherApp {
    void Start(const std::string& langCode) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init Error: %s\n", SDL_GetError());
            return;
        }

        if (!SDL_CreateWindowAndRenderer(
                "Tilky Engine Launcher",
                1060,
                800,
                SDL_WINDOW_RESIZABLE,
                &window,
                &renderer
            )) {
            std::cerr << "Failed to start launcher: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        io.Fonts->AddFontFromFileTTF(
            "../EngineAssets/Fonts/Notosans.ttf",
            18.0f
        );

        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        Localisation::LoadLanguage(langCode);
    }

    void Update() {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                quitRequested = true;
            }
        }

        SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int windowWidth, windowHeight;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

        ImGui::Begin(Localisation::Get("launcher.name").c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);

        if (ImGui::Button(Localisation::Get("launcher.create_project").c_str())) {
            std::cout << "Create Project clicked\n";
        }

        for (const fs::directory_entry& entry : fs::directory_iterator(ProjectManager::GetDefaultProjectsFolder())) {
            if (entry.is_directory()) {
                //std::cout << entry.path().filename().string() << '\n';
            }
        }

        ImGui::Text(Localisation::Get("launcher.projects").c_str());

        ImGui::SetCursorPosY(static_cast<float>(windowHeight) - 35.0f);
        if (ImGui::Button(Localisation::Get("launcher.quit").c_str())) {
            quitRequested = true;
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(static_cast<float>(windowWidth) - 120.0f);
        if (ImGui::Button("English")) Localisation::LoadLanguage("en");
        ImGui::SameLine();
        if (ImGui::Button("Türkçe")) Localisation::LoadLanguage("tr");

        ImGui::End();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }

    bool QuitRequested() {
        return quitRequested;
    }

    void Destroy() {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }

        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();
    }
}