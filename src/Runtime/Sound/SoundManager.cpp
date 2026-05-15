//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Sound/SoundManager.hpp"

#include <AL/al.h>
#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Loadables.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace {
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;

    std::vector<ALuint> buffers;
    std::unordered_map<std::string, ALuint> sources;

}

namespace SoundManager {
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

    bool LoadWAVToOpenALBuffer(const char *path, ALuint &buffer) {
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


    bool CreateSource(const std::string& name) {
        if (sources.contains(name)) {
            return true;
        }

        ALuint source = 0;
        alGenSources(1, &source);

        if (!CheckALErrors("alGenSource failed")) {
            spdlog::error("Failed to create OpenAL source {}", name);
            return false;
        }

        alSourcef(source, AL_GAIN, 1.0f);
        alSourcef(source, AL_PITCH, 1.0f);
        alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSourcei(source, AL_LOOPING, AL_FALSE);

        if (!CheckALErrors("Failed to configure OpenAL source")) {
            alDeleteSources(1, &source);
            return false;
        }

        sources[name] = source;

        spdlog::info("Created OpenAL Source {}", name);
        return true;
    }

    bool IsSourcePlaying(const std::string& sourceName) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            return false;
        }

        ALint state = 0;
        alGetSourcei(it->second, AL_SOURCE_STATE, &state);

        CheckALErrors("Failed to get source state");

