//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Sound/SoundManager.hpp"

#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Loadables.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace {
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;

    std::unordered_map<std::string, ALuint> buffers;
    std::unordered_map<std::string, ALuint> sources;

    bool IsSupportedSoundExtension(std::string extension) {
        std::ranges::transform(
            extension,
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );

        return extension == ".wav";
    }

    std::string NormalizeSoundFileName(const std::string& fileName) {
        if (fileName.empty()) return {};
        return fs::path(fileName).lexically_normal().generic_string();
    }
}

namespace SoundManager {
    static bool CheckALErrors(const char* message) {
        const ALenum error = alGetError();

        if (error != AL_NO_ERROR) {
            spdlog::error("{}, OpenAL error code: {}", message, error);

            return false;
        }

        return true;
    }

    static ALenum GetOpenALFormat(const SDL_AudioSpec& spec) {
        if (spec.channels == 1) {
            if (spec.format == SDL_AUDIO_U8) return AL_FORMAT_MONO8;
            if (spec.format == SDL_AUDIO_S16LE) return AL_FORMAT_MONO16;
        }
        else if (spec.channels == 2) {
            if (spec.format == SDL_AUDIO_U8) return AL_FORMAT_STEREO8;
            if (spec.format == SDL_AUDIO_S16LE) return AL_FORMAT_STEREO16;
        }

        return 0;
    }

    bool LoadWAVToOpenALBuffer(const char* path, ALuint& buffer) {
        SDL_AudioSpec wavSpec{};
        Uint8* wavData = nullptr;
        Uint32 wavLength = 0;

        if (!SDL_LoadWAV(path, &wavSpec, &wavData, &wavLength)) {
            spdlog::error("SDL_LoadWAV failed for {}: {}", path, SDL_GetError());

            return false;
        }

        const ALenum format = GetOpenALFormat(wavSpec);

        if (format == 0) {
            spdlog::error(
                "Unsupported WAV format. Channels: {}, Format: {}. "
                "Use 8-bit or 16-bit PCM mono/stereo WAV",
                wavSpec.channels,
                static_cast<int>(wavSpec.format)
            );

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
        if (sources.contains(name)) return true;

        ALuint source = 0;
        alGenSources(1, &source);

        if (!CheckALErrors("alGenSources failed")) {
            spdlog::error("Failed to create OpenAL source '{}'", name);

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

        spdlog::info("Created OpenAL source '{}'", name);

        return true;
    }

    bool IsSourcePlaying(const std::string& sourceName) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) return false;

        ALint state = 0;
        alGetSourcei(iterator->second, AL_SOURCE_STATE, &state);

        CheckALErrors("Failed to get source state");

        return state == AL_PLAYING;
    }

    void PlaySoundOnSourceIfNotPlaying(const std::string& sourceName, const std::string& soundFileName) {
        if (IsSourcePlaying(sourceName)) return;
        PlaySoundOnSource(sourceName, soundFileName);
    }

    void GenerateSounds() {
        for (const ALuint source: sources | std::views::values) {
            alSourceStop(source);
            alSourcei(source, AL_BUFFER, 0);
        }

        for (auto &[fileName, buffer]: buffers) {
            if (buffer != 0) alDeleteBuffers(1, &buffer);
        }

        buffers.clear();

        const fs::path soundsPath = ProjectManager::GetSoundsPath();

        if (!fs::exists(soundsPath)) {
            std::error_code error;
            fs::create_directories(soundsPath, error);

            if (error) {
                spdlog::error("Failed to create Sounds folder {}: {}", soundsPath.string(), error.message());
                return;
            }
        }

        if (!fs::is_directory(soundsPath)) {
            spdlog::error("Sounds path is not a directory: {}", soundsPath.string());
            return;
        }

        std::error_code iteratorError;

        for (fs::recursive_directory_iterator iterator(soundsPath, iteratorError);
             !iteratorError && iterator != fs::recursive_directory_iterator(); iterator.increment(iteratorError)) {
            const fs::directory_entry &entry = *iterator;

            if (!entry.is_regular_file()) continue;
            if (!IsSupportedSoundExtension(entry.path().extension().string())) continue;

            std::error_code relativeError;
            const fs::path relativePath = fs::relative(entry.path(), soundsPath, relativeError);

            if (relativeError) {
                spdlog::error("Failed to create relative sound path for {}: {}", entry.path().string(),
                              relativeError.message());
                continue;
            }

            const std::string fileName = NormalizeSoundFileName(relativePath.generic_string());

            if (buffers.contains(fileName)) {
                spdlog::warn("Ignoring duplicate sound filename '{}'", fileName);
                continue;
            }

            ALuint buffer = 0;

            if (!LoadWAVToOpenALBuffer(entry.path().string().c_str(), buffer)) {
                spdlog::error("Failed to load sound '{}'", fileName);
                continue;
            }

            buffers.emplace(fileName, buffer);
            spdlog::info("Loaded sound '{}'", fileName);
        }

        if (iteratorError) spdlog::error("Failed while scanning Sounds folder {}: {}", soundsPath.string(),
                                         iteratorError.message());

        spdlog::info("Generated {} sound buffer(s)", buffers.size());
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
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::warn("Tried to destroy missing source: '{}'", sourceName);

            return;
        }

