//
// Created by berke on 5/3/2026.
//

#ifndef TILKY_ENGINE_LOCALISATION_H
#define TILKY_ENGINE_LOCALISATION_H

#include <string>
#include <unordered_map>

namespace Localisation {
    // Language should be the name of the corresponding json file in EngineAssets/Local/
    // ie. for English. the languageCode should be en
    bool LoadLanguage(const std::string& languageCode);

    // Get the respective translation in the last loaded language
    const std::string& Get(const std::string& key);

    // Returns the last loaded languageCode
    const std::string& CurrentLanguage();
}

#endif //TILKY_ENGINE_LOCALISATION_H