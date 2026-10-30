//
// Created by berke on 5/14/2026.
//

/// Manages everything that happens after opening a project.
/// Starts, updates, and shuts down the editor or game runtime systems.

#include "Headers/Runtime/RuntimeSession.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
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
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/GlobalEventVariables.hpp"
#include "Headers/Runtime/EventSystem.hpp"

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
    // The background is editor state rather than part of Level, so it is
    // snapshotted next to it: Game.LoadLevel changes it during Play.

    bool relativeMouseMode = true;

    float fpsTimer = 0.0f;
    int fps = 0;

    int vsyncMode = -1; // adaptive, OpenGLInit.cpp's own default
#endif

    // Creates the renderer and its window. The engine does this once at
    // launch, hidden; the standalone game once per run, visible.
    bool CreateRenderer(const std::string& windowName, const bool startHidden) {
        ZoneScopedN("CreateRenderer");
        // TODO: TILKY_TODO Try Vulkan first when Vulkan support is ready.
        renderer = RendererFactory::CreateRenderer(RendererBackend::OPENGL);

        if (!renderer) {
            spdlog::critical("Failed to create renderer");
            return false;
        }

        if (!renderer->Initialize(windowName, startHidden)) {
            spdlog::critical("Failed to initialize {} renderer: {}", renderer->GetName(), SDL_GetError());

            renderer.reset();
            return false;
        }

        return true;
    }

#ifdef TILKY_STANDALONE
    bool StartRenderer(const std::string& windowName, const bool useEditorCamera) {
        ZoneScopedN("StartRenderer");

        if (!CreateRenderer(windowName, false)) return false;

        renderer->SetDrawMode(IRenderer::DrawMode::SOLID);
        renderer->SetUseEditorCamera(useEditorCamera);

        if (!renderer->CreateMap()) {
            spdlog::critical("Failed to create renderer map");

            renderer->Shutdown();
            renderer.reset();

            return false;
        }

        return true;
    }
#else
    // Whether CreateMap() has made the map's GPU buffers; later sessions
    // only refill them.
    bool mapCreated = false;

    // Readies the persistent renderer for this session's level and shows it.
    bool StartRenderer(const bool useEditorCamera) {
        ZoneScopedN("StartRenderer");

        if (!renderer) {
            spdlog::critical("The game window failed to initialize at engine start; check the log above");
            return false;
        }

        renderer->MakeCurrent();
        renderer->SetVSync(vsyncMode);
        renderer->SetUseEditorCamera(useEditorCamera);
        renderer->SetDrawMode(IRenderer::DrawMode::SOLID);

        if (!mapCreated) {
            // The atlas built at engine start is reused if no image changed since.
            renderer->RefreshTexturesFromLevel();

            if (!renderer->CreateMap()) {
                spdlog::critical("Failed to create renderer map");
                return false;
            }

            mapCreated = true;
        }
        else renderer->ReloadMap();

        renderer->SetWindowVisible(true);
        return true;
    }

    // Ends the session without destroying the window, GL context or atlas.
    void StopRenderer() {
        if (!renderer) return;

        renderer->MakeCurrent();
        renderer->EndSession();

        InputManager::SetRelativeMouseMode(renderer->GetWindow(), false);
        SDL_ShowCursor();
    }