        const ALuint source = iterator->second;

        alSourceStop(source);
        alDeleteSources(1, &source);

        sources.erase(iterator);

        spdlog::info("Destroyed OpenAL source '{}'", sourceName);
    }

    void DestroyOpenAL() {
        for (ALuint source : sources | std::views::values) {
            alSourceStop(source);
            alDeleteSources(1, &source);
        }

        sources.clear();

        for (auto& [fileName, buffer] : buffers) {
            if (buffer == 0) continue;
            alDeleteBuffers(1, &buffer);
            buffer = 0;
        }

        buffers.clear();

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

    void PlaySoundOnSource(const std::string& sourceName, const std::string& soundFileName) {
        const auto sourceIterator = sources.find(sourceName);

        if (sourceIterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        const std::string normalizedFileName = NormalizeSoundFileName(soundFileName);
        const auto bufferIterator = buffers.find(normalizedFileName);

        if (bufferIterator == buffers.end()) {
            spdlog::error("Sound not found: '{}'", soundFileName);
            return;
        }

        const ALuint buffer = bufferIterator->second;

        if (buffer == 0) {
            spdlog::error("Sound '{}' has no valid OpenAL buffer", soundFileName);
            return;
        }

        const ALuint source = sourceIterator->second;

        alSourceStop(source);
        alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcePlay(source);

        CheckALErrors("Failed to play sound on source");
        spdlog::info("Played sound '{}' on source '{}'", soundFileName, sourceName);
    }
    // region Setters

    void SetSourcePitch(const std::string& sourceName,const float pitch) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_PITCH, pitch);
        CheckALErrors("Failed to set source pitch");
    }

    void SetSourceGain(const std::string& sourceName, const float gain) {
        const auto iterator =sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second,AL_GAIN, gain);
        CheckALErrors("Failed to set source gain");
    }

    void SetSourcePosition(const std::string& sourceName, const Vector3 pos) {
        const auto iterator =sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSource3f(iterator->second, AL_POSITION, pos.x, pos.y, pos.z);
        CheckALErrors("Failed to set source position");
    }

    void SetSourceLooping(const std::string& sourceName, const bool looping) {
        const auto iterator =sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcei(iterator->second, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
        CheckALErrors("Failed to set source looping");
    }

    void SetSourceReferenceDistance(const std::string& sourceName, const float distance) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_REFERENCE_DISTANCE, distance);
        CheckALErrors("Failed to set AL_REFERENCE_DISTANCE");
    }

    void SetSourceMaxDistance(const std::string& sourceName, const float maxDistance) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_MAX_DISTANCE, maxDistance);
        CheckALErrors("Failed to set AL_MAX_DISTANCE");
    }

    void SetSourceRollOffFactor(const std::string& sourceName, const float rollOffFactor) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_ROLLOFF_FACTOR, rollOffFactor);
        CheckALErrors("Failed to set AL_ROLLOFF_FACTOR");
    }

    void SetSourceInnerConeAngle(const std::string& sourceName,const float innerConeAngle) {
        const auto iterator =sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_CONE_INNER_ANGLE, innerConeAngle);
        CheckALErrors("Failed to set AL_CONE_INNER_ANGLE");
    }

    void SetSourceOuterConeAngle(const std::string& sourceName, const float outerConeAngle) {
        const auto iterator =sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_CONE_OUTER_ANGLE, outerConeAngle);
        CheckALErrors("Failed to set AL_CONE_OUTER_ANGLE");
    }

    void SetSourceOuterGain(const std::string& sourceName, const float outerGain) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_CONE_OUTER_GAIN, outerGain);
        CheckALErrors("Failed to set AL_CONE_OUTER_GAIN");
    }

    void SetListenerPosition(const Vector3& pos) {
        alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
        CheckALErrors("Failed to set listener position");
    }

    void SetListenerVelocity(const Vector3 &velocity) {
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

    // endregion
}