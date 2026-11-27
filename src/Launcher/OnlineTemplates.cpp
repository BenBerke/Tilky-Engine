//
// Created by berke on 11/21/2026.
//
#include "Headers/Launcher/OnlineTemplates.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Launcher/ArchiveInstaller.hpp"
#include "Headers/Project/ProjectManager.hpp"

using json = nlohmann::json;

namespace OnlineTemplates {
    namespace {
        // ============================================================================
        // CONFIGURATION - where templates.json is hosted. The "Publish Templates"
        // workflow (.github/workflows/publish-templates.yml) keeps it up to date, so
        // a kit change only needs a push to main, never a launcher or engine release.
        // The format is described in OnlineTemplates.hpp.
        // ============================================================================
        constexpr const char* TEMPLATES_MANIFEST_URL =
            "https://raw.githubusercontent.com/BenBerke/Tilky-Engine/main/templates.json";

        // TILKY_TEMPLATES_MANIFEST_URL replaces it, to try a manifest before it's
        // published (or to test against a local server).
        std::string ManifestUrl() {
            const char* overrideUrl = std::getenv("TILKY_TEMPLATES_MANIFEST_URL");
            return overrideUrl != nullptr && overrideUrl[0] != '\0' ? std::string(overrideUrl) : std::string(TEMPLATES_MANIFEST_URL);
        }

        constexpr long CONNECT_TIMEOUT_MS = 8000;
        constexpr long MANIFEST_TIMEOUT_MS = 15000;
        constexpr long THUMBNAIL_TIMEOUT_MS = 30000;
        constexpr long DOWNLOAD_TIMEOUT_MS = 10L * 60L * 1000L;

        fs::path CachedManifestFile() { return GetCacheFolder() / "templates.json"; }

        // Ids and build versions become file names: only plain characters, so a
        // manifest entry can't point outside the cache folder.
        bool IsSafeName(const std::string& text) {
            if (text.empty() || text.size() > 100 || text == "." || text == "..") return false;
            return std::ranges::all_of(text, [](const char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            });
        }

        fs::path ZipFileFor(const std::string& id, const std::string& buildVersion) {
            return GetCacheFolder() / id / (buildVersion + ".zip");
        }

        fs::path ThumbnailFileFor(const std::string& id) { return GetCacheFolder() / id / "thumbnail.png"; }
        fs::path ThumbnailUrlFileFor(const std::string& id) { return GetCacheFolder() / id / "thumbnail.url"; }

        std::vector<long> ParseVersionParts(const std::string& version) {
            std::vector<long> parts;
            std::stringstream ss(version);
            std::string part;
            while (std::getline(ss, part, '.')) {
                try {
                    parts.push_back(std::stol(part)); // "1-beta1" reads as 1
                } catch (const std::exception&) {
                    parts.push_back(0);
                }
            }
            return parts;
        }

        // Compares the first `count` parts only (missing parts are 0). -1, 0 or 1.
        int CompareVersions(const std::vector<long>& a, const std::vector<long>& b, const std::size_t count) {
            for (std::size_t i = 0; i < count; ++i) {
                const long valueA = i < a.size() ? a[i] : 0;
                const long valueB = i < b.size() ? b[i] : 0;
                if (valueA != valueB) return valueA < valueB ? -1 : 1;
            }
            return 0;
        }

        std::vector<OnlineTemplate> ParseManifest(const std::string& text, std::string& outError) {
            std::vector<OnlineTemplate> result;

            json data;
            try {
                data = json::parse(text);
            } catch (const std::exception& e) {
                outError = std::string("could not parse templates.json: ") + e.what();
                return result;
            }

            if (!data.contains("templates") || !data["templates"].is_array()) {
                outError = "templates.json has no \"templates\" array";
                return result;
            }

            for (const json& entry : data["templates"]) {
                if (!entry.is_object()) continue;

                OnlineTemplate info;
                info.id = entry.value("id", std::string());
                info.name = entry.value("name", info.id);
                info.description = entry.value("description", std::string());
                info.thumbnailUrl = entry.value("thumbnailUrl", std::string());

                if (!IsSafeName(info.id)) {
                    spdlog::warn("Skipping templates.json entry with an invalid id '{}'", info.id);
                    continue;
                }

                if (entry.contains("builds") && entry["builds"].is_array()) {
                    for (const json& buildEntry : entry["builds"]) {
                        if (!buildEntry.is_object()) continue;

                        Build build;
                        build.version = buildEntry.value("version", std::string());
                        build.minEngineVersion = buildEntry.value("minEngineVersion", std::string());
                        build.maxEngineVersion = buildEntry.value("maxEngineVersion", std::string());
                        build.downloadUrl = buildEntry.value("downloadUrl", std::string());
                        build.sizeBytes = buildEntry.value("size", static_cast<std::uint64_t>(0));
                        build.sha256 = buildEntry.value("sha256", std::string());

                        if (!IsSafeName(build.version) || build.downloadUrl.empty()) {
                            spdlog::warn("Skipping a build of template '{}' with an invalid version or no downloadUrl", info.id);
                            continue;
                        }
                        info.builds.push_back(std::move(build));
                    }
                }

                if (info.builds.empty()) continue;
                result.push_back(std::move(info));
            }

            return result;
        }

