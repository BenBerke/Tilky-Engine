//
// Created by berke on 5/3/2026.
//
#include "Headers/Project/ProjectManager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
    bool projectLoaded = false;

    fs::path currentProjectFile;
    fs::path currentProjectFolder;
    fs::path currentAssetsPath;
    fs::path currentLevelsPath;
    fs::path currentTexturesPath;
    fs::path currentSoundsPath;

    std::string currentProjectName;
}

namespace ProjectManager {

    fs::path GetUserHomeDirectory() {
#if _WIN32
        const char* userProfile = std::getenv("USERPROFILE");
#else
        const char* userProfile = std::getenv("HOME");
#endif

        if (userProfile != nullptr) {
            return fs::path(userProfile);
        }

        spdlog::warn("Could not find user home directory. Falling back to current working directory.");
        return fs::current_path();
    }

    fs::path GetDefaultProjectsFolder() {
#ifdef _WIN32
        return GetUserHomeDirectory() / "Documents" / "Tilky Engine" / "Projects";
#else
        const char* xdgDataHome = std::getenv("XDG_DATA_HOME");

        if (xdgDataHome != nullptr) {
            return fs::path(xdgDataHome) / "Tilky Engine" / "Projects";
        }

        return GetUserHomeDirectory() / ".local" / "share" / "Tilky Engine" / "Projects";
#endif
    }

    void LaunchEngine(const fs::path& projectFile) {
#ifdef _WIN32
        const fs::path engineExe = GetEngineBasePath() / "Tilky_Engine.exe";

        std::wstring appPath = engineExe.wstring();

        std::wstring commandLine =
            L"\"" + appPath + L"\" --project \"" + projectFile.wstring() + L"\"";

        std::wstring workingDirectory = GetEngineBasePath().wstring();

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);

        PROCESS_INFORMATION processInfo{};

        spdlog::info("Launching engine executable: {}", engineExe.string());
        spdlog::info("Opening project file: {}", projectFile.string());

        const BOOL success = CreateProcessW(
            appPath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.c_str(),
            &startupInfo,
            &processInfo
        );

        if (!success) {
            spdlog::error(
                "Failed to launch Tilky_Engine.exe. Windows error code: {}. Engine path: {}",
                GetLastError(),
                engineExe.string()
            );
            return;
        }

        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);

#else
        const fs::path engineExe = GetEngineBasePath() / "TIlky_Engine";

        const std::string command =
            "\"" + engineExe.string() + "\" --project \"" + projectFile.string() + "\"";

        spdlog::info("Launching engine with command: {}", command);

        const int result = std::system(command.c_str());

        if (result != 0) {
            spdlog::error("Engine process returned non-zero exit code: {}", result);
        }
