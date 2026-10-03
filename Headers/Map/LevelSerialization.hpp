#ifndef TILKY_ENGINE_LEVELSERIALIZATION_HPP
#define TILKY_ENGINE_LEVELSERIALIZATION_HPP

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Objects/Level.hpp"

namespace LevelSerialization {
    struct LevelExtraData {
        std::string backgroundTextureFileName;
    };

    std::string CleanLevelName(const std::string& levelName);

    // A level is identified by its file name without ".bson", and its file may
    // be in any folder under Assets, so level names must be unique project-wide.

    // Every level file anywhere under the project's Assets folder, sorted.
    std::vector<std::filesystem::path> ListLevelFiles();

    // Every level file under Assets called levelName - more than one is an error
    // the editor tries to prevent, but files can still be copied in by hand.
    std::vector<std::filesystem::path> FindLevelFiles(const std::string& levelName);

    // The one level file called levelName. Empty, with errorMessage set, when
    // there is no such level or more than one.
    std::filesystem::path FindLevelPath(const std::string& levelName, std::string* errorMessage = nullptr);

    // Where saving levelName should write: its existing file, or Assets/<name>.bson
    // for a level that has never been saved. Empty, with errorMessage set, when
    // more than one level has that name.
    std::filesystem::path ResolveLevelSavePath(const std::string& levelName, std::string* errorMessage = nullptr);

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