#ifndef TILKY_ENGINE_SAMPLEPROJECTMANAGER_HPP
#define TILKY_ENGINE_SAMPLEPROJECTMANAGER_HPP

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Headers/Launcher/EngineVersionManager.hpp"

namespace fs = std::filesystem;

// Downloadable sample projects (for now the Tilky Showcase), listed in a small
// samples.json manifest hosted next to the samples themselves - see
// SAMPLES_MANIFEST_URL in SampleProjectManager.cpp for the format.
//
// Installing one downloads its release ZIP, checks the SHA-256, extracts it
// into a temporary folder and only then moves the project folder into the
// default projects folder (Documents/Tilky Engine/Projects). If a folder with
// that name already exists the sample is installed next to it as
// "<name> (2)", "<name> (3)", ... - an existing project is never replaced.
//
// Same threading model as EngineVersionManager: every public function is safe
// to call from the UI thread, work runs on one worker thread per install, and
// PollEvents() hands finished installs back to the UI once per frame.
namespace SampleProjectManager {
    // Progress reporting reuses the engine installer's phases and struct.
    using InstallPhase = EngineVersionManager::InstallPhase;
    using InstallProgress = EngineVersionManager::InstallProgress;

    struct SampleInfo {
        std::string id;              // stable key, e.g. "tilky-showcase"
        std::string name;            // shown in the launcher
        std::string version;
        std::string description;
        std::string projectFolder;   // top-level folder inside the ZIP that holds project.tilky
        std::string downloadUrl;
        std::uint64_t sizeBytes = 0;
        std::string sha256;
        std::string minEngineVersion; // empty if the manifest doesn't say

        // Filled in by GetSamples(): a folder called projectFolder already
        // exists in the default projects folder.
        bool installed = false;
    };

    struct InstallOutcomeEvent {
        std::string id;
        std::string name;
        bool success = false;
        std::string errorMessage;  // empty when success is true
        fs::path installedFolder;  // the new project's folder when success is true
    };

    class SampleProjectManager {
    public:
        static SampleProjectManager& Instance();

        SampleProjectManager(const SampleProjectManager&) = delete;
        SampleProjectManager& operator=(const SampleProjectManager&) = delete;

        // Sweeps leftovers from an interrupted install. The manifest is only
        // fetched when RefreshManifestAsync() is first called (the launcher does
        // that when its Sample Projects window opens).
        void Initialize();
        void Shutdown();

        std::vector<InstallOutcomeEvent> PollEvents();

        void RefreshManifestAsync();
        bool HasFetchedManifest() const;
        bool IsManifestLoading() const;
        std::optional<std::string> GetManifestError() const;

        std::vector<SampleInfo> GetSamples() const;

        void Install(const std::string& id);
        void CancelInstall(const std::string& id);
        bool IsInstalling(const std::string& id) const;
        InstallProgress GetInstallProgress(const std::string& id) const;

    private:
        SampleProjectManager() = default;
        ~SampleProjectManager();

        struct InstallState {
            InstallPhase phase = InstallPhase::NotInstalling;
            float fraction = 0.0f;
            std::string errorMessage;
            fs::path installedFolder;
            std::atomic_bool cancelRequested{false};
            std::thread worker;
        };

        void RunInstall(SampleInfo info);
        void RunManifestFetch();

        mutable std::mutex mutex_;
        std::vector<SampleInfo> samples_;
        bool manifestFetched_ = false;
        bool manifestLoading_ = false;
        std::optional<std::string> manifestError_;
        std::thread manifestThread_;
        std::map<std::string, std::unique_ptr<InstallState>> installs_;
        bool shutDown_ = false;
    };
}

#endif //TILKY_ENGINE_SAMPLEPROJECTMANAGER_HPP
