#include "../../Headers/Engine/GameTime.hpp"
#include "../../Headers/Engine/InputManager.hpp"

#include "../../Headers/Engine/Local/Local.hpp"
#include "../../Headers/Project/ProjectManager.hpp"

#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "../../Headers/MapEditor/MapEditor.hpp"
#include "../../Headers/Renderer/TextureManager.hpp"

#include "../../Headers/Map/MapQueries.hpp"
#include "../../Headers/Map/LevelManager.hpp"

#include "../../Headers/Objects/Player.hpp"
#include "../../Headers/Objects/Sector.hpp"

#include "../MapEditor/MapEditorInternal.hpp"
#include "../Renderer/Renderer/RendererInternal.hpp"

#include <AL/al.h>
#include <AL/alc.h>

#include <SDL3/SDL_audio.h>
#include <thread>
#include <chrono>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 960

namespace fs = std::filesystem;

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

static bool CheckALErrors(const char *message) {
    const ALenum error = alGetError();

    if (error != AL_NO_ERROR) {
        spdlog::error("{}, OpenAL error code: {}", message, error);
        return false;
    }

    return true;
}

static ALenum GetOpenALFormat(const SDL_AudioSpec &spec) {
    if (spec.channels == 1) {
        if (spec.format == SDL_AUDIO_U8) return AL_FORMAT_MONO8;
        else if (spec.format == SDL_AUDIO_S16LE) return AL_FORMAT_MONO16;
    } else if (spec.channels == 2) {
        if (spec.format == SDL_AUDIO_U8) return AL_FORMAT_STEREO8;
        else if (spec.format == SDL_AUDIO_S16LE) return AL_FORMAT_STEREO16;
    }

    return 0;
}

static bool LoadWAVToOpenALBuffer(const char *path, ALuint &buffer) {
    SDL_AudioSpec wavSpec{};
    Uint8 *wavData = nullptr;
    Uint32 wavLength = 0;

    if (!SDL_LoadWAV(path, &wavSpec, &wavData, &wavLength)) {
        spdlog::error("SDL_LoadWAV Failed for {} : {}", path, SDL_GetError());
        return false;
    }

    const ALenum format = GetOpenALFormat(wavSpec);
    if (format == 0) {
        spdlog::error("Unsupported WAV format. Channels: {}, Fromat: {},  Use 8-bit or 16-bit PCM mono/stereo WAV",
                      wavSpec.channels, static_cast<int>(wavSpec.format));

        SDL_free(wavData);
        return false;
    }

    alGenBuffers(1, &buffer);

    if (!CheckALErrors("alGenBuffers failed")) {
        SDL_free(wavData);
        return false;
    }

    alBufferData(buffer, format, wavData, static_cast<ALsizei>(wavLength), wavSpec.freq);

    SDL_free(wavData);

    if (!CheckALErrors("alBufferData failed")) {
        alDeleteBuffers(1, &buffer);
        buffer = 0;
        return false;
    }

    spdlog::info("Loaded WAV into OpenAL buffer: {}", path);
    return true;
}

static void TempOpenAL() {
    ALCdevice *device = alcOpenDevice(nullptr);
    if (device == nullptr) {
        spdlog::critical("Failed to open OpenAL device");
        return;
    }

    ALCcontext *context = alcCreateContext(device, nullptr);
    if (context == nullptr) {
        spdlog::critical("Failed to create OpenAL context");
        alcCloseDevice(device);
        return;
    }

    if (!alcMakeContextCurrent(context)) {
        spdlog::critical("Failed to make OpenAL context current");
        alcDestroyContext(context);
        alcCloseDevice(device);
        return;
    }

    spdlog::info("OpenAL initialized");

    ALuint buffer = 0;

    if (!LoadWAVToOpenALBuffer("C:/Users/berke/Desktop/CLion Projects/Wolfy Engine/Shoot.wav", buffer)) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
        alcCloseDevice(device);
        return;
    }

    ALuint source = 0;
    alGenSources(1, &source);

    if (!CheckALErrors("alGenSources failed")) {
        alDeleteBuffers(1, &buffer);
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
        alcCloseDevice(device);
        return;
    }

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, 1.0f);
    alSourcef(source, AL_PITCH, 1.0f);

    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);

    if (!CheckALErrors("Failed to open openAL source.")) {
        alDeleteSources(1, &source);
        alDeleteBuffers(1, &buffer);
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
        alcCloseDevice(device);
        return;
    }

    alSourcePlay(source);

    spdlog::info("Playing Shoot.wav");

    ALint state = AL_PLAYING;
    while (state == AL_PLAYING) {
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("Finished playing");

    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context);
    alcCloseDevice(device);
}

int main(int argc, char** argv) {
    InitEngineLogger();

    spdlog::info("Engine started");

    TempOpenAL();
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

    //Localisation::LoadLanguage("tr");

    //todo: Proper Game Initilization

    bool editorMode = true;
    if (editorMode) MapEditor::Start();

    while (editorMode) {
        InputManager::BeginFrame();
        editorMode = !(MapEditor::QuitRequested());
        MapEditor::Update();
    }

    MapEditor::Destroy();

    MapEditor::LoadLevel(MapEditor::currentMap);

    if (!LevelManager::HasCurrentLevel()) {
        spdlog::critical("No current level loaded");
        return 1;
    }

    Level& level = LevelManager::CurrentLevel();

    Player::position = MapEditor::playerStartPos;

    MapQueries::AssignWallsToSectors(
        level.sectors,
        level.walls
    );

    Player::Start(level.sectors);

    if (!Renderer::Initialize()) {
        spdlog::critical("Failed to initialize renderer {}", SDL_GetError());
        return 1;
    }

    for (const auto& pathInput : MapEditorInternal::textureInputs) {
        if (pathInput[0] == '\0') {
            continue;
        }

        const int textureIndex = TextureManager::CreateTexture(pathInput.data());

        if (textureIndex == -1) {
            spdlog::error("Failed to load texture {}", pathInput.data());
        }
    }

    RendererInternal::backgroundTextureIndex = MapEditor::backgroundTextureIndex;

    if (Player::currentSector == -1) {
        spdlog::warn("Player is not inside any sector");
    }

    if (!Renderer::CreateMap()) {
        spdlog::critical("Failed to create map");
        return 1;
    }

    InputManager::SetRelativeMouseMode(Renderer::window, true);

    static float timer = 0;
    static float timerHelper = 0;

    static int fps = 0;

    spdlog::info("Starting the game loop");
    bool running = true;
    while (running) {
        timer = GameTime::time;

        InputManager::BeginFrame();
        GameTime::Update();

        Player::Update(
            level.walls,
            level.sectors
        );

        running =
            !InputManager::GetKeyDown(SDL_SCANCODE_ESCAPE) &&
            !InputManager::QuitRequested();

        if (InputManager::GetKeyDown(SDL_SCANCODE_1)) {
            Player::position.x += 1;
        }

        Renderer::Update(Player::position, Player::angle);

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

        SDL_GL_SwapWindow(Renderer::window);

        if (timer > timerHelper + 1.3f) {
            fps = static_cast<int>(GameTime::GetFPS());
            timerHelper = timer;
        }
    }

    spdlog::info("Finished the game loop");
    Renderer::Destroy();

    return 0;
}