//
// Created by berke on 5/14/2026.
//

#ifndef TILKY_ENGINE_RUNTIMESESSION_H
#define TILKY_ENGINE_RUNTIMESESSION_H

namespace RuntimeSession {
    bool Start(const std::string &windowName, bool isEngine);
    void Update(bool isEngine);
    void Shutdown(bool isEngine);
}

#endif //TILKY_ENGINE_RUNTIMESESSION_H