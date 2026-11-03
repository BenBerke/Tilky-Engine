// Headless play test for the Classic FPS Kit: a real Play session, hidden, at a
// fixed 60 fps. Bot.lua plays the game and asks for input and screenshots by
// naming an entity "BOT:<keys>|<command>;<command>". Run it on a COPY of the
// template: it adds the bot to the copy's Player prefab and Main Menu.
//
// usage: playtest <project.tilky> <level> <outDir> [maxFrames]
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glad/glad.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "src/Runtime/RuntimeSession.cpp"

#include "Headers/Editor/Editor.hpp"
#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/GlobalEventVariables.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Gameplay/ParticleSystem.hpp"
#include "Headers/Runtime/Sound/MusicPlayer.hpp"
#include "Headers/TagRegistry.hpp"

namespace fs = std::filesystem;

Uint64 HarnessCounter() {
    static Uint64 counter = 0;
    return ++counter;
}

static bool harnessKeys[SDL_SCANCODE_COUNT] = {};
const bool* HarnessKeyboardState(int* numkeys) {
    if (numkeys) *numkeys = SDL_SCANCODE_COUNT;
    return harnessKeys;
}

static float mouseX = 0.0f, mouseY = 0.0f;
static SDL_MouseButtonFlags mouseButtons = 0;
SDL_MouseButtonFlags HarnessMouseState(float* x, float* y) {
    if (x) *x = mouseX;
    if (y) *y = mouseY;
    return mouseButtons;
}

// The bot turns the player itself, so the mouse never moves the camera.
SDL_MouseButtonFlags HarnessRelativeMouseState(float* x, float* y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    return mouseButtons;
}

SDL_Window* HarnessWindow() { return renderer ? renderer->GetWindow() : nullptr; }

static fs::path outDir;
static std::string pendingCapture;

void HarnessBeforeSwap(SDL_Window* window) {
    if (pendingCapture.empty()) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    for (int y = 0; y < h; ++y)
        std::memcpy(static_cast<unsigned char*>(surface->pixels) + y * surface->pitch,
                    pixels.data() + static_cast<size_t>(h - 1 - y) * w * 4, static_cast<size_t>(w) * 4);
    // The template's thumbnail: the view without the HUD or the FPS counter, at half size.
    if (pendingCapture == "thumb") {
        SDL_Surface* small = SDL_CreateSurface(480, 270, SDL_PIXELFORMAT_RGBA32);
        const SDL_Rect from = {160, 60, 960, 540};
        SDL_BlitSurfaceScaled(surface, &from, small, nullptr, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(surface);
        surface = small;
    }
    const fs::path path = outDir / (pendingCapture + ".png");
    if (!IMG_SavePNG(surface, path.string().c_str())) std::printf("save failed: %s\n", SDL_GetError());
    else std::printf("[shot] %s\n", path.string().c_str());
    SDL_DestroySurface(surface);
    pendingCapture.clear();
}

static SDL_Scancode KeyFor(const std::string& name) {
    if (name == "LCtrl") return SDL_SCANCODE_LCTRL;
    if (name == "Space") return SDL_SCANCODE_SPACE;
    if (name == "Esc") return SDL_SCANCODE_ESCAPE;
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (name[0] - 'A'));
    if (name.size() == 1 && name[0] >= '1' && name[0] <= '9') return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (name[0] - '1'));
    return SDL_SCANCODE_UNKNOWN;
}

static std::vector<std::string> Split(const std::string& text, const char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == std::string::npos) end = text.size();
        if (end > start) parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

// Reads the bot's request. False when the bot says DONE.
static bool ApplyBotRequest() {
    std::memset(harnessKeys, 0, sizeof(harnessKeys));
    mouseButtons = 0;
    static std::string lastShot;

    for (const Entity& e : LevelManager::CurrentLevel().entities) {
        if (e.name.rfind("BOT:", 0) != 0) continue;
        std::string rest = e.name.substr(4), commands;
        if (const size_t bar = rest.find('|'); bar != std::string::npos) {
            commands = rest.substr(bar + 1);
            rest = rest.substr(0, bar);
        }
        for (const std::string& key : Split(rest, ',')) {
            const SDL_Scancode code = KeyFor(key);
            if (code != SDL_SCANCODE_UNKNOWN) harnessKeys[code] = true;
        }
        for (const std::string& command : Split(commands, ';')) {
            if (command == "DONE") return false;
            if (command.rfind("SHOT=", 0) == 0 && command != lastShot) {
                lastShot = command;
                pendingCapture = command.substr(5);
            }
            if (command.rfind("MOUSE=", 0) == 0) { // MOUSE=x,y,down: fractions of the window
                const auto parts = Split(command.substr(6), ',');
                int w = 1280, h = 720;
                SDL_GetWindowSize(renderer->GetWindow(), &w, &h);
                mouseX = std::stof(parts[0]) * static_cast<float>(w);
                mouseY = std::stof(parts[1]) * static_cast<float>(h);
                if (parts.size() > 2 && parts[2] == "1") mouseButtons = SDL_BUTTON_LMASK;
            }
        }
    }
    return true;
}

