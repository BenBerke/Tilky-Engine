//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Sound/SoundManager.hpp"
#include "Headers/Engine/AssetFS.hpp"

#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "Headers/Engine/BuildVariant.hpp"
#include "Headers/Engine/FolderWatcher.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Loadables.hpp"
#include "Headers/Project/AssetSets.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Sound/SoundDecoders.hpp"

namespace fs = std::filesystem;

namespace {
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;

    std::unordered_map<std::string, ALuint> buffers;
    // When each buffer's file was last written, so GenerateSounds() only
    // re-decodes files that changed since it last ran.
    std::unordered_map<std::string, fs::file_time_type> bufferWriteTimes;
    std::unordered_map<std::string, ALuint> sources;

    // Counts sound files under Assets being added, removed, renamed or
    // written, so BeginSession() can skip GenerateSounds() while none were.
    // Its walk of Assets took ~0.6 s on a 2,300-file project, on every Play.
    FolderWatcher soundWatcher({".wav", ".mp3", ".ogg", ".flac"});
    std::uint64_t generatedSoundGeneration = 0;

    bool IsSupportedSoundExtension(std::string extension) {
        std::ranges::transform(
            extension,
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );

        return extension == ".wav" || SoundDecoders::IsCompressedExtension(extension);
    }

    // Sounds are keyed by their path inside Assets, extension included, as
    // the inspector stores it (AssetBrowser::ToAssetReference): "open.wav"
    // and "open.mp3" are two sounds, and "open" is neither.
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

    // A sound file read and decoded, ready for alBufferData. Made on any
    // thread: decoding touches no OpenAL state, so the editor's background
    // load runs it on worker threads and only the upload on the main one.
    struct DecodedSound {
        // 16-bit PCM from SoundDecoders, or SDL_LoadWAV's own buffer.
        std::vector<int16_t> pcm;
        Uint8* wavData = nullptr;
        Uint32 wavLength = 0;

        ALenum format = 0;
        int frequency = 0;

        DecodedSound() = default;
        DecodedSound(const DecodedSound&) = delete;
        DecodedSound& operator=(const DecodedSound&) = delete;

        DecodedSound(DecodedSound&& other) noexcept
            : pcm(std::move(other.pcm)), wavData(std::exchange(other.wavData, nullptr)),
              wavLength(std::exchange(other.wavLength, 0)), format(other.format), frequency(other.frequency) {}

        DecodedSound& operator=(DecodedSound&& other) noexcept {
            if (this != &other) {
                SDL_free(wavData);
                pcm = std::move(other.pcm);
                wavData = std::exchange(other.wavData, nullptr);
                wavLength = std::exchange(other.wavLength, 0);
                format = other.format;
                frequency = other.frequency;
            }
            return *this;
        }

        ~DecodedSound() { SDL_free(wavData); }

        [[nodiscard]] const void* Data() const {
            return wavData != nullptr ? static_cast<const void*>(wavData) : static_cast<const void*>(pcm.data());
        }

        [[nodiscard]] ALsizei Size() const {
            return static_cast<ALsizei>(wavData != nullptr ? wavLength : pcm.size() * sizeof(int16_t));
        }
    };

    // WAV through SDL, MP3 / OGG / FLAC through SoundDecoders. Safe on any thread.
    bool DecodeSoundFile(const fs::path& path, DecodedSound& out) {
        const std::string pathText = path.string();

        if (!SoundDecoders::IsCompressedExtension(path.extension().string())) {
            SDL_AudioSpec wavSpec{};

            bool loaded = false;
            if (AssetFS::IsPacked()) {
                std::vector<unsigned char> bytes;
                SDL_IOStream* stream = AssetFS::ReadFile(path, bytes) ? SDL_IOFromConstMem(bytes.data(), bytes.size()) : nullptr;
                if (stream == nullptr) SDL_SetError("%s isn't in the game's data", pathText.c_str());
                loaded = stream != nullptr && SDL_LoadWAV_IO(stream, true, &wavSpec, &out.wavData, &out.wavLength);
            }
            else loaded = SDL_LoadWAV(pathText.c_str(), &wavSpec, &out.wavData, &out.wavLength);

            if (!loaded) {
                spdlog::error("SDL_LoadWAV failed for {}: {}", pathText, SDL_GetError());
                return false;
            }

            out.format = GetOpenALFormat(wavSpec);
            out.frequency = wavSpec.freq;

            if (out.format == 0) {
                spdlog::error(
                    "Unsupported WAV format. Channels: {}, Format: {}. "
                    "Use 8-bit or 16-bit PCM mono/stereo WAV",
                    wavSpec.channels,
                    static_cast<int>(wavSpec.format)
                );
                return false;
            }

            return true;
        }

        SoundDecoders::PCM pcm;
        std::string error;
        if (!SoundDecoders::Decode(path, pcm, error)) {
            spdlog::error("Couldn't decode {}: {}", pathText, error);
            return false;
        }

        if (pcm.channels == 1) out.format = AL_FORMAT_MONO16;
        else if (pcm.channels == 2) out.format = AL_FORMAT_STEREO16;

        if (out.format == 0) {
            spdlog::error("Unsupported sound {}: {} channels; use mono or stereo", pathText, pcm.channels);
            return false;
        }

        out.frequency = pcm.sampleRate;
        out.pcm = std::move(pcm.samples);
        return true;
    }

