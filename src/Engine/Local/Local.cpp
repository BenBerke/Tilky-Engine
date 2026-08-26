//
// Created by berke on 5/3/2026.
//

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <SDL3/SDL_log.h>

#include "../../../Headers/Engine/Local/Local.hpp"

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

#ifndef TILKY_CONTENT_ROOT
#define TILKY_CONTENT_ROOT "."
#endif

namespace {
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, std::string> englishStrings;

    std::string currentLanguage = "en";
    const std::string missingString = "<missing>";

    fs::path GetContentRootPath() {
        return fs::path(TILKY_CONTENT_ROOT);
    }

    fs::path GetEngineAssetsPath() {
        return GetContentRootPath() / "EngineAssets";
    }

    fs::path BuildLanguagePath(const std::string& languageCode) {
        return ProjectManager::FindAssetPath(
            fs::path("EngineAssets") / "Local" / (languageCode + ".json")
        );
    }

    bool LoadLanguageFile(
        const std::string& languageCode,
        std::unordered_map<std::string, std::string>& destination
    ) {
        const fs::path path = BuildLanguagePath(languageCode);
        std::ifstream file(path);

        if (!file.is_open()) {
            SDL_Log(
                "Failed to open localisation file: %s",
                path.string().c_str()
            );
            return false;
        }

        json data;

        try {
            file >> data;
        }
        catch (const std::exception& e) {
            SDL_Log(
                "Failed to parse localisation file %s: %s",
                path.string().c_str(),
                e.what()
            );
            return false;
        }

        std::unordered_map<std::string, std::string> loadedStrings;

        for (const auto& [key, value] : data.items()) {
            if (value.is_string()) {
                loadedStrings[key] = value.get<std::string>();
            }
        }

        destination = std::move(loadedStrings);
        return true;
    }
}

//todo: Localisation doesn't work in the editor for the first time (It gets fixed after a restart)

// Refer to Local.hpp for comments
namespace Localisation {
    bool LoadLanguage(const std::string& languageCode) {
        spdlog::info("Requested language: {}", languageCode);
        spdlog::info("Current working directory: {}",fs::current_path().string());
        spdlog::info("Engine base path: {}",ProjectManager::GetEngineBasePath().string());

        // Always keep English loaded as the fallback language.
        if (!LoadLanguageFile("en", englishStrings)) {
            spdlog::critical("Failed to load the English fallback language");
            return false;
        }

        if (languageCode == "en") strings = englishStrings;

        else if (!LoadLanguageFile(languageCode, strings)) return false;


        currentLanguage = languageCode;
        return true;
    }

    const std::string& Get(const std::string& key) {
        const auto translatedIt = strings.find(key);

        if (translatedIt != strings.end()) return translatedIt->second;


        spdlog::error("Localisation key '%s' is missing from language '%s'. Using English.",key.c_str(),currentLanguage.c_str());

        const auto englishIt = englishStrings.find(key);

        if (englishIt != englishStrings.end()) return englishIt->second;


        // Critical because missing localization key means the ImGUI button will not function
        spdlog::critical("Localisation key '%s' is also missing from English.",key.c_str());

        return missingString;
    }

    const std::string& CurrentLanguage() {
        return currentLanguage;
    }
}