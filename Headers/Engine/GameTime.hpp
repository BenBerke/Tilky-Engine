//
// Created by berke on 4/5/2026.
//

#ifndef TILKY_ENGINE_GAMETIME_H
#define TILKY_ENGINE_GAMETIME_H

namespace GameTime {
    extern float deltaTime;
    extern double time; // Seconds passed since game start
    extern double frame; // Frames passed since game start
    extern float smoothedFPS;

    void Update();
    float GetFPS();
}

#endif //TILKYENGINE_GAMETIME_H