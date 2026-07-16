#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

#include "Headers/Project/ProjectManager.hpp"

#include "Headers/Editor/Editor.hpp"
#include "Headers/Runtime/RuntimeSession.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

/// This script is only used by Tilky_Engine.
/// Exported games use Tilky_Standalone.

namespace fs = std::filesystem;

namespace {
    enum class EngineMode {
        EDITOR,
        PLAY,
        RUNTIME_EDITOR
    };

    EngineMode currentMode = EngineMode::EDITOR;
    bool modeActive = false;

    bool InitEngineLogger() {
        const fs::path logPath =
            ProjectManager::GetDefaultProjectsFolder().parent_path()
            / "Logs"
            / "engine_log.txt";

        try {
            fs::create_directories(logPath.parent_path());

            const auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

            consoleSink->set_level(spdlog::level::trace);

            const auto fileSink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    logPath.string(),
                    true
                );

            fileSink->set_level(spdlog::level::trace);

            std::vector<spdlog::sink_ptr> sinks{ consoleSink, fileSink};

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
            spdlog::info(
                "Engine log path: {}",
                logPath.string()
            );

            return true;
        }
        catch (const spdlog::spdlog_ex& exception) {
            SDL_Log("Failed to initialize engine logger: %s", exception.what());

            return false;
        }
    }

    bool StartMode(const EngineMode mode) {
        bool started = false;

        switch (mode) {
            case EngineMode::EDITOR:
                Editor::Start();
                started = true;
                break;

            case EngineMode::PLAY:
                started = RuntimeSession::Start(
                    Localisation::Get("screen.title.engine"),
                    RuntimeSession::PLAY
                );

                if (started) {
                    spdlog::info("Starting the game loop");
                }
                break;

            case EngineMode::RUNTIME_EDITOR:
                started = RuntimeSession::Start(
                    Localisation::Get("screen.title.engine"),
                    RuntimeSession::EDITOR
                );

                if (started) spdlog::info("Starting the runtime editor loop");

                break;
        }

        if (!started) {
            spdlog::critical("Failed to start engine mode");
            return false;
        }

        currentMode = mode;
        modeActive = true;

        return true;
    }

    void StopCurrentMode(const bool prepareEditorLevelForRuntime) {
        if (!modeActive) return;

        switch (currentMode) {
            case EngineMode::EDITOR:
                Editor::Destroy();
                if (prepareEditorLevelForRuntime) Editor::LoadLevel(Editor::currentMap);
                break;

            case EngineMode::PLAY:
            case EngineMode::RUNTIME_EDITOR:
                RuntimeSession::Shutdown();
                break;
        }

        modeActive = false;
    }

    bool SwitchMode(const EngineMode newMode) {
        if (modeActive && currentMode == newMode) return true;

        const bool prepareEditorLevelForRuntime = currentMode == EngineMode::EDITOR && newMode != EngineMode::EDITOR;

        StopCurrentMode(prepareEditorLevelForRuntime);

        if (StartMode(newMode)) return true;

        if (newMode == EngineMode::EDITOR) return false;

        spdlog::error("Failed to start requested mode. Returning to the main editor.");

        return StartMode(EngineMode::EDITOR);
    }

    bool FindProjectArgument(const int argc, char** argv, fs::path& projectFile) {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];

            if (argument != "--project") continue;

            if (index + 1 >= argc) {
                spdlog::critical("--project requires a project file path");

                return false;
            }

            projectFile = argv[++index];
            return true;
        }

        spdlog::critical("No project provided. Use --project <path>");

        return false;
    }

    bool LoadProject(const fs::path& projectFile) {
        if (!ProjectManager::LoadProjectMetaData(projectFile)) {
            spdlog::critical("Failed to load project metadata from {}",projectFile.string());

            return false;
        }

        const std::string languageCode = ProjectManager::GetCurrentLanguageInLauncher();

        if (Localisation::LoadLanguage(languageCode)) return true;

        spdlog::error("Failed to load localisation '{}'. Falling back to English.",languageCode);

        if (!Localisation::LoadLanguage("en")) {
            spdlog::critical("Failed to load fallback English localisation");

            return false;
        }

        return true;
    }

    bool UpdateEditorMode() {
        Editor::Update();

        if (InputManager::QuitRequested() ||
            Editor::ShutdownRequested()) {
            StopCurrentMode(false);
            return false;
        }

        if (Editor::SwitchToRuntimeEditorRequested()) {
            spdlog::info("Runtime editor requested");

            return SwitchMode(EngineMode::RUNTIME_EDITOR);
        }

        if (Editor::QuitRequested()) {
            if (!Editor::PlayRequested()) {
                StopCurrentMode(false);
                return false;
            }

            return SwitchMode(EngineMode::PLAY);
        }

        return true;
    }

    bool UpdateRuntimeMode() {
        RuntimeSession::Update();

        if (!InputManager::QuitRequested()) return true;

        return SwitchMode(EngineMode::EDITOR);
    }

    bool UpdateCurrentMode() {
        switch (currentMode) {
            case EngineMode::EDITOR:return UpdateEditorMode();
            case EngineMode::PLAY:
            case EngineMode::RUNTIME_EDITOR:return UpdateRuntimeMode();
        }

        spdlog::critical("Unknown engine mode");
        return false;
    }
}

#include <SDL3/SDL_main.h>

int main(const int argc, char** argv) {
    if (!InitEngineLogger()) return 1;

    spdlog::info("Engine started");

    fs::path projectFile;

    if (!FindProjectArgument(argc, argv, projectFile)) return 1;
    if (!LoadProject(projectFile)) return 1;
    if (!StartMode(EngineMode::EDITOR)) return 1;

    while (modeActive) {
        InputManager::BeginFrame();

        if (!UpdateCurrentMode()) break;
    }


    // Covers unexpected exits where a mode is still running.
    StopCurrentMode(false);

    spdlog::info("Engine shutdown complete");
    spdlog::shutdown();

    return 0;
}