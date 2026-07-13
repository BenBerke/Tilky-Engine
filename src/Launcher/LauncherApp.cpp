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
#include <spdlog/spdlog.h>

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

    constexpr int DEFAULT_LAUNCHER_WIDTH  = 1080;
    constexpr int DEFAULT_LAUNCHER_HEIGHT = 960;

    constexpr int MIN_LAUNCHER_WIDTH  = 640;
    constexpr int MIN_LAUNCHER_HEIGHT = 480;

    // Keep space for titlebar/borders/taskbar and avoid spawning edge-to-edge.
    constexpr int LAUNCHER_WINDOW_MARGIN = 80;

    void ClampLauncherWindowSizeToDisplay() {
        windowWidth  = DEFAULT_LAUNCHER_WIDTH;
        windowHeight = DEFAULT_LAUNCHER_HEIGHT;

        const SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display == 0) {
            spdlog::warn("Failed to get primary display: {}", SDL_GetError());
            return;
        }

        SDL_Rect usableBounds{};
        if (!SDL_GetDisplayUsableBounds(display, &usableBounds)) {
            spdlog::warn("Failed to get display usable bounds: {}", SDL_GetError());
            return;
        }

        const int maxWidth = std::max(
            MIN_LAUNCHER_WIDTH,
            usableBounds.w - LAUNCHER_WINDOW_MARGIN
        );

        const int maxHeight = std::max(
            MIN_LAUNCHER_HEIGHT,
            usableBounds.h - LAUNCHER_WINDOW_MARGIN
        );

        windowWidth = std::clamp(
            DEFAULT_LAUNCHER_WIDTH,
            MIN_LAUNCHER_WIDTH,
            maxWidth
        );

        windowHeight = std::clamp(
            DEFAULT_LAUNCHER_HEIGHT,
            MIN_LAUNCHER_HEIGHT,
            maxHeight
        );
    }

    void CenterLauncherWindowOnDisplay() {
        const SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display == 0) {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            return;
        }

        SDL_Rect usableBounds{};
        if (!SDL_GetDisplayUsableBounds(display, &usableBounds)) {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            return;
        }

        const int x = usableBounds.x + (usableBounds.w - windowWidth) / 2;
        const int y = usableBounds.y + (usableBounds.h - windowHeight) / 2;

        SDL_SetWindowPosition(window, x, y);
    }

    std::map<std::string, std::string> languages;

    bool CopyLuaAutocompleteFiles(const fs::path& projectPath) {
        const fs::path autocompleteFolder =
            ProjectManager::FindAssetPath(fs::path("EngineAssets") / "AutoComplete");

        if (!fs::exists(autocompleteFolder)) {
            spdlog::error(
                "AutoComplete folder does not exist: {}",
                autocompleteFolder.string()
            );
            return false;
        }

        const fs::path sourceApiFile = autocompleteFolder / "tilky_api.txt";
        const fs::path sourceLuarcFile = autocompleteFolder / "luarc.txt";

        if (!fs::exists(sourceApiFile)) {
            spdlog::error(
                "Lua autocomplete API file does not exist: {}",
                sourceApiFile.string()
            );
            return false;
        }

        if (!fs::exists(sourceLuarcFile)) {
            spdlog::error(
                "Lua autocomplete config file does not exist: {}",
                sourceLuarcFile.string()
            );
            return false;
        }

        const fs::path tilkyFolder = projectPath / ".tilky";
        fs::create_directories(tilkyFolder);

        const fs::path destinationApiFile = tilkyFolder / "tilky_api.lua";
        const fs::path destinationLuarcFile = projectPath / ".luarc.json";

        try {
            fs::copy_file(
                sourceApiFile,
                destinationApiFile,
                fs::copy_options::overwrite_existing
            );

            fs::copy_file(
                sourceLuarcFile,
                destinationLuarcFile,
                fs::copy_options::overwrite_existing
            );
        }
        catch (const std::exception& e) {
            spdlog::error(
                "Failed to copy Lua autocomplete files to project '{}': {}",
                projectPath.string(),
                e.what()
            );
            return false;
        }

        spdlog::info(
            "Copied Lua autocomplete files to project: {}",
            projectPath.string()
        );

        return true;
    }

    bool RefreshLanguages() {
        languages.clear();

        const fs::path languagesPath =
            ProjectManager::FindAssetPath(fs::path("EngineAssets") / "Local");

        if (!fs::exists(languagesPath)) {
            spdlog::critical("Languages folder does not exists at {}", languagesPath.string());
            return false;
        }

        for (const auto& entry : fs::directory_iterator(languagesPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != ".json") {
                continue;
            }

            const std::string languageCode =
                entry.path().stem().string();

            std::string displayName = languageCode;

            try {
                std::ifstream file(entry.path());

                if (file.is_open()) {
                    json languageJson;
                    file >> languageJson;

                    displayName = languageJson.value("language.name", languageCode);
                }
            }
            catch (const std::exception& e) {
                spdlog::error("Failed to read language file: {} {}", entry.path().string(), e.what());
            }

            languages[languageCode] = displayName;
        }

        return true;
    }

    void PutSpace(const int n) {
        for (int i = 0; i < n; i++) {
            ImGui::Spacing();
        }
    }
}