        return state == AL_PLAYING;
    }

    void PlaySoundOnSourceIfNotPlaying(const std::string& sourceName, const int soundIndex) {
        if (IsSourcePlaying(sourceName)) {
            return;
        }

        PlaySoundOnSource(sourceName, soundIndex);
    }

    void GenerateSounds() {
        // Stop all sources before deleting old buffers.
        for (auto& source : sources | std::views::values) {
            alSourceStop(source);
            alSourcei(source, AL_BUFFER, 0);
        }

        // Delete previous buffers.
        for (ALuint& buffer : buffers) {
            if (buffer != 0) {
                alDeleteBuffers(1, &buffer);
                buffer = 0;
            }
        }

        buffers.clear();

        const fs::path soundsPath = ProjectManager::GetSoundsPath();
        const Level& level = LevelManager::CurrentLevel();

        buffers.resize(level.sounds.size(), 0);

        for (int i = 0; i < static_cast<int>(level.sounds.size()); ++i) {
            const Sound& sound = level.sounds[i];

            if (sound.fileName.empty()) {
                spdlog::warn("Skipping empty sound at index {}", i);
                continue;
            }

            fs::path soundPath = soundsPath / sound.fileName;

            if (!soundPath.has_extension()) {
                soundPath += ".wav";
            }

            ALuint buffer = 0;

            if (!LoadWAVToOpenALBuffer(soundPath.string().c_str(), buffer)) {
                spdlog::error(
                    "Failed to load sound index {}: {}",
                    i,
                    soundPath.string()
                );
                continue;
            }

            buffers[i] = buffer;

            spdlog::info(
                "Loaded sound index {} '{}' from {}",
                i,
                sound.fileName,
                soundPath.string()
            );
        }

        spdlog::info(
            "Generated {} sound buffer slot(s) from current level",
            buffers.size()
        );
    }

    bool InitializeOpenAL() {
        device = alcOpenDevice(nullptr);
        if (device == nullptr) {
            spdlog::critical("Failed to open OpenAL device");
            return false;
        }

        context = alcCreateContext(device, nullptr);
        if (context == nullptr) {
            spdlog::critical("Failed to create OpenAL context");
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }

        if (!alcMakeContextCurrent(context)) {
            spdlog::critical("Failed to make OpenAL context current");
            alcDestroyContext(context);
            alcCloseDevice(device);
            context = nullptr;
            device = nullptr;
            return false;
        }

        spdlog::info("OpenAL initialized");

        GenerateSounds();

        SetListenerPosition({0.0f, 0.0f, 0.0f});
        SetListenerVelocity({0.0f, 0.0f, 0.0f});
        SetListenerOrientation({0.0f, 0.0f, 0.0f});

        spdlog::info("Sounds generated");
        return true;
    }

    void DestroySource(const std::string& sourceName) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::warn("Tried to destroy missing source: {}", sourceName);
            return;
        }

        const ALuint source = it->second;

        alSourceStop(source);
        alDeleteSources(1, &source);

        sources.erase(it);

        spdlog::info("Destroyed OpenAL source '{}'", sourceName);
    }

    void DestroyOpenAL() {
        for (auto& source : sources | std::views::values) {
            alSourceStop(source);
            alDeleteSources(1, &source);
        }

        sources.clear();

        for (ALuint& buffer : buffers) {
            if (buffer != 0) {
                alDeleteBuffers(1, &buffer);
                buffer = 0;
            }
        }

        buffers.clear();

        if (context != nullptr) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
            context = nullptr;
        }

        if (device != nullptr) {
            alcCloseDevice(device);
            device = nullptr;
        }

        spdlog::info("OpenAL destroyed");
    }

    void PlaySoundOnSource(const std::string& sourceName, const int soundIndex) {
        spdlog::info("Playing sound index {}", soundIndex);

        const auto sourceIt = sources.find(sourceName);

        if (sourceIt == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        if (soundIndex < 0 || soundIndex >= static_cast<int>(buffers.size())) {
            spdlog::error(
                "Invalid sound index {}. Sound buffer count: {}",
                soundIndex,
                buffers.size()
            );
            return;
        }

        const ALuint buffer = buffers[soundIndex];

        if (buffer == 0) {
            spdlog::error("Sound index {} has no valid OpenAL buffer", soundIndex);
            return;
        }

        const ALuint source = sourceIt->second;

        alSourceStop(source);
        alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcePlay(source);

        CheckALErrors("Failed to play sound on source");

        spdlog::info("Played sound index {} on source '{}'", soundIndex, sourceName);
    }

    //region Setters

    // Check SoundManager.hpp for comments

    void SetSourcePitch(const std::string& sourceName, const float pitch) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_PITCH, pitch);
        CheckALErrors("Failed to set source pitch");
    }

    void SetSourceGain(const std::string& sourceName, const float gain) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_GAIN, gain);
        CheckALErrors("Failed to set source gain");
    }

    void SetSourcePosition(const std::string& sourceName, const Vector3 pos) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSource3f(it->second, AL_POSITION, pos.x, pos.y, pos.z);
        CheckALErrors("Failed to set source position");
    }

    void SetSourceLooping(const std::string& sourceName, const bool looping) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcei(it->second, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        CheckALErrors("Failed to set source looping");
    }

    void SetSourceReferenceDistance(const std::string& sourceName, float distance) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_REFERENCE_DISTANCE, distance);
        CheckALErrors("Failed to set AL_REFERENCE_DISTANCE");
    }

    void SetSourceMaxDistance(const std::string& sourceName, float maxDistance) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_MAX_DISTANCE, maxDistance);
        CheckALErrors("Failed to set source looping");
    }

    void SetSourceRollOffFactor(const std::string& sourceName, float rollOffFactor) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_ROLLOFF_FACTOR, rollOffFactor);
        CheckALErrors("Failed to set AL_ROLLOFF_FACTOR");
    }

    void SetSourceInnerConeAngle(const std::string& sourceName, float innerConeAngle) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_CONE_INNER_ANGLE, innerConeAngle);
        CheckALErrors("Failed to set AL_CONE_INNER_ANGLE");
    }

    void SetSourceOuterConeAngle(const std::string& sourceName, float outerConeAngle) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_CONE_OUTER_ANGLE, outerConeAngle);
        CheckALErrors("Failed to set AL_CONE_OUTER_ANGLE");
    }

    void SetSourceOuterGain(const std::string& sourceName, float outerGain) {
        const auto it = sources.find(sourceName);

        if (it == sources.end()) {
            spdlog::error("Source not found: {}", sourceName);
            return;
        }

        alSourcef(it->second, AL_CONE_OUTER_GAIN, outerGain);
        CheckALErrors("Failed to set AL_CONE_OUTER_GAIN");
    }

    void SetListenerPosition(const Vector3 pos) {
        alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
        CheckALErrors("Failed to set listener position");
    }

    void SetListenerVelocity(const Vector3 velocity) {
        alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
        CheckALErrors("Failed to set listener velocity");
    }

    void SetListenerOrientation(const Vector3& forward) {
        const ALfloat orientation[] = {
            forward.x,
            forward.y,
            forward.z,

            0.0f,
            1.0f,
            0.0f
        };

        alListenerfv(AL_ORIENTATION, orientation);
        CheckALErrors("Failed to set listener orientation");
    }

    void SetListenerGain(const float gain) {
        alListenerf(AL_GAIN, gain);
        CheckALErrors("Failed to set listener gain");
    }

    void SetListenerDistanceModel(const ALenum model) {
        alDistanceModel(model);
        CheckALErrors("Failed to set distance model");
    }

    void SetListenerDopplerFactor(const float factor) {
        alDopplerFactor(factor);
        CheckALErrors("Failed to set Doppler factor");
    }

    void SetListenerSpeedOfSound(const float speed) {
        alSpeedOfSound(speed);
        CheckALErrors("Failed to set speed of sound");
    }
    
    //endregion
}