//
// Created by berke on 5/4/2026.
//
#include "Headers/Launcher/LauncherApp.hpp"

#include <fstream>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <nlohmann/json.hpp>
#include <SDL3_image/SDL_image.h>

using json = nlohmann::json;

namespace {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    bool quitRequested = false;

    bool creatingProject = false;
    std::array<char, 64> projectName{};

    fs::path pendingProjectToOpen;

    SDL_Texture* logo = nullptr;

    int windowWidth = 1080, windowHeight = 960;

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
                windowWidth,
                windowHeight,
                SDL_WINDOW_RESIZABLE,
                &window,
                &renderer
            )) {
            std::cerr << "Failed to start launcher: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return;
        }

        SDL_Surface* windowIcon = IMG_Load("../LauncherAssets/Fox.png");
        if (windowIcon == nullptr) std::cerr << "Failed to load image!" << SDL_GetError() << std::endl;
        else if (!SDL_SetWindowIcon(window, windowIcon)) std::cerr << "Failed to set launcher image icon!" << SDL_GetError() << std::endl;

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

        logo = IMG_LoadTexture(renderer, "../LauncherAssets/LogoWithWhiteText.png");
        if (logo == nullptr) {
            std::cerr << "Failed to load logo!" << std::endl;
        }
        else {
            SDL_SetTextureBlendMode(logo, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(logo, 145);
        }
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

        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

        ImGui::Begin(Localisation::Get("launcher.name").c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);

        if (ImGui::Button(Localisation::Get("launcher.create_project").c_str())) {
            creatingProject = true;
        }
        if (creatingProject) {
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth) * 0.5f, 100.0f), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(windowWidth) * .25f, static_cast<float>(windowHeight) * .5f), ImGuiCond_Always);
            ImGui::Begin(Localisation::Get("launcher.create_project").c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

            ImGui::InputText(Localisation::Get("launcher.input_name").c_str(), projectName.data(), projectName.size());

            if (ImGui::Button(Localisation::Get("launcher.create").c_str())) {
                ProjectManager::CreateDirectory(projectName.data());
                creatingProject = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(Localisation::Get("common.cancel").c_str())) {
                creatingProject = false;
            }

            ImGui::End();
        }

        ImGui::Text(Localisation::Get("launcher.projects").c_str());

        PutSpace(10);

        //region Listing Projects
        fs::path projectToDelete;
        for (const auto& entry : fs::recursive_directory_iterator(ProjectManager::GetDefaultProjectsFolder())) {
            if (entry.is_regular_file() && entry.path().extension() == ".tilky") {
                const fs::path& foundPath = entry.path();
                std::ifstream file(foundPath);
                if (file.is_open()) {
                    try {
                        json data = json::parse(file);
                        if (data.contains("name")) {
                            const std::string name = data["name"];

                            ImGui::Text("%s", name.c_str());
                            ImGui::SameLine();

                            std::string openButtonId =
                                Localisation::Get("launcher.open_project") + "##open_" + foundPath.string();

                            if (ImGui::Button(openButtonId.c_str())) {
                                pendingProjectToOpen = foundPath.parent_path();
                                quitRequested = true;
                            }

                            ImGui::SameLine();

                            std::string deleteButtonId =
                                Localisation::Get("launcher.delete_project") + "##delete_" + foundPath.string();

                            if (ImGui::Button(deleteButtonId.c_str())) {
                                projectToDelete = foundPath.parent_path();
                            }
                        }
                    } catch (json::parse_error& e) {
                        std::cerr << "JSON format error in " << foundPath.filename() << ": " << e.what() << std::endl;
                    }
                }

                break;
            }
        }
        if (!projectToDelete.empty()) {
            try {
                const std::uintmax_t removedCount = fs::remove_all(projectToDelete);

                std::cout << "Deleted project folder: "
                          << projectToDelete
                          << " removed items: "
                          << removedCount
                          << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to delete project folder: "
                          << projectToDelete
                          << " error: "
                          << e.what()
                          << std::endl;
            }
        }

        //endregion

        ImGui::SetCursorPosY(static_cast<float>(windowHeight) - 35.0f);
        if (ImGui::Button(Localisation::Get("launcher.quit").c_str())) {
            quitRequested = true;
        }

        ImGui::SameLine();


        // Languages

        constexpr int langCount = 5;
        ImGui::SetCursorPosX(static_cast<float>(windowWidth) - langCount * 60.0f);
        if (ImGui::Button("English")) Localisation::LoadLanguage("en");
        ImGui::SameLine();
        if (ImGui::Button("Türkçe")) Localisation::LoadLanguage("tr");
        ImGui::SameLine();
        if (ImGui::Button("Kazakh")) Localisation::LoadLanguage("qa");
        ImGui::SameLine();
        if (ImGui::Button("Russian")) Localisation::LoadLanguage("ru");
        ImGui::SameLine();
        if (ImGui::Button("Polish")) Localisation::LoadLanguage("pl");

        ImGui::End();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        // Logo
        constexpr float logoWidth = 600.0f, logoHeight = 600.0f;
        if (logo != nullptr) {
            SDL_FRect logoRect = {
                static_cast<float>(windowWidth) / 2.0f - logoWidth/2.0f,
                static_cast<float>(windowHeight) / 2.0f - logoHeight/2.0f,
                logoWidth,
                logoHeight
            };

            SDL_RenderTexture(renderer, logo, nullptr, &logoRect);
        }
        else  std::cout << "Logo failed to load" << std::endl;

        SDL_RenderPresent(renderer);
    }

    bool QuitRequested() {
        return quitRequested;
    }

    fs::path GetPendingProjectToOpen() {
        return pendingProjectToOpen;
    }

    void Destroy() {
        if (logo != nullptr) {
            SDL_DestroyTexture(logo);
            logo = nullptr;
        }

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