//
// Created by berke on 5/4/2026.
//

#ifndef TILKY_ENGINE_LAUNCHERAPP_HPP
#define TILKY_ENGINE_LAUNCHERAPP_HPP
#include <filesystem>
#include <string>

namespace LauncherApp {
    void Start(const std::string& langCode);
    void Update();
    void Destroy();

    std::filesystem::path GetPendingProjectToOpen();

    bool QuitRequested();
}

#endif //TILKY_ENGINE_LAUNCHERAPP_HPP