#ifndef TILKY_ENGINE_ARCHIVEINSTALLER_HPP
#define TILKY_ENGINE_ARCHIVEINSTALLER_HPP

#include <filesystem>
#include <functional>
#include <string>

namespace fs = std::filesystem;

// The download -> verify -> extract steps shared by everything the launcher
// installs from a GitHub release: engine versions (EngineVersionManager) and
// sample projects (SampleProjectManager). Every function is blocking and meant
// to run on that installer's worker thread. None of them touch the UI.
namespace ArchiveInstaller {
    // Called with 0..1 as the transfer progresses. Return false to abort it.
    using DownloadProgress = std::function<bool(float fraction)>;

    // Called with 0..1 after each extracted ZIP entry.
    using ExtractProgress = std::function<void(float fraction)>;

    // Downloads `url` into `destination` (truncating it), following redirects -
    // GitHub release assets redirect to blob storage. On failure returns false
    // and sets `error` to a short technical message ("download error: ...",
    // "download failed with HTTP status 404"). An abort requested through
    // `progress` also returns false; callers check their own cancel flag first.
    bool Download(const std::string& url, const fs::path& destination, long timeoutMs,
                  const DownloadProgress& progress, std::string& error);

    // Compares the file's SHA-256 with `expectedHex` (any case). An empty
    // `expectedHex` logs a warning and passes, so a manifest entry without a
    // hash still installs.
    bool VerifySha256(const fs::path& file, const std::string& expectedHex, std::string& error);

    // Extracts every entry of the ZIP at `zipFile` into `destinationDirectory`,
    // which must already exist.
    bool ExtractZip(const fs::path& zipFile, const fs::path& destinationDirectory,
                    const ExtractProgress& progress, std::string& error);
}

#endif //TILKY_ENGINE_ARCHIVEINSTALLER_HPP
