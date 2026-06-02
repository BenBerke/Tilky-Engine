//
// StandaloneMain.cpp
//

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/RuntimeSession.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    const fs::path projectFile = ProjectManager::GetEngineBasePath() / "project.tilky";

    constexpr bool IS_ENGINE = false;

    if (!fs::exists(projectFile)) {
        std::cerr << "Missing project.tilky beside executable:\n"
                  << projectFile.string()
                  << "\n";
        return 1;
    }

    if (!ProjectManager::LoadProjectMetaData(projectFile)) {
        std::cerr << "Failed to load project metadata\n";
        return 1;
    }

    if (!LevelManager::LoadFirstProjectLevel()) {
        std::cerr << "Failed to load startup level for the project\n";
        return 1;
    }

    if (!RuntimeSession::Start(ProjectManager::GetProjectName(), IS_ENGINE)) {
        std::cerr << "Failed to start standalone runtime session.\n";
        return 1;
    }

    bool quit = false;

    while (!quit) {
        InputManager::BeginFrame();

        RuntimeSession::Update(IS_ENGINE);

        if (InputManager::QuitRequested()) quit = true; // When the X button is clicked on the window for the respective OS
    }

    RuntimeSession::Shutdown(IS_ENGINE);

    return 0;
}