//
// Created by berke on 5/14/2026.
//

/// Manages everything that happens after opening a project.
/// Starts, updates, and shuts down the editor or game runtime systems.

#include "Headers/Runtime/RuntimeSession.hpp"

#include <memory>
#include <string>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#ifndef TILKY_STANDALONE
#include <imgui.h>
#endif

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"

#include "Headers/Runtime/LevelSystem.hpp"
#include "Headers/Runtime/Sound/AudioSystem.hpp"

#include "Headers/Runtime/Renderer/IRenderer.hpp"
#include "Headers/Runtime/Renderer/RendererFactory.hpp"

#ifndef TILKY_STANDALONE
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"
#include "Headers/Runtime/RuntimeEditor/RuntimeEditor.hpp"
#endif

namespace {
#ifdef TILKY_STANDALONE
    constexpr RuntimeSession::RuntimeType activeRuntimeType = RuntimeSession::STANDALONE;
#else
    RuntimeSession::RuntimeType activeRuntimeType = RuntimeSession::EDITOR;
#endif

    std::unique_ptr<IRenderer> renderer;

#ifndef TILKY_STANDALONE
    std::unique_ptr<Level> editorLevelSnapshot;

    bool relativeMouseMode = true;

    float fpsTimer = 0.0f;
    int fps = 0;
#endif

    bool StartRenderer(const std::string& windowName, const bool useEditorCamera) {
        // TODO: Try Vulkan first when Vulkan support is ready.
        renderer = RendererFactory::CreateRenderer(RendererBackend::OPENGL);

        if (!renderer) {
            spdlog::critical("Failed to create renderer");
            return false;
        }

        renderer->SetUseEditorCamera(useEditorCamera);

        if (!renderer->Initialize(windowName)) {
            spdlog::critical("Failed to initialize {} renderer: {}", renderer->GetName(), SDL_GetError());

            renderer.reset();
            return false;
        }

        if (!renderer->CreateMap()) {
            spdlog::critical("Failed to create renderer map");

            renderer->Shutdown();
            renderer.reset();

            return false;
        }

        return true;
    }

    bool StartGameSystems(Level& level) {
        if (!SoundManager::InitializeOpenAL()) {
            spdlog::critical("Failed to initialize OpenAL");
            return false;
        }

        AudioSystem::Start(level);
        LevelSystem::Start(level);
        AudioSystem::ApplyListenerSettings(level);

        return true;
    }

    void ShutdownGameSystems(Level& level) {
        LevelSystem::Shutdown(level);
        AudioSystem::Shutdown(level);
        SoundManager::DestroyOpenAL();
    }

    void ShutdownRenderer() {
        if (!renderer) return;

        renderer->Shutdown();
        renderer.reset();
    }

    void FinishStartup() {
        InputManager::SetRelativeMouseMode(renderer->GetWindow(), true);

        SDL_HideCursor();

        spdlog::info("Runtime started using {} renderer", renderer->GetName());
    }

#ifdef TILKY_STANDALONE

    bool StartStandalone(Level& level, const std::string& windowName) {
        MapQueries::RebuildSectorRuntimeLinks(level);

        if (!StartRenderer(windowName, false)) return false;

        if (!StartGameSystems(level)) {
            ShutdownRenderer();
            return false;
        }

        FinishStartup();
        return true;
    }

    void UpdateStandalone(Level& level) {
        {
            ZoneScopedN("Level");
            LevelSystem::Update(level);
        }

        {
            ZoneScopedN("Renderer");

            renderer->BeginFrame();

            // TODO: Add a proper debug-view setting.
            constexpr bool RENDER_DEBUG = false;
            constexpr bool RENDER_UI = true;
            renderer->Update(RENDER_DEBUG, RENDER_UI);

            renderer->BeginImGuiFrame();
            renderer->EndImGuiFrame();

            renderer->EndFrame();
        }

        {
            ZoneScopedN("Audio");
            AudioSystem::Update(level);
        }
    }

    void ShutdownStandalone(Level& level) {
        ShutdownGameSystems(level);
    }

#else

    void SetRelativeMouseMode(const bool enabled) {
        relativeMouseMode = enabled;

        InputManager::SetRelativeMouseMode(renderer->GetWindow(), enabled);

        if (enabled) SDL_HideCursor();
        else SDL_ShowCursor();
    }

    void UpdateMouseCapture(const bool mouseBlockedByImGui, const bool ignoreImGuiMouseBlocking) {
        if (InputManager::GetKeyDown(SDL_SCANCODE_TAB) &&
            relativeMouseMode) [[unlikely]] {
            SetRelativeMouseMode(false);
        }

        const bool mouseClickAllowed = ignoreImGuiMouseBlocking || !mouseBlockedByImGui;

        if (InputManager::GetMouseButtonDown(SDL_BUTTON_LEFT) &&
            !relativeMouseMode &&
            mouseClickAllowed) [[unlikely]] {
            SetRelativeMouseMode(true);
        }
    }

    void UpdateFpsCounter() {
        fpsTimer += GameTime::deltaTime;

        if (fpsTimer < 1.3f) return;

        fps = static_cast<int>(GameTime::GetFPS());
        fpsTimer = 0.0f;
    }

    void RenderDebugText() {
        renderer->RenderTextRaw(
            "FPS: " + std::to_string(fps),
            {0.0f, 20.0f},
            {0.5f, 0.5f},
            Vector3{255.0f, 0.0f, 255.0f}
        );
    }

