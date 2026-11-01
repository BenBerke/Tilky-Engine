#ifndef TILKY_ENGINE_UILAYOUTS_HPP
#define TILKY_ENGINE_UILAYOUTS_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Headers/Objects/Level.hpp"

// UI layouts: UI entities kept outside any level, in .uilayout files under
// Assets, edited in the UI editor exactly like a level's UI. A level lists
// the layouts it shows (WorldSettings::uiLayouts) by layout ID, which is
// saved inside the file, so renaming or moving the file keeps every level
// pointing at it. Lua names a layout by its file name (UI.SetLayoutVisible).
//
// While playing, each listed layout's entities are copied into the level
// (Instantiate). A level switch keeps a layout's copy when the next level
// lists it too, so its state carries on (see LevelTransfer).
//
// A layout's events, dispatches and script fields can only point at its own
// entities: anything else means nothing in the levels it's used in.
namespace UILayouts {
    inline constexpr std::string_view kExtension = ".uilayout";

    struct Info {
        std::uint64_t id = 0;
        std::filesystem::path path; // absolute
        std::string name;           // the file name without extension
    };

    // Every layout file under Assets, sorted by name. Scanned on first use
    // and again after Rescan.
    const std::vector<Info>& List();
    void Rescan();

    // nullptr when no file has that ID / name.
    const Info* Find(std::uint64_t id);
    const Info* FindByName(const std::string& name);

    // The layout's entities: loaded from its file once, then kept, so the
    // editor's unsaved edits are what Play shows. nullptr when the file is
    // missing or broken (logged).
    Level* Get(std::uint64_t id);

    // The layout `level` is (a Level returned by Get), or 0.
    std::uint64_t IdOf(const Level* level);

    // A new empty layout file with a new ID. False with errorMessage set if
    // it can't be written.
    bool Create(const std::filesystem::path& absolutePath, std::uint64_t* outID = nullptr, std::string* errorMessage = nullptr);

    // Whether the kept copy differs from the file.
    bool IsDirty(std::uint64_t id);
    // Every loaded layout that is dirty.
    std::vector<std::uint64_t> DirtyLayouts();
    bool Save(std::uint64_t id, std::string* errorMessage = nullptr);

    // Drops one kept copy (the next Get reads the file), or all of them and
    // the file list (a different project).
    void Invalidate(std::uint64_t id);
    void Clear();

    // Copies the layout's entities into `level` as new entities, each marked
    // with Entity::layoutID. Returns their IDs (none if it can't be loaded).
    std::vector<ID> Instantiate(Level& level, std::uint64_t id);

    // Orders the level's UI components the way they draw: the listed layouts
    // bottom first, the level's own UI on top.
    void SortUIDrawOrder(Level& level);
}

#endif //TILKY_ENGINE_UILAYOUTS_HPP
