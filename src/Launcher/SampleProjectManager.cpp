//
// Created by berke on 10/10/2026.
//
#include "Headers/Launcher/SampleProjectManager.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Launcher/ArchiveInstaller.hpp"
#include "Headers/Project/ProjectManager.hpp"

using json = nlohmann::json;

namespace SampleProjectManager {
    namespace {
        // ============================================================================
        // CONFIGURATION - where samples.json is hosted. It lives in the Tilky-Showcase
        // repository so a new showcase release only has to update that repository,
        // never the launcher. Format:
        //
        //   {
        //     "manifestVersion": 1,
        //     "samples": [{
        //       "id": "tilky-showcase", "name": "Tilky Showcase", "version": "1.0.0",
        //       "description": "...", "projectFolder": "Tilky Showcase",
        //       "downloadUrl": "https://github.com/.../Tilky_Showcase-1.0.0.zip",
        //       "size": 2884160, "sha256": "...", "minEngineVersion": ""
        //     }]
        //   }
        // ============================================================================
        constexpr const char* SAMPLES_MANIFEST_URL =
            "https://raw.githubusercontent.com/BenBerke/Tilky-Showcase/main/samples.json";

        constexpr long CONNECT_TIMEOUT_MS = 8000;
        constexpr long MANIFEST_TIMEOUT_MS = 15000;
        constexpr long DOWNLOAD_TIMEOUT_MS = 10L * 60L * 1000L;

        // Downloads and half-extracted samples go here, never straight into the
        // projects folder, so an interrupted install can't show up as a broken project.
        fs::path WorkFolder() {
            return ProjectManager::GetEngineFolder() / "SampleDownloads";
        }

        std::vector<SampleInfo> ParseManifest(const std::string& manifestJson, std::string& outError) {
            std::vector<SampleInfo> result;

            json data;
            try {
                data = json::parse(manifestJson);
            } catch (const std::exception& e) {
                outError = std::string("could not parse samples.json: ") + e.what();
                return result;
            }

            if (!data.contains("samples") || !data["samples"].is_array()) {
                outError = "samples.json has no \"samples\" array";
                return result;
            }

            for (const auto& entry : data["samples"]) {
                SampleInfo info;
                info.id = entry.value("id", std::string());
                info.name = entry.value("name", info.id);
                info.version = entry.value("version", std::string());
                info.description = entry.value("description", std::string());
                info.projectFolder = entry.value("projectFolder", info.name);
                info.downloadUrl = entry.value("downloadUrl", std::string());
                info.sizeBytes = entry.value("size", static_cast<std::uint64_t>(0));
                info.sha256 = entry.value("sha256", std::string());
                info.minEngineVersion = entry.value("minEngineVersion", std::string());

                // projectFolder becomes a folder name under Projects: refuse anything
                // that could point somewhere else.
                const fs::path folder(info.projectFolder);
                const bool safeFolder = !info.projectFolder.empty() && folder.filename() == folder
                                        && info.projectFolder != "." && info.projectFolder != "..";

                if (info.id.empty() || info.downloadUrl.empty() || !safeFolder) {
                    spdlog::warn("Skipping samples.json entry '{}' with a missing id/downloadUrl or an invalid projectFolder", info.id);
                    continue;
                }

                result.push_back(std::move(info));
            }

            return result;
        }

        // "<base>", or "<base> (2)", "<base> (3)", ... - the first name not taken
        // in `parent`.
        fs::path FirstFreeFolder(const fs::path& parent, const std::string& base) {
            std::error_code ec;
            fs::path candidate = parent / base;
            for (int n = 2; fs::exists(candidate, ec); ++n) {
                candidate = parent / (base + " (" + std::to_string(n) + ")");
            }
            return candidate;
        }

        void RemoveWorkFiles(const std::string& id) {
            std::error_code ec;
            fs::remove(WorkFolder() / (id + ".zip.part"), ec);
            fs::remove_all(WorkFolder() / (id + "-installing"), ec);
        }
    }

    SampleProjectManager& SampleProjectManager::Instance() {
        static SampleProjectManager instance;
        return instance;
    }

    SampleProjectManager::~SampleProjectManager() {
        Shutdown();
    }

    void SampleProjectManager::Initialize() {
        // Anything still in the work folder belongs to an install that never
        // finished (a finished or failed one cleans up after itself).
        std::error_code ec;
        if (fs::exists(WorkFolder(), ec)) {
            fs::remove_all(WorkFolder(), ec);
            if (ec) spdlog::warn("Couldn't clear leftover sample downloads in {}: {}", WorkFolder().string(), ec.message());
        }
    }

    void SampleProjectManager::Shutdown() {
        std::vector<std::thread> threadsToJoin;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutDown_) return;
            shutDown_ = true;

