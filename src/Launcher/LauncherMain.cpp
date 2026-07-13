//
// Created by berke on 5/3/2026.
//

#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <chrono>

#include "Headers/Launcher/LauncherApp.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

bool WriteLauncherVariablesJson(const json &launcherVariablesData) {
    const fs::path launcherVarsPath = ProjectManager::GetLauncherVariables();

    try {
        fs::create_directories(launcherVarsPath.parent_path());
    } catch (std::exception &e) {
        spdlog::critical("Could not create launcher variables folder {}", e.what());
        return false;
    }

    std::ofstream file(launcherVarsPath);

    if (!file.is_open()) {
        spdlog::critical("Failed to open launcher variables file for writing");
        return false;
    }

    file << launcherVariablesData.dump(4);
    file.close();

    return true;
}

int main() {
    const fs::path projectsPath = ProjectManager::GetDefaultProjectsFolder();

    fs::path logPath = projectsPath.parent_path() / "Logs" / "launcher_log.txt";

    try {
        fs::create_directories(logPath.parent_path());

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::trace);

        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);

        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
        auto logger = std::make_shared<spdlog::logger>("Launcher", sinks.begin(), sinks.end());

        spdlog::set_default_logger(logger);

        spdlog::flush_on(spdlog::level::warn);
    } catch (const spdlog::spdlog_ex &ex) {
        std::cerr << ex.what() << std::endl;
    }

    spdlog::info("Engine started");

    try {
        fs::create_directories(projectsPath);
        spdlog::info("Projects path already exists: {}", projectsPath.string());
    } catch (std::exception &e) {
        spdlog::critical("Failed to create projects directory {}", e.what());
        return 1;
    }

    // Editor Variables JSON
    const fs::path launcherVarsPath = ProjectManager::GetLauncherVariables();

    try {
        fs::create_directories(launcherVarsPath.parent_path());
    } catch (std::exception &e) {
        spdlog::critical("Could not create Launcher.tilky {}", e.what());
        return 1;
    }

    std::ifstream launcherVars(launcherVarsPath);
    json launcherVariablesData;

    if (launcherVars.is_open()) {
        try {
            launcherVars >> launcherVariablesData;
        } catch (std::exception &e) {
            spdlog::warn("Launcher variables could not load. Falling back to default values {}", e.what());

            launcherVariablesData = {
                {"lang", "en"},
                {"projectCount", 0}
            };
        }

        launcherVars.close();
    } else {
        launcherVariablesData = {
            {"lang", "en"},
            {"projectCount", 0}
        };

        std::ofstream varFile(launcherVarsPath);

        if (varFile.is_open()) {
            varFile << launcherVariablesData.dump(4);
            varFile.close();

            std::cout << "Created launcher vars at: " << launcherVarsPath << std::endl;
        } else {
            spdlog::critical("Failed to create launcher variables at: {}", launcherVarsPath.string());
        }
    }

    LauncherApp::Start(launcherVariablesData["lang"]);
    spdlog::info("Starting the launcher app");

    while (!LauncherApp::QuitRequested()) LauncherApp::Update();

    spdlog::info("Quitting the launcher app. Starting the renderer");

    const fs::path projectToOpen = LauncherApp::GetPendingProjectToOpen();

    launcherVariablesData["lang"] = Localisation::CurrentLanguage();
    WriteLauncherVariablesJson(launcherVariablesData);

    if (!projectToOpen.empty()) {
        ProjectManager::OpenProject(projectToOpen);
    } else spdlog::error("No project is found at {}", projectToOpen.string());

    LauncherApp::Destroy();
    // CreateProjectDirectory(projectName);

    return 0;
}