namespace LauncherApp {
    void Start(const std::string& langCode) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            spdlog::critical("SDL_Init failed: {}", SDL_GetError());
            return;
        }

        ClampLauncherWindowSizeToDisplay();

        if (!SDL_CreateWindowAndRenderer(
            "Tilky_Engine Launcher",
            windowWidth,
            windowHeight,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer
        )) {
            spdlog::critical("Failed to create window or renderer for launcher: {}", SDL_GetError());
            SDL_Quit();
            return;
        }

        CenterLauncherWindowOnDisplay();

        const fs::path basePath = ProjectManager::GetEngineBasePath();

        // Window icon
        const fs::path iconPath = basePath / "LauncherAssets" / "Fox.png";

        std::cout << "Loading window icon from: " << iconPath << std::endl;

        if (!fs::exists(iconPath)) spdlog::error("Icon file does not exist at {}", iconPath.string());
        else {
            SDL_Surface* windowIcon = IMG_Load(iconPath.string().c_str());

            if (windowIcon == nullptr) spdlog::error("Failed to load window icon: {}", SDL_GetError());
            else {
                if (!SDL_SetWindowIcon(window, windowIcon))
                    spdlog::error("Failed to set launcher window icon, {}", SDL_GetError());

                SDL_DestroySurface(windowIcon);
            }
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        const fs::path fontPath = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

        spdlog::info("Loading ImGui font from: {}", fontPath.string());

        io.Fonts->AddFontFromFileTTF(
            fontPath.string().c_str(),
            18.0f
        );

        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        Localisation::LoadLanguage(langCode);
        RefreshLanguages();

        // Background/logo texture
        const fs::path logoPath = basePath / "LauncherAssets" / "LogoWithWhiteText.png";

        if (!fs::exists(logoPath)) spdlog::error("Background logo does not exists at: {}", logoPath.string());
        else {
            logo = IMG_LoadTexture(renderer, logoPath.string().c_str());

            if (logo == nullptr) spdlog::error("Failed to load logo {}", SDL_GetError());
            else {
                SDL_SetTextureBlendMode(logo, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(logo, 145);
            }
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
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(windowWidth) * .25f, static_cast<float>(windowHeight) * .8f), ImGuiCond_Always);
            ImGui::Begin(Localisation::Get("launcher.create_project").c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

            ImGui::InputText(Localisation::Get("launcher.input_name").c_str(), projectName.data(), projectName.size());

            if (ImGui::Button(Localisation::Get("launcher.create").c_str())) {
                const std::string newProjectName = projectName.data();

                ProjectManager::CreateProjectDirectory(newProjectName);

                const fs::path projectPath =
                    ProjectManager::GetDefaultProjectsFolder() / newProjectName;

                CopyLuaAutocompleteFiles(projectPath);

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

        //region Languages

        if (languages.empty()) {
            RefreshLanguages();
        }

        const ImGuiStyle& style = ImGui::GetStyle();

        float totalLanguageButtonWidth = 0.0f;

        for (const auto& [languageCode, displayName] : languages) {
            const float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
            const float buttonWidth = textWidth + style.FramePadding.x * 2.0f;

            totalLanguageButtonWidth += buttonWidth;
            totalLanguageButtonWidth += style.ItemSpacing.x;
        }

        if (!languages.empty()) {
            totalLanguageButtonWidth -= style.ItemSpacing.x;
        }

        const float languageStartX =
            std::max(10.0f, static_cast<float>(windowWidth) - totalLanguageButtonWidth - 10.0f);

        ImGui::SetCursorPosX(languageStartX);

        int languageIndex = 0;

        for (const auto& [languageCode, displayName] : languages) {
            ImGui::PushID(languageCode.c_str());

            const bool isCurrentLanguage =
                Localisation::CurrentLanguage() == languageCode;

            if (isCurrentLanguage) {
                ImGui::BeginDisabled();
            }

            if (ImGui::Button(displayName.c_str())) {
                if (Localisation::LoadLanguage(languageCode)) {
                    spdlog::info("Changed language to {}", languageCode);
                }
                else {
                    spdlog::error("Failed to change language to {}", languageCode);
                }
            }

            if (isCurrentLanguage) {
                ImGui::EndDisabled();
            }

            ImGui::PopID();

            ++languageIndex;

            if (languageIndex < static_cast<int>(languages.size())) {
                ImGui::SameLine();
            }
        }

        //endregion

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
        else  spdlog::error("Logo failed to load");

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