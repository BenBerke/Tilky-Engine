//
// Created by berke on 8/2/2026.
//
#pragma once

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

namespace fs = std::filesystem;

// Installs, removes, and launches versioned copies of the engine (Tilky_Engine +
// Standalone + runtime assets) fetched from GitHub Releases, so a project can be
// opened with the exact engine version it was authored against without the user
// ever leaving the launcher.
//
// Versions live side by side under ProjectManager::GetEngineVersionsFolder(), one
// directory per version - see ProjectManager.hpp for the exact layout. This class
// only manages *which* versions are present on disk and reports install progress;
// ProjectManager owns the actual path conventions and process launching so there is
// a single source of truth both this class and ProjectManager::OpenProject() read
// from.
//
// Thread-safety: every public member function is safe to call from the UI thread.
// Downloads and ZIP extraction run on a background worker thread per in-progress
// install; PollEvents() drains their results into UI-visible state once per frame
// and never blocks.
namespace EngineVersionManager {

    enum class InstallPhase {
        NotInstalling,
        Downloading,
        VerifyingHash,
        Extracting,
        Validating,
        Complete,
        Failed
    };

    struct InstallProgress {
        InstallPhase phase = InstallPhase::NotInstalling;

        // 0..1. Meaningful during Downloading (bytes so far / total) and Extracting
        // (entries extracted so far / total entries). Left at 0 for other phases.
        float fraction = 0.0f;

        // Plain technical detail (e.g. "HTTP 404", "hash mismatch: expected <a>, got
        // <b>"), only set when phase == Failed. Not localised, same spirit as the
        // rest of this codebase's spdlog messages - the launcher UI should pair it
        // with a localised headline for the phase that failed.
        std::string errorMessage;
    };

    // One entry from versions.json, filtered to a single platform.
    struct VersionInfo {
        std::string version;
        std::string platform;
        std::string channel;            // "stable" / "development" / whatever the manifest uses
        std::string downloadUrl;
        std::uint64_t sizeBytes = 0;
        std::string sha256;
        std::string minLauncherVersion; // empty if the manifest entry omitted it

        // True if EngineVersions/<version>/ already has a valid install. Filled in by
        // GetAvailableVersions() - not part of the raw manifest entry itself.
        bool installed = false;
    };

    struct RemovalCheck {
        // False only when the version's engine executable currently looks like it's
        // running (see the implementation note on IsEngineExecutableInUse). This is
        // the one hard rule from the spec ("do not remove while running"); whether
        // other projects still reference the version is informational only - the
        // launcher UI already has the project list in memory to build that warning
        // itself (see DrawEngineVersionsModal in LauncherApp.cpp) and shows it in the
        // remove-confirmation dialog regardless of `allowed`.
        bool allowed = true;
        bool processRunning = false;
    };

    // One-shot notification that an install reached a terminal state, surfaced by
    // PollEvents() exactly once for each install (whichever way it ends). After this
    // fires, that version drops out of IsInstalling()/GetInstallProgress() and simply
    // reports through IsVersionInstalled() like any other version - LauncherApp.cpp
    // uses this to push a single toast instead of re-detecting "did this just finish"
    // every frame.
    struct InstallOutcomeEvent {
        std::string version;
        bool success = false;
        std::string errorMessage; // empty when success is true
    };

    class EngineVersionManager {
    public:
        static EngineVersionManager& Instance();

        EngineVersionManager(const EngineVersionManager&) = delete;
        EngineVersionManager& operator=(const EngineVersionManager&) = delete;

        // Scans EngineVersions/ on disk (cheap: one directory listing + one exe-exists
        // check per entry) and kicks off an async manifest refresh. Call once, after
        // ProjectManager's paths are usable, before anything else here.
        void Initialize();

        // Requests cancellation of any in-flight download/install, then joins every
        // worker thread. Safe to call even if nothing is running or Initialize() was
        // never called. Blocks briefly (bounded by the network timeouts in the .cpp)
        // - call this after the launcher's own render loop has already stopped, same
        // as the rest of LauncherApp::Destroy().
        void Shutdown();

        // Drains everything the worker threads have produced since the last call and
        // returns any installs that reached a terminal state (see InstallOutcomeEvent)
        // so the caller can react once (typically: push a toast). Call once per frame
        // from the UI thread; never blocks on network I/O.
        std::vector<InstallOutcomeEvent> PollEvents();

        void RefreshManifestAsync();
        bool IsManifestLoading() const;
        std::optional<std::string> GetManifestError() const;

        // Versions from the manifest that target the running platform, plus any
        // locally installed version the manifest no longer lists (so it stays visible
        // and removable). Empty until the first successful manifest fetch completes,
        // except for the "installed but not in the manifest" entries.
        std::vector<VersionInfo> GetAvailableVersions() const;

        bool IsVersionInstalled(const std::string& version) const;

        // Newest installed version on the "stable" channel by loose semantic-version
        // comparison, or std::nullopt if nothing stable is installed. Used to pick a
        // default engineVersion for brand-new projects.
        std::optional<std::string> GetLatestInstalledStableVersion() const;

        // No-op (with a log line) if `version` is already installed, already
        // installing, or not present in the last-fetched manifest for this platform.
        void InstallVersion(const std::string& version);
        void CancelInstall(const std::string& version);
        bool IsInstalling(const std::string& version) const;
        InstallProgress GetInstallProgress(const std::string& version) const;

        // Checks whether removal is currently allowed (see RemovalCheck above) - does
        // NOT itself delete anything.
        RemovalCheck CheckVersionRemoval(const std::string& version) const;

        // Deletes EngineVersions/<version>/. Refuses (returns false, logs why) if
        // CheckVersionRemoval() would disallow it or an install for this version is
        // in progress.
        bool RemoveVersion(const std::string& version);

    private:
        EngineVersionManager() = default;
        ~EngineVersionManager();

        struct InstallState {
            InstallPhase phase = InstallPhase::NotInstalling;
            float fraction = 0.0f;
            std::string errorMessage;
            std::atomic_bool cancelRequested{false};
            std::thread worker;
        };

        void RunInstall(std::string version, VersionInfo info);
        void RunManifestFetch();
        void CleanupTempInstallArtifacts(const std::string& version) const;
        void ScanInstalledVersionsLocked();

        mutable std::mutex mutex_;

        std::vector<VersionInfo> manifestVersions_;
        bool manifestLoading_ = false;
        std::optional<std::string> manifestError_;
        std::thread manifestThread_;

        std::vector<std::string> installedVersionsCache_;

        std::map<std::string, std::unique_ptr<InstallState>> installs_;

        bool shutDown_ = false;
    };
}