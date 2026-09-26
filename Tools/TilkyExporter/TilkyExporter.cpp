/* TilkyExporter — C++ replacement for the Rust tilky_exporter tool.
 *
 * This standalone CLI copies engine & project folders into the desired
 * export location passed via command-line arguments.
 *
 * Arguments:
 *   1. Path to the current project's project.tilky file.
 *   2. Path to the desired export folder.
 *   3. Path to Standalone.exe.
 *
 * Assets/ is copied as a whole. On top of that every model a level uses is
 * checked: textures and side files (.mtl, .bin, ...) that live outside
 * Assets/ are copied next to the exported model, where ModelLoader looks for
 * them, and anything missing is reported by name.
 */

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool RecreateExportDirectory(const fs::path& directory) {
    std::error_code ec;

    fs::remove_all(directory, ec);
    if (ec) {
        std::cerr << "Failed to clean export folder: " << directory << " — " << ec.message() << "\n";
        return false;
    }

    fs::create_directories(directory, ec);
    if (ec) {
        std::cerr << "Failed to create export folder: " << directory << " — " << ec.message() << "\n";
        return false;
    }

    return true;
}

static bool CopyFileChecked(const fs::path &from, const fs::path &to) {
    if (!fs::exists(from)) {
        std::cerr << "Source file does not exist: " << from << "\n";
        return false;
    }
    if (!fs::is_regular_file(from)) {
        std::cerr << "Source is not a file: " << from << "\n";
        return false;
    }

    if (to.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        if (ec) {
            std::cerr << "Failed to create folder: " << to.parent_path() << " — " << ec.message() << "\n";
            return false;
        }
    }

    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Failed to copy file from " << from << " to " << to << " — " << ec.message() << "\n";
        return false;
    }
    return true;
}

static bool CopyDirRecursive(const fs::path &from, const fs::path &to) {
    if (!fs::exists(from)) {
        std::cerr << "Source directory does not exist: " << from << "\n";
        return false;
    }
    if (!fs::is_directory(from)) {
        std::cerr << "Source is not a directory: " << from << "\n";
        return false;
    }

    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) {
        std::cerr << "Failed to create destination folder: " << to << " — " << ec.message() << "\n";
        return false;
    }

    for (const auto &entry : fs::directory_iterator(from)) {
        const fs::path destPath = to / entry.path().filename();

        if (entry.is_directory()) {
            if (!CopyDirRecursive(entry.path(), destPath)) return false;
        }
        else {
            if (!CopyFileChecked(entry.path(), destPath)) return false;
        }
    }
    return true;
}

static bool CopyAllDlls(const fs::path &from, const fs::path &to) {
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(from, ec)) {
        if (ec) {
            std::cerr << "Failed to read runtime folder: " << from
                      << " — " << ec.message() << "\n";
            return false;
        }

        if (!entry.is_regular_file()) continue;

        const auto ext = entry.path().extension().string();
        
        // Case-insensitive ".dll" check
        if (ext.size() != 4) continue;
        if (!(ext[0] == '.' &&
              (ext[1] == 'd' || ext[1] == 'D') &&
              (ext[2] == 'l' || ext[2] == 'L') &&
              (ext[3] == 'l' || ext[3] == 'L')))
            continue;

        const fs::path destination = to / entry.path().filename();
        if (!CopyFileChecked(entry.path(), destination))
            return false;

        std::cout << "Copied DLL from " << entry.path() << " to " << destination << "\n";
    }
    return true;
}

static bool IsInsideDirectory(const fs::path& path, const fs::path& directory) {
    std::error_code ec;
    const fs::path canonicalPath = fs::weakly_canonical(path, ec);
    if (ec) return false;

    const fs::path canonicalDirectory = fs::weakly_canonical(directory, ec);
    if (ec) return false;

    const fs::path relative = canonicalPath.lexically_relative(canonicalDirectory);
    return !relative.empty() && *relative.begin() != "..";
}

// Model reference -> names of the levels using it.
static std::map<std::string, std::set<std::string>> CollectLevelModels(const fs::path& assetsSrc, std::vector<std::string>& problems) {
    std::map<std::string, std::set<std::string>> usersByModel;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsSrc, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".bson") continue;

        Level level;
        std::string errorMessage;

        if (!LevelSerialization::LoadLevelFromFile(it->path(), level, nullptr, &errorMessage)) {
            problems.push_back("Could not read level " + it->path().string() + " to check its models: " + errorMessage);
            continue;
        }

        for (const ComponentModel& model : level.models.components)
            if (!model.fileName.empty()) usersByModel[model.fileName].insert(level.name);
    }

    if (ec) problems.push_back("Failed to scan " + assetsSrc.string() + " for levels: " + ec.message());

    return usersByModel;
}

static std::string JoinNames(const std::set<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
    return joined;
}