// Adds the bot to the copy of the project this is running on.
static void AddBot() {
    const fs::path assets = ProjectManager::GetAssetsPath();
    {
        const fs::path file = assets / "Prefabs/Player/Player.prefab";
        Level prefab;
        std::string error;
        if (!LevelSerialization::LoadPrefabFromFile(file, prefab, &error)) std::printf("prefab: %s\n", error.c_str());
        LevelManager::ScopedCurrentLevel scope(prefab);
        ComponentScript& bot = prefab.entities[0].AddScript();
        bot.fileName = "Scripts/Test/Bot";
        bot.enabled = true;
        LevelSerialization::SavePrefabToFile(file, prefab, &error);
    }
    {
        const fs::path file = assets / "Levels/Main Menu.bson";
        Level level;
        std::string error;
        if (!LevelSerialization::LoadLevelFromFile(file, level, nullptr, &error)) std::printf("menu: %s\n", error.c_str());
        LevelManager::ScopedCurrentLevel scope(level);
        ComponentScript& bot = level.entities[0].AddScript();
        bot.fileName = "Scripts/Test/Bot";
        bot.enabled = true;
        LevelSerialization::SaveLevelToFile(file, level, nullptr, &error);
    }
}

// Saving and loading every level must lose nothing a beginner would notice.
static int RoundTrip(const fs::path& assets) {
    int failures = 0;
    for (const auto& file : fs::directory_iterator(assets / "Levels")) {
        if (file.path().extension() != ".bson") continue;
        Level before, after;
        std::string error;
        const fs::path copy = file.path().parent_path() / "RoundTrip.tmp.bson";
        bool ok = LevelSerialization::LoadLevelFromFile(file.path(), before, nullptr, &error);
        {
            LevelManager::ScopedCurrentLevel scope(before);
            ok = ok && LevelSerialization::SaveLevelToFile(copy, before, nullptr, &error);
        }
        ok = ok && LevelSerialization::LoadLevelFromFile(copy, after, nullptr, &error);
        fs::remove(copy);

        size_t shut = 0, shutAfter = 0, walls = 0;
        for (const Sector& s : before.sectors) if (s.floors.at(0).floor.height == s.floors.at(0).ceiling.height) ++shut;
        for (const Sector& s : after.sectors) if (s.floors.at(0).floor.height == s.floors.at(0).ceiling.height) ++shutAfter;
        for (const Wall& w : after.walls) walls += w.events.size();
        const bool same = ok && before.sectors.size() == after.sectors.size() && before.walls.size() == after.walls.size() &&
                          before.entities.size() == after.entities.size() && before.comments.size() == after.comments.size() && shut == shutAfter;
        for (const auto& [entityID, snap] : after.wallSnaps) {
            const ComponentTransform* transform = after.transforms.Get(entityID);
            const Wall* wall = nullptr;
            for (const Wall& each : after.walls) if (each.id == snap.wallID) wall = &each;
            std::printf("[snap] %s: entity %s wall %s at %.2f %.2f\n", after.name.c_str(), after.GetEntity(entityID) ? after.GetEntity(entityID)->name.c_str() : "MISSING",
                        wall ? "found" : "MISSING", transform ? transform->position.x : 0.0f, transform ? transform->position.z : 0.0f);
        }
        std::printf("[roundtrip] %s: %s, sectors %zu, walls %zu (%zu events), entities %zu, comments %zu, shut %zu %s\n",
                    file.path().stem().string().c_str(), same ? "same" : "DIFFERENT", after.sectors.size(), after.walls.size(), walls,
                    after.entities.size(), after.comments.size(), shutAfter, error.c_str());
        if (!same) ++failures;
    }
    std::fflush(stdout);
    return failures;
}

