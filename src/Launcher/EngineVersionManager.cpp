//
// Created by berke on 8/2/2026.
//
#include "Headers/Launcher/EngineVersionManager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

#include <cpr/cpr.h>
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_os.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Launcher/Sha256.hpp"
#include "Headers/Project/ProjectManager.hpp"

#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace EngineVersionManager {
    namespace {
        // ============================================================================
        // CONFIGURATION - the one place that names where versions.json is hosted.
        // versions.json is committed to this repository's default branch and served
        // straight from raw.githubusercontent.com - see the "Where versions.json is
        // hosted" section of ENGINE_VERSION_MANAGEMENT.md for why, and what to change
        // here (and in .github/workflows/release-engine.yml) if the repository ever
        // moves.
        // ============================================================================
        constexpr const char* MANIFEST_URL =
             "https://raw.githubusercontent.com/BenBerke/Tilky-Engine/main/versions.json";

        constexpr long CONNECT_TIMEOUT_MS = 8000;
        constexpr long MANIFEST_TIMEOUT_MS = 15000;
        constexpr long DOWNLOAD_TIMEOUT_MS = 30L * 60L * 1000L; // engine ZIPs can be large on a slow connection

        // Platform string as it appears in versions.json - keep in sync with whatever
        // the release workflow writes into the manifest (see
        // .github/workflows/release-engine.yml).
#if defined(_WIN32)
    #if defined(_M_ARM64)
        constexpr const char* CURRENT_PLATFORM = "windows-arm64";
    #else
        constexpr const char* CURRENT_PLATFORM = "windows-x64";
    #endif
#elif defined(__linux__)
    #if defined(__aarch64__)
        constexpr const char* CURRENT_PLATFORM = "linux-arm64";
    #else
        constexpr const char* CURRENT_PLATFORM = "linux-x64";
    #endif
#elif defined(__APPLE__)
    #if defined(__aarch64__)
        constexpr const char* CURRENT_PLATFORM = "macos-arm64";
    #else
        constexpr const char* CURRENT_PLATFORM = "macos-x64";
    #endif
#else
        constexpr const char* CURRENT_PLATFORM = "unknown";
#endif

        std::string LowercaseHexDigest(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool EndsWith(const std::string_view text, const std::string_view suffix) {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // Loose numeric version comparison ("0.9.0" < "0.10.0" < "1.0.0"). This is not
        // a full semver implementation (no pre-release precedence, no build metadata
        // handling) - it only needs to order the plain "major.minor.patch"-style
        // strings this feature's versions.json examples use.
        std::vector<long> ParseVersionParts(const std::string& version) {
            std::vector<long> parts;
            std::stringstream ss(version);
            std::string part;

            while (std::getline(ss, part, '.')) {
                try {
                    parts.push_back(std::stol(part));
                } catch (const std::exception&) {
                    parts.push_back(0);
                }
            }

            return parts;
        }

        bool IsVersionNewer(const std::string& a, const std::string& b) {
            const std::vector<long> partsA = ParseVersionParts(a);
            const std::vector<long> partsB = ParseVersionParts(b);
            const std::size_t count = std::max(partsA.size(), partsB.size());

            for (std::size_t i = 0; i < count; ++i) {
                const long valueA = i < partsA.size() ? partsA[i] : 0;
                const long valueB = i < partsB.size() ? partsB[i] : 0;
                if (valueA != valueB) return valueA > valueB;
            }

            return false;
        }

        std::vector<VersionInfo> ParseManifest(const std::string& manifestJson, std::string& outError) {
            std::vector<VersionInfo> result;

            json data;
            try {
                data = json::parse(manifestJson);
            } catch (const std::exception& e) {
                outError = std::string("could not parse versions.json: ") + e.what();
                return result;
            }

            if (!data.contains("versions") || !data["versions"].is_array()) {
                outError = "versions.json has no \"versions\" array";
                return result;
            }

            for (const auto& entry : data["versions"]) {
                const std::string platform = entry.value("platform", std::string());
                if (platform != CURRENT_PLATFORM) continue;

                VersionInfo info;
                info.version = entry.value("version", std::string());
                info.platform = platform;
                info.channel = entry.value("channel", std::string("stable"));
                info.downloadUrl = entry.value("downloadUrl", std::string());
                info.sizeBytes = entry.value("size", static_cast<std::uint64_t>(0));
                info.sha256 = entry.value("sha256", std::string());
                info.minLauncherVersion = entry.value("minLauncherVersion", std::string());

                if (info.version.empty() || info.downloadUrl.empty()) {
                    spdlog::warn("Skipping versions.json entry with a missing version/downloadUrl for platform {}", platform);
                    continue;
                }

                result.push_back(std::move(info));
            }

            return result;
        }

        // Best-effort, platform-specific check for "does a running process currently
        // have this exact executable open". Used only to satisfy "do not remove a
        // version while its engine process is running" (see CheckVersionRemoval()) -
        // it is intentionally system-wide rather than limited to processes this
        // launcher itself spawned, since RequestOpenProject() in LauncherApp.cpp
        // always quits the launcher before ProjectManager::LaunchEngine() ever runs,
        // so a launcher-session-scoped handle would never actually be checked while
        // this launcher's own UI is interactive. A false negative here just means
        // RemoveVersion() proceeds and the OS itself will most likely refuse (or, on
        // Windows, defer) the underlying file deletion of a running executable.
        bool IsEngineExecutableInUse(const fs::path& exePath) {
            std::error_code existsEc;
            if (!fs::exists(exePath, existsEc)) return false;

#ifdef _WIN32
            // A running Windows executable image is normally opened by the OS loader
            // without FILE_SHARE_DELETE, so requesting DELETE access with no sharing
            // fails with a sharing violation while it's still running. This is the
            // same technique many installers use to detect "please close the app
            // first" without enumerating every process on the system.
            const HANDLE handle = CreateFileW(
                exePath.wstring().c_str(),
                DELETE,
                0, // no sharing - fails if anything else has so much as a read handle open
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (handle == INVALID_HANDLE_VALUE) {
                return GetLastError() == ERROR_SHARING_VIOLATION;
            }

            CloseHandle(handle);
            return false;
#elif defined(__linux__)
            std::error_code canonicalEc;
            const fs::path target = fs::canonical(exePath, canonicalEc);
            if (canonicalEc) return false;

            DIR* procDir = opendir("/proc");
            if (procDir == nullptr) return false;

            bool inUse = false;
            const struct dirent* entry;
            while ((entry = readdir(procDir)) != nullptr) {
                const std::string_view name = entry->d_name;
                if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])) == 0) continue;

                const std::string linkPath = std::string("/proc/") + entry->d_name + "/exe";

                char resolved[4096];
                const ssize_t len = readlink(linkPath.c_str(), resolved, sizeof(resolved) - 1);
                if (len <= 0) continue;
                resolved[len] = '\0';

                std::error_code compareEc;
                if (fs::equivalent(fs::path(resolved), target, compareEc)) {
                    inUse = true;
                    break;
                }
            }

            closedir(procDir);
            return inUse;
#else
            // No implementation for this platform yet - err on the side of allowing
            // removal. The "referencing projects" warning the launcher UI builds from
            // its own project list (see DrawEngineVersionsModal) is the remaining
            // safeguard here.
            (void)exePath;
            return false;
#endif
        }
    }

    EngineVersionManager& EngineVersionManager::Instance() {
        static EngineVersionManager instance;
        return instance;
    }

    EngineVersionManager::~EngineVersionManager() {
        Shutdown();
    }

    void EngineVersionManager::Initialize() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ScanInstalledVersionsLocked();
        }

        // Sweep leftover temp artifacts from a previous run that didn't exit cleanly
        // (a normal completion or failure already cleans up after itself via
        // CleanupTempInstallArtifacts).
        const fs::path versionsFolder = ProjectManager::GetEngineVersionsFolder();
        std::error_code existsEc;
        if (fs::exists(versionsFolder, existsEc)) {
            try {
                for (const auto& entry : fs::directory_iterator(versionsFolder)) {
                    const std::string name = entry.path().filename().string();
                    std::error_code removeEc;

                    if (EndsWith(name, ".zip.part")) {
                        fs::remove(entry.path(), removeEc);
                        spdlog::info("Removed leftover partial download from a previous run: {}", entry.path().string());
                    } else if (EndsWith(name, "-installing")) {
                        fs::remove_all(entry.path(), removeEc);
                        spdlog::info("Removed leftover incomplete install from a previous run: {}", entry.path().string());
                    }
                }
            } catch (const std::exception& e) {
                spdlog::warn("Failed to sweep leftover install artifacts: {}", e.what());
            }
        }

        RefreshManifestAsync();
    }

    void EngineVersionManager::Shutdown() {
        std::vector<std::thread> threadsToJoin;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutDown_) return;
            shutDown_ = true;

            for (auto& [version, state] : installs_) {
                state->cancelRequested = true;
                if (state->worker.joinable()) threadsToJoin.push_back(std::move(state->worker));
            }
        }

        // Joining happens outside the lock: RunInstall() takes this same mutex to
        // report progress, so holding it here while joining would deadlock against
        // the very threads we're waiting on.
        for (auto& t : threadsToJoin) {
            if (t.joinable()) t.join();
        }

        if (manifestThread_.joinable()) manifestThread_.join();
    }

    std::vector<InstallOutcomeEvent> EngineVersionManager::PollEvents() {
        std::vector<InstallOutcomeEvent> events;

        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = installs_.begin(); it != installs_.end();) {
            InstallState& state = *it->second;

            if (state.phase == InstallPhase::Complete || state.phase == InstallPhase::Failed) {
                if (state.worker.joinable()) state.worker.join();

                InstallOutcomeEvent event;
                event.version = it->first;
                event.success = state.phase == InstallPhase::Complete;
                event.errorMessage = state.errorMessage;
                events.push_back(std::move(event));

                it = installs_.erase(it);
            } else {
                ++it;
            }
        }

        return events;
    }

    void EngineVersionManager::RefreshManifestAsync() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (manifestLoading_) return; // already in flight
            manifestLoading_ = true;
            manifestError_.reset();
        }

        if (manifestThread_.joinable()) manifestThread_.join();
        manifestThread_ = std::thread(&EngineVersionManager::RunManifestFetch, this);
    }

    void EngineVersionManager::RunManifestFetch() {
        cpr::Session session;
        const auto cacheBust = std::chrono::system_clock::now().time_since_epoch().count();

        session.SetUrl(cpr::Url{std::string(MANIFEST_URL) + "?nocache=" +std::to_string(cacheBust)});

        session.SetHeader(cpr::Header{
            {"Cache-Control", "no-cache"},
            {"Pragma", "no-cache"}
        });
        session.SetTimeout(cpr::Timeout{MANIFEST_TIMEOUT_MS});
        session.SetConnectTimeout(cpr::ConnectTimeout{CONNECT_TIMEOUT_MS});
        session.SetRedirect(cpr::Redirect{true});

        const cpr::Response response = session.Get();

        spdlog::info("versions.json response:\n{}", response.text);

        std::lock_guard<std::mutex> lock(mutex_);
        manifestLoading_ = false;

        if (response.error.code != cpr::ErrorCode::OK) {
            manifestError_ = "could not reach " + std::string(MANIFEST_URL) + ": " + response.error.message;
            spdlog::error("Engine version manifest fetch failed: {}", *manifestError_);
            return;
        }

        if (response.status_code != 200) {
            manifestError_ = "versions.json request returned HTTP " + std::to_string(response.status_code);
            spdlog::error("Engine version manifest fetch failed: {}", *manifestError_);
            return;
        }

        std::string parseError;
        std::vector<VersionInfo> parsed = ParseManifest(response.text, parseError);

        if (!parseError.empty()) {
            manifestError_ = parseError;
            spdlog::error("Engine version manifest fetch failed: {}", *manifestError_);
            return;
        }

        manifestVersions_ = std::move(parsed);
        spdlog::info("Loaded engine version manifest: {} version(s) available for {}", manifestVersions_.size(), CURRENT_PLATFORM);
    }

    bool EngineVersionManager::IsManifestLoading() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestLoading_;
    }

    std::optional<std::string> EngineVersionManager::GetManifestError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return manifestError_;
    }

    void EngineVersionManager::ScanInstalledVersionsLocked() {
        installedVersionsCache_.clear();

        const fs::path versionsFolder = ProjectManager::GetEngineVersionsFolder();

        std::error_code existsEc;
        if (!fs::exists(versionsFolder, existsEc)) return;

        try {
            for (const auto& entry : fs::directory_iterator(versionsFolder)) {
                if (!entry.is_directory()) continue;

                const std::string name = entry.path().filename().string();
                if (EndsWith(name, "-installing")) continue;

                if (ProjectManager::IsEngineVersionInstalled(name)) {
                    installedVersionsCache_.push_back(name);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to scan EngineVersions folder '{}': {}", versionsFolder.string(), e.what());
        }
    }

    std::vector<VersionInfo> EngineVersionManager::GetAvailableVersions() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<VersionInfo> result = manifestVersions_;

        for (VersionInfo& info : result) {
            info.installed = std::find(installedVersionsCache_.begin(), installedVersionsCache_.end(), info.version)
                              != installedVersionsCache_.end();
        }

        // Any locally installed version the manifest doesn't (or no longer) list is
        // still surfaced, so it stays visible and removable.
        for (const std::string& installedVersion : installedVersionsCache_) {
            const bool alreadyListed = std::any_of(result.begin(), result.end(),
                [&](const VersionInfo& info) { return info.version == installedVersion; });

            if (alreadyListed) continue;

            VersionInfo info;
            info.version = installedVersion;
            info.platform = CURRENT_PLATFORM;
            info.channel = "unknown";
            info.installed = true;
            result.push_back(std::move(info));
        }

        return result;
    }

    bool EngineVersionManager::IsVersionInstalled(const std::string& version) const {
        return ProjectManager::IsEngineVersionInstalled(version);
    }

    std::optional<std::string> EngineVersionManager::GetLatestInstalledStableVersion() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::optional<std::string> latest;

        for (const std::string& installedVersion : installedVersionsCache_) {
            // "Stable" means the manifest currently calls it stable, if we know about
            // it; a locally installed version the manifest doesn't mention is treated
            // as stable too, rather than excluded, since it's still an installed,
            // presumably-working copy.
            const auto manifestEntry = std::find_if(manifestVersions_.begin(), manifestVersions_.end(),
                [&](const VersionInfo& info) { return info.version == installedVersion; });

            const bool isStable = manifestEntry == manifestVersions_.end() || manifestEntry->channel == "stable";
            if (!isStable) continue;

            if (!latest || IsVersionNewer(installedVersion, *latest)) {
                latest = installedVersion;
            }
        }

        return latest;
    }

    void EngineVersionManager::InstallVersion(const std::string& version) {
        VersionInfo info;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (installs_.count(version) > 0) {
                spdlog::warn("Engine version {} install requested but one is already in progress", version);
                return;
            }

            const auto it = std::find_if(manifestVersions_.begin(), manifestVersions_.end(),
                [&](const VersionInfo& candidate) { return candidate.version == version; });

            if (it == manifestVersions_.end()) {
                spdlog::error("Engine version {} is not in the manifest for platform {} - cannot install", version, CURRENT_PLATFORM);
                return;
            }

            info = *it;
        }

        if (ProjectManager::IsEngineVersionInstalled(version)) {
            spdlog::warn("Engine version {} install requested but it's already installed", version);
            return;
        }

        auto state = std::make_unique<InstallState>();
        state->phase = InstallPhase::Downloading;
        InstallState* const statePtr = state.get();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            installs_[version] = std::move(state);
        }

        statePtr->worker = std::thread(&EngineVersionManager::RunInstall, this, version, info);
    }

    void EngineVersionManager::CancelInstall(const std::string& version) {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = installs_.find(version);
        if (it == installs_.end()) return;

        it->second->cancelRequested = true;
    }

    bool EngineVersionManager::IsInstalling(const std::string& version) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return installs_.count(version) > 0;
    }

    InstallProgress EngineVersionManager::GetInstallProgress(const std::string& version) const {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = installs_.find(version);
        if (it == installs_.end()) return InstallProgress{};

        InstallProgress progress;
        progress.phase = it->second->phase;
        progress.fraction = it->second->fraction;
        progress.errorMessage = it->second->errorMessage;
        return progress;
    }

    void EngineVersionManager::CleanupTempInstallArtifacts(const std::string& version) const {
        const fs::path versionsFolder = ProjectManager::GetEngineVersionsFolder();
        const fs::path partFile = versionsFolder / (version + ".zip.part");
        const fs::path installingDir = versionsFolder / (version + "-installing");

        std::error_code ec;
        fs::remove(partFile, ec);
        fs::remove_all(installingDir, ec);
    }

    void EngineVersionManager::RunInstall(std::string version, VersionInfo info) {
        const fs::path versionsFolder = ProjectManager::GetEngineVersionsFolder();
        const fs::path partFile = versionsFolder / (version + ".zip.part");
        const fs::path installingDir = versionsFolder / (version + "-installing");
        const fs::path finalDir = ProjectManager::GetEngineVersionDirectory(version);

        const auto setProgress = [this, &version](const InstallPhase phase, const float fraction) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(version);
            if (it == installs_.end()) return;
            it->second->phase = phase;
            it->second->fraction = fraction;
        };

        const auto setFailed = [this, &version](const std::string& message) {
            spdlog::error("Engine version {} install failed: {}", version, message);
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(version);
            if (it == installs_.end()) return;
            it->second->phase = InstallPhase::Failed;
            it->second->errorMessage = message;
        };

        const auto isCancelled = [this, &version]() {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = installs_.find(version);
            if (it == installs_.end()) return true;
            return it->second->cancelRequested.load();
        };

        try {
            fs::create_directories(versionsFolder);
        } catch (const std::exception& e) {
            setFailed(std::string("could not create EngineVersions folder: ") + e.what());
            return;
        }

        // Clears out anything left over from an earlier failed attempt at this same
        // version before starting a fresh one.
        CleanupTempInstallArtifacts(version);

        // ---- Download into a .part file ----
        setProgress(InstallPhase::Downloading, 0.0f);
        spdlog::info("Installing engine version {}: downloading {}", version, info.downloadUrl);

        {
            std::ofstream outFile(partFile, std::ios::binary | std::ios::trunc);
            if (!outFile.is_open()) {
                setFailed("could not open .part file for writing: " + partFile.string());
                return;
            }

            cpr::Session session;
            session.SetUrl(cpr::Url{info.downloadUrl});
            session.SetTimeout(cpr::Timeout{DOWNLOAD_TIMEOUT_MS});
            session.SetConnectTimeout(cpr::ConnectTimeout{CONNECT_TIMEOUT_MS});
            session.SetRedirect(cpr::Redirect{true}); // GitHub Release assets redirect to blob storage

            session.SetProgressCallback(cpr::ProgressCallback{
                [this, &version](const cpr::cpr_pf_arg_t downloadTotal, const cpr::cpr_pf_arg_t downloadNow,
                                  cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) -> bool {
                    const float fraction = downloadTotal > 0
                        ? static_cast<float>(static_cast<double>(downloadNow) / static_cast<double>(downloadTotal))
                        : 0.0f;

                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = installs_.find(version);
                    if (it == installs_.end()) return false; // entry gone (Shutdown()) - abort the transfer
                    it->second->fraction = fraction;
                    return !it->second->cancelRequested.load(); // false aborts the transfer (libcurl convention)
                }
            });

            const cpr::Response response = session.Download(outFile);
            outFile.close();

            if (isCancelled()) {
                CleanupTempInstallArtifacts(version);
                setFailed("install cancelled");
                return;
            }

            if (response.error.code != cpr::ErrorCode::OK) {
                CleanupTempInstallArtifacts(version);
                setFailed("download error: " + response.error.message);
                return;
            }

            if (response.status_code != 200) {
                CleanupTempInstallArtifacts(version);
                setFailed("download failed with HTTP status " + std::to_string(response.status_code));
                return;
            }
        }

        // ---- Verify SHA-256 ----
        setProgress(InstallPhase::VerifyingHash, 0.0f);

        const std::optional<std::string> actualHash = TilkySha256::HashFile(partFile);
        if (!actualHash) {
            CleanupTempInstallArtifacts(version);
            setFailed("could not read the downloaded file to verify its hash");
            return;
        }

        if (info.sha256.empty()) {
            spdlog::warn("Engine version {} manifest entry has no sha256 - skipping hash verification", version);
        } else if (LowercaseHexDigest(info.sha256) != *actualHash) {
            CleanupTempInstallArtifacts(version);
            setFailed("hash mismatch: expected " + info.sha256 + ", got " + *actualHash);
            return;
        }

        if (isCancelled()) {
            CleanupTempInstallArtifacts(version);
            setFailed("install cancelled");
            return;
        }

        // ---- Extract into <version>-installing ----
        setProgress(InstallPhase::Extracting, 0.0f);

        try {
            fs::create_directories(installingDir);
        } catch (const std::exception& e) {
            CleanupTempInstallArtifacts(version);
            setFailed(std::string("could not create extraction directory: ") + e.what());
            return;
        }

        {
            struct ExtractProgressContext {
                EngineVersionManager* self;
                const std::string* version;
                int32_t totalEntries;
                int32_t extractedEntries;
            };

            // Pre-count entries so the per-entry callback below can report "N of M" as
            // a fraction - mz_zip_reader_save_all() only calls back once an entry has
            // finished extracting, it doesn't report byte-level progress.
            int32_t totalEntries = 0;
            {
                void* countReader = mz_zip_reader_create();
                if (countReader != nullptr && mz_zip_reader_open_file(countReader, partFile.string().c_str()) == MZ_OK) {
                    int32_t navResult = mz_zip_reader_goto_first_entry(countReader);
                    while (navResult == MZ_OK) {
                        ++totalEntries;
                        navResult = mz_zip_reader_goto_next_entry(countReader);
                    }
                    mz_zip_reader_close(countReader);
                }
                if (countReader != nullptr) mz_zip_reader_delete(&countReader);
            }

            void* reader = mz_zip_reader_create();
            if (reader == nullptr) {
                CleanupTempInstallArtifacts(version);
                setFailed("could not create ZIP reader");
                return;
            }

            ExtractProgressContext progressCtx{this, &version, totalEntries, 0};

            mz_zip_reader_set_entry_cb(reader, &progressCtx,
                [](void*, void* userdata, mz_zip_file*, const char*) -> int32_t {
                    auto* ctx = static_cast<ExtractProgressContext*>(userdata);
                    ctx->extractedEntries++;
                    const float fraction = ctx->totalEntries > 0
                        ? static_cast<float>(ctx->extractedEntries) / static_cast<float>(ctx->totalEntries)
                        : 0.0f;

                    std::lock_guard<std::mutex> lock(ctx->self->mutex_);
                    const auto it = ctx->self->installs_.find(*ctx->version);
                    if (it != ctx->self->installs_.end()) it->second->fraction = fraction;

                    return MZ_OK;
                }
            );

            const int32_t openResult = mz_zip_reader_open_file(reader, partFile.string().c_str());
            int32_t extractResult = MZ_STREAM_ERROR;

            if (openResult == MZ_OK) {
                extractResult = mz_zip_reader_save_all(reader, installingDir.string().c_str());
                mz_zip_reader_close(reader);
            }

            mz_zip_reader_delete(&reader);

            if (openResult != MZ_OK) {
                CleanupTempInstallArtifacts(version);
                setFailed("could not open the downloaded ZIP (it may be corrupt) - minizip-ng error " + std::to_string(openResult));
                return;
            }

            if (extractResult != MZ_OK) {
                CleanupTempInstallArtifacts(version);
                setFailed("ZIP extraction failed - minizip-ng error " + std::to_string(extractResult));
                return;
            }
        }

        if (isCancelled()) {
            CleanupTempInstallArtifacts(version);
            setFailed("install cancelled");
            return;
        }

        // ---- Validate ----
        setProgress(InstallPhase::Validating, 0.0f);

        const fs::path installedExe = installingDir / ProjectManager::GetEngineVersionExecutablePath(version).filename();

        std::error_code exeExistsEc;
        if (!fs::exists(installedExe, exeExistsEc)) {
            CleanupTempInstallArtifacts(version);
            setFailed("extracted ZIP is missing " + installedExe.filename().string() + " - expected it at " + installedExe.string());
            return;
        }

        // ---- Rename into place (only after validation succeeds) ----
        std::error_code removeFinalEc;
        fs::remove_all(finalDir, removeFinalEc); // shouldn't normally exist - InstallVersion() already checks - but a very old stray directory could be here

        std::error_code renameEc;
        fs::rename(installingDir, finalDir, renameEc);
        if (renameEc) {
            CleanupTempInstallArtifacts(version);
            setFailed("could not move the installed files into place: " + renameEc.message());
            return;
        }

        std::error_code removePartEc;
        fs::remove(partFile, removePartEc); // best-effort; a lingering .part is harmless and gets swept on the next Initialize()

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ScanInstalledVersionsLocked();
            const auto it = installs_.find(version);
            if (it != installs_.end()) {
                it->second->phase = InstallPhase::Complete;
                it->second->fraction = 1.0f;
            }
        }

        spdlog::info("Engine version {} installed successfully at {}", version, finalDir.string());
    }

    RemovalCheck EngineVersionManager::CheckVersionRemoval(const std::string& version) const {
        RemovalCheck check;

        if (IsEngineExecutableInUse(ProjectManager::GetEngineVersionExecutablePath(version))) {
            check.allowed = false;
            check.processRunning = true;
        }

        return check;
    }

    bool EngineVersionManager::RemoveVersion(const std::string& version) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (installs_.count(version) > 0) {
                spdlog::error("Cannot remove engine version {} - an install for it is currently in progress", version);
                return false;
            }
        }

        const RemovalCheck check = CheckVersionRemoval(version);
        if (!check.allowed) {
            spdlog::error(
                "Cannot remove engine version {} - its executable currently looks like it's running",
                version
            );
            return false;
        }

        const fs::path versionDir = ProjectManager::GetEngineVersionDirectory(version);

        std::error_code removeEc;
        const std::uintmax_t removedCount = fs::remove_all(versionDir, removeEc);

        if (removeEc) {
            spdlog::error("Failed to remove engine version {} at {}: {}", version, versionDir.string(), removeEc.message());
            return false;
        }

        if (removedCount == 0) {
            spdlog::warn("Engine version {} had nothing to remove at {}", version, versionDir.string());
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ScanInstalledVersionsLocked();
        }

        spdlog::info("Removed engine version {}", version);

        return true;
    }
}