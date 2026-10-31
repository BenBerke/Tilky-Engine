//
// Created by berke on 10/31/2026.
//

#include "Headers/Runtime/Sound/MusicPlayer.hpp"

#include "Headers/Objects/Level.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

namespace {
    constexpr const char* SOURCE = "tilky_music";

    enum class Phase {
        STOPPED,
        PLAYING,
        GAP,        // between tracks: `pendingIndex` starts when gapLeft runs out
        FADING_OUT, // then `pendingIndex` starts, or (-1) the music stops
        PAUSED      // `pausedPhase` is what it was doing
    };

    MusicSettings settings;
    bool hasSource = false;

    Phase phase = Phase::STOPPED;
    Phase pausedPhase = Phase::PLAYING;
    int current = -1;       // the track playing (or last played)
    int pendingIndex = -1;
    int direction = 1;      // Ping-pong
    float fadeLevel = 1.0f; // 0..1, multiplies the volume
    bool fadingIn = false;
    float gapLeft = 0.0f;

    std::vector<int> order; // Shuffle: this round's order, current = order[orderPosition]
    size_t orderPosition = 0;
    std::mt19937 random{std::random_device{}()};

    int Count() { return static_cast<int>(settings.tracks.size()); }

    bool EnsureSource() {
        if (hasSource) return true;
        if (!SoundManager::CreateSource(SOURCE)) return false;

        SoundManager::SetSourceRelative(SOURCE, true);
        SoundManager::SetSourceLooping(SOURCE, false);
        hasSource = true;
        return true;
    }

    void ApplyGain() {
        if (hasSource) SoundManager::SetSourceGain(SOURCE, std::max(0.0f, settings.volume) * std::clamp(fadeLevel, 0.0f, 1.0f));
    }

    // A new shuffle round starting with `first`, or (-1) with any track
    // but the one just played.
    void NewOrder(const int first) {
        order.resize(settings.tracks.size());
        std::iota(order.begin(), order.end(), 0);
        std::ranges::shuffle(order, random);

        if (first >= 0) {
            std::iter_swap(order.begin(), std::ranges::find(order, first));
        }
        else if (order.size() > 1 && order.front() == current) {
            std::iter_swap(order.begin(), order.begin() + 1);
        }

        orderPosition = 0;
    }

    // The track after the current one, -1 when Once has played them all.
    int NextIndex() {
        const int count = Count();
        if (count == 0) return -1;

        switch (settings.mode) {
            case MusicSettings::ONCE:
                return current + 1 < count ? current + 1 : -1;
            case MusicSettings::LOOP:
                return (current + 1) % count;
            case MusicSettings::PING_PONG: {
                if (count == 1) return 0;
                if (current + direction < 0 || current + direction >= count) direction = -direction;
                return current + direction;
            }
            case MusicSettings::SHUFFLE:
                if (order.size() != settings.tracks.size() || ++orderPosition >= order.size()) NewOrder(-1);
                return order[orderPosition];
        }
        return -1;
    }

    // Starts track `index`, from silence when `fadeIn` (and the level has a
    // fade). False if its file isn't loaded.
    bool PlayTrack(const int index, const bool fadeIn) {
        if (index < 0 || index >= Count() || !EnsureSource()) return false;

        const std::string& track = settings.tracks[index];
        current = index;

        if (track.empty() || !SoundManager::HasSound(track)) {
            spdlog::warn("Music: track {} ('{}') isn't a loaded sound, skipped", index + 1, track);
            return false;
        }

        fadingIn = fadeIn && settings.fade > 0.0f;
        fadeLevel = fadingIn ? 0.0f : 1.0f;
        ApplyGain();
        SoundManager::PlaySoundOnSource(SOURCE, track);
        phase = Phase::PLAYING;
        return true;
    }

    // Plays `index`, or the tracks after it until one loads. Stops if none does.
    void PlayFrom(int index, const bool fadeIn) {
        for (int tried = 0; tried < Count() && index >= 0; ++tried) {
            if (PlayTrack(index, fadeIn)) return;
            index = NextIndex();
        }

        if (hasSource) SoundManager::StopSource(SOURCE);
        phase = Phase::STOPPED;
    }

    void StopNow() {
        if (hasSource) SoundManager::StopSource(SOURCE);
        phase = Phase::STOPPED;
        current = -1;
        pendingIndex = -1;
        direction = 1;
        fadingIn = false;
        fadeLevel = 1.0f;
        order.clear();
    }