    // Hands a decoded sound to a new buffer. Main thread.
    bool UploadDecodedSound(const DecodedSound& sound, const fs::path& path, ALuint& buffer) {
        alGenBuffers(1, &buffer);
        if (!CheckALErrors("alGenBuffers failed")) return false;

        alBufferData(buffer, sound.format, sound.Data(), sound.Size(), sound.frequency);

        if (!CheckALErrors("alBufferData failed")) {
            alDeleteBuffers(1, &buffer);
            buffer = 0;
            return false;
        }

        spdlog::info("Loaded sound into OpenAL buffer: {}", path.string());
        return true;
    }

    bool LoadSoundToOpenALBuffer(const fs::path& path, ALuint& buffer) {
        DecodedSound sound;
        return DecodeSoundFile(path, sound) && UploadDecodedSound(sound, path, buffer);
    }

    // A sound file under Assets, as GenerateSounds() and the background load find it.
    struct SoundFile {
        std::string fileName; // the key: its path inside Assets
        fs::path path;
        fs::file_time_type writeTime;
        bool hasWriteTime = false;
    };

    // Every sound under Assets, one per key. Touches no OpenAL state, so
    // the background load runs it on its own thread.
    std::vector<SoundFile> ListSoundFiles(const fs::path& assetsPath) {
        std::vector<SoundFile> files;

        // Sounds can live anywhere under Assets, like textures and models.

        if (!AssetFS::IsDirectory(assetsPath)) {
            spdlog::error("Assets path is not a directory: {}", assetsPath.string());
            return files;
        }

        std::unordered_set<std::string> seen;

        if (AssetFS::IsPacked()) {
            for (const fs::path& path : AssetFS::ListPackedFiles(assetsPath, true)) {
                if (!IsSupportedSoundExtension(path.extension().string())) continue;

                SoundFile file;
                file.fileName = NormalizeSoundFileName(path.lexically_relative(assetsPath).generic_string());

                if (!seen.insert(file.fileName).second) {
                    spdlog::warn("Ignoring duplicate sound filename '{}'", file.fileName);
                    continue;
                }

                file.path = path;
                file.hasWriteTime = AssetFS::WriteTime(path, file.writeTime);
                files.push_back(std::move(file));
            }
            return files;
        }
        std::error_code iteratorError;

        for (fs::recursive_directory_iterator iterator(assetsPath, iteratorError);
             !iteratorError && iterator != fs::recursive_directory_iterator(); iterator.increment(iteratorError)) {
            const fs::directory_entry &entry = *iterator;

            if (!entry.is_regular_file()) continue;
            if (!IsSupportedSoundExtension(entry.path().extension().string())) continue;

            std::error_code relativeError;
            const fs::path relativePath = fs::relative(entry.path(), assetsPath, relativeError);

            if (relativeError) {
                spdlog::error("Failed to create relative sound path for {}: {}", entry.path().string(),
                              relativeError.message());
                continue;
            }

            SoundFile file;
            file.fileName = NormalizeSoundFileName(relativePath.generic_string());

            if (!seen.insert(file.fileName).second) {
                spdlog::warn("Ignoring duplicate sound filename '{}'", file.fileName);
                continue;
            }

            file.path = entry.path();

            std::error_code timeError;
            file.writeTime = entry.last_write_time(timeError);
            file.hasWriteTime = !timeError;

            files.push_back(std::move(file));
        }

        if (iteratorError) spdlog::error("Failed while scanning Assets folder {}: {}", assetsPath.string(),
                                         iteratorError.message());

        return files;
    }