#endif
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
            spdlog::error("Missing .tilky project file at: {}", projectFile.string());
            return false;
        }

        if (!LoadProjectMetaData(projectFile)) {
            spdlog::error("Failed to load project metadata before launching engine: {}", projectFile.string());
            return false;
        }

        LaunchEngine(projectFile);

        return true;
    }

    void CreateProject(const fs::path& directory, const std::string& projectName) {
        const fs::path assetsPath = directory / "Assets";
        const fs::path levelsPath = assetsPath / "Levels";
        const fs::path texturesPath = assetsPath / "Textures";
        const fs::path soundsPath = assetsPath / "Sounds";

        bool openProject = true;

        try {
            if (!fs::exists(assetsPath)) {
                if (!fs::create_directories(assetsPath)) {
                    spdlog::critical("Could not create project assets folder: {}", assetsPath.string());
                    openProject = false;
                }
                else spdlog::info("Created project assets folder: {}", assetsPath.string());
            }

            if (!fs::exists(levelsPath)) {
                if (!fs::create_directories(levelsPath)) {
                    spdlog::critical("Could not create project levels folder: {}", levelsPath.string());
                    openProject = false;
                }
                else spdlog::info("Created project levels folder: {}", levelsPath.string());
            }

            if (!fs::exists(texturesPath)) {
                if (!fs::create_directories(texturesPath)) {
                    spdlog::critical("Could not create project textures folder: {}", texturesPath.string());
                    openProject = false;
                }
                else spdlog::info("Created project textures folder: {}", texturesPath.string());
            }

            if (!fs::exists(soundsPath)) {
                if (!fs::create_directories(soundsPath)) {
                    spdlog::critical("Could not create project sounds folder: {}", soundsPath.string());
                    openProject = false;
                }
                else spdlog::info("Created sounds path folder: {}", soundsPath.string());
            }

            const fs::path dataPath = directory / "project.tilky";

            json projectData;
            projectData["name"] = projectName;
            projectData["assetsFolder"] = "Assets";

            std::ofstream file(dataPath);

            if (file.is_open()) {
                file << projectData.dump(4);
                file.close();

                spdlog::info("Created project metadata file: {}", dataPath.string());
            }
            else {
                spdlog::error("Failed to create project metadata file for project '{}': {}", projectName, dataPath.string());
                openProject = false;
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to create project '{}': {}", projectName, e.what());
            openProject = false;
        }

        if (!openProject) {
            spdlog::error("Project '{}' was not opened because project creation failed.", projectName);
            return;
        }

        OpenProject(directory);
    }

    void CreateProjectDirectory(const std::string& projectName) {
        const fs::path path = GetDefaultProjectsFolder() / projectName;

        try {
            if (!fs::exists(path)) {
                if (fs::create_directories(path)) {
                    spdlog::info("Created project folder: {}", path.string());
                    CreateProject(path, projectName);
                }
                else {
                    spdlog::error("Failed to create project folder: {}", path.string());
                }
            }
            else {
                spdlog::warn("Project folder already exists. Opening existing project: {}", path.string());
                OpenProject(path);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to create project directory '{}': {}", path.string(), e.what());
        }
    }

    bool LoadProjectMetaData(const fs::path& tilkyEnginePath) {
        if (!fs::exists(tilkyEnginePath)) {
            spdlog::error("Project file does not exist: {}", tilkyEnginePath.string());
            return false;
        }

        std::ifstream file(tilkyEnginePath);

        if (!file.is_open()) {
            spdlog::error("Failed to open project file: {}", tilkyEnginePath.string());
            return false;
        }

        json projectData;

        try {
            file >> projectData;
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to parse project file '{}': {}", tilkyEnginePath.string(), e.what());
            return false;
        }

        currentProjectFile = tilkyEnginePath;
        currentProjectFolder = tilkyEnginePath.parent_path();
        currentProjectName = projectData.value("name", currentProjectFolder.filename().string());

        const std::string assetsFolder = projectData.value("assetsFolder", "Assets");

        currentAssetsPath = currentProjectFolder / assetsFolder;
        currentLevelsPath = currentAssetsPath / "Levels";
        currentTexturesPath = currentAssetsPath / "Textures";
        currentSoundsPath = currentAssetsPath / "Sounds";

        if (!fs::exists(currentAssetsPath)) {
            spdlog::critical("Project is missing Assets folder: {}", currentAssetsPath.string());
            return false;
        }

        if (!fs::exists(currentLevelsPath)) {
            spdlog::critical("Project is missing Levels folder: {}", currentLevelsPath.string());
            return false;
        }

        if (!fs::exists(currentTexturesPath)) {
            spdlog::critical("Project is missing Textures folder: {}", currentTexturesPath.string());
            return false;
        }

        if (!fs::exists(currentSoundsPath)) {
            spdlog::critical("Project is missing Sounds folder: {}", currentSoundsPath.string());
        }

        projectLoaded = true;

        spdlog::info("Loaded project metadata: {}", tilkyEnginePath.string());
        spdlog::info("Current project name: {}", currentProjectName);
        spdlog::info("Project assets path: {}", currentAssetsPath.string());

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

    fs::path GetSoundsPath() {
        return currentSoundsPath;
    }

    fs::path GetContentRootPath() {
#ifdef TILKY_CONTENT_ROOT
        return fs::path(TILKY_CONTENT_ROOT);
#else
        spdlog::warn("TILKY_CONTENT_ROOT is not defined. Falling back to current working directory.");
        return fs::current_path();
#endif
    }

    fs::path FindAssetPath(const fs::path& relativePath) {
        const fs::path sourcePath =
            GetContentRootPath() / relativePath;

        const fs::path packagedPath =
            GetEngineBasePath() / relativePath;

#ifndef NDEBUG
        if (fs::exists(sourcePath)) {
            return sourcePath;
        }

        if (fs::exists(packagedPath)) {
            return packagedPath;
        }
#else
        if (fs::exists(packagedPath)) {
            return packagedPath;
        }

        if (fs::exists(sourcePath)) {
            return sourcePath;
        }
#endif

        spdlog::error(
            "Asset path not found. Relative path: '{}'. Tried source path '{}' and packaged path '{}'.",
            relativePath.string(),
            sourcePath.string(),
            packagedPath.string()
        );

        return packagedPath;
    }

    fs::path GetEngineBasePath() {
        const char* basePath = SDL_GetBasePath();

        if (basePath == nullptr) {
            spdlog::warn("SDL_GetBasePath returned null. Falling back to current working directory.");
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
            spdlog::warn("Launcher config not found. Falling back to English. Expected path: {}", configPath.string());
            return "en";
        }

        std::ifstream file(configPath);

        if (!file.is_open()) {
            spdlog::error("Could not open launcher config. Falling back to English. Path: {}", configPath.string());
            return "en";
        }

        try {
            json configData;
            file >> configData;

            const std::string language = configData.value("lang", "en");

            spdlog::info("Loaded launcher language setting: {}", language);

            return language;
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to read launcher config. Falling back to English. Error: {}", e.what());
            return "en";
        }
    }

    std::string GetProjectName() {
        return currentProjectName;
    }
}