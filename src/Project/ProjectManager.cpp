//
// Created by berke on 5/3/2026.
//
#include "Headers/Project/ProjectManager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

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
    fs::path currentScriptsPath;

    std::string currentProjectName;
    std::string currentEngineVersion;
    std::string currentLastOpenLevelName;
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

    bool LaunchEngine(const fs::path& projectFile, const fs::path& engineDirectory) {
        const fs::path resolvedEngineDir = engineDirectory.empty() ? GetEngineBasePath() : engineDirectory;

#ifdef _WIN32
        const fs::path engineExe = resolvedEngineDir / "Tilky_Engine.exe";

        std::wstring appPath = engineExe.wstring();

        std::wstring commandLine =
            L"\"" + appPath + L"\" --project \"" + projectFile.wstring() + L"\"";

        std::wstring workingDirectory = resolvedEngineDir.wstring();

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
            return false;
        }

        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);

        return true;

#else
        // NOTE: this was already a typo'd "TIlky_Engine" (capital I) before this
        // change, which meant it never actually matched the real CMake target output
        // name ("Tilky_Engine") on Linux/macOS - fixed here since EngineVersionManager
        // needs this exact name to match on every platform it validates installs on
        // (see ProjectManager::GetEngineVersionExecutablePath).
        const fs::path engineExe = resolvedEngineDir / "Tilky_Engine";

        const std::string command =
            "\"" + engineExe.string() + "\" --project \"" + projectFile.string() + "\"";

        spdlog::info("Launching engine with command: {}", command);

        const int result = std::system(command.c_str());

        if (result != 0) {
            spdlog::error("Engine process returned non-zero exit code: {}", result);
            return false;
        }

        return true;
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

        std::error_code existsEc;
        if (!fs::exists(projectFile, existsEc)) {
            spdlog::error("Missing .tilky project file at: {}", projectFile.string());
            return false;
        }

        if (!LoadProjectMetaData(projectFile)) {
            spdlog::error("Failed to load project metadata before launching engine: {}", projectFile.string());
            return false;
        }

        if (currentEngineVersion.empty()) {
            // No engine version pinned (project predates EngineVersionManager, or was
            // never assigned one) - fall back to the original behaviour and launch
            // whatever engine copy sits next to the running launcher.
            return LaunchEngine(projectFile);
        }

        if (!IsEngineVersionInstalled(currentEngineVersion)) {
            spdlog::error(
                "Project '{}' requires engine version {}, which is not installed. Expected executable at: {}",
                currentProjectName,
                currentEngineVersion,
                GetEngineVersionExecutablePath(currentEngineVersion).string()
            );
            return false;
        }

        return LaunchEngine(projectFile, GetEngineVersionDirectory(currentEngineVersion));
    }

    void CreateProject(const fs::path& directory, const std::string& projectName) {
        const fs::path assetsPath = directory / "Assets";

        bool openProject = true;

        try {
            if (!fs::exists(assetsPath)) {
                if (!fs::create_directories(assetsPath)) {
                    spdlog::critical("Could not create project assets folder: {}", assetsPath.string());
                    openProject = false;
                }
                else spdlog::info("Created project assets folder: {}", assetsPath.string());
            }

            // Levels/Textures/Sounds/Scripts subfolders are intentionally NOT
            // created here anymore: assets are allowed in any folder under
            // Assets, so nothing is pre-created at a fixed, predetermined
            // location. The asset browser creates folders (and levels) on
            // demand wherever the user actually asks for them instead.

            const fs::path dataPath = directory / "project.tilky";

            json projectData;
            projectData["name"] = projectName;
            projectData["assetsFolder"] = "Assets";
            projectData["engineVersion"] = ""; // set later by the launcher (new-project default pin) or via "Select Version"

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
        std::error_code tilkyExistsEc;
        if (!fs::exists(tilkyEnginePath, tilkyExistsEc)) {
            spdlog::error("Project file does not exist: {}", tilkyEnginePath.string());
            return false;
        }

        std::ifstream file(tilkyEnginePath);

        if (!file.is_open()) {
            spdlog::error("Failed to open project file: {}", tilkyEnginePath.string());
            return false;
        }

        json projectData;

        try { file >> projectData; }
        catch (const std::exception& e) {
            spdlog::error("Failed to parse project file '{}': {}", tilkyEnginePath.string(), e.what());
            return false;
        }

        currentProjectFile = tilkyEnginePath;
        currentProjectFolder = tilkyEnginePath.parent_path();
        currentProjectName = projectData.value("name", currentProjectFolder.filename().string());
        currentEngineVersion = projectData.value("engineVersion", std::string());

        // Empty when the project has never had a level recorded yet (or predates
        // this field). A plain level name, same shape Editor::LoadLevel() and
        // Editor::currentMap use - no path or extension.
        currentLastOpenLevelName = projectData.value("lastOpenLevel", std::string());

        const std::string assetsFolder = projectData.value("assetsFolder", "Assets");

        currentAssetsPath = currentProjectFolder / assetsFolder;
        currentLevelsPath = currentAssetsPath / "Levels";
        currentTexturesPath = currentAssetsPath / "Textures";
        currentSoundsPath = currentAssetsPath / "Sounds";
        currentScriptsPath = currentAssetsPath / "Scripts";

        // Only Assets itself is required. Levels/Textures/Sounds/Scripts are
        // still computed above (for anything that wants a sensible default
        // location), but are no longer mandatory - a project simply doesn't
        // have to keep assets in those specific subfolders anymore. Code
        // that actually reads from one of these paths (e.g. LevelManager)
        // already checks for its own existence before using it.
        std::error_code assetsExistsEc;
        if (!fs::exists(currentAssetsPath, assetsExistsEc)) {
            spdlog::critical("Project is missing Assets folder: {}", currentAssetsPath.string());
            return false;
        }

        projectLoaded = true;

        spdlog::info("Loaded project metadata: {}", tilkyEnginePath.string());
        spdlog::info("Current project name: {}", currentProjectName);
        spdlog::info("Project assets path: {}", currentAssetsPath.string());
        spdlog::info("Project engine version: {}", currentEngineVersion.empty() ? "(none pinned)" : currentEngineVersion);

        return true;
    }

    bool HasProject() {return projectLoaded;}

    fs::path GetProjectFiles() {return currentProjectFile;}

    fs::path GetProjectFolder() {return currentProjectFolder;}

    fs::path GetAssetsPath() {return currentAssetsPath;}

    fs::path GetTexturesPath() {return currentTexturesPath;
    }
    fs::path GetLevelsPath() {return currentLevelsPath;}

    fs::path GetSoundsPath() {return currentSoundsPath;}

    fs::path GetScriptsPath() {return currentScriptsPath;}

    fs::path GetContentRootPath() {
#ifdef TILKY_CONTENT_ROOT
        return fs::path(TILKY_CONTENT_ROOT);
#else
        spdlog::warn("TILKY_CONTENT_ROOT is not defined. Falling back to current working directory.");
        return fs::current_path();
#endif
    }

    fs::path FindAssetPath(const fs::path& relativePath) {
        const fs::path sourcePath = GetContentRootPath() / relativePath;

        const fs::path packagedPath = GetEngineBasePath() / relativePath;

        std::error_code sourceExistsEc;
        std::error_code packagedExistsEc;

#ifndef NDEBUG
        if (fs::exists(sourcePath, sourceExistsEc)) {
            return sourcePath;
        }

        if (fs::exists(packagedPath, packagedExistsEc)) {
            return packagedPath;
        }
#else
        if (fs::exists(packagedPath, packagedExistsEc)) {
            return packagedPath;
        }

        if (fs::exists(sourcePath, sourceExistsEc)) {
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

    fs::path GetDefaultExportFolder() {
        std::string projectName = GetProjectName();

        if (projectName.empty()) { projectName = GetProjectFolder().filename().string(); }

        if (projectName.empty()) {projectName = "Untitled";}

        return GetEngineFolder() / "Exports" / projectName;
    }

    fs::path GetEngineVersionsFolder() {
        return GetEngineFolder() / "EngineVersions";
    }

    fs::path GetEngineVersionDirectory(const std::string& version) {
        return GetEngineVersionsFolder() / version;
    }

    fs::path GetEngineVersionExecutablePath(const std::string& version) {
#ifdef _WIN32
        return GetEngineVersionDirectory(version) / "Tilky_Engine.exe";
#else
        return GetEngineVersionDirectory(version) / "Tilky_Engine";
#endif
    }

    fs::path GetEngineVersionStandaloneExecutablePath(const std::string& version) {
        // Matches Tilky_Standalone's CMake OUTPUT_NAME ("Standalone"), not the
        // target name - see the note on this function in the header.
#ifdef _WIN32
        return GetEngineVersionDirectory(version) / "Standalone.exe";
#else
        return GetEngineVersionDirectory(version) / "Standalone";
#endif
    }

    bool IsEngineVersionInstalled(const std::string& version) {
        if (version.empty()) return false;

        std::error_code existsEc;
        return fs::exists(GetEngineVersionExecutablePath(version), existsEc);
    }

    bool SetProjectEngineVersion(const fs::path& projectFolderOrFile, const std::string& version) {
        fs::path tilkyFile = projectFolderOrFile;
        if (tilkyFile.extension() != ".tilky") {
            tilkyFile = tilkyFile / "project.tilky";
        }

        std::error_code existsEc;
        if (!fs::exists(tilkyFile, existsEc)) {
            spdlog::error("Cannot set engine version - project file does not exist: {}", tilkyFile.string());
            return false;
        }

        json projectData;

        {
            std::ifstream inFile(tilkyFile);
            if (!inFile.is_open()) {
                spdlog::error("Cannot set engine version - failed to open project file: {}", tilkyFile.string());
                return false;
            }

            try { inFile >> projectData; }
            catch (const std::exception& e) {
                spdlog::error("Cannot set engine version - failed to parse project file '{}': {}", tilkyFile.string(), e.what());
                return false;
            }
        }

        projectData["engineVersion"] = version;

        std::ofstream outFile(tilkyFile);
        if (!outFile.is_open()) {
            spdlog::error("Cannot set engine version - failed to open project file for writing: {}", tilkyFile.string());
            return false;
        }

        outFile << projectData.dump(4);
        outFile.close();

        // Keep the in-memory copy consistent if this happens to be the currently
        // loaded project, so callers don't have to re-run LoadProjectMetaData just to
        // see their own write reflected in GetProjectEngineVersion().
        std::error_code equivalentEc;
        if (fs::equivalent(tilkyFile, currentProjectFile, equivalentEc)) {
            currentEngineVersion = version;
        }

        spdlog::info("Set engine version for '{}' to {}", tilkyFile.string(), version);

        return true;
    }

    std::string GetLastOpenLevelName() {
        return currentLastOpenLevelName;
    }

    bool SetLastOpenLevelName(const std::string& levelName) {
        if (currentProjectFile.empty()) {
            spdlog::error("Cannot set last open level - no project is currently loaded.");
            return false;
        }

        json projectData;

        {
            std::ifstream inFile(currentProjectFile);
            if (!inFile.is_open()) {
                spdlog::error("Cannot set last open level - failed to open project file: {}", currentProjectFile.string());
                return false;
            }

            try { inFile >> projectData; }
            catch (const std::exception& e) {
                spdlog::error("Cannot set last open level - failed to parse project file '{}': {}", currentProjectFile.string(), e.what());
                return false;
            }
        }

        projectData["lastOpenLevel"] = levelName;

        std::ofstream outFile(currentProjectFile);
        if (!outFile.is_open()) {
            spdlog::error("Cannot set last open level - failed to open project file for writing: {}", currentProjectFile.string());
            return false;
        }

        outFile << projectData.dump(4);
        outFile.close();

        // This function only ever operates on the currently loaded project (unlike
        // SetProjectEngineVersion, which can target an arbitrary one), so the
        // in-memory copy is always the one to update.
        currentLastOpenLevelName = levelName;

        spdlog::info("Set last open level for '{}' to '{}'", currentProjectName, levelName);

        return true;
    }

    fs::path GetLauncherVariables() { return GetEngineFolder() / "Launcher.tilky";}

    std::string GetCurrentLanguageInLauncher() {
        const fs::path configPath = GetLauncherVariables();

        std::error_code configExistsEc;
        if (!fs::exists(configPath, configExistsEc)) {
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

    std::string GetProjectName() { return currentProjectName;}

    std::string GetProjectEngineVersion() { return currentEngineVersion;}
}