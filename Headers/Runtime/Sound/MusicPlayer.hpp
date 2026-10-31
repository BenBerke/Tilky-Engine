//
// Created by berke on 10/31/2026.
//

#ifndef TILKY_ENGINE_MUSICPLAYER_HPP
#define TILKY_ENGINE_MUSICPLAYER_HPP

struct Level;
struct MusicSettings;

// Plays the level's music list (WorldSettings::music) on one unpositioned
// source that lives for the whole play session, so it can carry on across
// Game.LoadLevel. Counts real time: pausing the game doesn't pause it.
namespace MusicPlayer {
    // A level started (Play, or a level switch). Music already playing the
    // same list keeps going and takes the new level's settings; otherwise it
    // stops, and the new level's starts if it is Play on Start.
    void OnLevelStart(const Level& level);

    // Track ends, gaps and fades. Every frame, with the real frame time.
    void Update(float realDeltaTime);

    // Play ended: the music stops and its source goes.
    void Shutdown();

    // Resumes a paused track; otherwise starts the list from its first track
    // (Shuffle: a new order). Already playing: nothing.
    void Start();
    // Fades out (if the level has a fade) and stops. The next Start begins
    // the list again.
    void Stop();
    // Holds the track where it is, for Start to resume.
    void Pause();
    // Plays track `index` (0-based) now, fading if the level has a fade. The
    // list carries on from there. False if there is no such track.
    bool SwitchToIndex(int index);
    // 0..1 (higher is louder than the files).
    void SetVolume(float volume);

    [[nodiscard]] int TrackCount();
    // The track playing, paused or between tracks (0-based); -1 when stopped.
    [[nodiscard]] int CurrentIndex();
}

#endif //TILKY_ENGINE_MUSICPLAYER_HPP