#endif

    void PrepareRuntimeLevel(Level& level) {
        MapQueries::RebuildSectorRuntimeLinks(level);

        // Before any system starts, so a disabled entity starts out of the game.
        level.ApplyAllEntityEnabled();

        for (ComponentTransform& transform : level.transforms.components)
            transform.UpdateObjectSectorAndFloor(level.sectors);
    }

    bool StartGameSystems(Level& level) {
#ifdef TILKY_STANDALONE
        if (!SoundManager::InitializeOpenAL()) {
            spdlog::critical("Failed to initialize OpenAL");
            return false;
        }
#else
        // The device was opened at engine start and stays open between sessions.
        SoundManager::BeginSession();
#endif

        // Global event variables keep between levels (SwitchRuntimeLevel
        // leaves them alone) but every session starts from the defaults.
        GlobalEventVariables::ResetValues();

        // Same for the clock: slow motion or a pause left over from the last
        // session doesn't carry into this one.
        GameTime::ResetGameClock();

        AudioSystem::Start(level);
        LevelSystem::Start(level);
        AudioSystem::ApplyListenerSettings(level);

        return true;
    }

    // Game.LoadLevel, after the frame that asked for it. The new level is read
    // from its saved file first, so a level that fails to load leaves the
    // current one running. The Lua state is kept, and with it Global.
    void SwitchRuntimeLevel(Level& level, const std::string& levelName) {
        ZoneScopedN("SwitchRuntimeLevel");

        Level loadedLevel;
        LevelSerialization::LevelExtraData extraData;
        std::string errorMessage;

        const std::filesystem::path levelPath = LevelSerialization::FindLevelPath(levelName, &errorMessage);

        if (levelPath.empty() || !LevelSerialization::LoadLevelFromFile(levelPath, loadedLevel, &extraData, &errorMessage)) {
            spdlog::error("Game.LoadLevel(\"{}\") failed: {}", levelName, errorMessage);
            return;
        }

        loadedLevel.name = levelName;

        LevelSystem::StopLevel(level);
        AudioSystem::Shutdown(level);

        // A script's OnDestroy may have asked for another level; the switch
        // already under way wins.
        LevelSystem::TakeRequestedLevel();

        level = std::move(loadedLevel);

        PrepareRuntimeLevel(level);
        renderer->ReloadMap();

        AudioSystem::Start(level);
        LevelSystem::Start(level);
        AudioSystem::ApplyListenerSettings(level);

        // The load time shouldn't show up as one huge frame.
        GameTime::Update();

        spdlog::info("Switched to level {}", levelName);
    }

    void ShutdownGameSystems(Level& level) {
        LevelSystem::Shutdown(level);
        // Stopping always leaves time running normally again.
        GameTime::ResetGameClock();
        AudioSystem::Shutdown(level);
#ifdef TILKY_STANDALONE
        SoundManager::DestroyOpenAL();
#else
        SoundManager::EndSession();
#endif
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

    // Where each shown message's box is drawn (its top), easing toward its
    // place in the stack so the others slide when one goes. By Message::id.
    std::unordered_map<uint64_t, float> messageBoxTops;

    // `text` split into the lines it's drawn as: at its '\n's, and between
    // words wherever a line would get wider than `wrapWidth` (0 = never).
    std::vector<std::string> WrapMessageLines(const std::string& text, const std::string& font, const float pixelSize,
                                              const float wrapWidth) {
        std::vector<std::string> lines;

        size_t start = 0;
        while (true) {
            const size_t end = text.find('\n', start);
            const std::string paragraph = text.substr(start, end == std::string::npos ? std::string::npos : end - start);

            if (wrapWidth <= 0.0f) lines.push_back(paragraph);
            else {
                std::string line;
                size_t wordStart = 0;
                while (wordStart <= paragraph.size()) {
                    const size_t wordEnd = std::min(paragraph.find(' ', wordStart), paragraph.size());
                    const std::string word = paragraph.substr(wordStart, wordEnd - wordStart);
                    wordStart = wordEnd + 1;

                    // A single word wider than the wrap width keeps a line to itself.
                    const std::string longer = line.empty() ? word : line + " " + word;
                    if (line.empty() || renderer->MeasureTextLine(font, pixelSize, longer) <= wrapWidth) line = longer;
                    else {
                        lines.push_back(line);
                        line = word;
                    }
                }
                lines.push_back(line);
            }

            if (end == std::string::npos) break;
            start = end + 1;
        }

        return lines;
    }

    // The Show Message dispatches' text, styled by the level's MessageStyle.
    void RenderEventMessages(const Level& level) {
        const std::vector<EventSystem::Message> messages = EventSystem::CurrentMessages();
        if (messages.empty() || renderer->screenHeight <= 0) {
            messageBoxTops.clear();
            return;
        }

        const MessageStyle& style = level.worldSettings.messageStyle;

        // Style sizes are pixels at the UI Reference Height, like UI Text.
        const float scale = static_cast<float>(renderer->screenHeight) / std::max(1.0f, ProjectManager::GetUIReferenceHeight());
        const float pixelSize = style.fontSize * scale;
        if (pixelSize <= 0.0f) return;

        const IRenderer::FontMetrics metrics = renderer->GetFontMetrics(style.font, pixelSize);
        const Vector2 padding = style.background ? style.padding * scale : Vector2{0.0f, 0.0f};

        struct Laid {
            const EventSystem::Message* message;
            std::vector<std::string> lines;
            std::vector<float> lineWidths;
            float textWidth = 0.0f;
            Vector2 boxSize;
        };

        // Newest first: the newest is at the top of the stack.
        std::vector<Laid> laid;
        float stackHeight = 0.0f;
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            Laid entry{&*it, WrapMessageLines(it->text, style.font, pixelSize, style.wrapWidth * scale)};
            for (const std::string& line : entry.lines) {
                entry.lineWidths.push_back(renderer->MeasureTextLine(style.font, pixelSize, line));
                entry.textWidth = std::max(entry.textWidth, entry.lineWidths.back());
            }
            entry.boxSize = {
                entry.textWidth + padding.x * 2.0f,
                static_cast<float>(entry.lines.size()) * metrics.lineHeight + padding.y * 2.0f
            };

            if (!laid.empty()) stackHeight += style.spacing * scale;
            stackHeight += entry.boxSize.y;
            laid.push_back(std::move(entry));
        }

        const int anchor = static_cast<int>(style.anchor);
        const int column = anchor % 3; // left, centre, right
        const int row = anchor / 3;    // top, middle, bottom

        const float screenWidth = static_cast<float>(renderer->screenWidth);
        const float screenHeight = static_cast<float>(renderer->screenHeight);
        const Vector2 anchorPoint = Vector2{screenWidth * 0.5f * column, screenHeight * 0.5f * row} + style.offset * scale;
        const float stackTop = anchorPoint.y - stackHeight * 0.5f * row;

        // Exponential ease: about 95% of the way in a quarter second.
        const float ease = 1.0f - std::exp(-12.0f * GameTime::unscaledDeltaTime);

        std::unordered_map<uint64_t, float> boxTops;
        float targetTop = stackTop;

        for (const Laid& entry : laid) {
            const EventSystem::Message& message = *entry.message;

            float top = targetTop;
            if (const auto previous = messageBoxTops.find(message.id); previous != messageBoxTops.end())
                top = previous->second + (targetTop - previous->second) * ease;
            boxTops[message.id] = top;
            targetTop += entry.boxSize.y + style.spacing * scale;

            const float left = anchorPoint.x - entry.boxSize.x * 0.5f * column;
            const float alpha = message.alpha;

            if (style.background) {
                const Vector4& color = style.backgroundColor;
                renderer->DrawScreenRectangle({left, top}, entry.boxSize, {color.x, color.y, color.z, color.w * alpha},
                                              style.backgroundTexture);
            }

            const Vector4 textColor = {message.color.x / 255.0f, message.color.y / 255.0f, message.color.z / 255.0f, alpha};
            const Vector4 shadowColor = {style.shadowColor.x, style.shadowColor.y, style.shadowColor.z, style.shadowColor.w * alpha};
            const Vector4 outlineColor = {style.outlineColor.x, style.outlineColor.y, style.outlineColor.z, style.outlineColor.w * alpha};
            const float outline = style.outlineThickness * scale;

            for (size_t i = 0; i < entry.lines.size(); ++i) {
                const float slack = entry.textWidth - entry.lineWidths[i];
                const float alignShift = style.align == MessageTextAlign::LEFT ? 0.0f
                                       : style.align == MessageTextAlign::CENTER ? slack * 0.5f
                                       : slack;
                const Vector2 baseline = {
                    left + padding.x + alignShift,
                    top + padding.y + metrics.ascender + metrics.lineHeight * static_cast<float>(i)
                };
                const std::string& line = entry.lines[i];

                if (style.shadow)
                    renderer->DrawTextLine(style.font, pixelSize, line, baseline + style.shadowOffset * scale, shadowColor);

                // The text drawn around a circle of the thickness, with
                // enough copies that thick outlines stay round.
                if (style.outline && outline > 0.0f) {
                    const int copies = std::clamp(static_cast<int>(std::ceil(outline * 3.0f)), 8, 32);
                    for (int k = 0; k < copies; ++k) {
                        const float angle = 6.2831853f * static_cast<float>(k) / static_cast<float>(copies);
                        renderer->DrawTextLine(style.font, pixelSize, line,
                                               baseline + Vector2{std::cos(angle), std::sin(angle)} * outline, outlineColor);
                    }
                }

                renderer->DrawTextLine(style.font, pixelSize, line, baseline, textColor);
            }
        }

        messageBoxTops = std::move(boxTops);
    }

