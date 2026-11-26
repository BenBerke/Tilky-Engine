#ifndef TILKY_ENGINE_ASSETFS_HPP
#define TILKY_ENGINE_ASSETFS_HPP

// Where the game's files come from.
//
// The editor (and a runtime that was never exported) reads them from disk
// exactly as before. An exported game reads them from its encrypted
// Data.pak instead: the exporter patches a key into the copied runtime
// executable, and a runtime with a key mounts the pack at its folder, so
// `<exe folder>/Assets/Textures/Wall.png` is that file inside the pack.
// Paths under that folder whose first part isn't in the pack (Saves...)
// still go to disk; paths that are, never do ("pack only").
//
// Data.pak: a plain header, then every file encrypted with ChaCha20 (its own
// nonce each, so any range decrypts on its own), then the encrypted index.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AssetFS {
    namespace fs = std::filesystem;

    using Key = std::array<std::uint8_t, 32>;

    inline constexpr const char* PACK_FILE_NAME = "Data.pak";

    // ---- The running game ----

    enum class MountResult {
        NotExported, // no key in this executable: read from disk
        Mounted,
        Failed,      // a key, but Data.pak is missing or doesn't open with it
    };

    // `root` is the folder the pack stands for (the executable's data folder).
    MountResult MountExportedPack(const fs::path& root, std::string* error = nullptr);
    bool IsPacked();

    // Files and folders, from the pack for a path it covers, else from disk.
    bool Exists(const fs::path& path);
    bool IsFile(const fs::path& path);
    bool IsDirectory(const fs::path& path);
    bool ReadFile(const fs::path& path, std::string& out);
    bool ReadFile(const fs::path& path, std::vector<unsigned char>& out);

    // A file's last write time; false if there's no such file. Packed files
    // never change, so they all report the same time.
    bool WriteTime(const fs::path& path, fs::file_time_type& out);

    // Whether the pack answers for `path` (always false while not packed).
    bool CoversPath(const fs::path& path);

    // Packed only: every file under `folder` (full paths, the pack's order),
    // recursively or only its direct files.
    std::vector<fs::path> ListPackedFiles(const fs::path& folder, bool recursive);

    // ---- Per-level file sets (the pack's index carries them) ----

    struct FileSet {
        std::string name;               // e.g. "level:<path in Assets>" (see AssetSets)
        std::vector<std::string> files; // names as the set's user keys them
    };

    // The sets the mounted pack holds (empty while not packed).
    const std::vector<FileSet>& PackedSets();

    // ---- Writing a pack (the exporter) ----

    Key NewKey();

    // Packs every file under `sourceRoot` into `pakPath`.
    bool WritePack(const fs::path& sourceRoot, const fs::path& pakPath, const Key& key, const std::vector<FileSet>& sets,
                   std::string& error);

    // Writes `key` into the key slot of a copied runtime executable. The
    // slot's file offset and size go to `slotOffset`/`slotSize` (a signed
    // macOS binary needs those pages' hashes redone).
    bool PatchKeyIntoExecutable(const fs::path& executable, const Key& key, std::string& error, std::size_t* slotOffset = nullptr,
                                std::size_t* slotSize = nullptr);
}

#endif // TILKY_ENGINE_ASSETFS_HPP