    // Starts `index` now, or fades the playing track out first.
    void SwitchTo(const int index) {
        if (phase == Phase::PLAYING && settings.fade > 0.0f) {
            phase = Phase::FADING_OUT;
            pendingIndex = index;
            fadingIn = false;
            return;
        }

        PlayFrom(index, settings.fade > 0.0f);
    }
}

namespace MusicPlayer {
    void OnLevelStart(const Level& level) {
        const MusicSettings& next = level.worldSettings.music;

        // The same list keeps playing through the switch.
        if (phase != Phase::STOPPED && !next.tracks.empty() && next.tracks == settings.tracks) {
            settings = next;
            ApplyGain();
            return;
        }

        StopNow();
        settings = next;

        if (settings.playOnStart && !settings.tracks.empty()) Start();
    }

    void Update(const float realDeltaTime) {
        switch (phase) {
            case Phase::PLAYING:
                if (fadingIn) {
                    fadeLevel = std::min(1.0f, fadeLevel + realDeltaTime / settings.fade);
                    fadingIn = fadeLevel < 1.0f;
                    ApplyGain();
                }

                if (!SoundManager::IsSourcePlaying(SOURCE)) {
                    const int next = NextIndex();
                    if (next < 0) StopNow();
                    else if (settings.gap > 0.0f) {
                        phase = Phase::GAP;
                        gapLeft = settings.gap;
                        pendingIndex = next;
                    }
                    else PlayFrom(next, false);
                }
                break;

            case Phase::GAP:
                gapLeft -= realDeltaTime;
                if (gapLeft <= 0.0f) PlayFrom(pendingIndex, false);
                break;

            case Phase::FADING_OUT:
                fadeLevel = settings.fade > 0.0f ? fadeLevel - realDeltaTime / settings.fade : 0.0f;
                ApplyGain();

                if (fadeLevel <= 0.0f) {
                    if (pendingIndex < 0) StopNow();
                    else PlayFrom(pendingIndex, true);
                }
                break;

            case Phase::STOPPED:
            case Phase::PAUSED:
                break;
        }
    }

    void Shutdown() {
        StopNow();
        if (hasSource) SoundManager::DestroySource(SOURCE);
        hasSource = false;
        settings = {};
    }

    void Start() {
        if (phase == Phase::PAUSED) {
            phase = pausedPhase;
            if (phase != Phase::GAP) SoundManager::ResumeSource(SOURCE);
            return;
        }

        // Fading out to a stop: start over instead.
        const bool stopping = phase == Phase::FADING_OUT && pendingIndex < 0;
        if (phase != Phase::STOPPED && !stopping) return;

        if (settings.tracks.empty()) {
            spdlog::warn("Music: the level has no music tracks");
            return;
        }

        direction = 1;
        current = -1;
        int first = 0;
        if (settings.mode == MusicSettings::SHUFFLE) {
            NewOrder(-1);
            first = order.front();
        }

        PlayFrom(first, settings.fade > 0.0f);
    }

    void Stop() {
        switch (phase) {
            case Phase::STOPPED:
                return;
            case Phase::PLAYING:
            case Phase::FADING_OUT:
                if (settings.fade > 0.0f) {
                    phase = Phase::FADING_OUT;
                    pendingIndex = -1;
                    fadingIn = false;
                    return;
                }
                break;
            default:
                break;
        }

        StopNow();
    }

    void Pause() {
        if (phase != Phase::PLAYING && phase != Phase::GAP && phase != Phase::FADING_OUT) return;

        pausedPhase = phase;
        phase = Phase::PAUSED;
        if (pausedPhase != Phase::GAP) SoundManager::PauseSource(SOURCE);
    }

    bool SwitchToIndex(const int index) {
        if (index < 0 || index >= Count()) return false;

        if (settings.mode == MusicSettings::SHUFFLE) NewOrder(index);

        // A paused track is replaced, so there is nothing left to resume.
        if (phase == Phase::PAUSED) {
            if (hasSource) SoundManager::StopSource(SOURCE);
            phase = Phase::STOPPED;
        }

        SwitchTo(index);
        return true;
    }

    void SetVolume(const float volume) {
        settings.volume = std::max(0.0f, volume);
        ApplyGain();
    }

    int TrackCount() {
        return Count();
    }

    int CurrentIndex() {
        return phase == Phase::STOPPED ? -1 : phase == Phase::GAP ? pendingIndex : current;
    }
}