// Returns false only on a hard copy failure. Missing files are collected in
// `problems` so every one of them is reported, not just the first.
static bool ExportModelDependencies(const fs::path& assetsSrc, const fs::path& assetsDest, std::vector<std::string>& problems) {
    const auto usersByModel = CollectLevelModels(assetsSrc, problems);

    for (const auto& [reference, levels] : usersByModel) {
        const fs::path modelSrc = (assetsSrc / fs::path(std::u8string(reference.begin(), reference.end()))).lexically_normal();
        const std::string usedBy = " (used by level " + JoinNames(levels) + ")";

        std::vector<ModelLoader::ModelDependency> dependencies;
        std::string errorMessage;

        if (!ModelLoader::CollectDependencies(modelSrc, assetsSrc, dependencies, errorMessage)) {
            problems.push_back("MISSING model '" + reference + "'" + usedBy + ": " + errorMessage);
            continue;
        }

        const fs::path modelDestDirectory = (assetsDest / modelSrc.lexically_relative(assetsSrc)).parent_path();

        for (const ModelLoader::ModelDependency& dependency : dependencies) {
            const std::string what = dependency.kind == ModelLoader::DependencyKind::Texture ? "texture" : "file";

            if (dependency.status == ModelLoader::DependencyStatus::Embedded) continue;

            if (dependency.status == ModelLoader::DependencyStatus::Missing) {
                std::string problem = "MISSING " + what + " '" + dependency.reference + "' needed by model '" + reference + "'";
                if (!dependency.materialName.empty()) problem += " (material '" + dependency.materialName + "')";

                problems.push_back(problem + usedBy);
                continue;
            }

            // Inside Assets it was already copied with everything else.
            if (IsInsideDirectory(dependency.resolvedPath, assetsSrc)) continue;

            const fs::path destination = modelDestDirectory / dependency.resolvedPath.filename();

            std::error_code ec;
            if (fs::exists(destination, ec)) {
                if (fs::file_size(destination, ec) == fs::file_size(dependency.resolvedPath, ec)) continue;

                problems.push_back(
                    "CONFLICT: external " + what + " " + dependency.resolvedPath.string() +
                    " of model '" + reference + "' was not copied because " + destination.string() +
                    " already exists with different contents"
                );
                continue;
            }

            if (!CopyFileChecked(dependency.resolvedPath, destination)) return false;

            std::cout << "Copied external " << what << " " << dependency.resolvedPath
                      << " of model '" << reference << "' to " << destination << "\n";
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: tilky_exporter <project_metadata_path> "
                     "<destination_path> <standalone_exe_path>\n";
        return 1;
    }

    const fs::path metadataPath     = argv[1];
    const fs::path destinationPath  = argv[2];
    const fs::path standaloneExePath = argv[3];

    const fs::path projectDir   = metadataPath.parent_path();
    const fs::path standaloneDir = standaloneExePath.parent_path();

    if (!RecreateExportDirectory(destinationPath)) return 1;

    // Copy project.tilky
    const fs::path metadataDest = destinationPath / "project.tilky";
    if (!CopyFileChecked(metadataPath, metadataDest)) return 1;
    std::cout << "Copied project metadata from " << metadataPath
              << " to " << metadataDest << "\n";

    // Copy Assets/
    const fs::path assetsSrc  = projectDir / "Assets";
    const fs::path assetsDest = destinationPath / "Assets";
    if (!CopyDirRecursive(assetsSrc, assetsDest)) return 1;
    std::cout << "Copied assets from " << assetsSrc
              << " to " << assetsDest << "\n";

    std::vector<std::string> modelProblems;
    if (!ExportModelDependencies(assetsSrc, assetsDest, modelProblems)) return 1;

    // Copy EngineAssets/Fonts
    const fs::path engineAssetsSrc = standaloneDir / "EngineAssets";
    const fs::path engineAssetsDest = destinationPath / "EngineAssets";
    if (!CopyDirRecursive(engineAssetsSrc, engineAssetsDest)) return 1;
    std::cout << "Copied engine assets from "
              << engineAssetsSrc << " to " << engineAssetsDest << "\n";

    // Copy Shaders/
    const fs::path shadersSrc  = standaloneDir / "Shaders";
    const fs::path shadersDest = destinationPath / "Shaders";
    if (!CopyDirRecursive(shadersSrc, shadersDest)) return 1;
    std::cout << "Copied shaders from " << shadersSrc
              << " to " << shadersDest << "\n";

    // Copy DLLs
    if (!CopyAllDlls(standaloneDir, destinationPath)) return 1;

    // Copy Standalone.exe
    // TODO: make it so the .exe name is the same as the game name
    const fs::path standaloneDest = destinationPath / "Standalone.exe";
    if (!CopyFileChecked(standaloneExePath, standaloneDest)) return 1;
    std::cout << "Copied standalone executable from " << standaloneExePath
              << " to " << standaloneDest << "\n";

    if (!modelProblems.empty()) {
        std::cerr << "\nExport completed, but " << modelProblems.size()
                  << " model dependency problem(s) were found. Affected models render without these files:\n";

        for (const std::string& problem : modelProblems) std::cerr << "  - " << problem << "\n";

        return 0;
    }

    std::cout << "Export completed successfully.\n";
    return 0;
}