        void SetUpSession(cpr::Session& session, const std::string& url, const long timeoutMs) {
            session.SetUrl(cpr::Url{url});
            session.SetTimeout(cpr::Timeout{std::chrono::milliseconds{timeoutMs}});
            session.SetConnectTimeout(cpr::ConnectTimeout{CONNECT_TIMEOUT_MS});
            session.SetRedirect(cpr::Redirect{true});
        }

        std::string ReadTextFile(const fs::path& file) {
            std::ifstream in(file, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        // Writes through a temporary file, so a crash can't leave half a file.
        bool WriteFileAtomically(const fs::path& file, const std::string& contents) {
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            const fs::path temp = file.string() + ".part";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out << contents;
                if (!out) return false;
            }
            fs::rename(temp, file, ec);
            if (ec) {
                fs::remove(temp, ec);
                return false;
            }
            return true;
        }

        // Fetches each template's thumbnail when it's missing or its URL changed.
        // Failures only log: a template without a thumbnail still works.
        void RefreshThumbnails(const std::vector<OnlineTemplate>& templates) {
            for (const OnlineTemplate& info : templates) {
                if (info.thumbnailUrl.empty()) continue;

                std::error_code ec;
                const fs::path thumbnail = ThumbnailFileFor(info.id);
                const fs::path urlFile = ThumbnailUrlFileFor(info.id);
                if (fs::exists(thumbnail, ec) && fs::exists(urlFile, ec) && ReadTextFile(urlFile) == info.thumbnailUrl) continue;

                cpr::Session session;
                SetUpSession(session, info.thumbnailUrl, THUMBNAIL_TIMEOUT_MS);
                const cpr::Response response = session.Get();
                if (response.error.code != cpr::ErrorCode::OK || response.status_code != 200 || response.text.empty()) {
                    spdlog::warn("Couldn't download the thumbnail of template {} from {}", info.id, info.thumbnailUrl);
                    continue;
                }

                if (!WriteFileAtomically(thumbnail, response.text) || !WriteFileAtomically(urlFile, info.thumbnailUrl))
                    spdlog::warn("Couldn't save the thumbnail of template {}", info.id);
            }
        }

        // Cached ZIPs of builds no longer in the manifest are dead weight (the
        // kit is ~25 MB). Builds still listed stay, for other engine versions.
        void RemoveStaleZips(const std::vector<OnlineTemplate>& templates) {
            std::error_code ec;
            for (const OnlineTemplate& info : templates) {
                const fs::path folder = GetCacheFolder() / info.id;
                if (!fs::is_directory(folder, ec)) continue;

                for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".zip") continue;
                    const std::string stem = entry.path().stem().string();
                    if (std::ranges::any_of(info.builds, [&](const Build& b) { return b.version == stem; })) continue;

                    std::error_code removeEc;
                    fs::remove(entry.path(), removeEc);
                    if (!removeEc) spdlog::info("Removed old template download {}", entry.path().string());
                }
            }
        }
    }

    fs::path GetCacheFolder() {
        return ProjectManager::GetEngineFolder() / "TemplateCache";
    }

    bool BuildFits(const Build& build, const std::string& engineVersion) {
        if (engineVersion.empty()) return true;

        const std::vector<long> version = ParseVersionParts(engineVersion);

        if (!build.minEngineVersion.empty()) {
            const std::vector<long> minimum = ParseVersionParts(build.minEngineVersion);
            if (CompareVersions(version, minimum, std::max(version.size(), minimum.size())) < 0) return false;
        }

        if (!build.maxEngineVersion.empty()) {
            // Only the parts the maximum names: "2.0" allows every 2.0.x.
            const std::vector<long> maximum = ParseVersionParts(build.maxEngineVersion);
            if (CompareVersions(version, maximum, maximum.size()) > 0) return false;
        }

        return true;
    }

    Manager& Manager::Instance() {
        static Manager instance;
        return instance;
    }

    Manager::~Manager() {
        Shutdown();
    }

    void Manager::Initialize() {
        std::error_code ec;
        const fs::path cache = GetCacheFolder();

        // A *.part file belongs to a download or write that never finished.
        if (fs::is_directory(cache, ec)) {
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cache, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".part") {
                    std::error_code removeEc;
                    fs::remove(entry.path(), removeEc);
                }
            }
        }

        if (fs::exists(CachedManifestFile(), ec)) {
            std::string error;
            std::vector<OnlineTemplate> parsed = ParseManifest(ReadTextFile(CachedManifestFile()), error);
            if (!error.empty()) {
                spdlog::warn("Ignoring the cached templates.json: {}", error);
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                templates_ = std::move(parsed);
                ++revision_;
            }
        }
    }

    void Manager::Shutdown() {
        std::vector<std::thread> threadsToJoin;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutDown_) return;
            shutDown_ = true;

            for (auto& [id, state] : downloads_) {
                state->cancelRequested = true;
                if (state->worker.joinable()) threadsToJoin.push_back(std::move(state->worker));
            }
        }

        // Joined outside the lock: the workers take it to report progress.
        for (auto& t : threadsToJoin) {
            if (t.joinable()) t.join();
        }
        if (manifestThread_.joinable()) manifestThread_.join();
    }

    void Manager::RefreshManifestAsync() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (manifestLoading_ || shutDown_) return;
            manifestLoading_ = true;
            manifestError_.reset();
        }

        if (manifestThread_.joinable()) manifestThread_.join();
        manifestThread_ = std::thread(&Manager::RunManifestFetch, this);
    }

    void Manager::RunManifestFetch() {
        const auto cacheBust = std::chrono::system_clock::now().time_since_epoch().count();
        cpr::Session session;
        SetUpSession(session, ManifestUrl() + "?nocache=" + std::to_string(cacheBust), MANIFEST_TIMEOUT_MS);
        session.SetHeader(cpr::Header{{"Cache-Control", "no-cache"}, {"Pragma", "no-cache"}});
        const cpr::Response response = session.Get();

        std::string error;
        std::vector<OnlineTemplate> parsed;

        if (response.error.code != cpr::ErrorCode::OK) {
            error = "could not reach " + ManifestUrl() + ": " + response.error.message;
        } else if (response.status_code != 200) {
            error = "templates.json request returned HTTP " + std::to_string(response.status_code);
        } else {
            parsed = ParseManifest(response.text, error);
        }

        if (!error.empty()) {
            spdlog::warn("Online template manifest fetch failed: {}", error);
            std::lock_guard<std::mutex> lock(mutex_);
            manifestLoading_ = false;
            manifestError_ = error;
            ++revision_;
            return;
        }

        if (!WriteFileAtomically(CachedManifestFile(), response.text))
            spdlog::warn("Couldn't cache templates.json in {}", GetCacheFolder().string());

        {
            // Published first so the tiles show up before the thumbnails arrive.
            std::lock_guard<std::mutex> lock(mutex_);
            templates_ = parsed;
            manifestIsFresh_ = true;
            ++revision_;
        }
        spdlog::info("Loaded online template manifest: {} template(s)", parsed.size());

        RefreshThumbnails(parsed);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // A download in flight could be writing a ZIP this would delete.
            if (downloads_.empty()) RemoveStaleZips(parsed);
            manifestLoading_ = false;
            ++revision_;
        }
    }

    bool Manager::IsManifestLoading() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestLoading_;
    }

    std::optional<std::string> Manager::GetManifestError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestError_;
    }

    std::uint64_t Manager::GetRevision() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return revision_;
    }

    std::vector<Offer> Manager::GetOffers(const std::string& engineVersion) const {
        std::vector<OnlineTemplate> templates;
        bool fresh;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            templates = templates_;
            fresh = manifestIsFresh_;
        }

        std::vector<Offer> offers;
        for (const OnlineTemplate& info : templates) {
            // The last build that fits is the newest; offline, the newest cached one.
            const Build* chosen = nullptr;
            for (const Build& build : info.builds) {
                if (!BuildFits(build, engineVersion)) continue;
                std::error_code ec;
                if (!fresh && !fs::exists(ZipFileFor(info.id, build.version), ec)) continue;
                chosen = &build;
            }
            if (chosen == nullptr) continue;

            Offer offer;
            offer.id = info.id;
            offer.name = info.name;
            offer.description = info.description;
            offer.build = *chosen;
            offer.zipFile = ZipFileFor(info.id, chosen->version);

            std::error_code ec;
            offer.cached = fs::exists(offer.zipFile, ec);
            if (fs::exists(ThumbnailFileFor(info.id), ec)) offer.thumbnailFile = ThumbnailFileFor(info.id);

            offers.push_back(std::move(offer));
        }
        return offers;
    }

    void Manager::Download(const Offer& offer) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutDown_) return;

        if (downloads_.contains(offer.id)) {
            spdlog::warn("Template {} download requested but one is already in progress", offer.id);
            return;
        }

        auto state = std::make_unique<DownloadState>();
        state->phase = DownloadPhase::Downloading;
        DownloadState* const statePtr = state.get();
        downloads_[offer.id] = std::move(state);
        statePtr->worker = std::thread(&Manager::RunDownload, this, offer);
    }

    void Manager::CancelDownload(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = downloads_.find(id);
        if (it != downloads_.end()) it->second->cancelRequested = true;
    }

    bool Manager::IsDownloading(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = downloads_.find(id);
        return it != downloads_.end() && it->second->phase != DownloadPhase::Complete && it->second->phase != DownloadPhase::Failed;
    }

    DownloadProgress Manager::GetDownloadProgress(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = downloads_.find(id);
        if (it == downloads_.end()) return DownloadProgress{};

        DownloadProgress progress;
        progress.phase = it->second->phase;
        progress.fraction = it->second->fraction;
        progress.errorMessage = it->second->errorMessage;
        return progress;
    }

    std::vector<DownloadOutcomeEvent> Manager::PollDownloadEvents() {
        std::vector<DownloadOutcomeEvent> events;
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = downloads_.begin(); it != downloads_.end();) {
            DownloadState& state = *it->second;
            if (state.phase == DownloadPhase::Complete || state.phase == DownloadPhase::Failed) {
                if (state.worker.joinable()) state.worker.join();

                DownloadOutcomeEvent event;
                event.id = it->first;
                event.success = state.phase == DownloadPhase::Complete;
                event.errorMessage = state.errorMessage;
                events.push_back(std::move(event));

                it = downloads_.erase(it);
            } else {
                ++it;
            }
        }
        return events;
    }

    void Manager::RunDownload(Offer offer) {
        const std::string id = offer.id;
        const fs::path partFile = offer.zipFile.string() + ".part";

        const auto finish = [this, &id](const DownloadPhase phase, const std::string& message) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = downloads_.find(id);
            if (it == downloads_.end()) return;
            it->second->phase = phase;
            it->second->errorMessage = message;
            if (phase == DownloadPhase::Complete) it->second->fraction = 1.0f;
            ++revision_;
        };

        const auto fail = [&](const std::string& message) {
            std::error_code ec;
            fs::remove(partFile, ec);
            spdlog::error("Template {} download failed: {}", id, message);
            finish(DownloadPhase::Failed, message);
        };

        const auto isCancelled = [this, &id] {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = downloads_.find(id);
            return it == downloads_.end() || it->second->cancelRequested.load();
        };

        try {
            fs::create_directories(offer.zipFile.parent_path());
        } catch (const std::exception& e) {
            fail(std::string("could not create the download folder: ") + e.what());
            return;
        }

        spdlog::info("Downloading template {} build {} from {}", id, offer.build.version, offer.build.downloadUrl);

        std::string error;
        const bool downloaded = ArchiveInstaller::Download(offer.build.downloadUrl, partFile, DOWNLOAD_TIMEOUT_MS,
            [this, &id](const float fraction) -> bool {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = downloads_.find(id);
                if (it == downloads_.end()) return false;
                it->second->fraction = fraction;
                return !it->second->cancelRequested.load();
            }, error);

        if (isCancelled()) { fail("download cancelled"); return; }
        if (!downloaded) { fail(error); return; }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = downloads_.find(id);
            if (it != downloads_.end()) it->second->phase = DownloadPhase::VerifyingHash;
        }
        if (!ArchiveInstaller::VerifySha256(partFile, offer.build.sha256, error)) { fail(error); return; }
        if (isCancelled()) { fail("download cancelled"); return; }

        std::error_code ec;
        fs::remove(offer.zipFile, ec);
        fs::rename(partFile, offer.zipFile, ec);
        if (ec) { fail("could not save the download: " + ec.message()); return; }

        spdlog::info("Template {} build {} downloaded to {}", id, offer.build.version, offer.zipFile.string());
        finish(DownloadPhase::Complete, std::string());
    }
}
