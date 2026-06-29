//
// Created by berke on 5/14/2026.
//

/// This script manages everything that happens after opening a project.
/// The editor and the runtime renderer.
/// This script does not run any logic. It simply starts, updates and ends the necessary functions.

#include "Headers/Runtime/RuntimeSession.hpp"

#include <memory>
#include <string>

#include <SDL3/SDL_error.h>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <imgui.h>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"

#include "Headers/Editor/Editor.hpp"

#include "Headers/Runtime/Sound/AudioSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp"

#include "Headers/Runtime/Renderer/IRenderer.hpp"
#include "Headers/Runtime/Renderer/RendererFactory.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

namespace {
    float timer = 0.0f;
    float timerHelper = 0.0f;
    int fps = 0;

    std::unique_ptr<IRenderer> renderer;

    std::unique_ptr<Level> editorLevelSnapshot;

    bool relativeMouseMode = true;

    void RenderDebugText() {
        renderer->RenderTextRaw(
            "FPS:" + std::to_string(fps),
            {0.0f, 20},
            {0.5f, 0.5f},
            Vector3{255.0f, 0.0f, 255.0f}
        );
    }
}

namespace RuntimeSession {
    bool Start(const std::string& windowName, const RuntimeType runtimeType) {
        if (!LevelManager::HasCurrentLevel()) {
            spdlog::critical("No current level loaded");
            return false;
        }

        Level& level = LevelManager::CurrentLevel();

        relativeMouseMode = true;

        if (runtimeType == PLAY) {
            editorLevelSnapshot = std::make_unique<Level>(level);
            spdlog::info("Runtime level snapshot created");
        }

        MapQueries::RebuildSectorRuntimeLinks(level);

        //todo try vulkan first
        renderer = RendererFactory::CreateRenderer(RendererBackend::OPENGL);

        if (renderer == nullptr) {
            spdlog::critical("Failed to create renderer");
            return false;
        }

        renderer->SetUseEditorCamera(runtimeType == EDITOR);

        if (!renderer->Initialize(windowName)) {
            spdlog::critical(
                "Failed to initialize {} renderer: {}",
                renderer->GetName(),
                SDL_GetError()
            );

            renderer.reset();
            return false;
        }

        if (!renderer->CreateMap()) {
            spdlog::critical("Failed to create map");

            renderer->Shutdown();
            renderer.reset();

            return false;
        }

        if (runtimeType == EDITOR) RuntimeEditor::Start(level, *renderer);
        else if(runtimeType == PLAY || runtimeType == STANDALONE) {
            if (!SoundManager::InitializeOpenAL()) {
                spdlog::critical("OpenAL failed");

                renderer->Shutdown();
                renderer.reset();

                return false;
            }

            AudioSystem::Start(level);

            LevelSystem::Start(level);

            AudioSystem::ApplyListenerSettings(level);
        }

        InputManager::SetRelativeMouseMode(renderer->GetWindow(), true);
        SDL_HideCursor();

        spdlog::info("Runtime started using {} renderer", renderer->GetName());
        return true;
    }

    void Update(const RuntimeType runtimeType) {
        if (!renderer) return;

        ZoneScoped;

        GameTime::Update();
        timer = GameTime::time;

        Level& level = LevelManager::CurrentLevel();

        const ImGuiIO& io = ImGui::GetIO();
        const bool mouseBlockedByImGui = io.WantCaptureMouse;
        const bool keyboardBlockedByImGui = io.WantCaptureKeyboard;

        if (runtimeType == PLAY || runtimeType == EDITOR) {
            if (InputManager::GetKeyDown(SDL_SCANCODE_TAB) && relativeMouseMode) [[unlikely]] {
                relativeMouseMode = false;

                InputManager::SetRelativeMouseMode(renderer->GetWindow(), false);
                SDL_ShowCursor();
            }

            const bool mouseClickAllowed = runtimeType == PLAY || !mouseBlockedByImGui;

            if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) &&!relativeMouseMode && mouseClickAllowed) [[unlikely]] {
                relativeMouseMode = true;

                InputManager::SetRelativeMouseMode(renderer->GetWindow(), true);
                SDL_HideCursor();
            }

            EditorFunctions::UpdateConsole(GameTime::deltaTime);
        }

        if (runtimeType == EDITOR) {
            RuntimeEditor::Update(
                level,
                *renderer,
                relativeMouseMode,
                mouseBlockedByImGui,
                keyboardBlockedByImGui,
                renderer->screenWidth,
                renderer->screenHeight
            );
        }

        {
            ZoneScopedN("Level");

            if (runtimeType == STANDALONE) LevelSystem::Update(level);
            else if (runtimeType == PLAY && relativeMouseMode) LevelSystem::Update(level);
        }

        {
            ZoneScopedN("Renderer");

            renderer->BeginFrame();

            //todo add proper debug view enadble/disable
            renderer->Update(runtimeType == PLAY || runtimeType == EDITOR);

            renderer->BeginImGuiFrame();

            if (runtimeType == EDITOR) RuntimeEditor::Draw(level);

            renderer->EndImGuiFrame();

            if (runtimeType != STANDALONE) {
                RenderDebugText();
                EditorFunctions::RenderConsole(renderer.get());
            }

            renderer->EndFrame();
        }

        {
            ZoneScopedN("Audio");
            AudioSystem::Update(level);
        }


        if (timer > timerHelper + 1.3f) [[unlikely]] {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    void Shutdown(const RuntimeType runtimeType) {
        Level& level = LevelManager::CurrentLevel();

        SoundManager::DestroyOpenAL();

        if (runtimeType == PLAY) {
            if (editorLevelSnapshot) {
                LevelManager::CurrentLevel() = *editorLevelSnapshot;
                editorLevelSnapshot.reset();

                spdlog::info("Runtime ended. Level restored to editor snapshot");
            }
            else spdlog::error("Couldn't find editor level snapshot");
        }

        if (runtimeType == PLAY || runtimeType == STANDALONE) LevelSystem::Shutdown(level);

        if (runtimeType == EDITOR) RuntimeEditor::Shutdown(level);

        AudioSystem::Shutdown(level);

        if (renderer) {
            renderer->Shutdown();
            renderer.reset();
        }
    }
}