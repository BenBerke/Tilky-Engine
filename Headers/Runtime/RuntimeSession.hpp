//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_RUNTIMESESSION_H
#define TILKY_ENGINE_RUNTIMESESSION_H

/// Responsible for anything that requires rendering the map

namespace RuntimeSession {
    enum RuntimeType {
        STANDALONE,
        EDITOR,
        PLAY
    };

    bool Start(const std::string &windowName, RuntimeType runtimeType);
    void Update(RuntimeType runtimeType);
    void Shutdown(RuntimeType runtimeType);
}

#endif //TILKY_ENGINE_RUNTIMESESSION_H