    // The editor's first load of every sound, off the main thread so the
    // editor opens at once. One manager thread lists the files, then it and
    // its helpers decode them; the main thread uploads what is ready each
    // frame (UpdateBackgroundLoad). Play waits for it (FinishBackgroundLoad).
    struct BackgroundSoundLoad {
        struct Result {
            size_t file = 0;
            bool decoded = false;
            DecodedSound sound;
        };

        fs::path assetsPath;            // set before the manager starts
        std::vector<SoundFile> files;   // written by the manager before `listed`
        std::atomic<bool> listed{false};
        std::atomic<size_t> nextFile{0};
        std::atomic<bool> cancelled{false};
        std::atomic<bool> workersDone{false};

        std::mutex mutex;
        std::condition_variable changed;
        std::vector<Result> ready;      // guarded by mutex

        std::thread manager;

        // Main thread only.
        size_t handled = 0;
        size_t loaded = 0;

        BackgroundSoundLoad() = default;
        BackgroundSoundLoad(const BackgroundSoundLoad&) = delete;
        BackgroundSoundLoad& operator=(const BackgroundSoundLoad&) = delete;

        // Never leaves a thread running, even when the engine exits
        // without DestroyOpenAL.
        ~BackgroundSoundLoad() {
            cancelled = true;
            if (manager.joinable()) manager.join();
        }
    };

    std::unique_ptr<BackgroundSoundLoad> backgroundLoad;

    void DecodeBackgroundSounds(BackgroundSoundLoad& job) {
        while (!job.cancelled) {
            const size_t index = job.nextFile.fetch_add(1);
            if (index >= job.files.size()) return;

            BackgroundSoundLoad::Result result;
            result.file = index;
            result.decoded = DecodeSoundFile(job.files[index].path, result.sound);

            {
                std::lock_guard lock(job.mutex);
                job.ready.push_back(std::move(result));
            }
            job.changed.notify_all();
        }
    }

    void RunBackgroundSoundLoad(BackgroundSoundLoad& job) {
        job.files = ListSoundFiles(job.assetsPath);
        job.listed = true;

        // Up to six threads, leaving cores for the editor and the atlas load.
        const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
        const size_t helperCount = std::min<size_t>({cores - 2, 5, job.files.size() > 0 ? job.files.size() - 1 : 0});

        std::vector<std::thread> helpers;
        helpers.reserve(helperCount);
        for (size_t i = 0; i < helperCount; ++i) helpers.emplace_back(DecodeBackgroundSounds, std::ref(job));

        DecodeBackgroundSounds(job);
        for (std::thread& helper : helpers) helper.join();

        {
            std::lock_guard lock(job.mutex);
            job.workersDone = true;
        }
        job.changed.notify_all();
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

    void StopSource(const std::string& sourceName) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourceStop(iterator->second);
        CheckALErrors("Failed to stop source");
    }

