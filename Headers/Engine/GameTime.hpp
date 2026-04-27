//
// Created by berke on 4/5/2026.
//

#ifndef TILKYENGINE_GAMETIME_H
#define TILKYENGINE_GAMETIME_H

#include <SDL3/SDL.h>

namespace GameTime {
    extern float deltaTime;
    extern float time;
    extern float smoothedFPS;

    void Update();
    float GetFPS();
}

#endif //TILKYENGINE_GAMETIME_H