    bool StartEditor(Level& level, const std::string& windowName) {
        relativeMouseMode = true;
        editorLevelSnapshot.reset();

        MapQueries::RebuildSectorRuntimeLinks(level);

        if (!StartRenderer(windowName, true)) return false;

        RuntimeEditor::Start(level, *renderer);

        FinishStartup();
        return true;
    }

    bool StartPlay(Level& level, const std::string& windowName) {
        relativeMouseMode = true;

        editorLevelSnapshot = std::make_unique<Level>(level);

        spdlog::info("Runtime level snapshot created");

        MapQueries::RebuildSectorRuntimeLinks(level);

        if (!StartRenderer(windowName, false)) {
            editorLevelSnapshot.reset();
            return false;
        }

        if (!StartGameSystems(level)) {
            editorLevelSnapshot.reset();
            ShutdownRenderer();
            return false;
        }

        FinishStartup();
        return true;
    }

    void UpdateEditor(Level& level) {
        const ImGuiIO& io = ImGui::GetIO();

        UpdateMouseCapture(io.WantCaptureMouse, false);

        EditorFunctions::UpdateConsole(GameTime::deltaTime);

        RuntimeEditor::Update(
            level,
            *renderer,
            relativeMouseMode,
            io.WantCaptureMouse,
            io.WantCaptureKeyboard,
            renderer->screenWidth,
            renderer->screenHeight
        );

        {
            ZoneScopedN("Renderer");

            renderer->BeginFrame();

            // TODO: Add a proper debug-view setting.
            constexpr bool RENDER_DEBUG = false;
            constexpr bool RENDER_UI = false;
            renderer->Update(RENDER_DEBUG, RENDER_UI);

            renderer->BeginImGuiFrame();

            RuntimeEditor::Draw(level);

            renderer->EndImGuiFrame();

            RenderDebugText();
            EditorFunctions::RenderConsole(renderer.get());

            renderer->EndFrame();
        }

        UpdateFpsCounter();
    }

    void UpdatePlay(Level& level) {
        const ImGuiIO& io = ImGui::GetIO();

        UpdateMouseCapture(io.WantCaptureMouse, true);

        EditorFunctions::UpdateConsole(GameTime::deltaTime);

        if (relativeMouseMode) {
            ZoneScopedN("Level");
            LevelSystem::Update(level);
        }

        {
            ZoneScopedN("Renderer");

            renderer->BeginFrame();

            // TODO: Add a proper debug-view setting.
            constexpr bool RENDER_DEBUG = false;
            constexpr bool RENDER_UI = true;
            renderer->Update(RENDER_DEBUG, RENDER_UI);

            renderer->BeginImGuiFrame();
            renderer->EndImGuiFrame();

            RenderDebugText();
            EditorFunctions::RenderConsole(renderer.get());

            renderer->EndFrame();
        }

        {
            ZoneScopedN("Audio");
            AudioSystem::Update(level);
        }

        UpdateFpsCounter();
    }

    void ShutdownEditor(Level& level) {
        RuntimeEditor::Shutdown(level);
    }

    void ShutdownPlay(Level& level) {
        ShutdownGameSystems(level);

        if (!editorLevelSnapshot) {
            spdlog::error("Could not find editor level snapshot");

            return;
        }

        LevelManager::CurrentLevel() = *editorLevelSnapshot;

        editorLevelSnapshot.reset();

        spdlog::info("Runtime ended. Level restored to editor snapshot");
    }

#endif
}

namespace RuntimeSession {
#ifdef TILKY_STANDALONE

    bool Start(const std::string& windowName) {
        if (!LevelManager::HasCurrentLevel()) {
            spdlog::critical("No current level loaded");
            return false;
        }

        return StartStandalone(LevelManager::CurrentLevel(), windowName);
    }

#else

    bool Start(const std::string& windowName, const RuntimeType runtimeType) {
        if (!LevelManager::HasCurrentLevel()) {
            spdlog::critical("No current level loaded");
            return false;
        }

        Level& level = LevelManager::CurrentLevel();

        switch (runtimeType) {
            case EDITOR:
                activeRuntimeType = EDITOR;
                return StartEditor(level, windowName);

            case PLAY:
                activeRuntimeType = PLAY;
                return StartPlay(level, windowName);

            case STANDALONE:
                spdlog::critical("STANDALONE cannot be selected at runtime. Build the Tilky_Standalone target instead.");
                return false;
        }

        spdlog::critical("Unknown runtime type");
        return false;
    }

#endif

    void Update() {
        if (!renderer || !LevelManager::HasCurrentLevel()) return;

        ZoneScoped;

        GameTime::Update();

        Level& level = LevelManager::CurrentLevel();

#ifdef TILKY_STANDALONE
        UpdateStandalone(level);
#else
        switch (activeRuntimeType) {
            case EDITOR: UpdateEditor(level); break;
            case PLAY: UpdatePlay(level);break;
            case STANDALONE: spdlog::critical("Invalid runtime type in Tilky_Engine"); break;
        }
#endif
    } // Update

    void Shutdown() {
        if (!LevelManager::HasCurrentLevel()) {
            ShutdownRenderer();
            return;
        }

        Level& level = LevelManager::CurrentLevel();

#ifdef TILKY_STANDALONE
        ShutdownStandalone(level);
#else
        switch (activeRuntimeType) {
            case EDITOR: ShutdownEditor(level); break;
            case PLAY: ShutdownPlay(level); break;
            case STANDALONE: spdlog::critical("Invalid runtime type in Tilky_Engine");break;
        }
#endif

        ShutdownRenderer();
    } // Shutdown
}