#ifdef TILKY_STANDALONE

    bool StartStandalone(Level& level, const std::string& windowName) {
        PrepareRuntimeLevel(level);

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
            RenderEventMessages(level);

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
        fpsTimer += GameTime::unscaledDeltaTime;

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

    bool StartEditor(Level& level) {
        relativeMouseMode = true;
        editorLevelSnapshot.reset();

        MapQueries::RebuildSectorRuntimeLinks(level);

        for (ComponentTransform& transform : level.transforms.components)
            transform.UpdateObjectSectorAndFloor(level.sectors);

        if (!StartRenderer(true)) return false;

        RuntimeEditor::Start(level, *renderer);

        FinishStartup();
        return true;
    }

    bool StartPlay(Level& level) {
        relativeMouseMode = true;

        editorLevelSnapshot = std::make_unique<Level>(level);

        spdlog::info("Runtime level snapshot created");

        PrepareRuntimeLevel(level);

        if (!StartRenderer(false)) {
            editorLevelSnapshot.reset();
            return false;
        }

        if (!StartGameSystems(level)) {
            editorLevelSnapshot.reset();
            StopRenderer();
            renderer->SetWindowVisible(false);
            return false;
        }

        FinishStartup();

        GameTime::Update();

        return true;
    }

    void UpdateEditor(Level& level) {
        const ImGuiIO& io = ImGui::GetIO();

        // The editor camera dims disabled entities' sprites, which reads
        // ownerEnabled, and the inspector checkbox writes enabled directly.
        level.SyncEntityEnabledStates();

        UpdateMouseCapture(io.WantCaptureMouse, false);

        {
            ZoneScopedN("Renderer");

            renderer->BeginFrame();

            // TODO: Add a proper debug-view setting.
            constexpr bool RENDER_DEBUG = true;
            constexpr bool RENDER_UI = false;
            renderer->Update(RENDER_DEBUG, RENDER_UI);

            renderer->BeginImGuiFrame();

            // The runtime editor writes sector values from its inspector, the
            // scroll wheel, UV nudges and texture drops. Diffing around it
            // passes any of those on to child sectors without hooking each one.
            const auto parentsBeforeDraw = level.SnapshotParentSectorValues();
            RuntimeEditor::Draw(level);
            level.PropagateSnapshottedSectorChanges(parentsBeforeDraw);

            renderer->EndImGuiFrame();

            // No FPS text here: the runtime editor's toolbar has a
            // Statistics menu for it.
            EditorFunctions::RenderConsole(renderer.get());

            renderer->EndFrame();
        }

        const auto parentsBeforeUpdate = level.SnapshotParentSectorValues();

        RuntimeEditor::Update(
            level,
            *renderer,
            relativeMouseMode,
            io.WantCaptureMouse,
            // Only a text field being typed into blocks the shortcuts.
            // WantCaptureKeyboard is true whenever any ImGui window has focus
            // (keyboard nav is on), and the panels that reappear after a
            // middle-mouse look take focus - locking the 1/2 tool keys.
            io.WantTextInput,
            renderer->screenWidth,
            renderer->screenHeight
        );

        level.PropagateSnapshottedSectorChanges(parentsBeforeUpdate);

        EditorFunctions::UpdateConsole(GameTime::unscaledDeltaTime);
        EditorFunctions::ClearConsole();
    }

    void UpdatePlay(Level& level) {
        const ImGuiIO& io = ImGui::GetIO();

        UpdateMouseCapture(io.WantCaptureMouse, true);

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

            RenderEventMessages(level);
            RenderDebugText();
            EditorFunctions::RenderConsole(renderer.get());

            renderer->EndFrame();
        }

        EditorFunctions::UpdateConsole(GameTime::unscaledDeltaTime);

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

    void SetVSyncMode(const int mode) {
        vsyncMode = std::clamp(mode, -1, 1);
    }

    int GetVSyncMode() {
        return vsyncMode;
    }

    bool Initialize(const std::string& windowName) {
        // Hidden until a session shows it.
        if (!CreateRenderer(windowName, true)) return false;

        if (!SoundManager::InitializeOpenAL()) {
            spdlog::critical("Failed to initialize OpenAL");
            return false;
        }

        return true;
    }

    void Destroy() {
        ShutdownRenderer();
        mapCreated = false;
        SoundManager::DestroyOpenAL();
    }

    bool Start(const RuntimeType runtimeType) {
        if (!LevelManager::HasCurrentLevel()) {
            spdlog::critical("No current level loaded");
            return false;
        }

        Level& level = LevelManager::CurrentLevel();

        switch (runtimeType) {
            case EDITOR:
                activeRuntimeType = EDITOR;
                return StartEditor(level);

            case PLAY:
                activeRuntimeType = PLAY;
                return StartPlay(level);

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

#ifndef TILKY_STANDALONE
        renderer->MakeCurrent();
#endif

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

        if (const std::optional<std::string> requestedLevel = LevelSystem::TakeRequestedLevel())
            SwitchRuntimeLevel(level, *requestedLevel);
    } // Update

    void Shutdown() {
        if (!LevelManager::HasCurrentLevel()) {
#ifdef TILKY_STANDALONE
            ShutdownRenderer();
#else
            StopRenderer();
#endif
            return;
        }

        Level& level = LevelManager::CurrentLevel();

#ifdef TILKY_STANDALONE
        ShutdownStandalone(level);
        ShutdownRenderer();
#else
        switch (activeRuntimeType) {
            case EDITOR: ShutdownEditor(level); break;
            case PLAY: ShutdownPlay(level); break;
            case STANDALONE: spdlog::critical("Invalid runtime type in Tilky_Engine");break;
        }

        StopRenderer();
#endif
    } // Shutdown

#ifndef TILKY_STANDALONE
    void HideWindow() {
        if (renderer) renderer->SetWindowVisible(false);
    }
#endif
}