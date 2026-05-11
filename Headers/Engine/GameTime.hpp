//
// Created by berke on 4/5/2026.
//

#ifndef TILKY_ENGINE_GAMETIME_H
#define TILKY_ENGINE_GAMETIME_H

#include <SDL3/SDL.h>

namespace GameTime {
    extern float deltaTime;
    extern float time; // Times passed since game start
    extern float smoothedFPS;

    void Update();
    float GetFPS();
}

#endif //TILKYENGINE_GAMETIME_H