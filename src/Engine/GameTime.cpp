//
// Created by berke on 4/6/2026.
//

#include "../../Headers/Engine/GameTime.hpp"
#include <SDL3/SDL.h>

#include <cmath>

namespace GameTime {
    float timeScale = 1.0f;

    float deltaTime = 0.0f;
    float unscaledDeltaTime = 0.0f;
    double timeInSeconds = 0.0f;
    float smoothedFPS = 0.0f;
    double frame = 0.0f;

    double time = 0.0;
    double unscaledTime = 0.0;
    std::uint64_t frameCount = 0;

    void Update() {
        const Uint64 now = SDL_GetPerformanceCounter();
        static Uint64 last = now;

        const Uint64 diff = now - last;
        last = now;

        unscaledDeltaTime = static_cast<float>(static_cast<double>(diff) / static_cast<double>(SDL_GetPerformanceFrequency()));
        deltaTime = unscaledDeltaTime * timeScale;
        timeInSeconds += unscaledDeltaTime;

        frame++;
    }

    float GetFPS() {
        float instantFPS = unscaledDeltaTime > 0.0f ? 1.0f / unscaledDeltaTime : 0.0f;
        smoothedFPS = smoothedFPS * 0.9f + instantFPS * 0.1f;

        return smoothedFPS;
    }

    void SetTimeScale(const float scale) {
        // NaN fails the comparison and ends up 0 too.
        timeScale = scale > 0.0f && std::isfinite(scale) ? scale : 0.0f;

        // Systems still to run this frame already see the new scale.
        deltaTime = unscaledDeltaTime * timeScale;
    }

    void AdvanceGameClock() {
        time += deltaTime;
        unscaledTime += unscaledDeltaTime;
        frameCount++;
    }

    void ResetGameClock() {
        SetTimeScale(1.0f);
        time = 0.0;
        unscaledTime = 0.0;
        frameCount = 0;
    }
}
