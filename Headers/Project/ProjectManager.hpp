//
// Created by berke on 5/3/2026.
//

#ifndef TILKY_ENGINE_PROJECTMANAGER_H
#define TILKY_ENGINE_PROJECTMANAGER_H

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace ProjectManager {
    void LaunchEngine(const fs::path &projectFile);



    // Returns the current user's home folder.
    // On Windows this usually comes from the USERPROFILE environment variable.
    // Example:
    // C:\Users\berke
    fs::path GetUserHomeDirectory();

    // Returns Tilky Engine's default projects folder.
    // This is where all user-created Tilky projects are stored.
    // Example:
    // C:\Users\berke\Documents\Tilky Engine\Projects
    fs::path GetDefaultProjectsFolder();

    // Opens an existing project from its project root folder.
    // Parameter:
    // - path should be the root folder of one specific project.
    // - It should be the folder that contains project.tilky.
    // - It should NOT be the project.tilky file itself.
    // - It should NOT be the top-level Projects folder.
    //
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject
    //
    // This function builds the project.tilky path internally, loads the project metadata,
    // then launches the engine with that .tilky file.
    bool OpenProject(const std::filesystem::path &path);

    // Creates the internal files/folders for a new project inside an already-created
    // project root folder.
    // Parameters:
    // - directory should be the root folder of one specific project.
    // - projectName should be the display/name value written into project.tilky.
    //
    // directory should NOT be the top-level Projects folder by itself.
    // directory should NOT be the .tilky file.
    //
    // Example directory:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject
    //
    // This function creates:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\project.tilky
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets\Levels
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets\Textures
    void CreateProject(const fs::path &directory, const std::string &projectName);

    // Creates a new project folder inside the top-level Projects folder, then creates
    // the actual project files inside it.
    // Parameter:
    // - projectName should be just the project folder/name, not a full path.
    // - It should NOT include ".tilky".
    // - It should NOT be "C:\Users\...\Projects\TestProject".
    // - It should just be something like "TestProject".
    //
    // Example projectName:
    // TestProject
    //
    // This function turns that into:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject
    //
    // Then it calls CreateProject() using that project root folder.
    void CreateProjectDirectory(const std::string &projectName);

    // Loads a project's metadata from its project.tilky file and stores the important
    // project paths inside ProjectManager.
    // Parameter:
    // - tilkyEnginePath should be the full path to one project's project.tilky file.
    // - It should NOT be the project root folder.
    // - It should NOT be the top-level Projects folder.
    //
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\project.tilky
    //
    // After loading, this fills:
    // currentProjectFile    = ...\TestProject\project.tilky
    // currentProjectFolder  = ...\TestProject
    // currentAssetsPath     = ...\TestProject\Assets
    // currentLevelsPath     = ...\TestProject\Assets\Levels
    // currentTexturesPath   = ...\TestProject\Assets\Textures
    bool LoadProjectMetaData(const fs::path &path);

    // Returns whether a project has successfully been loaded into ProjectManager.
    // This does not return a file path.
    // Example result:
    // true if C:\Users\berke\Documents\Tilky Engine\Projects\TestProject\project.tilky was loaded successfully.
    bool HasProject();

    // Returns the currently loaded project's .tilky project file.
    // This is the metadata file used to open/load the project.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\project.tilky
    fs::path GetProjectFiles();

    // Returns the root folder of the currently loaded project.
    // This is the folder that contains project.tilky and the Assets folder.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject
    fs::path GetProjectFolder();

    // Returns the Assets folder of the currently loaded project.
    // This folder contains project-specific assets such as levels and textures.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets
    fs::path GetAssetsPath();

    // Returns the Textures folder of the currently loaded project.
    // This is where project-specific texture files should be stored.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets\Textures
    fs::path GetTexturesPath();

    // Returns the Levels folder of the currently loaded project.
    // This is where project-specific level JSON files should be saved and loaded from.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets\Levels
    fs::path GetLevelsPath();

    // Returns the Sounds folder of the currently loaded project.
    // This is where project-specific sound files should be saved and loaded from.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets\Sounds
    fs::path GetSoundsPath();

    // Returns the Scripts folder of the currently loaded project.
    // This is where project-specific scripts files should be saved and loaded from.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Projects\TestProject\Assets\Scripts
    fs::path GetScriptsPath();

    // Returns the folder where the currently running executable is located.
    // This comes from SDL_GetBasePath(), so in CLion it usually points to the build folder.
    // This is NOT the same as the user's Documents\Tilky Engine folder.
    // Example:
    // C:\Users\x\Desktop\CLion Projects\Tilky Engine\cmake-build-debug
    fs::path GetEngineBasePath();

    // Returns Tilky Engine's user data folder inside Documents\TilkyEngine.
    // This is the parent folder of the Projects folder.
    // Use this for launcher-wide files such as Launcher.tilky.
    // Example:
    // C:\Users\x\Documents\Tilky Engine
    fs::path GetEngineFolder();

    // Returns Launcher.tilky insdie Documents\TilkyEngine which stores metadata about the launcher, such as the language
    fs::path GetLauncherVariables();

    std::string GetCurrentLanguageInLauncher();

    // Returns the name of the currently loaded project.
    // This comes from the "name" field inside project.tilky.
    // Example result:
    // TestProject
    std::string GetProjectName();

    std::filesystem::path GetContentRootPath();

    std::filesystem::path FindAssetPath(const std::filesystem::path& relativePath);

}

#endif //TILKY_ENGINE_PROJECTMANAGER_H