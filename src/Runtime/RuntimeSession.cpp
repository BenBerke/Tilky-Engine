//
// Created by berke on 5/14/2026.

/// This script manages everything that happens after opening a project. The editor and the runtime renderer
/// This script does not run any logic. It simply starts, updates and ends the necessary functions

#include "Headers/Runtime/RuntimeSession.hpp"

#include <memory>
#include <string>

#include <SDL3/SDL_error.h>
#include <spdlog/spdlog.h>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Editor/Editor.hpp"
#include "Headers/Runtime/Sound/AudioSystem.hpp"
#include "Headers/Runtime/ScriptSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include <tracy/Tracy.hpp>

#include "Headers/Map/MapQueries.hpp"

namespace {
    float timer = 0.0f;
    float timerHelper = 0.0f;
    int fps = 0;

    std::unique_ptr<OpenGL> renderer;

    std::unique_ptr<Level> editorLevelSnapshot;

    bool relativeMouseMode = true;

    void RenderDebugText() {
        renderer->RenderTextRaw(
            "FPS:" + std::to_string(fps),
            {0.0f, static_cast<float>(OpenGLRendererInternal::screenHeight - 7)},
            {.5f, .5f},
             Vector3{255.0f, .0f, 255.0f}
        );
    }
}

namespace RuntimeSession {
    bool Start(const std::string &windowName) {
        if (!LevelManager::HasCurrentLevel()) {
            spdlog::critical("No current level loaded");
            return false;
        }

        Level& level = LevelManager::CurrentLevel();
        editorLevelSnapshot = std::make_unique<Level>(level);
        spdlog::info("Runtime level snapshot created");

        MapQueries::AssignWallsToSectors(
            level.sectors,
            level.walls
        );

        MapQueries::AssignNeighborsToSectors(level.sectors);

        renderer = std::make_unique<OpenGL>();

        if (!renderer->Initialize(windowName)) {
            spdlog::critical("Failed to initialize {} renderer: {}", renderer->GetName(), SDL_GetError());
            renderer.reset();
            return false;
        }

        if (!renderer->CreateMap()) {
            spdlog::critical("Failed to create map");
            renderer->Shutdown();
            renderer.reset();
            return false;
        }

        if (!SoundManager::InitializeOpenAL()) {
            spdlog::critical("OpenAL failed");
            renderer->Shutdown();
            renderer.reset();
            return false;
        }

        AudioSystem::Start(level);
        AudioSystem::ApplyListenerSettings(level);

        if (!ScriptSystem::Initialize()) {
            spdlog::critical("Failed to initialize script system");
            return false;
        }

        ScriptSystem::Start(level);

        InputManager::SetRelativeMouseMode(renderer->GetWindow(), true);

        LevelSystem::Start(level);

        spdlog::info("Runtime started using {} renderer", renderer->GetName());
        return true;
    }

    void Update() {
        if (!renderer) {
            return;
        }

        ZoneScoped;

        GameTime::Update();
        timer = GameTime::time;

        if (InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE) && relativeMouseMode) [[unlikely]] {
            relativeMouseMode = false;
            InputManager::SetRelativeMouseMode(renderer->GetWindow(), false);
            SDL_ShowCursor();
        }
        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) && !relativeMouseMode) [[unlikely]] {
            relativeMouseMode = true;
            InputManager::SetRelativeMouseMode(renderer->GetWindow(), true);
            SDL_HideCursor();
        }

        Level& level = LevelManager::CurrentLevel();

        {
            ZoneScopedN("Renderer");
            renderer->BeginFrame();
            renderer->Update();
            RenderDebugText();
            renderer->EndFrame();
        }

        {
            ZoneScopedN("Audio");
            AudioSystem::Update(level);
        }

        {
            ZoneScopedN("Scripts");
            ScriptSystem::Update(level);
        }

        {
            ZoneScopedN("Level");
            if (relativeMouseMode) LevelSystem::Update(level);
        }

        if (timer > timerHelper + 1.3f) [[unlikely]] {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    void Shutdown() {
        if (LevelManager::HasCurrentLevel()) {
            AudioSystem::Shutdown(LevelManager::CurrentLevel());
        }

        SoundManager::DestroyOpenAL();

        if (editorLevelSnapshot) {
            LevelManager::CurrentLevel() = *editorLevelSnapshot;
            editorLevelSnapshot.reset();

            spdlog::info("Runtime ended. Level restored to editor snapshot");
        }
        else {
            spdlog::error("Couldn't find editor level snapshot");
        }

        if (renderer) {
            renderer->Shutdown();
            renderer.reset();
        }
    }
}