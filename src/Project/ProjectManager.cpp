//
// Created by berke on 5/3/2026.
//
#include "Headers/Project/ProjectManager.hpp"

#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
    bool projectLoaded = false;

    fs::path currentProjectFile;
    fs::path currentProjectFolder;
    fs::path currentAssetsPath;
    fs::path currentLevelsPath;
    fs::path currentTexturesPath;

    std::string currentProjectName;
}

namespace ProjectManager {

    // For function documentation see ProjectManager.hpp

    fs::path GetUserHomeDirectory() {
#if _WIN32
        const char *userProfile = std::getenv("USERPROFILE");
#else
        const char *userProfile = std::getenv("HOME");
#endif

        if (userProfile != nullptr) {
            return fs::path(userProfile);
        }

        return fs::current_path();
    }

    fs::path GetDefaultProjectsFolder() {
#ifdef _WIN32
        return GetUserHomeDirectory() / "Documents" / "Tilky Engine" / "Projects";
#else
        const char *xdgDataHome = std::getenv("XDG_DATA_HOME");

        if (xdgDataHome != nullptr) {
            return fs::path(xdgDataHome) / "Tilky Engine" / "Projects";
        }

        return GetUserHomeDirectory() / ".local" / "share" / "Tilky Engine" / "Projects";
#endif
    }

    // Launches the engine executable and passes it a specific .tilky project file.
    // Parameter:
    // - projectFile should be the full path to the project's project.tilky file.
    // - It should NOT be the project root folder.
    // - It should NOT be the top-level Projects folder.
    //
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\project.tilky
    void LaunchEngine(const fs::path &projectFile) {
        const std::string command = "Wolfy_Engine.exe --project \"" + projectFile.string() + "\"";
        std::system(command.c_str());
    }

    bool OpenProject(const fs::path& path) {
        fs::path projectFile;

        if (path.extension() == ".tilky") {
            projectFile = path;
        }
        else {
            projectFile = path / "project.tilky";
        }

        if (!fs::exists(projectFile)) {
            std::cout << "Missing .tilky file at: " << projectFile << std::endl;
            return false;
        }

        if (!LoadProjectMetaData(projectFile)) {
            std::cout << "Failed to load project metadata before launching engine\n";
            return false;
        }

        LaunchEngine(projectFile);

        return true;
    }

    void CreateProject(const fs::path &directory, const std::string &projectName) {
        const fs::path assetsPath = directory / "Assets";
        const fs::path levelsPath = assetsPath / "Levels";
        const fs::path texturesPath = assetsPath / "Textures";

        bool openProject = true;

        try {
            if (!fs::exists(assetsPath)) {
                if (!fs::create_directories(assetsPath)) {
                    std::cout << "Could not create assets folder" << std::endl;
                    openProject = false;
                }
            }

            if (!fs::exists(levelsPath)) {
                if (!fs::create_directories(levelsPath)) {
                    std::cout << "Could not create levels folder" << std::endl;
                    openProject = false;
                }
            }

            if (!fs::exists(texturesPath)) {
                if (!fs::create_directories(texturesPath)) {
                    std::cout << "Could not create textures folder" << std::endl;
                    openProject = false;
                }
            }

            const fs::path dataPath = directory / "project.tilky";

            json projectData;
            projectData["name"] = projectName;
            projectData["assetsFolder"] = "Assets";

            std::ofstream file(dataPath);

            if (file.is_open()) {
                file << projectData.dump(4);
                file.close();
            } else {
                std::cout << "Failed to create metadata " << projectName << std::endl;
            }
        } catch (std::exception &e) {
            std::cout << "Failed to create project " << e.what() << std::endl;
        }

        if (!openProject) {
            return;
        }

        OpenProject(directory);
    }

    void CreateDirectory(const std::string &projectName) {
        const fs::path path = GetDefaultProjectsFolder() / projectName;

        try {
            if (!fs::exists(path)) {
                if (fs::create_directories(path)) {
                    std::cout << "Folder created at " + path.string() << std::endl;
                    CreateProject(path, projectName);
                } else {
                    std::cout << "Failed to create folder " << std::endl;
                }
            } else {
                std::cout << "Folder already exists " << std::endl;
                OpenProject(path);
            }
        } catch (std::exception &e) {
            std::cout << "Failed to create project " << e.what() << std::endl;
        }
    }

    bool LoadProjectMetaData(const fs::path &tilkyEnginePath) {
        if (!fs::exists(tilkyEnginePath)) {
            std::cout << "Project file does not exist" << std::endl;
            return false;
        }

        std::ifstream file(tilkyEnginePath);

        if (!file.is_open()) {
            std::cout << "Failed to open project" << std::endl;
            return false;
        }

        json projectData;

        try {
            file >> projectData;
        } catch (std::exception &e) {
            std::cout << "Failed to parse project file:" << e.what() << std::endl;
            return false;
        }

        currentProjectFile = tilkyEnginePath;
        currentProjectFolder = tilkyEnginePath.parent_path();
        currentProjectName = projectData.value("name", currentProjectFolder.filename().string());

        const std::string assetsFolder = projectData.value("assetsFolder", "Assets");

        currentAssetsPath = currentProjectFolder / assetsFolder;
        currentLevelsPath = currentAssetsPath / "Levels";
        currentTexturesPath = currentAssetsPath / "Textures";

        if (!fs::exists(currentAssetsPath)) {
            std::cout << "Missing Assets folder: " << currentAssetsPath << '\n';
            return false;
        }

        if (!fs::exists(currentLevelsPath)) {
            std::cout << "Missing Levels folder: " << currentLevelsPath << '\n';
            return false;
        }

        if (!fs::exists(currentTexturesPath)) {
            std::cout << "Missing Textures folder: " << currentTexturesPath << '\n';
            return false;
        }

        projectLoaded = true;

        return true;
    }

    bool HasProject() {
        return projectLoaded;
    }

    fs::path GetProjectFiles() {
        return currentProjectFile;
    }

    fs::path GetProjectFolder() {
        return currentProjectFolder;
    }

    fs::path GetAssetsPath() {
        return currentAssetsPath;
    }

    fs::path GetTexturesPath() {
        return currentTexturesPath;
    }

    fs::path GetLevelsPath() {
        return currentLevelsPath;
    }

    fs::path GetEngineBasePath() {
        const char *basePath = SDL_GetBasePath();

        if (basePath == nullptr) {
            return fs::current_path();
        }

        return fs::path(basePath);
    }

    fs::path GetEngineFolder() {
        const fs::path projectsPath = GetDefaultProjectsFolder();
        const fs::path tilkyEngineFolder = projectsPath.parent_path();

        return tilkyEngineFolder;
    }

    fs::path GetLauncherVariables() {
        return GetEngineFolder() / "Launcher.tilky";
    }

    std::string GetCurrentLanguageInLauncher() {
        const fs::path configPath = GetLauncherVariables();

        if (!fs::exists(configPath)) {
            std::cout << "Launcher config not found. Using English.\n";
            return "en";
        }

        std::ifstream file(configPath);

        if (!file.is_open()) {
            std::cout << "Could not open launcher config. Using English.\n";
            return "en";
        }

        try {
            json configData;
            file >> configData;

            return configData.value("lang", "en");
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to read launcher language: " << e.what() << std::endl;
            return "en";
        }
    }

    std::string GetProjectName() {
        return currentProjectName;
    }
}
