//
// Created by berke on 5/4/2026.
//
#include "Headers/Launcher/LauncherApp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Engine/Local/Local.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_timer.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <nlohmann/json.hpp>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace {
    // ============================================================================
    // Core window / renderer state (existing)
    // ============================================================================
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    bool quitRequested = false;
    bool rendererVSyncEnabled = false;

    constexpr Uint64 TARGET_FRAME_NS = SDL_NS_PER_SECOND / 60;

    // Guards Update()/Destroy() against a Start() that bailed out early (SDL_Init or
    // window/renderer creation failure): without this, the main loop in LauncherMain.cpp
    // would still call Update() on a null window, and Destroy() would still call into an
    // ImGui context/backend that was never created.
    bool launcherStarted = false;

    fs::path pendingProjectToOpen;

    SDL_Texture* logo = nullptr;

    int windowWidth = 1080, windowHeight = 960;

    constexpr int DEFAULT_LAUNCHER_WIDTH  = 1080;
    constexpr int DEFAULT_LAUNCHER_HEIGHT = 960;

    constexpr int MIN_LAUNCHER_WIDTH  = 640;
    constexpr int MIN_LAUNCHER_HEIGHT = 480;

    // Keep space for titlebar/borders/taskbar and avoid spawning edge-to-edge.
    constexpr int LAUNCHER_WINDOW_MARGIN = 80;

    // The launcher-variables JSON owned by LauncherMain.cpp. We keep a pointer to it for
    // the whole Start()..Destroy() lifetime and both read from and write into it directly
    // (window geometry, sort mode, imported project paths, per-project pin/last-opened data).
    json* launcherVariables = nullptr;

    constexpr size_t PROJECT_NAME_MAX_LENGTH = 128;
    constexpr float TOAST_DURATION_SECONDS = 3.5f;

    std::map<std::string, std::string> languages;

    // ============================================================================
    // Project model
    // ============================================================================
    enum class SortMode {
        RecentlyOpened = 0,
        Name = 1,
        DateModified = 2
    };

    struct ProjectEntry {
        std::string name;
        fs::path folderPath;
        fs::path tilkyFilePath;
        fs::file_time_type lastModified{};
        bool isImported = false;
        bool isMissing = false;
        bool isPinned = false;
        long long lastOpenedUnix = 0;
    };

    enum class ToastLevel { Info, Success, Warning, Error };

    struct Toast {
        std::string message;
        ToastLevel level = ToastLevel::Info;
        float remainingSeconds = 0.0f;
    };

    std::vector<ProjectEntry> projectEntries;
    std::vector<ProjectEntry*> visibleProjectEntries;
    bool projectListDirty = true;
    bool visibleProjectListDirty = true;

    char searchBuffer[128] = {};
    bool focusSearchBoxRequested = false;

    SortMode currentSortMode = SortMode::RecentlyOpened;

    fs::path selectedProjectPath;

    fs::path projectPendingDelete;
    bool projectPendingDeleteIsImported = false;
    bool wantOpenDeleteConfirmPopup = false;

    bool wantOpenNewProjectPopup = false;
    std::array<char, PROJECT_NAME_MAX_LENGTH> newProjectNameBuffer{};
    std::string newProjectValidationError;

    bool wantOpenRenamePopup = false;
    fs::path projectBeingRenamed;
    std::array<char, PROJECT_NAME_MAX_LENGTH> renameBuffer{};
    std::string renameValidationError;

    std::vector<Toast> toasts;

    std::mutex dialogResultMutex;
    std::optional<fs::path> pendingImportResult;
    std::optional<fs::path> pendingLocateResult;
    fs::path projectBeingLocated;

    // ============================================================================
    // Forward declarations (definitions are ordered for readability further below,
    // not for call order, since everything here is mutually-referencing).
    // ============================================================================
    void ClampLauncherWindowSizeToDisplay(int desiredWidth, int desiredHeight);
    void CenterLauncherWindowOnDisplay();
    bool CopyLuaAutocompleteFiles(const fs::path& projectPath);
    bool RefreshLanguages();

    bool IsFilesystemSafeName(const std::string& name);
    std::string ValidateNewProjectName(const std::string& rawName);
    std::string ValidateDisplayName(const std::string& rawName);

    std::string ToLowerCopy(const std::string& s);
    bool CaseInsensitiveLess(const std::string& a, const std::string& b);

    std::string NormalizedKey(const fs::path& p);
    void LoadProjectMeta(ProjectEntry& entry);
    void SaveProjectMetaField(const fs::path& folderPath, const std::string& field, const json& value);
    bool IsPathInside(const fs::path& child, const fs::path& parent);

    std::vector<ProjectEntry> DiscoverLocalProjects();
    std::vector<ProjectEntry> LoadImportedProjects();
    void RemoveImportedProject(const fs::path& folderPath);
    void RefreshProjectList();
    void RebuildVisibleProjectList();

    void MarkProjectOpened(const ProjectEntry& entry);
    void RequestOpenProject(const ProjectEntry& entry);

    std::string FormatRelativeTime(long long unixSeconds);

    void PushToast(std::string message, ToastLevel level);
    void UpdateToasts(float deltaSeconds);
    ImVec4 ToastColor(ToastLevel level);
    void DrawToasts();

    void RevealInFileManager(const fs::path& path);

    void SDLCALL ImportFolderPickedCallback(void* userdata, const char* const* filelist, int filter);
    void TriggerImportProjectDialog();
    void ProcessPendingImport();

    void SDLCALL LocateFolderPickedCallback(void* userdata, const char* const* filelist, int filter);
    void TriggerLocateProjectDialog(const fs::path& missingFolder);
    void ProcessPendingLocate();

    void ApplyLauncherStyle();
    void DrawBackgroundLogo();

    void DrawTopBar();
    void DrawNewProjectModal();
    void DrawRenameModal();
    void DrawDeleteConfirmModal();
    void DrawProjectCard(ProjectEntry& entry, float cardWidth);
    void DrawProjectList(float availableHeight);
    void DrawFooter();
    void HandleGlobalShortcuts();

    // ============================================================================
    // Window placement (existing logic, Clamp now takes a desired size so a persisted
    // window size can be restored instead of always resetting to the hardcoded default)
    // ============================================================================
    void ClampLauncherWindowSizeToDisplay(const int desiredWidth, const int desiredHeight) {
        windowWidth  = desiredWidth;
        windowHeight = desiredHeight;

        const SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display == 0) {
            spdlog::warn("Failed to get primary display: {}", SDL_GetError());
            return;
        }

        SDL_Rect usableBounds{};
        if (!SDL_GetDisplayUsableBounds(display, &usableBounds)) {
            spdlog::warn("Failed to get display usable bounds: {}", SDL_GetError());
            return;
        }

        const int maxWidth = std::max(
            MIN_LAUNCHER_WIDTH,
            usableBounds.w - LAUNCHER_WINDOW_MARGIN
        );

        const int maxHeight = std::max(
            MIN_LAUNCHER_HEIGHT,
            usableBounds.h - LAUNCHER_WINDOW_MARGIN
        );

        windowWidth = std::clamp(desiredWidth, MIN_LAUNCHER_WIDTH, maxWidth);
        windowHeight = std::clamp(desiredHeight, MIN_LAUNCHER_HEIGHT, maxHeight);
    }

    void CenterLauncherWindowOnDisplay() {
        const SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display == 0) {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            return;
        }

        SDL_Rect usableBounds{};
        if (!SDL_GetDisplayUsableBounds(display, &usableBounds)) {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            return;
        }

        const int x = usableBounds.x + (usableBounds.w - windowWidth) / 2;
        const int y = usableBounds.y + (usableBounds.h - windowHeight) / 2;

        SDL_SetWindowPosition(window, x, y);
    }

    // ============================================================================
    // Lua autocomplete file copy (existing, unchanged)
    // ============================================================================
    bool CopyLuaAutocompleteFiles(const fs::path& projectPath) {
        const fs::path autocompleteFolder =
            ProjectManager::FindAssetPath(fs::path("EngineAssets") / "AutoComplete");

        if (!fs::exists(autocompleteFolder)) {
            spdlog::error(
                "AutoComplete folder does not exist: {}",
                autocompleteFolder.string()
            );
            return false;
        }

        const fs::path sourceApiFile = autocompleteFolder / "tilky_api.txt";
        const fs::path sourceLuarcFile = autocompleteFolder / "luarc.txt";

        if (!fs::exists(sourceApiFile)) {
            spdlog::error(
                "Lua autocomplete API file does not exist: {}",
                sourceApiFile.string()
            );
            return false;
        }

        if (!fs::exists(sourceLuarcFile)) {
            spdlog::error(
                "Lua autocomplete config file does not exist: {}",
                sourceLuarcFile.string()
            );
            return false;
        }

        const fs::path tilkyFolder = projectPath / ".tilky";
        fs::create_directories(tilkyFolder);

        const fs::path destinationApiFile = tilkyFolder / "tilky_api.lua";
        const fs::path destinationLuarcFile = projectPath / ".luarc.json";

        try {
            fs::copy_file(
                sourceApiFile,
                destinationApiFile,
                fs::copy_options::overwrite_existing
            );

            fs::copy_file(
                sourceLuarcFile,
                destinationLuarcFile,
                fs::copy_options::overwrite_existing
            );
        }
        catch (const std::exception& e) {
            spdlog::error(
                "Failed to copy Lua autocomplete files to project '{}': {}",
                projectPath.string(),
                e.what()
            );
            return false;
        }

        spdlog::info(
            "Copied Lua autocomplete files to project: {}",
            projectPath.string()
        );

        return true;
    }

    // ============================================================================
    // Localisation language list (existing, unchanged)
    // ============================================================================
    bool RefreshLanguages() {
        languages.clear();

        const fs::path languagesPath =
            ProjectManager::FindAssetPath(fs::path("EngineAssets") / "Local");

        if (!fs::exists(languagesPath)) {
            spdlog::critical("Languages folder does not exists at {}", languagesPath.string());
            return false;
        }

        for (const auto& entry : fs::directory_iterator(languagesPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != ".json") {
                continue;
            }

            const std::string languageCode =
                entry.path().stem().string();

            std::string displayName = languageCode;

            try {
                std::ifstream file(entry.path());

                if (file.is_open()) {
                    json languageJson;
                    file >> languageJson;

                    displayName = languageJson.value("language.name", languageCode);
                }
            }
            catch (const std::exception& e) {
                spdlog::error("Failed to read language file: {} {}", entry.path().string(), e.what());
            }

            languages[languageCode] = displayName;
        }

        return true;
    }

    // ============================================================================
    // Project-name validation
    // ============================================================================
    bool IsFilesystemSafeName(const std::string& name) {
        if (name.empty()) return false;

        if (std::isspace(static_cast<unsigned char>(name.front())) ||
            std::isspace(static_cast<unsigned char>(name.back())) ||
            name.front() == '.' || name.back() == '.') {
            return false;
        }

        static const std::string illegalChars = R"(<>:"/\|?*)";
        for (const unsigned char c : name) {
            if (c < 32) return false; // control characters
            if (illegalChars.find(static_cast<char>(c)) != std::string::npos) return false;
        }

        // Reserved Windows device names (case-insensitive), rejected on every platform
        // so a project created on Linux/macOS still opens cleanly if shared to Windows.
        static const std::array<std::string, 22> reservedNames = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };

        std::string upperName = name;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(),
            [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });

        for (const auto& reserved : reservedNames) {
            if (upperName == reserved) return false;
        }

        return true;
    }

    std::string ValidateNewProjectName(const std::string& rawName) {
        std::string name = rawName;
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();

        if (name.empty()) {
            return Localisation::Get("launcher.invalid_name_empty");
        }

        if (!IsFilesystemSafeName(name)) {
            return Localisation::Get("launcher.invalid_name_chars");
        }

        std::error_code ec;
        const fs::path candidateFolder = ProjectManager::GetDefaultProjectsFolder() / name;
        if (fs::exists(candidateFolder, ec)) {
            return Localisation::Get("launcher.invalid_name_duplicate");
        }

        return {};
    }

    std::string ValidateDisplayName(const std::string& rawName) {
        std::string name = rawName;
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();

        if (name.empty()) {
            return Localisation::Get("launcher.invalid_name_empty");
        }

        return {};
    }

    // ============================================================================
    // Small string helpers
    // ============================================================================
    std::string ToLowerCopy(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    bool CaseInsensitiveLess(const std::string& a, const std::string& b) {
        return ToLowerCopy(a) < ToLowerCopy(b);
    }

    // ============================================================================
    // Per-project persisted metadata (pinned / last-opened), stored in the shared
    // launcher-variables JSON under "projectMeta", keyed by normalized folder path.
    // ============================================================================
    std::string NormalizedKey(const fs::path& p) {
        std::error_code ec;
        fs::path normalized = fs::absolute(p, ec);
        if (ec) normalized = p;
        return normalized.lexically_normal().generic_string();
    }

    void LoadProjectMeta(ProjectEntry& entry) {
        entry.isPinned = false;
        entry.lastOpenedUnix = 0;

        if (launcherVariables == nullptr) return;
        if (!launcherVariables->contains("projectMeta")) return;

        const json& metaRoot = (*launcherVariables)["projectMeta"];
        if (!metaRoot.is_object()) return;

        const std::string key = NormalizedKey(entry.folderPath);
        if (!metaRoot.contains(key)) return;

        const json& entryMeta = metaRoot[key];
        entry.isPinned = entryMeta.value("pinned", false);
        entry.lastOpenedUnix = entryMeta.value("lastOpened", static_cast<long long>(0));
    }

    void SaveProjectMetaField(const fs::path& folderPath, const std::string& field, const json& value) {
        if (launcherVariables == nullptr) return;

        if (!launcherVariables->contains("projectMeta") || !(*launcherVariables)["projectMeta"].is_object()) {
            (*launcherVariables)["projectMeta"] = json::object();
        }

        const std::string key = NormalizedKey(folderPath);
        (*launcherVariables)["projectMeta"][key][field] = value;
    }

    bool IsPathInside(const fs::path& child, const fs::path& parent) {
        std::error_code ec;
        const fs::path canonicalChild = fs::weakly_canonical(child, ec);
        if (ec) return false;
        const fs::path canonicalParent = fs::weakly_canonical(parent, ec);
        if (ec) return false;

        auto childIt = canonicalChild.begin();
        for (auto parentIt = canonicalParent.begin(); parentIt != canonicalParent.end(); ++parentIt, ++childIt) {
            if (childIt == canonicalChild.end() || *childIt != *parentIt) return false;
        }
        return true;
    }

    // ============================================================================
    // Project discovery
    // ============================================================================
    std::vector<ProjectEntry> DiscoverLocalProjects() {
        std::vector<ProjectEntry> result;
        const fs::path projectsRoot = ProjectManager::GetDefaultProjectsFolder();

        if (!fs::exists(projectsRoot)) return result;

        for (const auto& dirEntry : fs::directory_iterator(projectsRoot)) {
            if (!dirEntry.is_directory()) continue;

            fs::path tilkyFile;
            for (const auto& fileEntry : fs::directory_iterator(dirEntry.path())) {
                if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".tilky") {
                    tilkyFile = fileEntry.path();
                    break;
                }
            }

            if (tilkyFile.empty()) continue; // not a project folder, skip quietly

            ProjectEntry entry;
            entry.folderPath = dirEntry.path();
            entry.tilkyFilePath = tilkyFile;

            std::ifstream file(tilkyFile);
            if (file.is_open()) {
                try {
                    json data = json::parse(file);
                    entry.name = data.value("name", dirEntry.path().filename().string());
                }
                catch (const std::exception& e) {
                    spdlog::error("Failed to parse project file '{}': {}", tilkyFile.string(), e.what());
                    entry.name = dirEntry.path().filename().string() + " (corrupted)";
                }
            } else {
                entry.name = dirEntry.path().filename().string() + " (unreadable)";
            }

            std::error_code timeEc;
            entry.lastModified = fs::last_write_time(tilkyFile, timeEc);

            result.push_back(std::move(entry));
        }

        return result;
    }

    std::vector<ProjectEntry> LoadImportedProjects() {
        std::vector<ProjectEntry> result;
        if (launcherVariables == nullptr) return result;
        if (!launcherVariables->contains("importedProjects")) return result;

        const json& importedArray = (*launcherVariables)["importedProjects"];
        if (!importedArray.is_array()) return result;

        for (const auto& pathValue : importedArray) {
            if (!pathValue.is_string()) continue;

            ProjectEntry entry;
            entry.isImported = true;
            entry.folderPath = fs::path(pathValue.get<std::string>());

            std::error_code ec;
            if (fs::exists(entry.folderPath, ec) && fs::is_directory(entry.folderPath, ec)) {
                for (const auto& fileEntry : fs::directory_iterator(entry.folderPath, ec)) {
                    if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".tilky") {
                        entry.tilkyFilePath = fileEntry.path();
                        break;
                    }
                }
            }

            if (entry.tilkyFilePath.empty()) {
                entry.isMissing = true;
                entry.name = entry.folderPath.filename().string();
            } else {
                std::ifstream file(entry.tilkyFilePath);
                if (file.is_open()) {
                    try {
                        json data = json::parse(file);
                        entry.name = data.value("name", entry.folderPath.filename().string());
                    }
                    catch (const std::exception& e) {
                        spdlog::error("Failed to parse imported project file '{}': {}", entry.tilkyFilePath.string(), e.what());
                        entry.name = entry.folderPath.filename().string() + " (corrupted)";
                    }
                }
                std::error_code timeEc;
                entry.lastModified = fs::last_write_time(entry.tilkyFilePath, timeEc);
            }

            result.push_back(std::move(entry));
        }

        return result;
    }

    void RemoveImportedProject(const fs::path& folderPath) {
        if (launcherVariables == nullptr) return;
        if (!launcherVariables->contains("importedProjects") || !(*launcherVariables)["importedProjects"].is_array()) return;

        auto& arr = (*launcherVariables)["importedProjects"];
        arr.erase(
            std::remove_if(arr.begin(), arr.end(), [&](const json& item) {
                return item.is_string() && fs::path(item.get<std::string>()) == folderPath;
            }),
            arr.end()
        );
    }

    void RefreshProjectList() {
        projectEntries.clear();

        try {
            std::vector<ProjectEntry> local = DiscoverLocalProjects();
            std::vector<ProjectEntry> imported = LoadImportedProjects();

            projectEntries.reserve(local.size() + imported.size());
            for (auto& e : local) projectEntries.push_back(std::move(e));
            for (auto& e : imported) projectEntries.push_back(std::move(e));

            for (auto& entry : projectEntries) {
                LoadProjectMeta(entry);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Failed to refresh project list: {}", e.what());
            PushToast(Localisation::Get("launcher.toast_scan_failed"), ToastLevel::Error);
        }

        if (launcherVariables != nullptr) {
            (*launcherVariables)["projectCount"] = static_cast<int>(projectEntries.size());
        }

        projectListDirty = false;
        visibleProjectListDirty = true;
    }

    void MarkProjectOpened(const ProjectEntry& entry) {
        const auto now = std::chrono::system_clock::now();
        const long long epochSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        SaveProjectMetaField(entry.folderPath, "lastOpened", epochSeconds);
    }

    void RequestOpenProject(const ProjectEntry& entry) {
        pendingProjectToOpen = entry.folderPath;
        MarkProjectOpened(entry);
        quitRequested = true;
    }

    // ============================================================================
    // Relative time formatting ("3 hours ago", etc.)
    // ============================================================================
    std::string FormatRelativeTime(const long long unixSeconds) {
        if (unixSeconds <= 0) {
            return Localisation::Get("launcher.never_opened");
        }

        const auto now = std::chrono::system_clock::now();
        const auto then = std::chrono::system_clock::time_point(std::chrono::seconds(unixSeconds));
        long long deltaSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - then).count();
        if (deltaSeconds < 0) deltaSeconds = 0;

        if (deltaSeconds < 90) return Localisation::Get("launcher.time_just_now");

        const long long minutes = deltaSeconds / 60;
        if (minutes < 60) return std::to_string(minutes) + " " + Localisation::Get("launcher.time_minutes_ago");

        const long long hours = minutes / 60;
        if (hours < 24) return std::to_string(hours) + " " + Localisation::Get("launcher.time_hours_ago");

        const long long days = hours / 24;
        if (days < 30) return std::to_string(days) + " " + Localisation::Get("launcher.time_days_ago");

        const long long months = days / 30;
        if (months < 12) return std::to_string(months) + " " + Localisation::Get("launcher.time_months_ago");

        const long long years = months / 12;
        return std::to_string(years) + " " + Localisation::Get("launcher.time_years_ago");
    }

    // ============================================================================
    // Toast notifications
    // ============================================================================
    void PushToast(std::string message, const ToastLevel level) {
        Toast toast;
        toast.message = std::move(message);
        toast.level = level;
        toast.remainingSeconds = TOAST_DURATION_SECONDS;
        toasts.push_back(std::move(toast));

        constexpr size_t MAX_TOASTS = 5;
        if (toasts.size() > MAX_TOASTS) {
            toasts.erase(toasts.begin(), toasts.begin() + static_cast<long>(toasts.size() - MAX_TOASTS));
        }
    }

    void UpdateToasts(const float deltaSeconds) {
        for (auto& toast : toasts) {
            toast.remainingSeconds -= deltaSeconds;
        }

        toasts.erase(
            std::remove_if(toasts.begin(), toasts.end(),
                [](const Toast& t) { return t.remainingSeconds <= 0.0f; }),
            toasts.end()
        );
    }

    ImVec4 ToastColor(const ToastLevel level) {
        switch (level) {
            case ToastLevel::Success: return ImVec4(0.20f, 0.55f, 0.30f, 0.95f);
            case ToastLevel::Warning: return ImVec4(0.65f, 0.50f, 0.10f, 0.95f);
            case ToastLevel::Error:   return ImVec4(0.60f, 0.18f, 0.18f, 0.95f);
            case ToastLevel::Info:
            default:                  return ImVec4(0.20f, 0.20f, 0.24f, 0.95f);
        }
    }

    void DrawToasts() {
        if (toasts.empty()) return;

        constexpr float PADDING = 16.0f;
        constexpr float TOAST_WIDTH = 320.0f;
        constexpr float TOAST_GAP = 8.0f;

        float cursorY = static_cast<float>(windowHeight) - PADDING;

        for (int i = static_cast<int>(toasts.size()) - 1; i >= 0; --i) {
            const Toast& toast = toasts[i];

            const ImVec2 textSize = ImGui::CalcTextSize(toast.message.c_str(), nullptr, false, TOAST_WIDTH - 24.0f);
            const float toastHeight = textSize.y + 20.0f;

            cursorY -= toastHeight;

            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(windowWidth) - TOAST_WIDTH - PADDING, cursorY));
            ImGui::SetNextWindowSize(ImVec2(TOAST_WIDTH, toastHeight));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ToastColor(toast.level));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

            const std::string toastId = "##toast" + std::to_string(i);
            ImGui::Begin(toastId.c_str(), nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::PushTextWrapPos(TOAST_WIDTH - 12.0f);
            ImGui::TextUnformatted(toast.message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::End();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            cursorY -= TOAST_GAP;
        }
    }

    // ============================================================================
    // Reveal-in-file-manager (cross platform shell-out)
    // ============================================================================
    void RevealInFileManager(const fs::path& path) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            PushToast(Localisation::Get("launcher.toast_path_missing"), ToastLevel::Warning);
            return;
        }

        const std::string pathStr = path.string();

