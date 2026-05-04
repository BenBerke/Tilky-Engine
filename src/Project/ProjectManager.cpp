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
    // Returns the current user's home folder.
    // On Windows this usually comes from the USERPROFILE environment variable.
    // Example:
    // C:\Users\berke
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

    // Returns Tilky Engine's default projects folder.
    // This is where all user-created Tilky projects are stored.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects
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

    void LaunchEngine(const fs::path &projectFile) {
        const std::string command = "Wolfy_Engine.exe --project \"" + projectFile.string() + "\"";
        std::system(command.c_str());
    }

    bool OpenProject(const fs::path &path) {
        const fs::path projectFile = path / "project.tilky";
        if (!fs::exists(projectFile)) {
            std::cout << "Missing .tilky file" << std::endl;
            return false;
        }

        LoadProject(path);
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

            const fs::path dataPath = directory / ("project.tilky");

            json projectData;
            projectData["name"] = projectName;
            projectData["assetsFolder"] = "Assets";

            std::ofstream file(dataPath);
            if (file.is_open()) {
                file << projectData.dump(4);
                file.close();
            } else std::cout << "Failed to create metadata " << projectName << std::endl;
        } catch (std::exception &e) {
            std::cout << "Failed to create project " << e.what() << std::endl;
        }

        if (!openProject) return;
        OpenProject(directory);
    }

    void CreateDirectory(const std::string &projectName) {
        const fs::path path = GetDefaultProjectsFolder() / projectName;
        try {
            if (!fs::exists(path)) {
                if (fs::create_directories(path)) {
                    std::cout << "Folder created at " + path.string() << std::endl;
                    CreateProject(path, projectName);
                } else std::cout << "Failed to create folder " << std::endl;
            } else {
                std::cout << "Folder already exists " << std::endl;
                OpenProject(path);
            }
        } catch (std::exception &e) {
            std::cout << "Failed to create project " << e.what() << std::endl;
        }
    }

    bool LoadProject(const fs::path &tilkyEnginePath) {
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
        }
        catch (std::exception &e) {
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

        // Returns whether a project has successfully been loaded into ProjectManager.
    // This does not return a file path.
    // Example result:
    // true if C:\Users\berke\Documents\Tilky Engine\Projects\TestProject\project.tilky was loaded successfully.
    bool HasProject() {
        return projectLoaded;
    }

    // Returns the currently loaded project's .tilky project file.
    // This is the metadata file used to open/load the project.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects\TestProject\project.tilky
    fs::path GetProjectFiles() {
        return currentProjectFile;
    }

    // Returns the root folder of the currently loaded project.
    // This is the folder that contains project.tilky and the Assets folder.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects\TestProject
    fs::path GetProjectFolder() {
        return currentProjectFolder;
    }

    // Returns the Assets folder of the currently loaded project.
    // This folder contains project-specific assets such as levels and textures.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects\TestProject\Assets
    fs::path GetAssetsPath() {
        return currentAssetsPath;
    }

    // Returns the Textures folder of the currently loaded project.
    // This is where project-specific texture files should be stored.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects\TestProject\Assets\Textures
    fs::path GetTexturesPath() {
        return currentTexturesPath;
    }

    // Returns the Levels folder of the currently loaded project.
    // This is where project-specific level JSON files should be saved and loaded from.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects\TestProject\Assets\Levels
    fs::path GetLevelsPath() {
        return currentLevelsPath;
    }

    // Returns the folder where the currently running executable is located.
    // This comes from SDL_GetBasePath(), so in CLion it usually points to the build folder.
    // This is NOT the same as the user's Documents\Tilky Engine folder.
    // Example:
    // C:\Users\berke\Desktop\CLion Projects\Wolfy Engine\cmake-build-debug
    fs::path GetEngineBasePath() {
        const char* basePath = SDL_GetBasePath();

        if (basePath == nullptr) {
            return fs::current_path();
        }

        return fs::path(basePath);
    }

    // Returns Tilky Engine's user data folder inside Documents.
    // This is the parent folder of the Projects folder.
    // Use this for launcher-wide files such as Launcher.json.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine
    fs::path GetEngineFolder() {
        const fs::path projectsPath = GetDefaultProjectsFolder();
        const fs::path tilkyEngineFolder = projectsPath.parent_path();

        return tilkyEngineFolder;
    }

    // Returns the name of the currently loaded project.
    // This comes from the "name" field inside project.tilky.
    // Example result:
    // TestProject
    std::string GetProjectName() {
        return currentProjectName;
    }
}