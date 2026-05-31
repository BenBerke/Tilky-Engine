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

static fs::path GetProjectFileFromArgs(int argc, char** argv) {
    fs::path projectFile;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];

        if (arg == "--project" && i + 1 < argc) {
            projectFile = argv[i + 1];
            i++;
            continue;
        }

        if (projectFile.empty()) {
            projectFile = arg;
        }
    }

    return projectFile;
}

int main(int argc, char** argv) {
    const fs::path projectFile = GetProjectFileFromArgs(argc, argv);

    if (projectFile.empty()) {
        std::cerr << "No project file provided.\n";
        std::cerr << "Usage:\n";
        std::cerr << "  Standalone.exe --project \"C:/path/to/project.tilky\"\n";
        return 1;
    }

    if (!fs::exists(projectFile)) {
        std::cerr << "Project file does not exist: "
                  << projectFile.string()
                  << "\n";
        return 1;
    }

    if (!ProjectManager::LoadProjectMetaData(projectFile)) {
        std::cerr << "Failed to load project metadata from: "
                  << projectFile.string()
                  << "\n";
        return 1;
    }

    if (!LevelManager::LoadFirstProjectLevel()) {
        std::cerr << "Failed to load startup level for project: "
                  << projectFile.string()
                  << "\n";
        return 1;
    }

    if (!RuntimeSession::Start()) {
        std::cerr << "Failed to start standalone runtime session.\n";
        return 1;
    }

    bool quit = false;

    while (!quit) {
        InputManager::BeginFrame();

        RuntimeSession::Update();

        if (InputManager::QuitRequested()) {
            quit = true;
        }
    }

    RuntimeSession::Shutdown();

    return 0;
}