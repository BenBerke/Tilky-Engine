#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include "Headers/Editor/Editor.hpp"

#include "../../Headers/Runtime/Gameplay/PlayerControllerSystem.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "Headers/Runtime/RuntimeSession.hpp"

/// This script is only used in the engine.
/// For running exported games, Standalone.exe is used

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

namespace fs = std::filesystem;

enum Mode {
    EDITOR,
    PLAY,
    RUNTIME_EDITOR,
};

static Mode currentMode = EDITOR;

static void DestroyModes() {
    switch (currentMode) {
        case EDITOR:
            Editor::Destroy();
            if (Editor::ShutdownRequested()) break;
            Editor::LoadLevel(Editor::currentMap);
            break;
        case PLAY:
            RuntimeSession::Shutdown(RuntimeSession::PLAY);
            break;
            case RUNTIME_EDITOR:
            RuntimeSession::Shutdown(RuntimeSession::EDITOR);
            break;
    }
}

static void SwitchModes(const Mode mode) {
    currentMode = mode;
    switch (mode) {
        case EDITOR:
            Editor::Start();
            break;
        case PLAY:
            if(!RuntimeSession::Start(Localisation::Get("screen.title.engine"), RuntimeSession::PLAY)) {
                spdlog::critical("Failed to start the session");
                return;
            }
            spdlog::info("Starting the game loop");
            break;
        case RUNTIME_EDITOR:
            if(!RuntimeSession::Start(Localisation::Get("screen.title.engine"), RuntimeSession::EDITOR)) {
                spdlog::critical("Failed to start the session");
                return;
            }
            spdlog::info("Starting the runtime editor loop");
    }
}

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

        const auto logger = std::make_shared<spdlog::logger>(
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
        SDL_Log("Failed to initialize engine logger %s", e.what());
        return false;
    }
}

static void RealTimeEditorUpdate() {
    RuntimeSession::Update(RuntimeSession::EDITOR);
}

static void EngineUpdate() {
    RuntimeSession::Update(RuntimeSession::PLAY);
}

static void EditorUpdate() {
    Editor::Update();
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
    SwitchModes(currentMode);

    bool quit = false;
    while (!quit) {
        InputManager::BeginFrame();
        quit = InputManager::QuitRequested() && currentMode == EDITOR;
        switch (currentMode) {
            case EDITOR:
                EditorUpdate();
                if (Editor::ShutdownRequested()) return 0;
                if (Editor::QuitRequested()) {
                    DestroyModes();
                    if (Editor::PlayRequested()) SwitchModes(PLAY);
                }
                if (Editor::SwitchToRuntimeEditorRequested()) {
                    DestroyModes();
                    SwitchModes(RUNTIME_EDITOR);
                    spdlog::info("SwitchToRuntimeEditorRequested");
                }
                break;
            case PLAY:
                EngineUpdate();
                if (InputManager::QuitRequested()) {
                    DestroyModes();
                    SwitchModes(EDITOR);
                }
                break;
            case RUNTIME_EDITOR:
                RealTimeEditorUpdate();
                if (InputManager::QuitRequested()) {
                    DestroyModes();
                    SwitchModes(EDITOR);
                }
                break;
        }
    }
    return 0;
}