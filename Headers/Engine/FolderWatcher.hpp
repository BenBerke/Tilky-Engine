#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>

// Counts changes under a folder, so work derived from its contents (the
// renderer's scan of every image in Assets) can be skipped while nothing
// changed. Counts files with one of `extensions` being added, removed,
// renamed or written, and folders being added, removed or renamed. Other
// files are ignored - saving a level into Assets is not an image change.
class FolderWatcher {
public:
    // Lower case, with the dot: ".png".
    explicit FolderWatcher(std::set<std::string> extensions);
    ~FolderWatcher();

    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    // A number that changes whenever a counted change happens under
    // `folder`. Asking about a different folder than last time starts
    // watching it and returns a new number. Where the folder can't be
    // watched (or off Windows) every call returns a new number, so callers
    // fall back to redoing their work each time.
    [[nodiscard]] std::uint64_t Generation(const std::filesystem::path& folder);

    void Stop();

private:
    struct Win32State; // handles and the notification buffer (FolderWatcher.cpp)

    void Start(const std::filesystem::path& folder);
    void Watch();
    [[nodiscard]] bool IsCounted(const std::filesystem::path& relative, unsigned long action) const;

    std::set<std::string> extensions;
    std::filesystem::path watchedFolder;

    std::atomic<std::uint64_t> generation{1};
    std::atomic<bool> watching{false};
    std::thread thread;

    std::unique_ptr<Win32State> win32;
};
