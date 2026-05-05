//
// Created by berke on 5/4/2026.
//

#ifndef WOLFY_ENGINE_LAUNCHERAPP_HPP
#define WOLFY_ENGINE_LAUNCHERAPP_HPP
#include <filesystem>
#include <string>

namespace LauncherApp {
    void Start(const std::string& langCode);
    void Update();
    void Destroy();

    std::filesystem::path GetPendingProjectToOpen();

    bool QuitRequested();
}

#endif //WOLFY_ENGINE_LAUNCHERAPP_HPP