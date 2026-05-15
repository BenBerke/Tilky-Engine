//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/RuntimeSession.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/MapEditor/MapEditor.hpp"
#include "Headers/Objects/Player.hpp"
#include "Headers/Objects/Loadables.hpp"
#include "Headers/Runtime/Renderer/Renderer/Renderer.hpp"
#include "Headers/Runtime/Sound/AudioSystem.hpp"
#include "Renderer/Renderer/RendererInternal.hpp"
#include "Headers/Runtime/ScriptSystem.hpp"

namespace {
    float timer = 0;
    float timerHelper = 0;
    int fps = 0;

    void RenderDebugText() {
        Renderer::RenderTextRaw(
            "FPS:" + std::to_string(fps),
            0,
            0,
            0.5f,
            Vector3{255, 255, 255}
        );

        Renderer::RenderTextRaw(
            "NoClip:" + std::to_string(Player::noClip),
            100,
            0,
            0.5f,
            Vector3{255, 255, 255}
        );

        Renderer::RenderTextRaw(
            "CS:" + std::to_string(Player::currentSector),
            200,
            0,
            0.5f,
            Vector3{255, 255, 255}
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
            MapEditor::playerStartPos.x,
            0.0f,
            MapEditor::playerStartPos.y
        };;

        MapQueries::AssignWallsToSectors(
         level.sectors,
            level.walls
        );

        Player::Start(level.sectors);

        if (!Renderer::Initialize()) {
            spdlog::critical("Failed to initialize renderer {}", SDL_GetError());
            return false;
        }

        RendererInternal::backgroundTextureIndex = MapEditor::backgroundTextureIndex;

        if (!Renderer::CreateMap()) {
            spdlog::critical("Failed to create map");
            return false;
        }

        if (!SoundManager::InitializeOpenAL()) {
            spdlog::critical("OpenAL failed");
            return false;
        }

        AudioSystem::Start(level);
        AudioSystem::ApplyListenerSettings(level);

        if (!ScriptSystem::Initialize()) {
            spdlog::critical("Failed to initialize script system");
            return false;
        }
        ScriptSystem::Start(level);

        InputManager::SetRelativeMouseMode(Renderer::window, true);

        for (const Texture& texture : level.textures) TextureManager::CreateTexture(texture.fileName);

        level.Start();
        for (Entity& entity : level.entities) entity.Start();

        spdlog::info("Runtime Started");
        return true;
    }

    void Update() {
        timer = GameTime::time;

        Level& level = LevelManager::CurrentLevel();

        Player::Update(
            level.walls,
            level.sectors
        );

        Renderer::Update();
        AudioSystem::Update(level);

        RenderDebugText();

        SDL_GL_SwapWindow(Renderer::window);

        if (timer > timerHelper + 1.3f) {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    void Shutdown() {
        SoundManager::DestroyOpenAL();
        Renderer::Destroy();
        AudioSystem::Shutdown(LevelManager::CurrentLevel());
    }
}
