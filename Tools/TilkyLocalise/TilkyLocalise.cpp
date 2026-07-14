/* TilkyLocalise — C++ replacement for the Rust tilky_localise tool.
 *
 * Interactive CLI that manages localization JSON files.
 * For every key present in the base language file but missing in the target,
 * inserts "$Missing Translation" so translators know what to fill in.
 *
 * Resolves the project root relative to the executable's own location
 * (expects the executable to live at <project_root>/Tools/TilkyLocalise/).
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static fs::path GetLocalDir(const fs::path &exePath) {
    // The executable lives at <project_root>/Tools/TilkyLocalise/tilky_localise
    // Navigate up two levels to reach the project root.
    fs::path toolDir = exePath.parent_path();
    fs::path projectRoot = toolDir.parent_path().parent_path();

    fs::path localDir = projectRoot / "EngineAssets" / "Local";

    if (!fs::exists(localDir)) {
        std::error_code ec;
        fs::create_directories(localDir, ec);
        if (ec) {
            std::cerr << "Failed to create Local directory: " << localDir
                      << " — " << ec.message() << "\n";
        }
    }

    return localDir;
}

static std::string ReadEntireFile(const fs::path &path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) return {};
    return {std::istreambuf_iterator<char>(ifs),
            std::istreambuf_iterator<char>()};
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    // Resolve the executable's own path.
    fs::path exePath;
    if (argc > 0 && argv[0] && argv[0][0] != '\0') {
        exePath = fs::absolute(argv[0]);
    } else {
        std::cerr << "Could not determine executable path.\n";
        return 1;
    }

    const fs::path localDir = GetLocalDir(exePath);

    std::vector<std::string> languageNames;

    std::cout << "Existing files found in " << localDir << ":\n";

    for (const auto &entry : fs::directory_iterator(localDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        const std::string languageName = entry.path().stem().string();
        const std::string contents     = ReadEntireFile(entry.path());

        try {
            json fileJson = json::parse(contents);
            languageNames.push_back(languageName);

            std::string lang = "No Name Key";
            if (fileJson.contains("language.name") && fileJson["language.name"].is_string()) {
                lang = fileJson["language.name"].get<std::string>();
            }

            std::cout << "  * " << lang << " : " << languageName << "\n";
        } catch (const json::exception &) {
            // Skip files that aren't valid JSON.
        }
    }

    // Prompt for base language
    std::cout << "\nSelect the base language by typing the value on the right "
                 "(\"en\" for English is suggested)\n";
    std::string firstInput;
    if (!std::getline(std::cin, firstInput)) return 1;
    // Trim whitespace
    while (!firstInput.empty() && (firstInput.back() == '\r' || firstInput.back() == '\n' ||
                                   firstInput.back() == ' '  || firstInput.back() == '\t'))
        firstInput.pop_back();

    // Prompt for target language
    std::cout << "Select the target language you want to translate to\n";
    std::string secondInput;
    if (!std::getline(std::cin, secondInput)) return 1;
    while (!secondInput.empty() && (secondInput.back() == '\r' || secondInput.back() == '\n' ||
                                    secondInput.back() == ' '  || secondInput.back() == '\t'))
        secondInput.pop_back();

    if (firstInput == secondInput) {
        std::cout << "Base and target can not be the same\n";
        return 0;
    }

    const bool foundBase   = std::find(languageNames.begin(), languageNames.end(), firstInput)  != languageNames.end();
    const bool foundTarget = std::find(languageNames.begin(), languageNames.end(), secondInput) != languageNames.end();

    // Read base JSON
    std::string baseContents;
    if (foundBase) {
        const fs::path basePath = localDir / (firstInput + ".json");
        baseContents = ReadEntireFile(basePath);
        if (baseContents.empty()) {
            std::cerr << "Failed to read base file: " << basePath << "\n";
            return 1;
        }
    } else {
        std::cout << "Base '" << firstInput << "' could not be found, falling back to en.json\n";
        const fs::path enPath = localDir / "en.json";
        baseContents = ReadEntireFile(enPath);
        if (baseContents.empty()) {
            std::cerr << "Fallback 'en.json' missing at " << enPath << "\n";
            return 1;
        }
    }

    // Read or create target JSON
    std::string targetContents;
    if (foundTarget) {
        const fs::path targetPath = localDir / (secondInput + ".json");
        targetContents = ReadEntireFile(targetPath);
    } else {
        std::cout << "Target '" << secondInput << "' could not be found, creating new file\n";
        targetContents = "{}";
    }

    // Parse
    json baseJson, targetJson;
    try {
        baseJson   = json::parse(baseContents);
        targetJson = json::parse(targetContents);
    } catch (const json::exception &e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
        return 1;
    }

    if (!baseJson.is_object()) {
        std::cerr << "Base JSON is not an object\n";
        return 1;
    }
    if (!targetJson.is_object()) {
        std::cerr << "Target JSON is not an object\n";
        return 1;
    }

    // Merge missing keys
    for (auto &[key, value] : baseJson.items()) {
        if (!targetJson.contains(key)) {
            targetJson[key] = "$Missing Translation";
        }
    }

    // Write output
    const fs::path destinationPath = localDir / (secondInput + ".json");
    {
        std::ofstream ofs(destinationPath);
        if (!ofs) {
            std::cerr << "Failed to write target file: " << destinationPath << "\n";
            return 1;
        }
        ofs << targetJson.dump(4) << "\n";
    }

    std::cout << "Successfully processed and updated target file! Press Enter to exit\n";
    std::string wait;
    std::getline(std::cin, wait);

    return 0;
}
