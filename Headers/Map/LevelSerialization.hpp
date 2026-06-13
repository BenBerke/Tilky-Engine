#ifndef TILKY_ENGINE_LEVELSERIALIZATION_HPP
#define TILKY_ENGINE_LEVELSERIALIZATION_HPP

#include <filesystem>
#include <string>

#include "Headers/Objects/Level.hpp"

namespace LevelSerialization {
    struct LevelExtraData {
        int backgroundTextureIndex = -1;
    };

    std::string CleanLevelName(const std::string& levelName);
    std::filesystem::path BuildLevelPath(const std::string& levelName);

    bool LoadLevelFromFile(
        const std::filesystem::path& levelFile,
        Level& outLevel,
        LevelExtraData* outExtraData = nullptr,
        std::string* errorMessage = nullptr
    );

    bool SaveLevelToFile(
        const std::filesystem::path& levelFile,
        const Level& level,
        const LevelExtraData* extraData = nullptr,
        std::string* errorMessage = nullptr
    );
}

#endif