#if defined(_WIN32)
        const std::string command = "explorer \"" + pathStr + "\"";
#elif defined(__APPLE__)
        const std::string command = "open \"" + pathStr + "\"";
#else
        const std::string command = "xdg-open \"" + pathStr + "\"";
#endif

        const int result = std::system(command.c_str());
        if (result != 0) {
            // On Windows in particular, explorer.exe frequently returns non-zero even when
            // it opened successfully, so this is a warning rather than a user-facing error.
            spdlog::warn("Reveal-in-file-manager command returned exit code {} for path: {}", result, pathStr);
        }
    }

    // ============================================================================
    // Import Existing Project / Locate Project — native SDL3 folder picker.
    // Requires SDL3's dialog subsystem (SDL_ShowOpenFolderDialog). If your installed
    // SDL3 predates this API, either update SDL3 or remove this block along with the
    // "Import" button in DrawTopBar() and the "Locate..." button in DrawProjectCard().
    // ============================================================================
    void SDLCALL ImportFolderPickedCallback(void* userdata, const char* const* filelist, int filter) {
        (void)userdata;
        (void)filter;

        if (filelist == nullptr) {
            spdlog::error("Import folder dialog error: {}", SDL_GetError());
            return;
        }
        if (filelist[0] == nullptr) return; // user cancelled

        std::lock_guard<std::mutex> lock(dialogResultMutex);
        pendingImportResult = fs::path(filelist[0]);
    }

    void TriggerImportProjectDialog() {
        SDL_ShowOpenFolderDialog(ImportFolderPickedCallback, nullptr, window, nullptr, false);
    }

    void ProcessPendingImport() {
        std::optional<fs::path> pickedFolder;
        {
            std::lock_guard<std::mutex> lock(dialogResultMutex);
            if (!pendingImportResult.has_value()) return;
            pickedFolder = pendingImportResult;
            pendingImportResult.reset();
        }

        std::error_code ec;
        fs::path tilkyFile;
        for (const auto& fileEntry : fs::directory_iterator(*pickedFolder, ec)) {
            if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".tilky") {
                tilkyFile = fileEntry.path();
                break;
            }
        }

        if (tilkyFile.empty()) {
            PushToast(Localisation::Get("launcher.toast_import_invalid"), ToastLevel::Error);
            return;
        }

        if (launcherVariables == nullptr) return;

        if (IsPathInside(*pickedFolder, ProjectManager::GetDefaultProjectsFolder())) {
            PushToast(Localisation::Get("launcher.toast_import_is_local"), ToastLevel::Warning);
            return;
        }

        if (!launcherVariables->contains("importedProjects") || !(*launcherVariables)["importedProjects"].is_array()) {
            (*launcherVariables)["importedProjects"] = json::array();
        }

        for (const auto& existing : (*launcherVariables)["importedProjects"]) {
            if (existing.is_string() && fs::path(existing.get<std::string>()) == *pickedFolder) {
                PushToast(Localisation::Get("launcher.toast_already_imported"), ToastLevel::Warning);
                return;
            }
        }

        (*launcherVariables)["importedProjects"].push_back(pickedFolder->string());
        PushToast(Localisation::Get("launcher.toast_project_imported"), ToastLevel::Success);
        projectListDirty = true;
    }

    void SDLCALL LocateFolderPickedCallback(void* userdata, const char* const* filelist, int filter) {
        (void)userdata;
        (void)filter;

        if (filelist == nullptr || filelist[0] == nullptr) return;

        std::lock_guard<std::mutex> lock(dialogResultMutex);
        pendingLocateResult = fs::path(filelist[0]);
    }

    void TriggerLocateProjectDialog(const fs::path& missingFolder) {
        projectBeingLocated = missingFolder;
        SDL_ShowOpenFolderDialog(LocateFolderPickedCallback, nullptr, window, nullptr, false);
    }

    void ProcessPendingLocate() {
        std::optional<fs::path> newFolder;
        {
            std::lock_guard<std::mutex> lock(dialogResultMutex);
            if (!pendingLocateResult.has_value()) return;
            newFolder = pendingLocateResult;
            pendingLocateResult.reset();
        }

        std::error_code ec;
        bool hasTilky = false;
        for (const auto& fileEntry : fs::directory_iterator(*newFolder, ec)) {
            if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".tilky") {
                hasTilky = true;
                break;
            }
        }

        if (!hasTilky) {
            PushToast(Localisation::Get("launcher.toast_import_invalid"), ToastLevel::Error);
            return;
        }

        if (launcherVariables != nullptr && launcherVariables->contains("importedProjects") &&
            (*launcherVariables)["importedProjects"].is_array()) {
            for (auto& item : (*launcherVariables)["importedProjects"]) {
                if (item.is_string() && fs::path(item.get<std::string>()) == projectBeingLocated) {
                    item = newFolder->string();
                    break;
                }
            }
        }

        PushToast(Localisation::Get("launcher.toast_project_located"), ToastLevel::Success);
        projectBeingLocated.clear();
        projectListDirty = true;
    }

    // ============================================================================
    // Style
    // ============================================================================
    void ApplyLauncherStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.FrameRounding = 5.0f;
        style.GrabRounding = 5.0f;
        style.ScrollbarRounding = 8.0f;
        style.WindowPadding = ImVec2(16.0f, 16.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
    }

    void DrawBackgroundLogo() {
        if (logo == nullptr) return;

        constexpr float logoWidth = 600.0f, logoHeight = 600.0f;
        const SDL_FRect logoRect = {
            static_cast<float>(windowWidth) / 2.0f - logoWidth / 2.0f,
            static_cast<float>(windowHeight) / 2.0f - logoHeight / 2.0f,
            logoWidth,
            logoHeight
        };

        SDL_RenderTexture(renderer, logo, nullptr, &logoRect);
    }

    // ============================================================================
    // Top bar: create / import / refresh + search + sort
    // ============================================================================
    void DrawTopBar() {
        if (ImGui::Button(Localisation::Get("launcher.create_project").c_str())) {
            std::fill(newProjectNameBuffer.begin(), newProjectNameBuffer.end(), '\0');
            newProjectValidationError.clear();
            wantOpenNewProjectPopup = true;
        }

        ImGui::SameLine();
        if (ImGui::Button(Localisation::Get("launcher.import_project").c_str())) {
            TriggerImportProjectDialog();
        }

        ImGui::SameLine();
        if (ImGui::Button(Localisation::Get("launcher.refresh").c_str())) {
            projectListDirty = true;
        }

        ImGui::Spacing();
        ImGui::TextUnformatted(Localisation::Get("launcher.projects").c_str());
        ImGui::Spacing();

        if (focusSearchBoxRequested) {
            ImGui::SetKeyboardFocusHere();
            focusSearchBoxRequested = false;
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputTextWithHint("##search", Localisation::Get("launcher.search_placeholder").c_str(),
            searchBuffer, sizeof(searchBuffer))) {
            visibleProjectListDirty = true;
        }

        ImGui::SameLine();

        static const char* sortLabelKeys[] = {
            "launcher.sort_recent", "launcher.sort_name", "launcher.sort_modified"
        };
        static const char* sortModeStrings[] = { "recent", "name", "modified" };

        const int sortIndex = static_cast<int>(currentSortMode);
        const std::string currentSortLabel = Localisation::Get(sortLabelKeys[sortIndex]);

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##sort_mode", currentSortLabel.c_str())) {
            for (int i = 0; i < 3; ++i) {
                const bool isSelected = (i == sortIndex);
                if (ImGui::Selectable(Localisation::Get(sortLabelKeys[i]).c_str(), isSelected)) {
                    currentSortMode = static_cast<SortMode>(i);
                    visibleProjectListDirty = true;
                    if (launcherVariables != nullptr) {
                        (*launcherVariables)["sortMode"] = sortModeStrings[i];
                    }
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ============================================================================
    // New Project modal
    // ============================================================================
    void DrawNewProjectModal() {
    constexpr const char* popupId = "Create Project###new_project_modal";

    if (wantOpenNewProjectPopup) {
        ImGui::OpenPopup(popupId);
        wantOpenNewProjectPopup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f)
    );

    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(Localisation::Get("launcher.input_name").c_str());

        const bool enterPressed = ImGui::InputText(
            "##new_project_name",
            newProjectNameBuffer.data(),
            newProjectNameBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

        if (!newProjectValidationError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", newProjectValidationError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        const bool triggerCreate = enterPressed || ImGui::Button(Localisation::Get("launcher.create").c_str());

        if (triggerCreate) {
            const std::string typedName = newProjectNameBuffer.data();
            const std::string validationMessage =
                ValidateNewProjectName(typedName);

            if (!validationMessage.empty()) newProjectValidationError = validationMessage;
            else {
                ProjectManager::CreateProjectDirectory(typedName);

                const fs::path projectPath = ProjectManager::GetDefaultProjectsFolder() / typedName;

                CopyLuaAutocompleteFiles(projectPath);

                PushToast(Localisation::Get("launcher.toast_project_created"), ToastLevel::Success);

                std::ranges::fill(newProjectNameBuffer,'\0');

                newProjectValidationError.clear();
                projectListDirty = true;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(Localisation::Get("common.cancel").c_str())) {
            std::ranges::fill(newProjectNameBuffer, '\0');

            newProjectValidationError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

    // ============================================================================
    // Rename modal (renames the display name stored inside the .tilky file only —
    // it never touches the project's folder path, so nothing else that may reference
    // that path can be broken by a rename).
    // ============================================================================
    void DrawRenameModal() {
        const std::string popupTitle =
        Localisation::Get("launcher.rename_project") + "###rename_modal";

        if (wantOpenRenamePopup) {
            ImGui::OpenPopup(popupTitle.c_str());
            wantOpenRenamePopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(Localisation::Get("launcher.rename_new_name").c_str());

            const bool enterPressed = ImGui::InputText("##rename_input", renameBuffer.data(), renameBuffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);

            if (!renameValidationError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
                ImGui::TextWrapped("%s", renameValidationError.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            const bool triggerSave = enterPressed || ImGui::Button(Localisation::Get("common.save").c_str());

            if (triggerSave) {
                const std::string typedName = renameBuffer.data();
                const std::string validationMessage = ValidateDisplayName(typedName);

                if (!validationMessage.empty()) {
                    renameValidationError = validationMessage;
                } else {
                    bool didRename = false;

                    for (auto& entry : projectEntries) {
                        if (entry.folderPath != projectBeingRenamed) continue;

                        std::ifstream inFile(entry.tilkyFilePath);
                        json data;
                        bool readOk = false;
                        if (inFile.is_open()) {
                            try {
                                data = json::parse(inFile);
                                readOk = true;
                            }
                            catch (const std::exception& e) {
                                spdlog::error("Failed to parse project file for rename '{}': {}", entry.tilkyFilePath.string(), e.what());
                            }
                        }
                        inFile.close();

                        if (readOk) {
                            data["name"] = typedName;
                            std::ofstream outFile(entry.tilkyFilePath);
                            if (outFile.is_open()) {
                                outFile << data.dump(4);
                                outFile.close();
                                didRename = true;
                            } else {
                                spdlog::error("Failed to open project file for writing during rename: {}", entry.tilkyFilePath.string());
                            }
                        }

                        break;
                    }

                    PushToast(
                        Localisation::Get(didRename ? "launcher.toast_project_renamed" : "launcher.toast_rename_failed"),
                        didRename ? ToastLevel::Success : ToastLevel::Error
                    );

                    if (didRename) projectListDirty = true;
                    projectBeingRenamed.clear();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(Localisation::Get("common.cancel").c_str())) {
                projectBeingRenamed.clear();
                renameValidationError.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // ============================================================================
    // Delete / remove confirmation modal
    // ============================================================================
    void DrawDeleteConfirmModal() {
        if (wantOpenDeleteConfirmPopup) {
            ImGui::OpenPopup("##delete_confirm_modal");
            wantOpenDeleteConfirmPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(Localisation::Get("launcher.confirm_delete_title").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string message = projectPendingDeleteIsImported
                ? Localisation::Get("launcher.confirm_remove_message")
                : Localisation::Get("launcher.confirm_delete_message");

            ImGui::PushTextWrapPos(400.0f);
            ImGui::TextUnformatted(message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            ImGui::TextDisabled("%s", projectPendingDelete.string().c_str());
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.24f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.16f, 0.16f, 1.0f));

            const std::string confirmLabel = projectPendingDeleteIsImported
                ? Localisation::Get("launcher.remove_from_list")
                : Localisation::Get("launcher.delete_project");

            if (ImGui::Button(confirmLabel.c_str())) {
                if (projectPendingDeleteIsImported) {
                    RemoveImportedProject(projectPendingDelete);
                    PushToast(Localisation::Get("launcher.toast_project_removed"), ToastLevel::Success);
                } else {
                    try {
                        const std::uintmax_t removedCount = fs::remove_all(projectPendingDelete);
                        spdlog::info("Deleted project folder '{}' ({} items removed)", projectPendingDelete.string(), removedCount);
                        PushToast(Localisation::Get("launcher.toast_project_deleted"), ToastLevel::Success);
                    }
                    catch (const std::exception& e) {
                        spdlog::error("Failed to delete project folder '{}': {}", projectPendingDelete.string(), e.what());
                        PushToast(Localisation::Get("launcher.toast_delete_failed"), ToastLevel::Error);
                    }
                }

                if (selectedProjectPath == projectPendingDelete) selectedProjectPath.clear();
                projectPendingDelete.clear();
                projectListDirty = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::Button(Localisation::Get("common.cancel").c_str())) {
                projectPendingDelete.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // ============================================================================
    // A single project card: name + subtext (selectable, click to select, double
    // click to open) plus an action-button row (Open/Pin/Rename/Show in folder/
    // Delete, or Locate/Remove for missing entries).
    // ============================================================================
    void DrawProjectCard(ProjectEntry& entry, const float cardWidth) {
        ImGui::PushID(&entry);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSplit(2);
        drawList->ChannelsSetCurrent(1);

        const ImVec2 cardTopLeft = ImGui::GetCursorScreenPos();
        const bool isSelected = (selectedProjectPath == entry.folderPath);

        ImGui::BeginGroup();
        ImGui::Indent(12.0f);
        ImGui::Dummy(ImVec2(1.0f, 6.0f));

        const ImVec2 nameBlockCursor = ImGui::GetCursorScreenPos();

        const bool selectableClicked = ImGui::Selectable("##card_select", isSelected,
            ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cardWidth * 0.6f, 40.0f));

        if (selectableClicked) {
            selectedProjectPath = entry.folderPath;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !entry.isMissing) {
                RequestOpenProject(entry);
            }
        }

        ImGui::SetCursorScreenPos(nameBlockCursor);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(entry.name.c_str());

        std::string subText;
        if (entry.isMissing) {
            subText = Localisation::Get("launcher.missing_project");
        } else if (entry.isImported) {
            subText = Localisation::Get("launcher.imported_project_label") + "   " + FormatRelativeTime(entry.lastOpenedUnix);
        } else {
            subText = FormatRelativeTime(entry.lastOpenedUnix);
        }
        ImGui::TextDisabled("%s", subText.c_str());
        ImGui::EndGroup();

        ImGui::Dummy(ImVec2(1.0f, 4.0f));

        if (entry.isMissing) {
            if (ImGui::SmallButton(Localisation::Get("launcher.locate_project").c_str())) {
                TriggerLocateProjectDialog(entry.folderPath);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(Localisation::Get("launcher.remove_from_list").c_str())) {
                projectPendingDelete = entry.folderPath;
                projectPendingDeleteIsImported = true;
                wantOpenDeleteConfirmPopup = true;
            }
        }
        else {
            if (ImGui::SmallButton(Localisation::Get("launcher.open_project").c_str())) {
                RequestOpenProject(entry);
            }
            ImGui::SameLine();

            const std::string pinLabel = entry.isPinned
                ? Localisation::Get("launcher.unpin_project")
                : Localisation::Get("launcher.pin_project");
            if (ImGui::SmallButton(pinLabel.c_str())) {
                entry.isPinned = !entry.isPinned;
                SaveProjectMetaField(entry.folderPath, "pinned", entry.isPinned);
                visibleProjectListDirty = true;
            }
            ImGui::SameLine();

            if (!entry.isImported) {
                if (ImGui::SmallButton(Localisation::Get("launcher.rename_project").c_str())) {
                    projectBeingRenamed = entry.folderPath;
                    std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
                    std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", entry.name.c_str());
                    renameValidationError.clear();
                    wantOpenRenamePopup = true;
                }
                ImGui::SameLine();
            }

            if (ImGui::SmallButton(Localisation::Get("launcher.show_in_folder").c_str())) {
                RevealInFileManager(entry.folderPath);
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.24f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.16f, 0.16f, 1.0f));

            const std::string deleteLabel = entry.isImported
                ? Localisation::Get("launcher.remove_from_list")
                : Localisation::Get("launcher.delete_project");
            if (ImGui::SmallButton(deleteLabel.c_str())) {
                projectPendingDelete = entry.folderPath;
                projectPendingDeleteIsImported = entry.isImported;
                wantOpenDeleteConfirmPopup = true;
            }

            ImGui::PopStyleColor(3);
        }

        ImGui::Dummy(ImVec2(1.0f, 6.0f));
        ImGui::Unindent(12.0f);
        ImGui::EndGroup();

        const ImVec2 cardBottomRight(cardTopLeft.x + cardWidth, ImGui::GetItemRectMax().y);

        drawList->ChannelsSetCurrent(0);
        ImU32 cardColor;
        if (entry.isMissing) cardColor = IM_COL32(55, 40, 40, 200);
        else if (isSelected) cardColor = IM_COL32(55, 85, 120, 220);
        else cardColor = IM_COL32(38, 38, 44, 200);
        drawList->AddRectFilled(cardTopLeft, cardBottomRight, cardColor, 6.0f);
        if (entry.isPinned && !entry.isMissing) {
            drawList->AddRect(cardTopLeft, cardBottomRight, IM_COL32(210, 175, 80, 220), 6.0f, 0, 1.5f);
        }
        drawList->ChannelsMerge();

        ImGui::Dummy(ImVec2(1.0f, 6.0f));

        ImGui::PopID();
    }

    // ============================================================================
    // Scrollable, searched + sorted project list
    // ============================================================================
    void RebuildVisibleProjectList() {
        visibleProjectEntries.clear();
        visibleProjectEntries.reserve(projectEntries.size());

        const std::string searchLower = ToLowerCopy(searchBuffer);
        for (auto& entry : projectEntries) {
            if (!searchLower.empty() && ToLowerCopy(entry.name).find(searchLower) == std::string::npos) {
                continue;
            }
            visibleProjectEntries.push_back(&entry);
        }

        std::stable_sort(visibleProjectEntries.begin(), visibleProjectEntries.end(), [](const ProjectEntry* a, const ProjectEntry* b) {
            if (a->isMissing != b->isMissing) return !a->isMissing && b->isMissing;
            if (a->isPinned != b->isPinned) return a->isPinned && !b->isPinned;

            switch (currentSortMode) {
                case SortMode::Name:
                    return CaseInsensitiveLess(a->name, b->name);
                case SortMode::DateModified:
                    if (a->lastModified != b->lastModified) return a->lastModified > b->lastModified;
                    return CaseInsensitiveLess(a->name, b->name);
                case SortMode::RecentlyOpened:
                default:
                    if (a->lastOpenedUnix != b->lastOpenedUnix) return a->lastOpenedUnix > b->lastOpenedUnix;
                    return CaseInsensitiveLess(a->name, b->name);
            }
        });

        visibleProjectListDirty = false;
    }

    void DrawProjectList(const float availableHeight) {
        if (projectListDirty) {
            RefreshProjectList();
        }

        ProcessPendingImport();
        ProcessPendingLocate();

        if (visibleProjectListDirty) {
            RebuildVisibleProjectList();
        }

        ImGui::BeginChild("##project_scroll_region", ImVec2(0.0f, availableHeight), true);

        if (visibleProjectEntries.empty()) {
            ImGui::Spacing();
            if (projectEntries.empty()) {
                ImGui::TextDisabled("%s", Localisation::Get("launcher.no_projects_title").c_str());
                ImGui::TextDisabled("%s", Localisation::Get("launcher.no_projects_hint").c_str());
            } else {
                ImGui::TextDisabled("%s", Localisation::Get("launcher.no_search_results").c_str());
            }
        } else {
            const float cardWidth = ImGui::GetContentRegionAvail().x;
            for (ProjectEntry* entry : visibleProjectEntries) {
                DrawProjectCard(*entry, cardWidth);
            }
        }

        ImGui::EndChild();
    }

    // ============================================================================
    // Footer: quit, shortcut hints, language selector
    // ============================================================================
    void DrawFooter() {
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(Localisation::Get("launcher.quit").c_str())) {
            quitRequested = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", Localisation::Get("launcher.shortcuts_hint").c_str());

        ImGui::SameLine();

        if (languages.empty()) {
            RefreshLanguages();
        }

        const std::string currentLangCode = Localisation::CurrentLanguage();
        const std::string currentLangDisplay = languages.count(currentLangCode)
            ? languages.at(currentLangCode)
            : currentLangCode;

        constexpr float comboWidth = 160.0f;
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        if (availableWidth > comboWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availableWidth - comboWidth);
        }

        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::BeginCombo("##language_select", currentLangDisplay.c_str())) {
            for (const auto& [code, display] : languages) {
                const bool isSelected = (code == currentLangCode);
                if (ImGui::Selectable(display.c_str(), isSelected)) {
                    if (code != currentLangCode) {
                        if (Localisation::LoadLanguage(code)) {
                            spdlog::info("Changed language to {}", code);
                            SDL_SetWindowTitle(window, Localisation::Get("launcher.name").c_str());
                        } else {
                            spdlog::error("Failed to change language to {}", code);
                            PushToast(Localisation::Get("launcher.toast_language_failed"), ToastLevel::Error);
                        }
                    }
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // ============================================================================
    // Keyboard shortcuts (gated behind !WantTextInput so typing in the search box
    // or a modal's text field never gets hijacked).
    // ============================================================================
    void HandleGlobalShortcuts() {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return;

        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            projectListDirty = true;
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
            std::fill(newProjectNameBuffer.begin(), newProjectNameBuffer.end(), '\0');
            newProjectValidationError.clear();
            wantOpenNewProjectPopup = true;
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
            focusSearchBoxRequested = true;
        }

        if (!selectedProjectPath.empty()) {
            const auto it = std::find_if(projectEntries.begin(), projectEntries.end(),
                [](const ProjectEntry& e) { return e.folderPath == selectedProjectPath; });

            if (it != projectEntries.end()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    projectPendingDelete = it->folderPath;
                    projectPendingDeleteIsImported = it->isImported;
                    wantOpenDeleteConfirmPopup = true;
                }

                if (!it->isMissing &&
                    (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
                    RequestOpenProject(*it);
                }
            }
        }
    }
}

namespace LauncherApp {
    void Start(json& sharedLauncherVariables) {
        launcherVariables = &sharedLauncherVariables;

        launcherStarted = false;

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            spdlog::critical("SDL_Init failed: {}", SDL_GetError());
            return;
        }

        int desiredWidth = DEFAULT_LAUNCHER_WIDTH;
        int desiredHeight = DEFAULT_LAUNCHER_HEIGHT;
        if (launcherVariables->contains("window") && (*launcherVariables)["window"].is_object()) {
            desiredWidth = (*launcherVariables)["window"].value("width", DEFAULT_LAUNCHER_WIDTH);
            desiredHeight = (*launcherVariables)["window"].value("height", DEFAULT_LAUNCHER_HEIGHT);
        }

        ClampLauncherWindowSizeToDisplay(desiredWidth, desiredHeight);

        if (!SDL_CreateWindowAndRenderer(
            "Tilky_Engine Launcher",
            windowWidth,
            windowHeight,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer
        )) {
            spdlog::critical("Failed to create window or renderer for launcher: {}", SDL_GetError());
            SDL_Quit();
            return;
        }

        CenterLauncherWindowOnDisplay();

        const char* rendererName = SDL_GetRendererName(renderer);
        spdlog::info("SDL renderer: {}", rendererName != nullptr ? rendererName : "<unknown>");

        rendererVSyncEnabled = SDL_SetRenderVSync(renderer, 1);
        if (!rendererVSyncEnabled) spdlog::warn("Failed to enable renderer VSync: {}", SDL_GetError());

        const fs::path basePath = ProjectManager::GetEngineBasePath();

        const fs::path iconPath = basePath / "LauncherAssets" / "Fox.png";
        spdlog::info("Loading window icon from: {}", iconPath.string());

        if (!fs::exists(iconPath)) spdlog::error("Icon file does not exist at {}", iconPath.string());
        else {
            SDL_Surface* windowIcon = IMG_Load(iconPath.string().c_str());

            if (windowIcon == nullptr) spdlog::error("Failed to load window icon: {}", SDL_GetError());
            else {
                if (!SDL_SetWindowIcon(window, windowIcon))
                    spdlog::error("Failed to set launcher window icon, {}", SDL_GetError());

                SDL_DestroySurface(windowIcon);
            }
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // this window is always repositioned/resized by us each frame; no per-widget layout is worth persisting

        const fs::path fontPath = ProjectManager::FindAssetPath("EngineAssets/Fonts/Notosans.ttf");

        spdlog::info("Loading ImGui font from: {}", fontPath.string());

        io.Fonts->AddFontFromFileTTF(
            fontPath.string().c_str(),
            18.0f
        );

        ImGui::StyleColorsDark();
        ApplyLauncherStyle();

        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        const std::string langCode = launcherVariables->value("lang", std::string("en"));
        Localisation::LoadLanguage(langCode);
        RefreshLanguages();

        SDL_SetWindowTitle(window, Localisation::Get("launcher.name").c_str());

        const std::string sortModeStr = launcherVariables->value("sortMode", std::string("recent"));
        if (sortModeStr == "name") currentSortMode = SortMode::Name;
        else if (sortModeStr == "modified") currentSortMode = SortMode::DateModified;
        else currentSortMode = SortMode::RecentlyOpened;

        // Background/logo texture
        const fs::path logoPath = basePath / "LauncherAssets" / "LogoWithWhiteText.png";

        if (!fs::exists(logoPath)) {
            spdlog::error("Background logo does not exists at: {}", logoPath.string());
        } else {
            logo = IMG_LoadTexture(renderer, logoPath.string().c_str());

            if (logo == nullptr) {
                spdlog::error("Failed to load logo {}", SDL_GetError());
            } else {
                SDL_SetTextureBlendMode(logo, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(logo, 145);
            }
        }

        projectListDirty = true;
        launcherStarted = true;
    }

    void Update() {
        const Uint64 frameStart = SDL_GetTicksNS();

        if (!launcherStarted) {
            // Start() never completed successfully; make sure the main loop in
            // LauncherMain.cpp exits immediately instead of calling into a null window.
            quitRequested = true;
            return;
        }

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                quitRequested = true;
            }
        }

        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        if (launcherVariables != nullptr) {
            (*launcherVariables)["window"]["width"] = windowWidth;
            (*launcherVariables)["window"]["height"] = windowHeight;
        }

        SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
        SDL_RenderClear(renderer);

        // Draw the watermark logo BEFORE the ImGui draw data so it sits behind the UI
        // (previously this was drawn after ImGui's render call and painted over everything).
        DrawBackgroundLogo();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        UpdateToasts(ImGui::GetIO().DeltaTime);

        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

        ImGui::Begin(Localisation::Get("launcher.name").c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        HandleGlobalShortcuts();

        DrawTopBar();

        constexpr float FOOTER_RESERVED_HEIGHT = 60.0f;
        const float listHeight = std::max(ImGui::GetContentRegionAvail().y - FOOTER_RESERVED_HEIGHT, 80.0f);
        DrawProjectList(listHeight);

        DrawFooter();

        DrawNewProjectModal();
        DrawRenameModal();
        DrawDeleteConfirmModal();

        ImGui::End();

        DrawToasts();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);

        // SDL_RenderPresent blocks when VSync is active. If the current driver cannot
        // enable VSync, cap the launcher manually instead of running an unrestricted
        // busy loop that consumes an entire CPU core/GPU queue.
        if (!rendererVSyncEnabled) {
            const Uint64 frameElapsed = SDL_GetTicksNS() - frameStart;
            if (frameElapsed < TARGET_FRAME_NS) {
                SDL_DelayNS(TARGET_FRAME_NS - frameElapsed);
            }
        }
    }

    bool QuitRequested() {
        return quitRequested;
    }

    fs::path GetPendingProjectToOpen() {
        return pendingProjectToOpen;
    }

    void Destroy() {
        if (!launcherStarted) return;
        launcherStarted = false;

        if (logo != nullptr) {
            SDL_DestroyTexture(logo);
            logo = nullptr;
        }

        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }

        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();

        launcherVariables = nullptr;
        rendererVSyncEnabled = false;
    }
}