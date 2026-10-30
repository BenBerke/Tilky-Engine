#ifndef TILKY_ENGINE_SAVESYSTEM_HPP
#define TILKY_ENGINE_SAVESYSTEM_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The files behind Lua's Saving table. Each save file is a set of named
// values, kept in memory once read; whatever changed during a frame is
// written once at its end (Flush), so scripts never call a save function.
//
// On disk a file is {"format": 1, "values": {name: {"type": ..., "value": ...}}},
// as JSON in the editor (readable while testing) and BSON in the exported
// game. "type" is "number", "text", "bool" or "vector3"; a vector3's value
// is [x, y, z]. "format" says which layout the file uses, so a future
// layout can still read old saves.
namespace SaveSystem {
    inline constexpr int FORMAT_VERSION = 1;

    // The folder saves are in: the project's Saves folder in the editor,
    // the player's app-data folder in the exported game.
    [[nodiscard]] std::filesystem::path Folder();

    // Why `fileName` can't be a save file's name, or "" if it can. Names are
    // plain: no folders, no leading '.', nothing Windows can't store.
    [[nodiscard]] std::string FileNameProblem(const std::string& fileName);

    // The rest take a valid file name.

    // The stored {"type", "value"} of `name`, or nullptr if it isn't saved.
    [[nodiscard]] const nlohmann::json* Find(const std::string& fileName, const std::string& name);

    void Set(const std::string& fileName, const std::string& name, const std::string& type, nlohmann::json value);

    // False if there was nothing to delete.
    bool DeleteKey(const std::string& fileName, const std::string& name);
    bool DeleteSaveFile(const std::string& fileName);

    [[nodiscard]] bool FileExists(const std::string& fileName);

    // Every save file's name, sorted, as of this frame's changes.
    [[nodiscard]] std::vector<std::string> GetFiles();

    // Writes every file changed since the last Flush. End of every frame.
    void Flush();

    // Forgets every file read, so the next read comes from disk. At the
    // start and end of a play session, after a Flush.
    void Reset();

    // Project Settings > Clear Saves: deletes everything in the project's
    // Saves folder. Never while playing. Returns how many files went.
    int ClearProjectSaves();
}

#endif //TILKY_ENGINE_SAVESYSTEM_HPP
