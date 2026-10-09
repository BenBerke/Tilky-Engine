//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_AUDIOSYSTEM_H
#define TILKY_ENGINE_AUDIOSYSTEM_H

struct Level;
struct ComponentAudioSource;

namespace AudioSystem {
    void Start(Level& level);
    void Update(Level& level);
    void Shutdown(Level& level);

    // Creates the sound source for one audio source and applies its settings,
    // the same as Start() does for every one. For an AudioSource added while
    // the game is running.
    void StartSource(Level& level, ComponentAudioSource& audio);

    // Frees the sound source of one audio source. Call before removing it.
    void DestroySource(ComponentAudioSource& audio);

    void ApplyListenerSettings(const Level& level);
}

#endif //TILKY_ENGINE_AUDIOSYSTEM_H