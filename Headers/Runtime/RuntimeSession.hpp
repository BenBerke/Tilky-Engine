//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_RUNTIMESESSION_H
#define TILKY_ENGINE_RUNTIMESESSION_H

#include <string>

/// Responsible for anything that requires rendering the map.

namespace RuntimeSession {
    enum RuntimeType {
        STANDALONE,
        EDITOR,
        PLAY
    };

#ifdef TILKY_STANDALONE
    bool Start(const std::string& windowName);
#else
    bool Start(
        const std::string& windowName,
        RuntimeType runtimeType
    );
#endif

    void Update();
    void Shutdown();
}

#endif // TILKY_ENGINE_RUNTIMESESSION_H