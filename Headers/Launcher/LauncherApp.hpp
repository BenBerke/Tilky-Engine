//
// Created by berke on 5/4/2026.
//
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace LauncherApp {
    void Start(nlohmann::json& launcherVariables);

    void Update();

    bool QuitRequested();

    fs::path GetPendingProjectToOpen();

    void Destroy();
}