//
// Created by berke on 4/5/2026.
//

#ifndef TILKY_ENGINE_GAMETIME_H
#define TILKY_ENGINE_GAMETIME_H

#include <cstdint>

namespace GameTime {
    // Multiplies deltaTime, so everything the game simulates speeds up, slows
    // down or (at 0) stops with it. Never negative. Back to 1 when a play
    // session starts or stops (ResetGameClock); kept across level changes.
    extern float timeScale;

    extern float deltaTime; // Seconds since the previous frame, times timeScale
    extern float unscaledDeltaTime; // Real seconds since the previous frame
    extern double timeInSeconds; // Real seconds passed since engine start
    extern double frame; // Frames passed since engine start
    extern float smoothedFPS;

    // The play session's clock, moved on by AdvanceGameClock once per frame
    // the game itself updates (not while the runtime has the mouse released).
    extern double time; // Scaled seconds the game has run
    extern double unscaledTime; // Real seconds the game has run
    extern std::uint64_t frameCount; // Frames the game has run

    void Update();
    float GetFPS();

    void SetTimeScale(float scale);

    void AdvanceGameClock();

    // timeScale back to 1 and the play session's clock to 0.
    void ResetGameClock();
}

#endif //TILKY_ENGINE_GAMETIME_H