            for (auto& [id, state] : installs_) {
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

    std::vector<InstallOutcomeEvent> SampleProjectManager::PollEvents() {
        std::vector<InstallOutcomeEvent> events;

        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = installs_.begin(); it != installs_.end();) {
            InstallState& state = *it->second;

            if (state.phase == InstallPhase::Complete || state.phase == InstallPhase::Failed) {
                if (state.worker.joinable()) state.worker.join();

                InstallOutcomeEvent event;
                event.id = it->first;
                const auto sample = std::ranges::find(samples_, it->first, &SampleInfo::id);
                event.name = sample != samples_.end() ? sample->name : it->first;
                event.success = state.phase == InstallPhase::Complete;
                event.errorMessage = state.errorMessage;
                event.installedFolder = state.installedFolder;
                events.push_back(std::move(event));

                it = installs_.erase(it);
            } else {
                ++it;
            }
        }

        return events;
    }

    void SampleProjectManager::RefreshManifestAsync() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (manifestLoading_ || shutDown_) return;
            manifestLoading_ = true;
            manifestError_.reset();
        }

        if (manifestThread_.joinable()) manifestThread_.join();
        manifestThread_ = std::thread(&SampleProjectManager::RunManifestFetch, this);
    }

    void SampleProjectManager::RunManifestFetch() {
        cpr::Session session;
        const auto cacheBust = std::chrono::system_clock::now().time_since_epoch().count();

        session.SetUrl(cpr::Url{std::string(SAMPLES_MANIFEST_URL) + "?nocache=" + std::to_string(cacheBust)});
        session.SetHeader(cpr::Header{{"Cache-Control", "no-cache"}, {"Pragma", "no-cache"}});
        session.SetTimeout(cpr::Timeout{MANIFEST_TIMEOUT_MS});
        session.SetConnectTimeout(cpr::ConnectTimeout{CONNECT_TIMEOUT_MS});
        session.SetRedirect(cpr::Redirect{true});

        const cpr::Response response = session.Get();

        std::lock_guard<std::mutex> lock(mutex_);
        manifestLoading_ = false;
        manifestFetched_ = true;

        if (response.error.code != cpr::ErrorCode::OK) {
            manifestError_ = "could not reach " + std::string(SAMPLES_MANIFEST_URL) + ": " + response.error.message;
            spdlog::error("Sample project manifest fetch failed: {}", *manifestError_);
            return;
        }

        if (response.status_code != 200) {
            manifestError_ = "samples.json request returned HTTP " + std::to_string(response.status_code);
            spdlog::error("Sample project manifest fetch failed: {}", *manifestError_);
            return;
        }

        std::string parseError;
        std::vector<SampleInfo> parsed = ParseManifest(response.text, parseError);

        if (!parseError.empty()) {
            manifestError_ = parseError;
            spdlog::error("Sample project manifest fetch failed: {}", *manifestError_);
            return;
        }

        samples_ = std::move(parsed);
        spdlog::info("Loaded sample project manifest: {} sample(s)", samples_.size());
    }

    bool SampleProjectManager::HasFetchedManifest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestFetched_ || manifestLoading_;
    }

    bool SampleProjectManager::IsManifestLoading() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestLoading_;
    }

    std::optional<std::string> SampleProjectManager::GetManifestError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestError_;
    }

    std::vector<SampleInfo> SampleProjectManager::GetSamples() const {
        std::vector<SampleInfo> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = samples_;
        }

        const fs::path projectsRoot = ProjectManager::GetDefaultProjectsFolder();
        for (SampleInfo& info : result) {
            std::error_code ec;
            info.installed = fs::exists(projectsRoot / info.projectFolder, ec);
        }

        return result;
    }

    void SampleProjectManager::Install(const std::string& id) {
        SampleInfo info;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutDown_) return;

            if (installs_.contains(id)) {
                spdlog::warn("Sample project {} install requested but one is already in progress", id);
                return;
            }

            const auto it = std::ranges::find(samples_, id, &SampleInfo::id);
            if (it == samples_.end()) {
                spdlog::error("Sample project {} is not in the manifest - cannot install", id);
                return;
            }
            info = *it;

            auto state = std::make_unique<InstallState>();
            state->phase = InstallPhase::Downloading;
            InstallState* const statePtr = state.get();
            installs_[id] = std::move(state);

            statePtr->worker = std::thread(&SampleProjectManager::RunInstall, this, info);
        }
    }

    void SampleProjectManager::CancelInstall(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = installs_.find(id);
        if (it != installs_.end()) it->second->cancelRequested = true;
    }

    bool SampleProjectManager::IsInstalling(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return installs_.contains(id);
    }

    InstallProgress SampleProjectManager::GetInstallProgress(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = installs_.find(id);
        if (it == installs_.end()) return InstallProgress{};

        InstallProgress progress;
        progress.phase = it->second->phase;
        progress.fraction = it->second->fraction;
        progress.errorMessage = it->second->errorMessage;
        return progress;
    }

    void SampleProjectManager::RunInstall(SampleInfo info) {
        const std::string id = info.id;
        const fs::path partFile = WorkFolder() / (id + ".zip.part");
        const fs::path installingDir = WorkFolder() / (id + "-installing");

        const auto setProgress = [this, &id](const InstallPhase phase, const float fraction) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(id);
            if (it == installs_.end()) return;
            it->second->phase = phase;
            it->second->fraction = fraction;
        };

        const auto setFraction = [this, &id](const float fraction) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(id);
            if (it != installs_.end()) it->second->fraction = fraction;
        };

        const auto isCancelled = [this, &id]() {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(id);
            return it == installs_.end() || it->second->cancelRequested.load();
        };

        // Every failure path cleans the work files up, then reports.
        const auto fail = [this, &id](const std::string& message) {
            RemoveWorkFiles(id);
            spdlog::error("Sample project {} install failed: {}", id, message);
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(id);
            if (it == installs_.end()) return;
            it->second->phase = InstallPhase::Failed;
            it->second->errorMessage = message;
        };

        try {
            fs::create_directories(WorkFolder());
        } catch (const std::exception& e) {
            fail(std::string("could not create the download folder: ") + e.what());
            return;
        }

        RemoveWorkFiles(id); // anything left over from an earlier attempt

        // ---- Download ----
        setProgress(InstallPhase::Downloading, 0.0f);
        spdlog::info("Installing sample project {}: downloading {}", id, info.downloadUrl);

        std::string error;
        const bool downloaded = ArchiveInstaller::Download(info.downloadUrl, partFile, DOWNLOAD_TIMEOUT_MS,
            [this, &id](const float fraction) -> bool {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = installs_.find(id);
                if (it == installs_.end()) return false;
                it->second->fraction = fraction;
                return !it->second->cancelRequested.load();
            }, error);

        if (isCancelled()) { fail("install cancelled"); return; }
        if (!downloaded) { fail(error); return; }

        // ---- Verify ----
        setProgress(InstallPhase::VerifyingHash, 0.0f);
        if (!ArchiveInstaller::VerifySha256(partFile, info.sha256, error)) { fail(error); return; }
        if (isCancelled()) { fail("install cancelled"); return; }

        // ---- Extract into the work folder ----
        setProgress(InstallPhase::Extracting, 0.0f);

        try {
            fs::create_directories(installingDir);
        } catch (const std::exception& e) {
            fail(std::string("could not create extraction directory: ") + e.what());
            return;
        }

        if (!ArchiveInstaller::ExtractZip(partFile, installingDir, setFraction, error)) { fail(error); return; }
        if (isCancelled()) { fail("install cancelled"); return; }

        // ---- Validate ----
        setProgress(InstallPhase::Validating, 0.0f);

        const fs::path extractedProject = installingDir / info.projectFolder;
        std::error_code existsEc;
        if (!fs::exists(extractedProject / "project.tilky", existsEc)) {
            fail("the downloaded ZIP has no " + info.projectFolder + "/project.tilky");
            return;
        }

        // ---- Move into the projects folder ----
        const fs::path projectsRoot = ProjectManager::GetDefaultProjectsFolder();
        fs::path destination;

        try {
            fs::create_directories(projectsRoot);
            destination = FirstFreeFolder(projectsRoot, info.projectFolder);
        } catch (const std::exception& e) {
            fail(std::string("could not prepare the projects folder: ") + e.what());
            return;
        }

        std::error_code renameEc;
        fs::rename(extractedProject, destination, renameEc);
        if (renameEc) {
            // rename can't cross drives; copy instead in that case.
            std::error_code copyEc;
            fs::copy(extractedProject, destination, fs::copy_options::recursive, copyEc);
            if (copyEc) {
                std::error_code cleanupEc;
                fs::remove_all(destination, cleanupEc);
                fail("could not move the project into " + projectsRoot.string() + ": " + copyEc.message());
                return;
            }
        }

        // A second copy ("Tilky Showcase (2)") gets that as its display name too,
        // so the launcher list can tell the two apart.
        if (destination.filename().string() != info.projectFolder) {
            const fs::path tilkyFile = destination / "project.tilky";
            try {
                json data = json::parse(std::ifstream(tilkyFile));
                data["name"] = destination.filename().string();
                std::ofstream(tilkyFile, std::ios::trunc) << data.dump(4);
            } catch (const std::exception& e) {
                spdlog::warn("Installed {} but couldn't rename it in project.tilky: {}", destination.string(), e.what());
            }
        }

        RemoveWorkFiles(id);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(id);
            if (it != installs_.end()) {
                it->second->installedFolder = destination;
                it->second->phase = InstallPhase::Complete;
                it->second->fraction = 1.0f;
            }
        }

        spdlog::info("Sample project {} installed at {}", id, destination.string());
    }
}