// The runtime editor's Play and Stop, and looping in the game's own update.
// usage: playtest <project.tilky> <level> <outDir> 0 --preview
static void CheckEmitters() {
    Level& level = LevelManager::CurrentLevel();
    const ID hostID = level.CreateEntity(false);
    Entity& host = *level.GetEntity(hostID);
    host.name = "Emitter Test";
    auto* hostTransform = level.transforms.Get(hostID);

    const auto tick = [](const int frames) {
        for (int i = 0; i < frames; ++i) {
            InputManager::BeginFrame();
            RuntimeSession::Update();
        }
    };
    const auto ownedBy = [](const ComponentInstanceID id) {
        size_t count = 0;
        for (const auto& particle : ParticleSystem::Particles()) if (particle.style->previewOwner == id) ++count;
        return count;
    };

    auto editorView = renderer->GetEditorCameraTransform();
    auto editorCamera = renderer->GetEditorCamera();
    editorView->position = {0.0f, 30.0f, 400.0f};
    editorCamera->yaw = 0.0f;
    editorCamera->pitch = -15.0f;
    hostTransform->position = {0.0f, 30.0f, 450.0f};

    auto* looping = host.AddComponent<ComponentParticleEmitter>();
    looping->textures = {"Sprites/FX/Debris/Spark.png"};
    looping->burstMin = looping->burstMax = 5;
    looping->speedMin = looping->speedMax = 12.0f;
    looping->spread = 180.0f;
    looping->gravity = 0.0f;
    looping->lifetimeMin = looping->lifetimeMax = 30.0f;
    looping->sizeMin = looping->sizeMax = 2.0f;
    looping->loop = true;
    looping->duration = 0.5f;
    const ComponentInstanceID loopID = looping->instanceID;

    ParticleSystem::PreviewPlay(*looping);
    tick(126);
    std::printf("[preview] loop: %zu particles after 2.1 s, want 25 (5 bursts of 5)\n", ownedBy(loopID));
    pendingCapture = "preview_loop";
    tick(2);

    auto* oneShot = host.AddComponent<ComponentParticleEmitter>();
    oneShot->textures = {"Sprites/FX/Blood/Drop 1.png"};
    oneShot->burstMin = oneShot->burstMax = 3;
    oneShot->rate = 20.0f;
    oneShot->duration = 1.0f;
    oneShot->speedMin = oneShot->speedMax = 8.0f;
    oneShot->gravity = 300.0f;
    oneShot->lifetimeMin = oneShot->lifetimeMax = 30.0f;
    oneShot->collision = ParticleCollision::DIE;
    oneShot->stainTextures = {"Sprites/FX/Blood/Small Stain 1.png"};
    oneShot->stainSizeMin = oneShot->stainSizeMax = 4.0f;
    const ComponentInstanceID shotID = oneShot->instanceID;
    looping = host.GetComponentInstance<ComponentParticleEmitter>(loopID);

    ParticleSystem::PreviewPlay(*oneShot);
    tick(180);
    pendingCapture = "preview_stains";
    tick(2);
    const size_t stainsSeen = ParticleSystem::Stains().size();
    std::printf("[preview] one shot: %zu stains, want 23 (3 + 20), %zu of its particles still flying, previewing %d\n",
                stainsSeen, ownedBy(shotID), ParticleSystem::IsPreviewing(shotID));
    const size_t loopBefore = ownedBy(loopID);
    ParticleSystem::PreviewStop(shotID);
    std::printf("[preview] stop one shot: stains %zu, want 0; loop's particles %zu -> %zu, want no drop\n",
                ParticleSystem::Stains().size(), loopBefore, ownedBy(loopID));
    ParticleSystem::PreviewStop(loopID);
    std::printf("[preview] stop loop: particles %zu, want 0, previewing %d\n", ParticleSystem::Particles().size(), ParticleSystem::IsPreviewing(loopID));
    std::printf("[preview] level untouched: clocks playing %d %d, want 0 0\n",
                host.GetComponentInstance<ComponentParticleEmitter>(loopID)->clock.playing,
                host.GetComponentInstance<ComponentParticleEmitter>(shotID)->clock.playing);

    // The game's update: Play On Start, then a loop and a one-shot.
    constexpr float DT = 1.0f / 60.0f;
    looping = host.GetComponentInstance<ComponentParticleEmitter>(loopID);
    oneShot = host.GetComponentInstance<ComponentParticleEmitter>(shotID);
    oneShot->isActive = false;
    looping->playOnStart = true;
    looping->started = false;
    for (int i = 0; i < 126; ++i) ParticleSystem::Update(level, DT);
    std::printf("[game] loop with Play On Start: %zu particles, want 25\n", ParticleSystem::Particles().size());
    ParticleSystem::StopEmitter(*looping);
    const size_t afterStop = ParticleSystem::Particles().size();
    for (int i = 0; i < 60; ++i) ParticleSystem::Update(level, DT);
    std::printf("[game] after Stop: %zu -> %zu, want no change, playing %d\n", afterStop, ParticleSystem::Particles().size(), looping->clock.playing);
    ParticleSystem::Start();

    looping->isActive = false;
    oneShot->isActive = true;
    oneShot->collision = ParticleCollision::NONE;
    ParticleSystem::PlayEmitter(*oneShot);
    for (int i = 0; i < 62; ++i) ParticleSystem::Update(level, DT);
    const bool playingAtEnd = oneShot->clock.playing;
    const size_t atEnd = ParticleSystem::Particles().size();
    for (int i = 0; i < 60; ++i) ParticleSystem::Update(level, DT);
    std::printf("[game] one cycle: %zu made, want 23, playing %d want 0, then %zu, want the same\n", atEnd, playingAtEnd, ParticleSystem::Particles().size());
    ParticleSystem::Start();

    // The inspector itself: the buttons, drawn and clicked for real. Click at
    // PREVIEW_CLICK=x,y in window pixels.
    looping->isActive = true;
    ImGuiDrawFunctions::EntityInspectorState state;
    state.allowParticlePreview = true;
    bool open = true;
    float clickX = -1.0f, clickY = -1.0f;
    if (const char* click = std::getenv("PREVIEW_CLICK")) std::sscanf(click, "%f,%f", &clickX, &clickY);
    for (int frame = 0; frame < 12; ++frame) {
        renderer->BeginFrame();
        renderer->Update(false, false);
        renderer->BeginImGuiFrame();
        ImGuiIO& io = ImGui::GetIO();
        if (clickX >= 0.0f) {
            io.AddMousePosEvent(clickX, clickY);
            if (frame == 6) io.AddMouseButtonEvent(0, true);
            if (frame == 8) io.AddMouseButtonEvent(0, false);
        }
        ImGuiDrawFunctions::DrawEntityEditor(host, state, &open, true);
        renderer->EndImGuiFrame();
        if (frame == 11) pendingCapture = "preview_inspector";
        renderer->EndFrame();
    }
    // Scrolled down to the second emitter's tag picker.
    oneShot->collision = ParticleCollision::DIE;
    if (auto id = TagRegistry::Find("Liquid")) oneShot->noStainTag = *id;
    for (int frame = 0; frame < 8; ++frame) {
        renderer->BeginFrame();
        renderer->Update(false, false);
        renderer->BeginImGuiFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(230.0f, 400.0f);
        if (frame == 3) io.AddMouseWheelEvent(0.0f, -500.0f);
        ImGuiDrawFunctions::DrawEntityEditor(host, state, &open, true);
        renderer->EndImGuiFrame();
        if (frame == 7) pendingCapture = "preview_tag";
        renderer->EndFrame();
    }
    std::printf("[preview] inspector: clicked %.0f,%.0f, first emitter previewing %d\n", clickX, clickY, ParticleSystem::IsPreviewing(loopID));
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[2], "--roundtrip") == 0) {
        if (!ProjectManager::LoadProjectMetaData(argv[1])) return 2;
        const fs::path settings = fs::path(argv[1]).parent_path() / "ProjectSettings.json";
        TagRegistry::LoadFromProjectSettings(settings);
        GlobalEventVariables::LoadFromProjectSettings(settings);
        return RoundTrip(ProjectManager::GetAssetsPath());
    }
    if (argc < 4) {
        std::printf("usage: playtest <project.tilky> <level> <outDir> [maxFrames]\n");
        return 1;
    }
    outDir = argv[3];
    fs::create_directories(outDir);
    const int maxFrames = argc > 4 ? std::atoi(argv[4]) : 40000;

    if (!ProjectManager::LoadProjectMetaData(argv[1])) return 2;
    const fs::path settings = fs::path(argv[1]).parent_path() / "ProjectSettings.json";
    TagRegistry::LoadFromProjectSettings(settings);
    GlobalEventVariables::LoadFromProjectSettings(settings);
    GlobalEventVariables::ResetValues();
    AddBot();
    if (!Localisation::LoadLanguage("en")) return 3;
    if (!Editor::LoadLevel(argv[2])) return 4;
    if (!Editor::Initialize()) return 5;
    if (!RuntimeSession::Initialize("playtest")) return 6;
    Editor::PrepareLevelForRuntime();
    InputManager::Initialize(Editor::GetWindow());

    const bool checkEmitters = argc > 5 && std::strcmp(argv[5], "--preview") == 0;
    if (!RuntimeSession::Start(checkEmitters ? RuntimeSession::EDITOR : RuntimeSession::PLAY)) return 7;
    SDL_SetWindowSize(renderer->GetWindow(), 1280, 720);
    SDL_SyncWindow(renderer->GetWindow());

    if (checkEmitters) {
        CheckEmitters();
        RuntimeSession::Shutdown();
        std::printf("[preview] after leaving: particles %zu, stains %zu, want 0 0\n", ParticleSystem::Particles().size(), ParticleSystem::Stains().size());
        std::fflush(stdout);
        RuntimeSession::Destroy();
        Editor::Destroy();
        return 0;
    }

    int frame = 0;
    for (; frame < maxFrames; ++frame) {
        InputManager::BeginFrame();
        if (!ApplyBotRequest()) break;
        const auto frameStart = std::chrono::steady_clock::now();
        RuntimeSession::Update();
        const float frameMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count();

        // How many particles and stains, and the slowest frame, twice a second.
        static float slowestMs = 0.0f;
        static size_t mostParticles = 0;
        slowestMs = std::max(slowestMs, frameMs);
        mostParticles = std::max(mostParticles, ParticleSystem::Particles().size());
        if (frame % 30 == 29) {
            if (mostParticles > 0 || !ParticleSystem::Stains().empty())
                std::printf("[fx] particles %zu stains %zu slowest frame %.1f ms\n", mostParticles, ParticleSystem::Stains().size(), slowestMs);
            slowestMs = 0.0f;
            mostParticles = 0;
        }

        // A corpse has to stay on its floor. Checked twice a second.
        if (frame % 30 == 0) {
            Level& corpseLevel = LevelManager::CurrentLevel();
            for (const ComponentMonsterAI& ai : corpseLevel.monsterAIs.components) {
                if (ai.state != MonsterAIState::DEAD) continue;
                const auto* transform = corpseLevel.transforms.Get(ai.ownerID);
                if (transform == nullptr) continue;
                const int index = MapQueries::FindSectorContainingPoint(corpseLevel.sectors, {transform->position.x, transform->position.z}, -1);
                if (index < 0) { std::printf("[corpse] outside the map: entity %d\n", static_cast<int>(ai.ownerID)); continue; }
                const float floorHeight = corpseLevel.sectors[index].floors[0].floor.height;
                if (transform->position.y < floorHeight - 1.0f)
                    std::printf("[corpse] BELOW FLOOR: entity %d at y %.1f, floor %.1f, in %s\n", static_cast<int>(ai.ownerID),
                                transform->position.y, floorHeight, corpseLevel.name.c_str());
                static std::map<ID, int> corpseAge;
                if (++corpseAge[ai.ownerID] > 12 && transform->position.y > floorHeight + 1.0f)
                    std::printf("[corpse] FLOATING: entity %d at y %.1f, floor %.1f, in %s\n", static_cast<int>(ai.ownerID),
                                transform->position.y, floorHeight, corpseLevel.name.c_str());
                static int corpseReports = 0;
                if (++corpseReports <= 5)
                    std::printf("[corpse] entity %d at y %.1f, floor %.1f\n", static_cast<int>(ai.ownerID), transform->position.y, floorHeight);
            }
            std::fflush(stdout);
        }

        // Says what music each level ended up with, a moment after it starts.
        static std::string musicLevel;
        static int musicFrame = 0;
        const Level& musicCurrent = LevelManager::CurrentLevel();
        if (musicCurrent.name != musicLevel) {
            musicLevel = musicCurrent.name;
            musicFrame = frame + 150;
        }
        if (frame == musicFrame) {
            const int index = MusicPlayer::CurrentIndex();
            const auto& tracks = musicCurrent.worldSettings.music.tracks;
            std::printf("[music] %s: list %d, index %d, %s, volume %.2f\n", musicCurrent.name.c_str(), MusicPlayer::TrackCount(), index,
                        index >= 0 && index < static_cast<int>(tracks.size()) ? tracks[index].c_str() : "(none)",
                        musicCurrent.worldSettings.music.volume);
            std::fflush(stdout);
        }
    }
    std::printf("[harness] stopped after %d frames (%.1f s), level %s\n", frame, frame / 60.0, LevelManager::CurrentLevel().name.c_str());
    std::fflush(stdout);

    RuntimeSession::Shutdown();
    RuntimeSession::Destroy();
    Editor::Destroy();
    return 0;
}
