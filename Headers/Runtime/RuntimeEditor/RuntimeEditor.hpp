//
// Created by berke on 6/3/2026.
//

#ifndef TILKY_ENGINE_RUNTIMEEDITOR_H
#define TILKY_ENGINE_RUNTIMEEDITOR_H

struct Level;

namespace RuntimeEditor {
    void Start(Level& level);
    void Update(Level& level);
    void Draw(Level& level);
    void Shutdown(Level& level);
}

#endif //TILKY_ENGINE_RUNTIMEEDITOR_H