//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_SOUNDMANAGER_H
#define TILKY_ENGINE_SOUNDMANAGER_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <AL/al.h>


#include "Headers/Math/Vector/Vector3.hpp"

struct Level;

namespace SoundManager {
    bool InitializeOpenAL();
    void DestroyOpenAL();

    // Per Play session in the engine, which keeps the device open between them.
    // Begin reloads sounds changed on disk; End stops and deletes every source.
    void BeginSession();
    void EndSession();

    void GenerateSounds();

    // An exported game's level change: frees the sounds the new level
    // doesn't need (not the playing or paused ones: the music, a carried
    // entity's) and loads its set. Does nothing without per-level sets.
    void PrepareForLevel(const Level& level);

    // Before PrepareForLevel, while a loading screen shows: decodes the
    // sounds `needs` names (paths in Assets) that aren't loaded, on worker
    // threads. Progress is done / total.
    void BeginLevelPreload(const std::vector<std::string>& needs);
    [[nodiscard]] std::pair<size_t, size_t> LevelPreloadProgress();
    void FinishLevelPreload();

    // The engine loads every sound in the background after OpenAL starts,
    // so the editor opens at once. The exported game doesn't: it loads them
    // all before its first frame.
    struct BackgroundLoadProgress {
        bool listed = false; // false while the Assets folder is still being scanned
        size_t total = 0;
        size_t done = 0;
    };

    // Uploads the sounds decoded since the last call, a few milliseconds'
    // worth. Once a frame, on the main thread.
    void UpdateBackgroundLoad();
    // Waits for the rest. BeginSession does, so Play has every sound.
    void FinishBackgroundLoad();
    [[nodiscard]] bool IsBackgroundLoading();
    [[nodiscard]] BackgroundLoadProgress GetBackgroundLoadProgress();

    bool CreateSource(const std::string& name);
    void DestroySource(const std::string& sourceName);

    void PlaySoundOnSource(const std::string& sourceName, const std::string& soundFileName);
    void PlaySoundOnSourceIfNotPlaying(const std::string& sourceName, const std::string& soundFileName);

    // Plays a sound once on the editor's own unpositioned source, restarting
    // it if it is already playing. Loads the file first when it was added
    // since the sounds were last generated.
    void PlayPreviewSound(const std::string& soundFileName, float pitch, float gain);

    bool IsSourcePlaying(const std::string& sourceName);

    // True if this sound (a path inside Assets with its extension: .wav,
    // .mp3, .ogg or .flac) is loaded.
    [[nodiscard]] bool HasSound(const std::string& soundFileName);

    // Stop rewinds; Pause keeps the place so ResumeSource can continue from it.
    void StopSource(const std::string& sourceName);
    void PauseSource(const std::string& sourceName);

    // Continues a paused source. Does nothing unless it is paused; returns
    // whether it resumed.
    bool ResumeSource(const std::string& sourceName);

    // The multiplier for the frequency. 1.0 is normal. 2.0 is an octave higher and double speed.
    void SetSourcePitch(const std::string& sourceName, float pitch);

    // The volume/amplitude multiplier. 1.0 is unity gain; 0.0 is silent.
    void SetSourceGain(const std::string& sourceName, float gain);

    void SetSourcePosition(const std::string& sourceName, Vector3 pos);

    // The way the source faces. The sound cone points along it.
    void SetSourceDirection(const std::string& sourceName, Vector3 direction);

    // Set to true to make the sound repeat automatically when it reaches the end.
    void SetSourceLooping(const std::string& sourceName, bool looping);

    // A relative source sits at the listener and plays unpositioned (2D).
    void SetSourceRelative(const std::string& sourceName, bool relative);

    // The distance at which the listener experiences the source's full gain.
    void SetSourceReferenceDistance(const std::string& sourceName, float distance);

    // The distance beyond which the sound no longer gets quieter.
    void SetSourceMaxDistance(const std::string& sourceName, float maxDistance);

    // How quickly the sound fades with distance.
    void SetSourceRollOffFactor(const std::string& sourceName, float rollOffFactor);

    // The full width of the cone, in degrees (0-360), where the sound is at full gain.
    void SetSourceInnerConeAngle(const std::string& sourceName, float innerConeAngle);

    // The full width of the cone, in degrees (0-360), past which the sound is at outer gain.
    void SetSourceOuterConeAngle(const std::string& sourceName, float outerConeAngle);

    // The gain used outside the outer cone.
    void SetSourceOuterGain(const std::string& sourceName, float outerGain);

    // Sets the master volume of the audio context.
    void SetListenerGain(float gain);

    // Sets the 3D position of the listener in the world.
    void SetListenerPosition(const Vector3& pos);

    // Sets the listener velocity. Used by OpenAL for Doppler shift.
    void SetListenerVelocity(const Vector3 &velocity);

    // Sets the listener orientation using the camera/player forward direction.
    void SetListenerOrientation(const Vector3& forward);

    // Sets the global distance attenuation model.
    void SetListenerDistanceModel(ALenum model);

    // Sets the global Doppler effect intensity.
    void SetListenerDopplerFactor(float factor);

    // Sets the speed of sound for Doppler calculations.
    void SetListenerSpeedOfSound(float speed);
}

#endif // TILKY_ENGINE_SOUNDMANAGER_H