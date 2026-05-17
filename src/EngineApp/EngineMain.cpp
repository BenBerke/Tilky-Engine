#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include "Headers/Editor/Editor.hpp"

#include "Headers/Objects/Player.hpp"
#include "../Runtime/Renderer/Renderer/RendererInternal.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "Headers/Runtime/RuntimeSession.hpp"

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

namespace fs = std::filesystem;

static void LaunchEditor();
static void LaunchEngine();

static bool InitEngineLogger() {
    const fs::path logPath =
        ProjectManager::GetDefaultProjectsFolder().parent_path()
        / "Logs"
        / "engine_log.txt";

    try {
        fs::create_directories(logPath.parent_path());

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::trace);

        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logPath.string(),
            true
        );
        fileSink->set_level(spdlog::level::trace);

        std::vector<spdlog::sink_ptr> sinks {
            consoleSink,
            fileSink
        };

        auto logger = std::make_shared<spdlog::logger>(
            "Engine",
            sinks.begin(),
            sinks.end()
        );

        logger->set_level(spdlog::level::trace);

        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);

        spdlog::info("Engine logger initialized");
        spdlog::info("Engine log path: {}", logPath.string());

        return true;
    }
    catch (const spdlog::spdlog_ex& e) {
        SDL_Log("Failed to initalize engine logger %s", e.what());
        return false;
    }
}

static void LaunchEngine() {
    if(!RuntimeSession::Start()) {
        spdlog::critical("Failed to start the session");
        return;
    }

    spdlog::info("Starting the game loop");
    bool running = true;
    while (running) {
        InputManager::BeginFrame();
        GameTime::Update();
        RuntimeSession::Update();

        running = !(InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE) || InputManager::QuitRequested());
    }
    spdlog::info("Finished the game loop");

    RuntimeSession::Shutdown();
    LaunchEditor();
}

static void LaunchEditor() {
    Editor::Start();
    while (!Editor::QuitRequested()) {
        InputManager::BeginFrame();
        Editor::Update();
    }
    const bool shutdown = Editor::ShutdownRequested();
    Editor::Destroy();

    if (shutdown) return;

    Editor::LoadLevel(Editor::currentMap);
    LaunchEngine();
}

int main(int argc, char** argv) {
    InitEngineLogger();

    spdlog::info("Engine started");

    fs::path projectFile;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--project" && i + 1 < argc) {
            projectFile = argv[i + 1];
        }
    }
    if (projectFile.empty()) {
        spdlog::critical("No project found");
        return 1;
    }

    if (!ProjectManager::LoadProjectMetaData(projectFile)) {
        spdlog::critical("Failed to load project metadata from {}", projectFile.string());
        return 1;
    }

    const std::string langCode = ProjectManager::GetCurrentLanguageInLauncher();

    if (!Localisation::LoadLanguage(langCode)) {
        spdlog::error("Failed to load localisation {}. Falling back to english", langCode);

        if (!Localisation::LoadLanguage("en")) {
            spdlog::critical("Failed to fall back to english");
            return 1;
        }
    }


    //todo: Proper Game Initilization
    LaunchEditor();

    return 0;
}