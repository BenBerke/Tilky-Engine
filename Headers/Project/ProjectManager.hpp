//
// Created by berke on 5/3/2026.
//

#ifndef WOLFY_ENGINE_PROJECTMANAGER_H
#define WOLFY_ENGINE_PROJECTMANAGER_H

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace ProjectManager {
    void LaunchEngine(const fs::path &projectFile);

    bool OpenProject(const std::filesystem::path &path);
    void CreateProject(const fs::path &directory, const std::string &projectName);
    void CreateDirectory(const std::string &projectName);
    bool LoadProjectMetaData(const fs::path &path);

    bool HasProject();

    fs::path GetUserHomeDirectory();
    fs::path GetDefaultProjectsFolder();
    fs::path GetProjectFiles();
    fs::path GetProjectFolder();
    fs::path GetAssetsPath();
    fs::path GetTexturesPath();
    fs::path GetLevelsPath();
    fs::path GetEngineBasePath();
    fs::path GetEngineFolder();
    fs::path GetLauncherVariables();
    std::string GetCurrentLanguageInLauncher();
    std::string GetProjectName();

}

#endif //WOLFY_ENGINE_PROJECTMANAGER_H