//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/RuntimeSession.hpp"

#include <memory>
#include <string>

#include <SDL3/SDL_error.h>
#include <spdlog/spdlog.h>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Editor/Editor.hpp"
#include "Headers/Objects/Player.hpp"
#include "Headers/Runtime/Sound/AudioSystem.hpp"
#include "Headers/Runtime/ScriptSystem.hpp"

#include "Headers/Runtime/Renderer/IRenderer.hpp"
#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

namespace {
    float timer = 0.0f;
    float timerHelper = 0.0f;
    int fps = 0;

    std::unique_ptr<OpenGL> renderer;

    bool relativeMouseMode = true;

    void RenderDebugText() {
        renderer->RenderTextRaw(
            "FPS:" + std::to_string(fps),
            {0.0f, 0.0f,},
            {.5f, .5f},
            Vector3{255.0f, 255.0f, 255.0f}
        );
        renderer->RenderTextRaw(
            "NoClip:" + std::to_string(Player::noClip),
            {210.0f, 0.0f,},
            {.5f, .5f},
            Vector3{255.0f, 255.0f, 255.0f}
        );
        renderer->RenderTextRaw(
            "CS:" + std::to_string(Player::currentSector),
            {400.0f, 0.0f,},
            {.5f, .5f},
            Vector3{255.0f, 255.0f, 255.0f}
        );
    }
}

namespace RuntimeSession {
    bool Start() {
        if (!LevelManager::HasCurrentLevel()) {
            spdlog::critical("No current level loaded");
            return false;
        }

        Level& level = LevelManager::CurrentLevel();

        Player::position = {
            Editor::playerStartPos.x,
            0.0f,
            Editor::playerStartPos.y
        };

        MapQueries::AssignWallsToSectors(
            level.sectors,
            level.walls
        );

        Player::Start(level.sectors);

        renderer = std::make_unique<OpenGL>();

        if (!renderer->Initialize()) {
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

        level.Start();

        for (Entity& entity : level.entities) {
            entity.Start();
        }

        spdlog::info("Runtime started using {} renderer", renderer->GetName());
        return true;
    }

    void Update() {
        if (!renderer) {
            return;
        }

        GameTime::Update();
        timer = GameTime::time;

        if (InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE) && relativeMouseMode) {
            relativeMouseMode = false;
            InputManager::SetRelativeMouseMode(renderer->GetWindow(), false);
            SDL_ShowCursor();
        }
        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) && !relativeMouseMode) {
            relativeMouseMode = true;
            InputManager::SetRelativeMouseMode(renderer->GetWindow(), true);
            SDL_HideCursor();
        }

        Level& level = LevelManager::CurrentLevel();

        if (relativeMouseMode) Player::Update(level.walls,level.sectors);

        renderer->BeginFrame();
        renderer->Update();

        AudioSystem::Update(level);
        ScriptSystem::Update(level);

        RenderDebugText();

        renderer->EndFrame();

        if (timer > timerHelper + 1.3f) {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    void Shutdown() {
        if (LevelManager::HasCurrentLevel()) {
            AudioSystem::Shutdown(LevelManager::CurrentLevel());
        }

        SoundManager::DestroyOpenAL();

        if (renderer) {
            renderer->Shutdown();
            renderer.reset();
        }
    }
}