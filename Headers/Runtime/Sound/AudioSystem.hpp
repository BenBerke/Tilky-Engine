//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_AUDIOSYSTEM_H
#define TILKY_ENGINE_AUDIOSYSTEM_H

struct Level;

namespace AudioSystem {
    void Start(Level& level);
    void Update(Level& level);
    void Shutdown(Level& level);
}

#endif //TILKY_ENGINE_AUDIOSYSTEM_H