    void PauseSource(const std::string& sourceName) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcePause(iterator->second);
        CheckALErrors("Failed to pause source");
    }

    bool ResumeSource(const std::string& sourceName) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return false;
        }

        ALint state = 0;
        alGetSourcei(iterator->second, AL_SOURCE_STATE, &state);

        // alSourcePlay on a stopped source would restart it, which is Play's job.
        if (state != AL_PAUSED) return false;

        alSourcePlay(iterator->second);
        return CheckALErrors("Failed to resume source");
    }

    void CancelBackgroundLoad() {
        // Its destructor stops the threads and waits for them.
        backgroundLoad.reset();
    }

    void GenerateSounds() {
        // A full reload replaces whatever the background load had left to do.
        CancelBackgroundLoad();

        for (const ALuint source: sources | std::views::values) {
            alSourceStop(source);
            alSourcei(source, AL_BUFFER, 0);
        }

        // Unchanged files keep their buffer; the rest are deleted below.
        std::unordered_map<std::string, ALuint> previousBuffers = std::move(buffers);
        std::unordered_map<std::string, fs::file_time_type> previousWriteTimes = std::move(bufferWriteTimes);
        buffers.clear();
        bufferWriteTimes.clear();

        int reusedCount = 0;

        // An exported game with per-level sets loads the level's own; the
        // rest come in the moment they're used (see FindOrLoadBuffer).
        const bool perLevel = AssetFS::IsPacked() && AssetSets::HasPackedSets();
        int skippedCount = 0;

        for (const SoundFile& file : ListSoundFiles(ProjectManager::GetAssetsPath())) {
            if (perLevel && !AssetSets::InCurrentSet(file.fileName)) {
                ++skippedCount;
                continue;
            }

            if (const auto previous = previousBuffers.find(file.fileName);
                file.hasWriteTime && previous != previousBuffers.end() && previous->second != 0 &&
                previousWriteTimes[file.fileName] == file.writeTime) {
                buffers.emplace(file.fileName, previous->second);
                bufferWriteTimes.emplace(file.fileName, file.writeTime);
                previousBuffers.erase(previous);
                ++reusedCount;
                continue;
            }

            ALuint buffer = 0;

            if (!LoadSoundToOpenALBuffer(file.path, buffer)) {
                spdlog::error("Failed to load sound '{}'", file.fileName);
                continue;
            }

            buffers.emplace(file.fileName, buffer);
            if (file.hasWriteTime) bufferWriteTimes.emplace(file.fileName, file.writeTime);
            spdlog::info("Loaded sound '{}'", file.fileName);
        }

        for (auto &[fileName, buffer]: previousBuffers)
            if (buffer != 0) alDeleteBuffers(1, &buffer);

        spdlog::info("Generated {} sound buffer(s), {} reused unchanged", buffers.size(), reusedCount);
        if (skippedCount > 0) spdlog::info("{} sound(s) aren't in this level's set and load when used", skippedCount);
    }

    // The buffer for a sound key; outside the level's set it is loaded now
    // (exported) or just reported (editor Play). 0 if there's no such sound.
    static ALuint FindOrLoadBuffer(const std::string& normalizedFileName) {
        if (!AssetSets::InCurrentSet(normalizedFileName)) AssetSets::NoteOnDemandUse(normalizedFileName, "Sound");

        if (const auto it = buffers.find(normalizedFileName); it != buffers.end()) return it->second;
        if (!AssetFS::IsPacked() || normalizedFileName.empty()) return 0;

        const fs::path path = ProjectManager::GetAssetsPath() / normalizedFileName;
        if (!IsSupportedSoundExtension(path.extension().string()) || !AssetFS::IsFile(path)) return 0;

        ALuint buffer = 0;
        if (!LoadSoundToOpenALBuffer(path, buffer)) return 0;

        buffers.emplace(normalizedFileName, buffer);
        spdlog::info("Loaded sound '{}' on demand", normalizedFileName);
        return buffer;
    }

    // BeginLevelPreload's decode; FinishLevelPreload moves it to preDecodedSounds.
    struct LevelSoundPreload {
        std::vector<SoundFile> files;
        std::vector<std::optional<DecodedSound>> decoded;
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
        std::atomic<bool> cancelled{false};
        std::vector<std::thread> threads;

        ~LevelSoundPreload() {
            cancelled = true;
            for (std::thread& thread : threads)
                if (thread.joinable()) thread.join();
        }
    };

    std::unique_ptr<LevelSoundPreload> levelPreload;
    std::unordered_map<std::string, DecodedSound> preDecodedSounds; // key -> decoded, for PrepareForLevel

    void BeginLevelPreload(const std::vector<std::string>& needs) {
        FinishLevelPreload();

        const std::unordered_set<std::string> wanted(needs.begin(), needs.end());
        auto job = std::make_unique<LevelSoundPreload>();
        for (SoundFile& file : ListSoundFiles(ProjectManager::GetAssetsPath()))
            if (wanted.contains(file.fileName) && !buffers.contains(file.fileName)) job->files.push_back(std::move(file));
        if (job->files.empty()) return;

        job->decoded.resize(job->files.size());

        const auto work = [raw = job.get()] {
            while (!raw->cancelled) {
                const size_t index = raw->next.fetch_add(1);
                if (index >= raw->files.size()) return;

                DecodedSound sound;
                if (DecodeSoundFile(raw->files[index].path, sound)) raw->decoded[index] = std::move(sound);
                raw->done.fetch_add(1);
            }
        };

        const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
        const size_t threadCount = std::min<size_t>({cores - 1, 4, job->files.size()});
        for (size_t i = 0; i < threadCount; ++i) job->threads.emplace_back(work);

        levelPreload = std::move(job);
    }

    std::pair<size_t, size_t> LevelPreloadProgress() {
        if (!levelPreload) return {0, 0};
        return {levelPreload->done.load(), levelPreload->files.size()};
    }

    void FinishLevelPreload() {
        if (!levelPreload) return;

        const std::unique_ptr<LevelSoundPreload> job = std::move(levelPreload);
        for (std::thread& thread : job->threads)
            if (thread.joinable()) thread.join();

        for (size_t i = 0; i < job->files.size(); ++i)
            if (job->decoded[i]) preDecodedSounds.insert_or_assign(job->files[i].fileName, std::move(*job->decoded[i]));
    }

    void PrepareForLevel(const Level& level) {
        if (!AssetFS::IsPacked() || !AssetSets::HasPackedSets() || context == nullptr) return;

        // Kept: the level's set, what the level's sources (carried entities
        // among them) name, and whatever is playing or paused (the music).
        std::unordered_set<std::string> keep;
        for (const auto& name : buffers | std::views::keys)
            if (AssetSets::InCurrentSet(name)) keep.insert(name);
        for (const ComponentAudioSource& audio : level.audioSources.components)
            if (!audio.soundFileName.empty()) keep.insert(NormalizeSoundFileName(audio.soundFileName));

        std::unordered_set<ALuint> inUse;
        for (const ALuint source : sources | std::views::values) {
            ALint state = AL_INITIAL;
            ALint attached = 0;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            alGetSourcei(source, AL_BUFFER, &attached);
            if (attached == 0) continue;
            if (state == AL_PLAYING || state == AL_PAUSED) inUse.insert(static_cast<ALuint>(attached));
        }

        int freed = 0;
        for (auto it = buffers.begin(); it != buffers.end();) {
            if (keep.contains(it->first) || inUse.contains(it->second)) {
                ++it;
                continue;
            }

            // A stopped source may still hold it.
            for (const ALuint source : sources | std::views::values) {
                ALint attached = 0;
                alGetSourcei(source, AL_BUFFER, &attached);
                if (static_cast<ALuint>(attached) == it->second) alSourcei(source, AL_BUFFER, 0);
            }

            if (it->second != 0) alDeleteBuffers(1, &it->second);
            bufferWriteTimes.erase(it->first);
            it = buffers.erase(it);
            ++freed;
        }

        // The new level's set, loaded now rather than on first use.
        int loaded = 0;
        for (const SoundFile& file : ListSoundFiles(ProjectManager::GetAssetsPath())) {
            if (buffers.contains(file.fileName) || !AssetSets::InCurrentSet(file.fileName)) continue;

            ALuint buffer = 0;
            const auto preDecoded = preDecodedSounds.find(file.fileName);
            const bool uploaded = preDecoded != preDecodedSounds.end() ? UploadDecodedSound(preDecoded->second, file.path, buffer)
                                                                       : LoadSoundToOpenALBuffer(file.path, buffer);
            if (!uploaded) {
                spdlog::error("Failed to load sound '{}'", file.fileName);
                continue;
            }
            buffers.emplace(file.fileName, buffer);
            ++loaded;
        }

        preDecodedSounds.clear();
        CheckALErrors("Failed to swap the level's sounds");
        spdlog::info("Level sounds: {} loaded, {} freed, {} in memory", loaded, freed, buffers.size());
    }

    // Uploads the decoded sounds waiting for it, for at most `budget` of this
    // frame (one at least), and ends the load once every file is in.
    static void PumpBackgroundLoad(const std::chrono::steady_clock::duration budget) {
        if (!backgroundLoad) return;
        BackgroundSoundLoad& job = *backgroundLoad;

        const auto start = std::chrono::steady_clock::now();
        bool workersDone = false;

        while (true) {
            std::vector<BackgroundSoundLoad::Result> results;
            {
                std::lock_guard lock(job.mutex);
                workersDone = job.workersDone;
                results.swap(job.ready);
            }

            for (size_t i = 0; i < results.size(); ++i) {
                // Out of time: the rest wait for the next frame, in front.
                if (i > 0 && std::chrono::steady_clock::now() - start >= budget) {
                    std::lock_guard lock(job.mutex);
                    job.ready.insert(job.ready.begin(), std::make_move_iterator(results.begin() + static_cast<std::ptrdiff_t>(i)),
                                     std::make_move_iterator(results.end()));
                    return;
                }

                BackgroundSoundLoad::Result& result = results[i];
                const SoundFile& file = job.files[result.file];
                ++job.handled;

                // Previewed in the inspector meanwhile, which loaded it then.
                if (buffers.contains(file.fileName)) {
                    ++job.loaded;
                    continue;
                }

                ALuint buffer = 0;
                if (!result.decoded || !UploadDecodedSound(result.sound, file.path, buffer)) {
                    spdlog::error("Failed to load sound '{}'", file.fileName);
                    continue;
                }

                buffers.emplace(file.fileName, buffer);
                if (file.hasWriteTime) bufferWriteTimes.emplace(file.fileName, file.writeTime);
                ++job.loaded;
                spdlog::info("Loaded sound '{}'", file.fileName);
            }

            // Results that came in while uploading go too, time allowing.
            if (workersDone || std::chrono::steady_clock::now() - start >= budget) break;

            std::lock_guard lock(job.mutex);
            if (job.ready.empty()) break;
        }

        if (!workersDone) return;

        {
            std::lock_guard lock(job.mutex);
            if (!job.ready.empty()) return;
        }

        job.manager.join();
        spdlog::info("Generated {} sound buffer(s) in the background", job.loaded);
        backgroundLoad.reset();
    }

    void UpdateBackgroundLoad() {
        // A few milliseconds a frame, so the editor stays smooth.
        PumpBackgroundLoad(std::chrono::milliseconds(4));
    }

    void FinishBackgroundLoad() {
        if (!backgroundLoad) return;

        spdlog::info("Waiting for the background sound load to finish");

        while (backgroundLoad) {
            PumpBackgroundLoad(std::chrono::steady_clock::duration::max());
            if (!backgroundLoad) break;

            BackgroundSoundLoad& job = *backgroundLoad;
            std::unique_lock lock(job.mutex);
            job.changed.wait_for(lock, std::chrono::milliseconds(50), [&job] { return !job.ready.empty() || job.workersDone; });
        }
    }

    bool IsBackgroundLoading() {
        return backgroundLoad != nullptr;
    }

    BackgroundLoadProgress GetBackgroundLoadProgress() {
        if (!backgroundLoad) return {};

        BackgroundLoadProgress progress;
        progress.listed = backgroundLoad->listed;
        if (progress.listed) progress.total = backgroundLoad->files.size();
        progress.done = backgroundLoad->handled;
        return progress;
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

        // The engine keeps the device open between sessions; read before
        // the walk, so a change made during it is picked up next session.
        // The exported game loads its sounds once and watches nothing.
        if (!BuildVariant::IsStandalone()) generatedSoundGeneration = soundWatcher.Generation(ProjectManager::GetAssetsPath());

        // The exported game has every sound before its first frame. The
        // editor opens at once and loads them in the background; Play waits
        // for the rest (BeginSession).
        if (BuildVariant::IsStandalone()) GenerateSounds();
        else {
            backgroundLoad = std::make_unique<BackgroundSoundLoad>();
            backgroundLoad->assetsPath = ProjectManager::GetAssetsPath();
            backgroundLoad->manager = std::thread(RunBackgroundSoundLoad, std::ref(*backgroundLoad));
        }

        SetListenerPosition({0.0f, 0.0f, 0.0f});
        SetListenerVelocity({0.0f, 0.0f, 0.0f});
        // Same as a camera's default forward. A zero vector is not a valid
        // orientation, and sounds started before a camera places the listener
        // would play at full volume, unpositioned.
        SetListenerOrientation({0.0f, 0.0f, 1.0f});

        spdlog::info(backgroundLoad ? "Sounds loading in the background" : "Sounds generated");

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

    void BeginSession() {
        // Play never starts with sounds missing.
        FinishBackgroundLoad();

        // Picks up sounds added, removed or edited since the last session.
        // Read before the walk, like in InitializeOpenAL().
        if (const std::uint64_t generation = soundWatcher.Generation(ProjectManager::GetAssetsPath());
            generation != generatedSoundGeneration) {
            generatedSoundGeneration = generation;
            GenerateSounds();
        }

        SetListenerPosition({0.0f, 0.0f, 0.0f});
        SetListenerVelocity({0.0f, 0.0f, 0.0f});
        SetListenerOrientation({0.0f, 0.0f, 1.0f});
    }

    void EndSession() {
        // Sources a script made without an audio component outlive
        // AudioSystem::Shutdown, so clear whatever is left.
        for (const ALuint source : sources | std::views::values) {
            alSourceStop(source);
            alDeleteSources(1, &source);
        }

        sources.clear();
    }

    void DestroyOpenAL() {
        CancelBackgroundLoad();

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
        bufferWriteTimes.clear();

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

    bool HasSound(const std::string& soundFileName) {
        const std::string name = NormalizeSoundFileName(soundFileName);
        if (buffers.contains(name)) return true;

        // Exported: a sound outside the level's set isn't loaded, but is there.
        if (!AssetFS::IsPacked() || name.empty()) return false;
        const fs::path path = ProjectManager::GetAssetsPath() / name;
        return IsSupportedSoundExtension(path.extension().string()) && AssetFS::IsFile(path);
    }

    void PlaySoundOnSource(const std::string& sourceName, const std::string& soundFileName) {
        const auto sourceIterator = sources.find(sourceName);

        if (sourceIterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        const std::string normalizedFileName = NormalizeSoundFileName(soundFileName);

        if (!HasSound(normalizedFileName)) {
            spdlog::error("Sound not found: '{}'", soundFileName);
            return;
        }

        const ALuint buffer = FindOrLoadBuffer(normalizedFileName);

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

    void PlayPreviewSound(const std::string& soundFileName, const float pitch, const float gain) {
        if (context == nullptr) {
            spdlog::error("Can't preview sound '{}': OpenAL is not initialized", soundFileName);
            return;
        }

        const std::string normalizedFileName = NormalizeSoundFileName(soundFileName);

        if (!buffers.contains(normalizedFileName)) {
            const fs::path path = ProjectManager::GetAssetsPath() / normalizedFileName;
            ALuint buffer = 0;

            if (!IsSupportedSoundExtension(path.extension().string()) || !fs::is_regular_file(path) ||
                !LoadSoundToOpenALBuffer(path, buffer)) {
                spdlog::error("Sound not found: '{}'", soundFileName);
                return;
            }

            buffers.emplace(normalizedFileName, buffer);
            std::error_code timeError;
            const fs::file_time_type writeTime = fs::last_write_time(path, timeError);
            if (!timeError) bufferWriteTimes.emplace(normalizedFileName, writeTime);
        }

        const std::string sourceName = "__editor_preview";
        if (!CreateSource(sourceName)) return;

        const ALuint source = sources[sourceName];
        alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(source, AL_PITCH, pitch);
        alSourcef(source, AL_GAIN, gain);
        alSourcei(source, AL_LOOPING, AL_FALSE);

        PlaySoundOnSource(sourceName, normalizedFileName);
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

    void SetSourceDirection(const std::string& sourceName, const Vector3 direction) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSource3f(iterator->second, AL_DIRECTION, direction.x, direction.y, direction.z);
        CheckALErrors("Failed to set source direction");
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

    void SetSourceRelative(const std::string& sourceName, const bool relative) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcei(iterator->second, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
        if (relative) alSource3f(iterator->second, AL_POSITION, 0.0f, 0.0f, 0.0f);
        CheckALErrors("Failed to set source relative");
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

        alSourcef(iterator->second, AL_CONE_INNER_ANGLE, std::clamp(innerConeAngle, 0.0f, 360.0f));
        CheckALErrors("Failed to set AL_CONE_INNER_ANGLE");
    }

    void SetSourceOuterConeAngle(const std::string& sourceName, const float outerConeAngle) {
        const auto iterator =sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_CONE_OUTER_ANGLE, std::clamp(outerConeAngle, 0.0f, 360.0f));
        CheckALErrors("Failed to set AL_CONE_OUTER_ANGLE");
    }

    void SetSourceOuterGain(const std::string& sourceName, const float outerGain) {
        const auto iterator = sources.find(sourceName);

        if (iterator == sources.end()) {
            spdlog::error("Source not found: '{}'", sourceName);
            return;
        }

        alSourcef(iterator->second, AL_CONE_OUTER_GAIN, std::clamp(outerGain, 0.0f, 1.0f));
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