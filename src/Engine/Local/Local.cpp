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

#include "Headers/Project/ProjectManager.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

#ifndef TILKY_CONTENT_ROOT
#define TILKY_CONTENT_ROOT "."
#endif

namespace {
    std::unordered_map<std::string, std::string> strings;
    std::string currentLanguage = "en"; // Default back to English in case of a bug

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
}

//todo: Localisation doesn't work in the editor for the first time (It gets fixed after a restart)

// Refer to Local.hpp for comments
namespace Localisation {
    bool LoadLanguage(const std::string& languageCode) {
        const fs::path path = BuildLanguagePath(languageCode);

        SDL_Log("Requested language: %s", languageCode.c_str());
        SDL_Log("Current working directory: %s", fs::current_path().string().c_str());
        SDL_Log("Engine base path: %s", ProjectManager::GetEngineBasePath().string().c_str());
        SDL_Log("Trying localisation file: %s", path.string().c_str());
        SDL_Log("Localisation file exists: %s", fs::exists(path) ? "true" : "false");

        std::ifstream file(path.string());

        if (!file.is_open()) {
            SDL_Log("Failed to open localisation file: %s", path.string().c_str());
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

        strings.clear();

        for (auto& [key, value] : data.items()) {
            if (value.is_string()) {
                strings[key] = value.get<std::string>();
            }
        }

        currentLanguage = languageCode;

        return true;
    }

    const std::string& Get(const std::string& key) {
        auto it = strings.find(key);

        if (it == strings.end()) {
            SDL_Log("Failed to find localisation key %s", key.c_str());
            return missingString;
        }

        return it->second;
    }


    const std::string& CurrentLanguage() {
        return currentLanguage;
    }
}