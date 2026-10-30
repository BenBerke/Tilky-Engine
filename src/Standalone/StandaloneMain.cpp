//
// StandaloneMain.cpp
//

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/RuntimeSession.hpp"
#include "Headers/TagRegistry.hpp"
#include "Headers/GlobalEventVariables.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[]) {
    const fs::path projectFile = ProjectManager::GetEngineBasePath() / "project.tilky";

    if (!fs::exists(projectFile)) {
        std::cerr << "Missing project.tilky beside executable:\n" << projectFile.string() << "\n";
        return 1;
    }

    if (!ProjectManager::LoadProjectMetaData(projectFile)) {
        std::cerr << "Failed to load project metadata\n";
        return 1;
    }

    // Engine text the game itself shows (the no-camera screen).
    if (!Localisation::LoadLanguage("en"))
        std::cerr << "Failed to load the English localisation; engine text will show as <missing>\n";

    // Tag names, for entity:HasTag and Game.FindEntitiesWithTag.
    if (!TagRegistry::LoadFromProjectSettings(ProjectManager::GetProjectFolder() / "ProjectSettings.json"))
        std::cerr << "Failed to load ProjectSettings.json; tags won't be found by name\n";

    // Project-wide event variables, read by event conditions.
    if (!GlobalEventVariables::LoadFromProjectSettings(ProjectManager::GetProjectFolder() / "ProjectSettings.json"))
        std::cerr << "Failed to load global event variables from ProjectSettings.json\n";

    LevelSerialization::LevelExtraData extraData;

    if (!LevelManager::LoadStartLevel(&extraData)) {
        std::cerr << "Failed to load startup level for the project\n";
        return 1;
    }

    if (!RuntimeSession::Start(ProjectManager::GetProjectName())) {
        std::cerr << "Failed to start standalone runtime session.\n";
        return 1;
    }

    InputManager::Initialize(nullptr);

    while (!InputManager::QuitRequested()) {
        InputManager::BeginFrame();
        RuntimeSession::Update();
    }

    RuntimeSession::Shutdown();

    return 0;
}