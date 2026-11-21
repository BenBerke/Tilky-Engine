#ifndef TILKY_ENGINE_ONLINETEMPLATES_HPP
#define TILKY_ENGINE_ONLINETEMPLATES_HPP

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

// Project templates downloaded from the internet, so a kit can change without a
// new launcher or engine release. ProjectTemplates::DiscoverTemplates() merges
// these with the templates bundled in the engine install: for the same id, an
// online build that fits the chosen engine version wins, and the bundled one is
// the fallback when there's no internet and nothing is cached.
//
// templates.json lives at the root of the engine repository, next to
// versions.json. The "Publish Templates" GitHub workflow writes it:
//
//   {
//     "manifestVersion": 1,
//     "templates": [{
//       "id": "fps-kit", "name": "Classic FPS Kit", "description": "...",
//       "thumbnailUrl": "https://raw.githubusercontent.com/.../thumbnail.png",
//       "builds": [{
//         "version": "3f9c2a1b7d0e",        // any text; names the cached ZIP
//         "minEngineVersion": "2.0.1",      // empty = any engine version
//         "maxEngineVersion": "",           // empty = no limit; "2.0" = any 2.0.x
//         "downloadUrl": "https://github.com/.../fps-kit-3f9c2a1b7d0e.zip",
//         "size": 24000000, "sha256": "..."
//       }]
//     }]
//   }
//
// A build fits an engine version when min <= version <= max. Of the builds
// that fit, the LAST one in the list is used, so newer builds are appended.
// The ZIP has the same layout as a bundled template: project.tilky + Assets/
// at its root.
//
// Everything is cached in <engine folder>/TemplateCache: the last manifest that
// downloaded (so cached templates still show up offline), each template's
// thumbnail, and each downloaded build's ZIP. A ZIP is only downloaded when a
// project is created from it, and kept for next time.
//
// Same threading model as SampleProjectManager: every public function is safe
// to call from the UI thread, and PollDownloadEvents() hands finished
// downloads back once per frame.
namespace OnlineTemplates {
    using DownloadPhase = EngineVersionManager::InstallPhase;
    using DownloadProgress = EngineVersionManager::InstallProgress;

    struct Build {
        std::string version;
        std::string minEngineVersion;
        std::string maxEngineVersion;
        std::string downloadUrl;
        std::uint64_t sizeBytes = 0;
        std::string sha256;
    };

    struct OnlineTemplate {
        std::string id;
        std::string name;
        std::string description;
        std::string thumbnailUrl;
        std::vector<Build> builds;
    };

    // What DiscoverTemplates needs for one template and one engine version.
    struct Offer {
        std::string id;
        std::string name;
        std::string description;
        fs::path thumbnailFile;  // empty if not downloaded (yet)
        Build build;
        fs::path zipFile;        // where the build's ZIP is (or will be) cached
        bool cached = false;     // zipFile exists, no download needed
    };

    struct DownloadOutcomeEvent {
        std::string id;
        bool success = false;
        std::string errorMessage;
    };

    // Exposed for the tests: does `engineVersion` fall within the build's range?
    // An empty engineVersion (unpinned project) fits every build.
    bool BuildFits(const Build& build, const std::string& engineVersion);

    fs::path GetCacheFolder();

    class Manager {
    public:
        static Manager& Instance();

        Manager(const Manager&) = delete;
        Manager& operator=(const Manager&) = delete;

        // Removes half-finished downloads and loads the cached manifest, so
        // templates downloaded before show up before (or without) a fetch.
        void Initialize();
        void Shutdown();

        void RefreshManifestAsync();
        bool IsManifestLoading() const;
        std::optional<std::string> GetManifestError() const;
        // Goes up whenever the offers may have changed (manifest or thumbnail
        // arrived, a download finished), so the UI knows to discover again.
        std::uint64_t GetRevision() const;

        // One offer per template that has a build fitting engineVersion. When
        // the newest manifest couldn't be fetched, only cached builds are
        // offered: a download would fail anyway.
        std::vector<Offer> GetOffers(const std::string& engineVersion) const;

        // Downloads the offer's build into its zipFile (verifying the SHA-256).
        void Download(const Offer& offer);
        void CancelDownload(const std::string& id);
        bool IsDownloading(const std::string& id) const;
        DownloadProgress GetDownloadProgress(const std::string& id) const;
        std::vector<DownloadOutcomeEvent> PollDownloadEvents();

    private:
        Manager() = default;
        ~Manager();

        struct DownloadState {
            DownloadPhase phase = DownloadPhase::NotInstalling;
            float fraction = 0.0f;
            std::string errorMessage;
            std::atomic_bool cancelRequested{false};
            std::thread worker;
        };

        void RunManifestFetch();
        void RunDownload(Offer offer);

        mutable std::mutex mutex_;
        std::vector<OnlineTemplate> templates_;
        bool manifestIsFresh_ = false; // templates_ came from the internet this session
        bool manifestLoading_ = false;
        std::optional<std::string> manifestError_;
        std::uint64_t revision_ = 0;
        std::thread manifestThread_;
        std::map<std::string, std::unique_ptr<DownloadState>> downloads_;
        bool shutDown_ = false;
    };
}

#endif //TILKY_ENGINE_ONLINETEMPLATES_HPP
