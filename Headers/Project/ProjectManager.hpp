//
// Created by berke on 5/3/2026.
//

#ifndef TILKY_ENGINE_PROJECTMANAGER_H
#define TILKY_ENGINE_PROJECTMANAGER_H

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace ProjectManager {
    // Launches the engine executable with --project <projectFile>. Returns true if
    // the process was successfully spawned.
    // engineDirectory selects which installed copy of the engine to run:
    // - Left empty (the default), this launches whatever copy sits next to the
    //   currently running launcher (GetEngineBasePath()) - the original,
    //   unversioned dev-build behaviour.
    // - Passed explicitly (e.g. GetEngineVersionDirectory(version)), this launches
    //   that specific installed engine version instead.
    bool LaunchEngine(const fs::path &projectFile, const fs::path &engineDirectory = fs::path());

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
    //
    // If the project's "engineVersion" field is set, this launches that exact
    // installed version (see GetEngineVersionDirectory()) and fails - logging an
    // error and returning false without launching anything - if that version is
    // not installed. Callers that let the user pick a project through the launcher
    // UI should check EngineVersionManager::IsVersionInstalled() before offering to
    // open a project at all; this check exists as a safety net, not as the primary
    // "show an Install button" UX (see DrawProjectCard in LauncherApp.cpp).
    // If "engineVersion" is unset (older projects, or projects created before this
    // field existed), this falls back to the original unversioned behaviour and
    // launches whatever engine copy sits next to the running launcher.
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
    //
    // It does NOT create Levels/Textures/Sounds/Scripts subfolders inside
    // Assets. Assets may live in any folder under Assets, so nothing is
    // pre-created at a fixed location - GetLevelsPath() / GetTexturesPath() /
    // GetSoundsPath() / GetScriptsPath() below still return those
    // conventional paths for anything that wants a sensible default, but
    // the paths are not guaranteed to exist on disk.
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
    //
    // This also reads the project's "engineVersion" field (empty string if the
    // project predates it) - see GetProjectEngineVersion().
    //
    // Only the Assets folder itself must actually exist on disk for this to
    // succeed. The Levels/Textures/Sounds/Scripts paths above are always
    // computed, but no longer required to exist - assets may live anywhere
    // under Assets, so a project missing one of those particular subfolders
    // is still considered valid.
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

    // Returns Launcher.tilky inside Documents\TilkyEngine which stores metadata about the launcher, such as the language
    fs::path GetLauncherVariables();

    // Returns the default export folder for the currently loaded project.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\Exports\TestProject
    fs::path GetDefaultExportFolder();

    // ============================================================================
    // Engine version management (EngineVersionManager support)
    //
    // Installed engine versions live side by side under GetEngineVersionsFolder(),
    // one directory per version, e.g.:
    // C:\Users\x\Documents\Tilky Engine\EngineVersions\0.9.0\Tilky_Engine.exe
    // C:\Users\x\Documents\Tilky Engine\EngineVersions\1.0.0\Tilky_Engine.exe
    //
    // These functions are the single source of truth for that layout - both
    // EngineVersionManager (installing/removing versions) and OpenProject() above
    // (launching a project's pinned version) resolve paths through them so the two
    // can never disagree about where a version lives or what its executable is
    // called.
    // ============================================================================

    // Returns the parent folder that holds every installed engine version.
    // Example:
    // C:\Users\x\Documents\Tilky Engine\EngineVersions
    fs::path GetEngineVersionsFolder();

    // Returns the install directory for one specific engine version. Does not
    // guarantee the version is actually installed there - see
    // IsEngineVersionInstalled().
    // Example:
    // C:\Users\x\Documents\Tilky Engine\EngineVersions\0.9.0
    fs::path GetEngineVersionDirectory(const std::string &version);

    // Returns where Tilky_Engine's executable would be for one specific installed
    // version (Tilky_Engine.exe on Windows, Tilky_Engine with no extension on
    // Linux/macOS - matching the real CMake target output name).
    fs::path GetEngineVersionExecutablePath(const std::string &version);

    // Returns where the Standalone player's executable would be for one specific
    // installed version. Note the real build output is named "Standalone"
    // (Tilky_Standalone's CMake OUTPUT_NAME), not "Tilky_Standalone" - this
    // resolves to the name that's actually produced by the build.
    fs::path GetEngineVersionStandaloneExecutablePath(const std::string &version);

    // True if GetEngineVersionExecutablePath(version) exists on disk. This is a
    // plain filesystem check with no manifest/network involvement - see
    // EngineVersionManager::IsVersionInstalled() for the version manager's own
    // (cached) copy of this same check.
    bool IsEngineVersionInstalled(const std::string &version);

    // Patches just the "engineVersion" field into a project's project.tilky,
    // leaving every other field untouched. Accepts either the project's root
    // folder or its project.tilky file directly. Returns false if the file
    // couldn't be read/parsed/written; does not require the project to be the
    // currently loaded one.
    bool SetProjectEngineVersion(const fs::path &projectFolderOrFile, const std::string &version);

    std::string GetCurrentLanguageInLauncher();

    // Returns the name of the currently loaded project.
    // This comes from the "name" field inside project.tilky.
    // Example result:
    // TestProject
    std::string GetProjectName();

    // Returns the currently loaded project's pinned engine version, e.g. "0.9.0".
    // This comes from the "engineVersion" field inside project.tilky and is empty
    // for projects that don't have one set yet (see SetProjectEngineVersion()).
    std::string GetProjectEngineVersion();

    std::filesystem::path GetContentRootPath();

    std::filesystem::path FindAssetPath(const std::filesystem::path& relativePath);

}

#endif //TILKY_ENGINE_PROJECTMANAGER_H