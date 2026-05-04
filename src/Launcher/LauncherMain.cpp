//
// Created by berke on 5/3/2026.
//
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Headers/Launcher/LauncherApp.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "src/MapEditor/MapEditorInternal.hpp"
#include "Headers/Engine/Local/Local.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;


fs::path GetLauncherVarsPath() {
    return ProjectManager::GetEngineFolder() / "Launcher.json";
}

bool WriteLauncherVariablesJson(const json& launcherVariablesData) {
    const fs::path launcherVarsPath = GetLauncherVarsPath();

    try {
        fs::create_directories(launcherVarsPath.parent_path());
    }
    catch (std::exception& e) {
        std::cerr << "Couldn't create launcher variables folder: " << e.what() << std::endl;
        return false;
    }

    std::ofstream file(launcherVarsPath);

    if (!file.is_open()) {
        std::cerr << "Failed to open launcher variables file for writing: "
                  << launcherVarsPath << std::endl;
        return false;
    }

    file << launcherVariablesData.dump(4);
    file.close();

    return true;
}

int main() {
    const fs::path projectsPath = ProjectManager::GetDefaultProjectsFolder();

    try {
        fs::create_directories(projectsPath);
        std::cout << "Created/Exists: " << projectsPath << std::endl;
    }
    catch (std::exception& e) {
        std::cout << "Failed to create projects directory " << std::endl;
        std::cerr << e.what() << std::endl;
        return 1;
    }

    // Editor Variables JSON
    const fs::path launcherVarsPath = GetLauncherVarsPath();

    try {
        fs::create_directories(launcherVarsPath.parent_path());
    }
    catch (std::exception& e) {
        std::cerr << "Couldn't create Launcher.json" << e.what() << std::endl;
    }

    std::ifstream launcherVars(launcherVarsPath);
    json launcherVariablesData;

    if (launcherVars.is_open()) {
        try {
            launcherVars >> launcherVariablesData;
        }
        catch (std::exception& e) {
            std::cerr << "Launcher variables couldn't load: " << e.what() << std::endl;

            launcherVariablesData = {
                {"lang", "en"},
                {"projectCount", 0}
            };
        }

        launcherVars.close();
    }
    else {
        launcherVariablesData = {
            {"lang", "en"},
            {"projectCount", 0}
        };

        std::ofstream varFile(launcherVarsPath);

        if (varFile.is_open()) {
            varFile << launcherVariablesData.dump(4);
            varFile.close();

            std::cout << "Created launcher vars at: " << launcherVarsPath << std::endl;
        }
        else {
            std::cerr << "Failed to create launcher variables at: " << launcherVarsPath << std::endl;
        }
    }

    LauncherApp::Start(launcherVariablesData["lang"]);

    while (!LauncherApp::QuitRequested()) {
        LauncherApp::Update();
    }

    launcherVariablesData["lang"] = Localisation::CurrentLanguage();
    WriteLauncherVariablesJson(launcherVariablesData);

    LauncherApp::Destroy();
    // CreateDirectory(projectName);

    return 0;
}