//
// Created by berke on 5/3/2026.
//

#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <spdlog/sinks/basic_file_sink.h>

#include "Headers/Launcher/LauncherApp.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
// #include "spdlog/sinks/rotating_file_sink.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    // Central definition of what a fresh launcher-variables file looks like, used both
    // when the file is missing and when it fails to parse. Keeping this in one place
    // means the two fallback paths in main() can never quietly drift apart.
    json DefaultLauncherVariables() {
        return {
            {"lang", "en"},
            {"projectCount", 0},
            {"window", {{"width", 1080}, {"height", 960}}},
            {"sortMode", "recent"},
            {"importedProjects", json::array()},
            {"projectMeta", json::object()}
        };
    }
}

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

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[]) {
    const fs::path projectsPath = ProjectManager::GetDefaultProjectsFolder();

    fs::path logPath = projectsPath.parent_path() / "Logs" / "launcher_log.txt";

    try {
        fs::create_directories(logPath.parent_path());

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::trace);

        // Rotating file sink instead of a truncating one: each launch starts a fresh
        // launcher_log.txt (rotate_on_open = true) but the last few runs are kept as
        // launcher_log.1.txt, launcher_log.2.txt, etc., so a crash can still be diagnosed
        // after the fact instead of being wiped by the very next launch.
        // constexpr size_t maxLogFileSize = 5 * 1024 * 1024; // 5 MB
        // constexpr size_t maxLogFileCount = 3;
        // auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        //     logPath.string(), maxLogFileSize, maxLogFileCount, /*rotate_on_open=*/true);

        // BASIC SINK FILE BECAUSE WE DONT NEED ROTATING RIGHT NOW
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(),true);

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
        spdlog::info("Projects folder ready at: {}", projectsPath.string());
    } catch (std::exception &e) {
        spdlog::critical("Failed to create projects directory {}", e.what());
        return 1;
    }

    // Launcher variables JSON (language, window geometry, sort mode, imported projects,
    // and per-project pin/last-opened metadata all live in this one file).
    const fs::path launcherVarsPath = ProjectManager::GetLauncherVariables();

    try {
        fs::create_directories(launcherVarsPath.parent_path());
    } catch (std::exception &e) {
        spdlog::critical("Could not create launcher variables folder {}", e.what());
        return 1;
    }

    json launcherVariablesData;

    std::ifstream launcherVars(launcherVarsPath);

    if (launcherVars.is_open()) {
        try {
            launcherVars >> launcherVariablesData;
        } catch (std::exception &e) {
            spdlog::warn("Launcher variables could not load. Falling back to default values {}", e.what());
            launcherVariablesData = DefaultLauncherVariables();
        }

        launcherVars.close();
    } else {
        launcherVariablesData = DefaultLauncherVariables();

        std::ofstream varFile(launcherVarsPath);

        if (varFile.is_open()) {
            varFile << launcherVariablesData.dump(4);
            varFile.close();

            spdlog::info("Created launcher vars at: {}", launcherVarsPath.string());
        } else {
            spdlog::critical("Failed to create launcher variables at: {}", launcherVarsPath.string());
        }
    }

    int exitCode = 0;

    LauncherApp::Destroy();

    // Everything below can touch the filesystem, SDL, or a scripted project file, so it's
    // wrapped in one top-level handler: an uncaught exception now gets logged (to the file
    // sink too, not just a console window that vanishes on a GUI app) instead of silently
    // crashing with no diagnostic trail.
    try {
        LauncherApp::Start(launcherVariablesData);
        spdlog::info("Starting the launcher app");

        while (!LauncherApp::QuitRequested()) {
            LauncherApp::Update();
        }

        spdlog::info("Quitting the launcher app");

        const fs::path projectToOpen = LauncherApp::GetPendingProjectToOpen();

        launcherVariablesData["lang"] = Localisation::CurrentLanguage();

        if (!WriteLauncherVariablesJson(launcherVariablesData)) {
            spdlog::warn("Failed to persist launcher variables on exit");
        }

        if (!projectToOpen.empty()) {
            spdlog::info("Opening project at {}", projectToOpen.string());
            ProjectManager::OpenProject(projectToOpen);
        } else {
            spdlog::info("Launcher closed without selecting a project");
        }
    }
    catch (const std::exception &e) {
        spdlog::critical("Unhandled exception in launcher: {}", e.what());
        exitCode = 1;
    }
    catch (...) {
        spdlog::critical("Unhandled non-standard exception in launcher");
        exitCode = 1;
    }

    return exitCode;
}