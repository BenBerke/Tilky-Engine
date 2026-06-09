//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_RUNTIMESESSION_H
#define TILKY_ENGINE_RUNTIMESESSION_H

namespace RuntimeSession {
    bool Start(const std::string &windowName, bool playMode);
    void Update(bool playMode);
    void Shutdown(bool playMode);
}

#endif //TILKY_ENGINE_RUNTIMESESSION_H