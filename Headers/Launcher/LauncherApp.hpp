//
// Created by berke on 5/4/2026.
//

#ifndef WOLFY_ENGINE_LAUNCHERAPP_HPP
#define WOLFY_ENGINE_LAUNCHERAPP_HPP
#include <string>

namespace LauncherApp {
    void Start(const std::string& langCode);
    void Update();
    void Destroy();

    bool QuitRequested();
}

#endif //WOLFY_ENGINE_LAUNCHERAPP_HPP