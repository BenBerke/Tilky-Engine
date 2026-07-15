/* TilkyExporter — C++ replacement for the Rust tilky_exporter tool.
 *
 * This standalone CLI copies engine & project folders into the desired
 * export location passed via command-line arguments.
 *
 * Arguments:
 *   1. Path to the current project's project.tilky file.
 *   2. Path to the desired export folder.
 *   3. Path to Standalone.exe.
 */

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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
            std::cerr << "Failed to create folder: " << to.parent_path()
                      << " — " << ec.message() << "\n";
            return false;
        }
    }

    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Failed to copy file from " << from << " to " << to
                  << " — " << ec.message() << "\n";
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
        std::cerr << "Failed to create destination folder: " << to
                  << " — " << ec.message() << "\n";
        return false;
    }

    for (const auto &entry : fs::directory_iterator(from)) {
        const fs::path destPath = to / entry.path().filename();

        if (entry.is_directory()) {
            if (!CopyDirRecursive(entry.path(), destPath))
                return false;
        } else {
            if (!CopyFileChecked(entry.path(), destPath))
                return false;
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

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
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

    // Copy EngineAssets/Fonts
    const fs::path fontsSrc  = standaloneDir / "EngineAssets" / "Fonts";
    const fs::path fontsDest = destinationPath / "EngineAssets" / "Fonts";
    if (!CopyDirRecursive(fontsSrc, fontsDest)) return 1;
    std::cout << "Copied engine fonts from " << fontsSrc
              << " to " << fontsDest << "\n";

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

    std::cout << "Export completed successfully.\n";
    return 0;
}
