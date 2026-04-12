//
// Created by berke on 4/6/2026.
//

#include "../../Headers/Engine/GameTime.h"
#include <SDL3/SDL.h>

namespace GameTime {
    float deltaTime = 0.0f;
    float time = 0.0f;
    float smoothedFPS = 0.0f;

    void Update() {
        const Uint64 now = SDL_GetPerformanceCounter();
        static Uint64 last = now;

        Uint64 diff = now - last;
        last = now;

        deltaTime = static_cast<float>(diff / static_cast<double>(SDL_GetPerformanceFrequency()));
        time += deltaTime;

        float instantFPS = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
        smoothedFPS = smoothedFPS * 0.9f + instantFPS * 0.1f;
    }

    float GetFPS() {
        return smoothedFPS;
    }
}