#ifndef TILKY_ENGINE_LEVELSERIALIZATION_HPP
#define TILKY_ENGINE_LEVELSERIALIZATION_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Objects/Level.hpp"

namespace LevelSerialization {
    // A script's public fields as JSON text - the field manifest the exporter
    // writes next to each precompiled script (see TilkyExporter.cpp).
    std::string ScriptPublicFieldsToJsonText(const std::vector<ScriptPublicField>& fields);
    bool ScriptPublicFieldsFromJsonText(const std::string& text, std::vector<ScriptPublicField>& outFields, std::string* errorMessage = nullptr);

    // Data saved in a level file that does not belong on the Level itself.
    // Empty for now; kept as the place for such data.
    struct LevelExtraData {
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

    // Removes the editor-only data - camera positions, wall snaps, selection
    // groups, comments, grid and background image - from a saved level file,
    // leaving everything the game reads untouched. Used on exported copies.
    // `removed` says whether there was any.
    bool StripEditorData(const std::filesystem::path& levelFile, bool* removed = nullptr, std::string* errorMessage = nullptr);

    bool SaveLevelToFile(
        const std::filesystem::path& levelFile,
        const Level& level,
        const LevelExtraData* extraData = nullptr,
        std::string* errorMessage = nullptr
    );

    // The given entities and their components in level.bson's own layout,
    // {"entities": [...], "components": {...}} - what a .prefab file holds.
    nlohmann::json EntitiesToJson(const Level& level, const std::vector<ID>& entityIDs);

    // Replaces outLevel's entities and components with the ones in `data`
    // (as written by EntitiesToJson), keeping their saved IDs.
    bool EntitiesFromJson(const nlohmann::json& data, Level& outLevel, std::string* errorMessage = nullptr);

    // A .prefab is a Level holding exactly one entity (see Prefab.hpp).
    bool LoadPrefabFromFile(const std::filesystem::path& prefabFile, Level& outPrefab, std::string* errorMessage = nullptr);
    bool SavePrefabToFile(const std::filesystem::path& prefabFile, const Level& prefab, std::string* errorMessage = nullptr);

    // One wall / sector as level.bson stores it in its "walls" / "sectors"
    // array. FromJson returns false (outWall/outSector untouched) for data
    // that can't be loaded.
    nlohmann::json WallToJson(const Wall& wall);
    bool WallFromJson(const nlohmann::json& data, Wall& outWall);
    nlohmann::json SectorToJson(const Sector& sector);
    bool SectorFromJson(const nlohmann::json& data, Sector& outSector);

    // One component's own fields, keyed as level.bson stores them, without
    // "ownerID"/"instanceID". FromJson returns false for data that can't be
    // loaded (the component is then skipped).
#define TILKY_DECLARE_COMPONENT_JSON(Type, Bit, Storage, LabelKey)     nlohmann::json ComponentFieldsToJson(const Type& component);     bool ComponentFieldsFromJson(const nlohmann::json& data, Type& component);
    TILKY_COMPONENTS(TILKY_DECLARE_COMPONENT_JSON)
#undef TILKY_DECLARE_COMPONENT_JSON
}

#endif