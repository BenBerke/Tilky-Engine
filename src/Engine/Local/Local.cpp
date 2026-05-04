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

using json = nlohmann::json;
namespace fs = std::filesystem;

#ifndef WOLFY_CONTENT_ROOT
#define WOLFY_CONTENT_ROOT "."
#endif

namespace {
    std::unordered_map<std::string, std::string> strings;
    std::string currentLanguage = "en";

    const std::string missingString = "<missing>";

    fs::path GetContentRootPath() {
        return fs::path(WOLFY_CONTENT_ROOT);
    }

    fs::path GetEngineAssetsPath() {
        return GetContentRootPath() / "EngineAssets";
    }

    fs::path BuildLanguagePath(const std::string& languageCode) {
        return GetEngineAssetsPath() / "Local" / (languageCode + ".json");
    }
}

namespace Localisation {
    bool LoadLanguage(const std::string& languageCode) {
        const fs::path path = BuildLanguagePath(languageCode);

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