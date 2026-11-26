#ifndef TILKY_PROJECT_ASSETSETS_HPP
#define TILKY_PROJECT_ASSETSETS_HPP

// Which assets each level needs, Unity style.
//
// The exporter works out every level's set and writes it into Data.pak; the
// exported game then keeps in memory only the textures and sounds of the
// level it is in (plus Always Loaded ones), and loads anything else the
// moment it's used (a warning in the log). Editor Play keeps everything
// loaded but warns about the same uses, and remembers them so the next
// export puts them in the level's set (and lists them in its report).
//
// A level's set is found by following names: every string in the level
// file that is an asset's path (with or without its extension), then the
// same in each prefab, flipbook, UI layout and model it reaches, and in
// each Lua script every string literal that names an asset (the whole path
// from Assets or its end from a folder boundary, extension optional).
// A name a script builds at runtime can't be seen: that's what the
// warnings and Always Loaded are for.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace AssetSets {
    namespace fs = std::filesystem;

    // Set names in the pack.
    inline constexpr const char* LEVEL_SET_PREFIX = "level:"; // + the level's path in Assets
    inline constexpr const char* ALWAYS_SET = "always";

    // ---- Project settings (ProjectSettings.json, "assetLoading") ----

    struct Settings {
        // Files or folders in Assets (generic paths); a folder covers everything in it.
        std::vector<std::string> alwaysLoaded;
        bool stripUnusedOnExport = false;
        // Shown while an exported game loads a level (0 = none: the last frame stays).
        std::uint64_t loadingScreenLayout = 0;
        // A global event variable set to 0..100 while it loads (0 = none).
        std::uint32_t loadingProgressVariable = 0;
    };

    Settings& Current();
    bool LoadFromProjectSettings(const fs::path& settingsPath);
    void WriteToProjectSettings(nlohmann::json& settings);

    // Whether `assetPath` (in Assets) is Always Loaded: listed, or inside a listed folder.
    bool IsAlwaysLoaded(const std::string& assetPath);
    // Whether it is listed itself (the toggle's state; a parent may still cover it).
    bool IsListedAlwaysLoaded(const std::string& assetPath);
    void SetAlwaysLoaded(const std::string& assetPath, bool alwaysLoaded);

    // ---- Working the sets out (the exporter, and editor Play) ----

    class Collector {
    public:
        // `assetsRoot` holds the sources (.lua scripts, not bytecode).
        explicit Collector(const fs::path& assetsRoot);
        ~Collector();

        // Every asset `levelFile` reaches (paths in Assets), the level itself not included.
        std::set<std::string> ForLevel(const fs::path& levelFile);

        // Always Loaded, expanded to files, plus what they and the project
        // settings reach.
        std::set<std::string> Always(const fs::path& projectSettingsFile);

        // Script string literals that look like asset paths (a folder or an
        // asset extension) but name no asset. "script: literal" each.
        const std::set<std::string>& Unresolved() const;

        // Every file under Assets (generic paths), for Strip.
        const std::vector<std::string>& AllFiles() const;

    private:
        struct State;
        std::unique_ptr<State> state;
    };

    // A level file's key: its path in Assets ("Levels/E1M1.bson").
    std::string LevelKey(const fs::path& levelFile);

    // Image or sound: the kinds an exported game keeps per level.
    bool IsImageFile(const std::string& assetPath);
    bool IsSoundFile(const std::string& assetPath);

    // ---- The running game ----

    // Whether the mounted pack carries sets (an older export doesn't: then
    // everything is loaded, as before).
    bool HasPackedSets();

    // The pack's set for the level plus the Always set. Empty when no sets.
    std::set<std::string> PackedNeeds(const std::string& levelKey);

    // ---- Uses outside the current level's set ----

    // The level now playing. Exported: from the pack's sets. Editor Play:
    // worked out from the saved files (`simulate`).
    void BeginLevel(const std::string& levelKey, bool simulate);
    void EndSession();

    // Whether `assetPath` is in the current level's set (always true when
    // no set is known, e.g. a project that was never exported).
    bool InCurrentSet(const std::string& assetPath);
    bool HasCurrentSet();
    // Moves on with every BeginLevel / EndSession.
    std::uint64_t SessionGeneration();

    // Logs a warning the first time an asset outside the set is used, and
    // remembers it (editor: in <Project>/.tilky/OnDemandAssets.json).
    void NoteOnDemandUse(const std::string& assetPath, const char* kind);

    // What editor Play recorded: level key -> assets used on demand there.
    std::vector<std::pair<std::string, std::set<std::string>>> ReadOnDemandLog(const fs::path& projectFolder);
    fs::path OnDemandLogPath(const fs::path& projectFolder);
}

#endif // TILKY_PROJECT_ASSETSETS_HPP
