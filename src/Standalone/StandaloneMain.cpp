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

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[]) {
    const fs::path projectFile = ProjectManager::GetEngineBasePath() / "project.tilky";

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

    if (!RuntimeSession::Start(ProjectManager::GetProjectName())) {
        std::cerr << "Failed to start standalone runtime session.\n";
        return 1;
    }

    while (!InputManager::QuitRequested()) {
        InputManager::BeginFrame();
        RuntimeSession::Update();
    }

    RuntimeSession::Shutdown();

    return 0;
}