/* TilkyExporter — C++ replacement for the Rust tilky_exporter tool.
 *
 * This standalone CLI copies engine & project folders into the desired
 * export location passed via command-line arguments.
 *
 * Arguments:
 *   1. Path to the current project's project.tilky file.
 *   2. Path to the desired export folder.
 *   3. Path to Standalone.exe.
 *   4+. Options, in any order:
 *      --renderer=opengl|vulkan  the graphics API the exported game draws
 *          with (written into its project.tilky). Without it the game keeps
 *          the project's own setting.
 *      --platforms=windows-x64,linux-x64,macos-arm64  which platforms to
 *          export. Each platform's Standalone comes from the engine's
 *          Runtimes/<platform>/ folder (this machine's own may also be the
 *          one beside the engine). Windows alone exports straight into the
 *          folder; otherwise each platform gets a subfolder (Windows/,
 *          Linux/, macOS/<Game>.app) and Linux/macOS also a <Game>-<OS>.zip
 *          that keeps the executable permission. Without it: this machine's
 *          platform only, straight into the folder, as before.
 *
 * Assets/ is copied as a whole. On top of that every model a level uses is
 * checked: textures and side files (.mtl, .bin, ...) that live outside
 * Assets/ are copied next to the exported model, where ModelLoader looks for
 * them, and anything missing is reported by name.
 *
 * Scripts are precompiled: each Assets/<id>.lua becomes <id>.luac (Lua
 * bytecode of the LuaScriptCompiler output) plus <id>.fields.json (its public
 * fields), and the source is not shipped. A script that doesn't compile fails
 * the export.
 *
 * Progress goes to stdout as "@progress <0-100>" lines, which the editor reads
 * to fill its export bar.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include "Headers/Engine/AssetFS.hpp"
#include "Headers/Launcher/Sha256.hpp"
#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Prefab.hpp"
#include "Headers/Objects/UILayouts.hpp"
#include "Headers/Project/AssetSets.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptCompiler.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

// Work is measured in bytes copied. A level, prefab, script or model is parsed
// rather than copied, so it counts as this many bytes. It's only a rough match
// for how long those take, so the bar is an estimate, not a clock.
static constexpr std::uint64_t kParsedFileWork = 8 * 1024 * 1024;

static std::uint64_t progressTotal = 1;
static std::uint64_t progressDone = 0;
static std::uint64_t packWork = 0; // writing Data.pak, about the data's size
static int progressReported = -1;

static void ReportProgress() {
    const int percent = static_cast<int>(std::min<std::uint64_t>(progressDone * 100 / progressTotal, 100));

    // Never backwards: the total can still grow once the models are known.
    if (percent <= progressReported) return;
    progressReported = percent;

    // Flushed, because stdout is a pipe when the editor runs the exporter.
    std::cout << "@progress " << percent << std::endl;
}

static void AddProgress(const std::uint64_t work) {
    progressDone += work;
    ReportProgress();
}

// Everything the export will do: all files under each folder, plus the parsing
// of levels, prefabs and scripts in Assets.
static void MeasureFolderWork(const fs::path& folder, const bool isAssets) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc)) continue;

        const std::uintmax_t size = it->file_size(entryEc);
        if (!entryEc) progressTotal += size;

        if (!isAssets) continue;

        const fs::path extension = it->path().extension();
        if (extension == ".bson") progressTotal += 2 * kParsedFileWork; // stripped, then checked for models
        else if (extension == Prefab::kExtension || extension == ".lua") progressTotal += kParsedFileWork;
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool RecreateExportDirectory(const fs::path& directory) {
    std::error_code ec;

    fs::remove_all(directory, ec);
    if (ec) {
        std::cerr << "Failed to clean export folder: " << directory << " — " << ec.message() << "\n";
        return false;
    }

    fs::create_directories(directory, ec);
    if (ec) {
        std::cerr << "Failed to create export folder: " << directory << " — " << ec.message() << "\n";
        return false;
    }

    return true;
}

static bool CopyFileChecked(const fs::path &from, const fs::path &to) {
    if (!fs::exists(from)) {
        std::cerr << "Source file does not exist: " << from << "\n";
        return false;
    }
    if (!fs::is_regular_file(from)) {
        std::cerr << "Source is not a file: " << from << "\n";
        return false;
    }

    if (to.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        if (ec) {
            std::cerr << "Failed to create folder: " << to.parent_path() << " — " << ec.message() << "\n";
            return false;
        }
    }

    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Failed to copy file from " << from << " to " << to << " — " << ec.message() << "\n";
        return false;
    }

    const std::uintmax_t size = fs::file_size(to, ec);
    if (!ec) AddProgress(size);

    return true;
}

static bool CopyDirRecursive(const fs::path &from, const fs::path &to) {
    if (!fs::exists(from)) {
        std::cerr << "Source directory does not exist: " << from << "\n";
        return false;
    }
    if (!fs::is_directory(from)) {
        std::cerr << "Source is not a directory: " << from << "\n";
        return false;
    }

    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) {
        std::cerr << "Failed to create destination folder: " << to << " — " << ec.message() << "\n";
        return false;
    }

    for (const auto &entry : fs::directory_iterator(from)) {
        const fs::path destPath = to / entry.path().filename();

        if (entry.is_directory()) {
            if (!CopyDirRecursive(entry.path(), destPath)) return false;
        }
        else {
            if (!CopyFileChecked(entry.path(), destPath)) return false;
        }
    }
    return true;
}

// Shared libraries the game loads at runtime: .dll on Windows, and on macOS
// .dylib (libMoltenVK.dylib, the Vulkan driver there).
static bool IsRuntimeLibrary(const fs::path &path) {
    std::string ext = path.extension().string();
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".dll" || ext == ".dylib";
}

static bool CopyAllDlls(const fs::path &from, const fs::path &to) {
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(from, ec)) {
        if (ec) {
            std::cerr << "Failed to read runtime folder: " << from
                      << " — " << ec.message() << "\n";
            return false;
        }

        if (!entry.is_regular_file()) continue;

        if (!IsRuntimeLibrary(entry.path())) continue;

        const fs::path destination = to / entry.path().filename();
        if (!CopyFileChecked(entry.path(), destination))
            return false;

        std::cout << "Copied DLL from " << entry.path() << " to " << destination << "\n";
    }
    return true;
}

static bool IsInsideDirectory(const fs::path& path, const fs::path& directory) {
    std::error_code ec;
    const fs::path canonicalPath = fs::weakly_canonical(path, ec);
    if (ec) return false;

    const fs::path canonicalDirectory = fs::weakly_canonical(directory, ec);
    if (ec) return false;

    const fs::path relative = canonicalPath.lexically_relative(canonicalDirectory);
    return !relative.empty() && *relative.begin() != "..";
}

// Model reference -> the levels and prefabs using it ("level X", "prefab Y").
// Prefabs count too: a script can spawn one into any level.
static std::map<std::string, std::set<std::string>> CollectLevelModels(const fs::path& assetsSrc, std::vector<std::string>& problems) {
    std::map<std::string, std::set<std::string>> usersByModel;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsSrc, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;

        const bool isPrefab = it->path().extension() == Prefab::kExtension;
        if (it->path().extension() != ".bson" && !isPrefab) continue;

        Level level;
        std::string errorMessage;

        const bool loaded = isPrefab
            ? LevelSerialization::LoadPrefabFromFile(it->path(), level, &errorMessage)
            : LevelSerialization::LoadLevelFromFile(it->path(), level, nullptr, &errorMessage);

        AddProgress(kParsedFileWork);

        if (!loaded) {
            problems.push_back(std::string("Could not read ") + (isPrefab ? "prefab " : "level ") + it->path().string() +
                               " to check its models: " + errorMessage);
            continue;
        }

        const std::string user = isPrefab
            ? "prefab " + it->path().lexically_relative(assetsSrc).generic_string()
            : "level " + level.name;

        for (const ComponentModel& model : level.models.components)
            if (!model.fileName.empty()) usersByModel[model.fileName].insert(user);
    }

    if (ec) problems.push_back("Failed to scan " + assetsSrc.string() + " for levels: " + ec.message());

    return usersByModel;
}

static std::string JoinNames(const std::set<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
    return joined;
}

// Returns false only on a hard copy failure. Missing files are collected in
// `problems` so every one of them is reported, not just the first.
static bool ExportModelDependencies(const fs::path& assetsSrc, const fs::path& assetsDest, std::vector<std::string>& problems) {
    const auto usersByModel = CollectLevelModels(assetsSrc, problems);

    progressTotal += usersByModel.size() * kParsedFileWork;

    for (const auto& [reference, levels] : usersByModel) {
        AddProgress(kParsedFileWork);

        const fs::path modelSrc = (assetsSrc / fs::path(std::u8string(reference.begin(), reference.end()))).lexically_normal();
        const std::string usedBy = " (used by " + JoinNames(levels) + ")";

        std::vector<ModelLoader::ModelDependency> dependencies;
        std::string errorMessage;

        if (!ModelLoader::CollectDependencies(modelSrc, assetsSrc, dependencies, errorMessage)) {
            problems.push_back("MISSING model '" + reference + "'" + usedBy + ": " + errorMessage);
            continue;
        }

        const fs::path modelDestDirectory = (assetsDest / modelSrc.lexically_relative(assetsSrc)).parent_path();

        for (const ModelLoader::ModelDependency& dependency : dependencies) {
            const std::string what = dependency.kind == ModelLoader::DependencyKind::Texture ? "texture" : "file";

            if (dependency.status == ModelLoader::DependencyStatus::Embedded) continue;

            if (dependency.status == ModelLoader::DependencyStatus::Missing) {
                std::string problem = "MISSING " + what + " '" + dependency.reference + "' needed by model '" + reference + "'";
                if (!dependency.materialName.empty()) problem += " (material '" + dependency.materialName + "')";

                problems.push_back(problem + usedBy);
                continue;
            }

            // Inside Assets it was already copied with everything else.
            if (IsInsideDirectory(dependency.resolvedPath, assetsSrc)) continue;

            const fs::path destination = modelDestDirectory / dependency.resolvedPath.filename();

            std::error_code ec;
            if (fs::exists(destination, ec)) {
                if (fs::file_size(destination, ec) == fs::file_size(dependency.resolvedPath, ec)) continue;

                problems.push_back(
                    "CONFLICT: external " + what + " " + dependency.resolvedPath.string() +
                    " of model '" + reference + "' was not copied because " + destination.string() +
                    " already exists with different contents"
                );
                continue;
            }

            if (!CopyFileChecked(dependency.resolvedPath, destination)) return false;

            std::cout << "Copied external " << what << " " << dependency.resolvedPath
                      << " of model '" << reference << "' to " << destination << "\n";
        }
    }

    return true;
}

static bool WriteFileChecked(const fs::path& path, const std::string_view contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (file) return true;

    std::cerr << "Failed to write " << path << "\n";
    return false;
}

// Replaces every Assets/<id>.lua in the export with <id>.luac (bytecode, line
// info kept so runtime errors still name a line) and <id>.fields.json.
// Compile errors are collected in `problems`; returns false if there were any
// or a file couldn't be written.
static bool PrecompileScripts(const fs::path& assetsDest, std::vector<std::string>& problems) {
    std::vector<fs::path> scripts;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsDest, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".lua") scripts.push_back(it->path());

    if (ec) {
        problems.push_back("Failed to scan " + assetsDest.string() + " for scripts: " + ec.message());
        return false;
    }

    sol::state lua;
    const std::size_t problemsBefore = problems.size();

    for (const fs::path& script : scripts) {
        AddProgress(kParsedFileWork);

        fs::path idPath = script.lexically_relative(assetsDest);
        const std::string assetId = idPath.replace_extension().generic_string();
        const std::string chunkName = assetId + ".lua";

        std::string source;
        {
            // Closed before the fs::remove below - Windows can't delete an open file.
            std::ifstream file(script, std::ios::binary);
            if (!file) {
                problems.push_back("Failed to read " + chunkName);
                continue;
            }
            source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        const LuaScriptCompiler::Result compiled = LuaScriptCompiler::Compile(source);
        if (!compiled.Succeeded()) {
            for (const LuaScriptCompiler::Diagnostic& error : compiled.errors)
                problems.push_back(chunkName + ":" + std::to_string(error.line) + ": " + error.message);
            continue;
        }

        // Same chunk name the editor uses, so error messages match.
        const sol::load_result loaded = lua.load(compiled.luaSource, "@" + chunkName, sol::load_mode::text);
        if (!loaded.valid()) {
            const sol::error error = loaded;
            problems.push_back(error.what());
            continue;
        }

        const sol::protected_function function = loaded;
        const sol::bytecode bytecode = function.dump();

        fs::path bytecodePath = script;
        bytecodePath.replace_extension(".luac");
        fs::path fieldsPath = script;
        fieldsPath.replace_extension(".fields.json");

        if (!WriteFileChecked(bytecodePath, bytecode.as_string_view())) return false;
        if (!WriteFileChecked(fieldsPath, LevelSerialization::ScriptPublicFieldsToJsonText(compiled.publicFields))) return false;

        if (!fs::remove(script, ec) || ec) {
            problems.push_back("Failed to remove the source of " + chunkName + " from the export: " + ec.message());
            continue;
        }
        std::cout << "Compiled script " << chunkName << "\n";
    }

    return problems.size() == problemsBefore;
}

// Takes the editor-only data (comments, selection groups, grid, wall snaps,
// editor cameras) out of every level in the export; the game never reads it.
// Returns false if a level couldn't be rewritten.
static bool StripEditorDataFromLevels(const fs::path& assetsDest, std::vector<std::string>& problems) {
    std::error_code ec;
    const std::size_t problemsBefore = problems.size();

    for (fs::recursive_directory_iterator it(assetsDest, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".bson") continue;

        bool removed = false;
        std::string errorMessage;

        AddProgress(kParsedFileWork);

        if (!LevelSerialization::StripEditorData(it->path(), &removed, &errorMessage))
            problems.push_back("Failed to strip editor data from " + it->path().string() + ": " + errorMessage);
        else if (removed)
            std::cout << "Stripped editor data from " << it->path().lexically_relative(assetsDest).generic_string() << "\n";
    }

    if (ec) problems.push_back("Failed to scan " + assetsDest.string() + " for levels: " + ec.message());

    return problems.size() == problemsBefore;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// The game opens the first level in project.tilky's list that exists. A project
// with no "levels" at all is older than the list: it opens any level it has.
static bool HasStartLevel(const fs::path& metadataPath, const fs::path& assetsPath) {
    std::ifstream metadataFile(metadataPath);
    const nlohmann::json metadata = nlohmann::json::parse(metadataFile, nullptr, false);

    if (!metadata.is_object()) return true;

    const auto levels = metadata.find("levels");
    if (levels == metadata.end() || !levels->is_array()) return true;

    std::set<std::string> listed;
    for (const nlohmann::json& level : *levels)
        if (level.is_string()) listed.insert(level.get<std::string>());

    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsPath, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".bson" && listed.contains(it->path().stem().string())) return true;

    return false;
}

// ---------------------------------------------------------------------------
// Platforms
// ---------------------------------------------------------------------------

struct ExportPlatform {
    const char* id;     // as in versions.json and the engine's Runtimes/<id>/
    const char* folder; // its subfolder when several platforms are exported
    bool windows;
    bool macos;
};

static constexpr ExportPlatform kPlatforms[] = {
    {"windows-x64", "Windows", true, false},
    {"linux-x64", "Linux", false, false},
    {"macos-arm64", "macOS", false, true},
};

static const ExportPlatform* FindPlatform(const std::string& id) {
    for (const ExportPlatform& platform : kPlatforms)
        if (id == platform.id) return &platform;
    return nullptr;
}

// The platform this exporter runs on: its own Standalone (beside it) is that
// platform's runtime even when the engine has no Runtimes/ folder (a local build).
static std::string HostPlatformId() {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
    return "macos-arm64";
#else
    return "linux-x64";
#endif
}

// A platform's game executable and the libraries it loads.
struct Runtime {
    fs::path directory;
    fs::path executable;
};

static std::optional<Runtime> FindRuntime(const ExportPlatform& platform, const fs::path& standaloneDir, const fs::path& standaloneExe) {
    const fs::path directory = standaloneDir / "Runtimes" / platform.id;
    const fs::path executable = directory / (platform.windows ? "Standalone.exe" : "Standalone");

    std::error_code ec;
    if (fs::is_regular_file(executable, ec)) return Runtime{directory, executable};
    if (HostPlatformId() == platform.id) return Runtime{standaloneDir, standaloneExe};
    return std::nullopt;
}

// The exported executable and runtime libraries, in bytes, for the bar.
static std::uint64_t RuntimeBytes(const Runtime& runtime) {
    std::uint64_t bytes = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(runtime.directory, ec)) {
        std::error_code entryEc;
        if (!IsRuntimeLibrary(entry.path()) && entry.path() != runtime.executable) continue;
        const std::uintmax_t size = entry.file_size(entryEc);
        if (!entryEc) bytes += size;
    }
    return bytes;
}

static std::uint64_t FolderBytes(const fs::path& folder) {
    std::uint64_t bytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc)) continue;
        const std::uintmax_t size = it->file_size(entryEc);
        if (!entryEc) bytes += size;
    }
    return bytes;
}

// UTF-8 for minizip-ng and SDL, which take char paths.
static std::string Utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// The project's name, usable as a file name (the .app and the zips).
static std::string GameName(const fs::path& metadataPath) {
    std::string name;
    {
        std::ifstream file(metadataPath);
        const nlohmann::json metadata = nlohmann::json::parse(file, nullptr, false);
        if (metadata.is_object() && metadata.contains("name") && metadata["name"].is_string())
            name = metadata["name"].get<std::string>();
    }
    if (name.empty()) name = Utf8(metadataPath.parent_path().filename());

    std::string clean;
    for (const char c : name) {
        const bool invalid = static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos;
        clean += invalid ? '_' : c;
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());

    return clean.empty() ? "Game" : clean;
}

static void MarkExecutable([[maybe_unused]] const fs::path& path) {
#ifndef _WIN32
    std::error_code ec;
    fs::permissions(path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
}

// ---------------------------------------------------------------------------
// macOS app bundle
// ---------------------------------------------------------------------------

static std::string XmlEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

static bool WriteInfoPlist(const fs::path& path, const std::string& name, const bool hasIcon) {
    // A reverse-DNS id from the name: letters, digits and dashes only.
    std::string idPart;
    for (const char c : name) {
        const bool keep = std::isalnum(static_cast<unsigned char>(c)) != 0;
        if (keep) idPart += c;
        else if (!idPart.empty() && idPart.back() != '-') idPart += '-';
    }
    while (!idPart.empty() && idPart.back() == '-') idPart.pop_back();
    if (idPart.empty()) idPart = "game";

    std::ofstream out(path, std::ios::trunc);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n<dict>\n"
           "    <key>CFBundleName</key><string>" << XmlEscape(name) << "</string>\n"
           "    <key>CFBundleDisplayName</key><string>" << XmlEscape(name) << "</string>\n"
           "    <key>CFBundleExecutable</key><string>Standalone</string>\n"
           "    <key>CFBundleIdentifier</key><string>com.tilky.game." << idPart << "</string>\n"
           "    <key>CFBundlePackageType</key><string>APPL</string>\n"
           "    <key>CFBundleShortVersionString</key><string>1.0</string>\n"
           "    <key>CFBundleVersion</key><string>1</string>\n";
    if (hasIcon) out << "    <key>CFBundleIconFile</key><string>AppIcon</string>\n";
    out << "    <key>LSMinimumSystemVersion</key><string>11.0</string>\n"
           "    <key>LSApplicationCategoryType</key><string>public.app-category.games</string>\n"
           "    <key>NSHighResolutionCapable</key><true/>\n"
           "</dict>\n</plist>\n";
    out.close();
    return static_cast<bool>(out);
}

// AppIcon.icns: the image letterboxed into a 512x512 PNG, stored as one
// 'ic09' entry (macOS reads PNG data inside .icns).
static bool WriteMacIcon(const fs::path& sourceImage, const fs::path& icnsPath) {
    SDL_Surface* source = IMG_Load(Utf8(sourceImage).c_str());
    if (source == nullptr) return false;

    constexpr int size = 512;
    SDL_Surface* square = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    if (square == nullptr) {
        SDL_DestroySurface(source);
        return false;
    }
    SDL_FillSurfaceRect(square, nullptr, 0);

    const float scale = std::min(static_cast<float>(size) / static_cast<float>(source->w), static_cast<float>(size) / static_cast<float>(source->h));
    const int width = std::max(1, static_cast<int>(static_cast<float>(source->w) * scale));
    const int height = std::max(1, static_cast<int>(static_cast<float>(source->h) * scale));
    const SDL_Rect target{(size - width) / 2, (size - height) / 2, width, height};
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    const bool blitted = SDL_BlitSurfaceScaled(source, nullptr, square, &target, SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(source);

    const fs::path pngPath = fs::path(icnsPath).replace_extension(".png");
    const bool saved = blitted && IMG_SavePNG(square, Utf8(pngPath).c_str());
    SDL_DestroySurface(square);
    if (!saved) return false;

    std::vector<char> png;
    {
        std::ifstream in(pngPath, std::ios::binary);
        png.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::error_code ec;
    fs::remove(pngPath, ec);
    if (png.empty()) return false;

    const auto bigEndian = [](std::ofstream& out, const std::uint32_t value) {
        const char bytes[4] = {static_cast<char>(value >> 24), static_cast<char>(value >> 16), static_cast<char>(value >> 8), static_cast<char>(value)};
        out.write(bytes, 4);
    };

    std::ofstream out(icnsPath, std::ios::binary | std::ios::trunc);
    out.write("icns", 4);
    bigEndian(out, static_cast<std::uint32_t>(16 + png.size()));
    out.write("ic09", 4);
    bigEndian(out, static_cast<std::uint32_t>(8 + png.size()));
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    out.close();
    return static_cast<bool>(out);
}

// ---------------------------------------------------------------------------
// ZIP with Unix permissions
// ---------------------------------------------------------------------------

// Zips `source` as `topName/...`. Entries carry Unix modes, so `executables`
// (paths relative to `source`) stay runnable when unzipped on Linux or macOS,
// even though this may run on Windows, whose folders can't store that.
static bool WriteZip(const fs::path& source, const std::string& topName, const fs::path& zipPath, const std::set<std::string>& executables) {
    std::error_code ec;
    fs::remove(zipPath, ec);

    std::vector<fs::path> entries;
    for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
    if (ec) {
        std::cerr << "Failed to read " << source << " for its zip: " << ec.message() << "\n";
        return false;
    }
    std::sort(entries.begin(), entries.end());

    void* writer = mz_zip_writer_create();
    if (writer == nullptr) return false;
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

    if (mz_zip_writer_open_file(writer, Utf8(zipPath).c_str(), 0, 0) != MZ_OK) {
        mz_zip_writer_delete(&writer);
        std::cerr << "Failed to create " << zipPath << "\n";
        return false;
    }

    const std::time_t now = std::time(nullptr);
    std::vector<char> buffer(1 << 16);

    const auto addEntry = [&](const std::string& name, const std::uint32_t mode, const fs::path* file) {
        mz_zip_file info{};
        info.version_madeby = static_cast<std::uint16_t>((MZ_HOST_SYSTEM_UNIX << 8) | 20);
        info.flag = MZ_ZIP_FLAG_UTF8;
        info.compression_method = file != nullptr ? MZ_COMPRESS_METHOD_DEFLATE : MZ_COMPRESS_METHOD_STORE;
        info.modified_date = now;
        info.external_fa = mode << 16;
        info.filename = name.c_str();

        if (mz_zip_writer_entry_open(writer, &info) != MZ_OK) return false;

        bool ok = true;
        if (file != nullptr) {
            std::ifstream in(*file, std::ios::binary);
            ok = static_cast<bool>(in);
            while (ok && in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = static_cast<std::int32_t>(in.gcount());
                if (count > 0 && mz_zip_writer_entry_write(writer, buffer.data(), count) != count) ok = false;
                AddProgress(static_cast<std::uint64_t>(std::max(count, 0)));
            }
        }

        return mz_zip_writer_entry_close(writer) == MZ_OK && ok;
    };

    bool ok = addEntry(topName + "/", 040755, nullptr);
    for (const fs::path& entry : entries) {
        if (!ok) break;

        const std::u8string relative8 = entry.lexically_relative(source).generic_u8string();
        const std::string relative(reinterpret_cast<const char*>(relative8.data()), relative8.size());
        const std::string name = topName + "/" + relative;

        std::error_code entryEc;
        if (fs::is_directory(entry, entryEc)) ok = addEntry(name + "/", 040755, nullptr);
        else ok = addEntry(name, executables.contains(relative) ? 0100755 : 0100644, &entry);
    }

    if (mz_zip_writer_close(writer) != MZ_OK) ok = false;
    mz_zip_writer_delete(&writer);

    if (!ok) std::cerr << "Failed to write " << zipPath << "\n";
    return ok;
}

// ---------------------------------------------------------------------------
// macOS code signature after the key patch
// ---------------------------------------------------------------------------

// Apple Silicon only runs signed code, and the macOS runtime carries the
// linker's ad-hoc signature: a CodeDirectory of SHA-256 hashes, one per
// page, with nothing signing the directory itself. Patching the key changes
// one or two pages, so their hashes are redone here; without this macOS
// kills the game at launch. A file that isn't a 64-bit Mach-O, or has no
// signature, is left alone.
static std::uint32_t ReadBigEndian32(const std::string& bytes, const size_t at) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3]));
}

static std::uint32_t ReadLittleEndian32(const std::string& bytes, const size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

static bool RehashMachOPages(const fs::path& executable, const size_t patchBegin, const size_t patchSize, std::string& error) {
    std::string bytes;
    {
        std::ifstream in(executable, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    constexpr std::uint32_t MH_MAGIC_64 = 0xFEEDFACF;
    constexpr std::uint32_t LC_CODE_SIGNATURE = 0x1D;
    constexpr std::uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0;
    constexpr std::uint32_t CSMAGIC_CODEDIRECTORY = 0xFADE0C02;
    constexpr std::uint8_t CS_HASHTYPE_SHA256 = 2;

    if (bytes.size() < 32 || ReadLittleEndian32(bytes, 0) != MH_MAGIC_64) return true;

    const std::uint32_t commandCount = ReadLittleEndian32(bytes, 16);
    size_t command = 32;
    size_t signatureOffset = 0;
    for (std::uint32_t i = 0; i < commandCount && command + 16 <= bytes.size(); ++i) {
        const std::uint32_t type = ReadLittleEndian32(bytes, command);
        const std::uint32_t size = ReadLittleEndian32(bytes, command + 4);
        if (type == LC_CODE_SIGNATURE) signatureOffset = ReadLittleEndian32(bytes, command + 8);
        if (size == 0) break;
        command += size;
    }
    if (signatureOffset == 0) return true;

    if (signatureOffset + 12 > bytes.size() || ReadBigEndian32(bytes, signatureOffset) != CSMAGIC_EMBEDDED_SIGNATURE) {
        error = "the macOS runtime's code signature couldn't be read";
        return false;
    }

    bool rehashed = false;
    const std::uint32_t blobCount = ReadBigEndian32(bytes, signatureOffset + 8);
    for (std::uint32_t i = 0; i < blobCount; ++i) {
        const size_t indexEntry = signatureOffset + 12 + static_cast<size_t>(i) * 8;
        if (indexEntry + 8 > bytes.size()) break;

        const size_t directory = signatureOffset + ReadBigEndian32(bytes, indexEntry + 4);
        if (directory + 40 > bytes.size() || ReadBigEndian32(bytes, directory) != CSMAGIC_CODEDIRECTORY) continue;

        const std::uint32_t hashOffset = ReadBigEndian32(bytes, directory + 16);
        const std::uint32_t codeSlots = ReadBigEndian32(bytes, directory + 28);
        const std::uint32_t codeLimit = ReadBigEndian32(bytes, directory + 32);
        const std::uint8_t hashSize = static_cast<std::uint8_t>(bytes[directory + 36]);
        const std::uint8_t hashType = static_cast<std::uint8_t>(bytes[directory + 37]);
        const std::uint8_t pageShift = static_cast<std::uint8_t>(bytes[directory + 39]);

        if (hashType != CS_HASHTYPE_SHA256 || hashSize != 32 || pageShift == 0 || pageShift > 24) {
            error = "the macOS runtime's code signature uses a hash this exporter can't redo";
            return false;
        }

        const size_t pageSize = static_cast<size_t>(1) << pageShift;
        const size_t firstPage = patchBegin / pageSize;
        const size_t lastPage = (patchBegin + patchSize - 1) / pageSize;

        for (size_t page = firstPage; page <= lastPage && page < codeSlots; ++page) {
            const size_t begin = page * pageSize;
            const size_t end = (std::min)(begin + pageSize, static_cast<size_t>(codeLimit));
            const size_t slot = directory + hashOffset + page * hashSize;
            if (end <= begin || slot + hashSize > bytes.size()) continue;

            TilkySha256::Hasher hasher;
            hasher.Update(reinterpret_cast<const std::uint8_t*>(bytes.data() + begin), end - begin);
            const std::array<std::uint8_t, 32> digest = hasher.Finalize();
            std::memcpy(bytes.data() + slot, digest.data(), 32);
            rehashed = true;
        }
    }

    if (!rehashed) {
        error = "the macOS runtime's code signature has no page hashes for the key";
        return false;
    }

    std::ofstream out(executable, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        error = "couldn't write " + executable.string();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The game's data (the same on every platform)
// ---------------------------------------------------------------------------

static bool ExportGameData(const fs::path& metadataPath, const fs::path& projectDir, const fs::path& standaloneDir,
                           const fs::path& dataDir, const std::string& renderer, std::vector<std::string>& modelProblems) {
    // Copy project.tilky
    const fs::path metadataDest = dataDir / "project.tilky";
    if (!CopyFileChecked(metadataPath, metadataDest)) return false;
    std::cout << "Copied project metadata from " << metadataPath
              << " to " << metadataDest << "\n";

    // The renderer picked in the export dialog.
    if (!renderer.empty()) {
        nlohmann::json metadata;
        {
            std::ifstream in(metadataDest);
            metadata = nlohmann::json::parse(in, nullptr, false);
        }
        if (!metadata.is_object()) {
            std::cerr << "Export failed: project.tilky could not be read to set the renderer\n";
            return false;
        }

        metadata["renderer"] = renderer;

        std::ofstream out(metadataDest, std::ios::trunc);
        out << metadata.dump(4);
        out.close();
        if (!out) {
            std::cerr << "Export failed: could not write the renderer into " << metadataDest << "\n";
            return false;
        }
        std::cout << "The exported game draws with " << renderer << "\n";
    }

    // Copy ProjectSettings.json (tag names), if the project has one
    const fs::path settingsSrc = projectDir / "ProjectSettings.json";
    if (fs::exists(settingsSrc)) {
        if (!CopyFileChecked(settingsSrc, dataDir / "ProjectSettings.json")) return false;
        std::cout << "Copied project settings from " << settingsSrc << "\n";
    }

    // Copy Assets/
    const fs::path assetsSrc  = projectDir / "Assets";
    const fs::path assetsDest = dataDir / "Assets";
    if (!CopyDirRecursive(assetsSrc, assetsDest)) return false;
    std::cout << "Copied assets from " << assetsSrc
              << " to " << assetsDest << "\n";

    std::vector<std::string> levelProblems;
    if (!StripEditorDataFromLevels(assetsDest, levelProblems)) {
        std::cerr << "\nExport failed: " << levelProblems.size() << " level problem(s):\n";
        for (const std::string& problem : levelProblems) std::cerr << "  - " << problem << "\n";
        return false;
    }

    if (!ExportModelDependencies(assetsSrc, assetsDest, modelProblems)) return false;

    std::vector<std::string> scriptProblems;
    if (!PrecompileScripts(assetsDest, scriptProblems)) {
        std::cerr << "\nExport failed: " << scriptProblems.size() << " script problem(s):\n";
        for (const std::string& problem : scriptProblems) std::cerr << "  - " << problem << "\n";
        return false;
    }

    // Copy EngineAssets/Fonts
    const fs::path engineAssetsSrc = standaloneDir / "EngineAssets";
    const fs::path engineAssetsDest = dataDir / "EngineAssets";
    if (!CopyDirRecursive(engineAssetsSrc, engineAssetsDest)) return false;
    std::cout << "Copied engine assets from "
              << engineAssetsSrc << " to " << engineAssetsDest << "\n";

    // No Shaders/: they're compiled into the runtime (EmbeddedShaders).

    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Per-level asset sets, Strip and the export report
// ---------------------------------------------------------------------------

struct AssetSetResult {
    std::vector<AssetFS::FileSet> sets;
    std::set<std::string> unresolved;                                    // "script: literal"
    std::vector<std::pair<std::string, std::set<std::string>>> onDemand; // from editor Play
    std::vector<std::string> stripped;
    std::uint64_t strippedBytes = 0;
    size_t levelCount = 0;
    size_t smallestSet = 0;
    size_t largestSet = 0;
    size_t alwaysCount = 0;
};

static size_t CountKept(const std::set<std::string>& files) {
    return static_cast<size_t>(std::count_if(files.begin(), files.end(), [](const std::string& file) {
        return AssetSets::IsImageFile(file) || AssetSets::IsSoundFile(file);
    }));
}

// Each level's set and the Always set from the project's sources, then
// (if the project asks for it) Strip on the staged copy: images, sounds and
// models in no set leave it.
static void BuildAssetSets(const fs::path& projectDir, const fs::path& stagedAssets, AssetSetResult& out) {
    const fs::path assetsSrc = projectDir / "Assets";
    AssetSets::Collector collector(assetsSrc);

    out.onDemand = AssetSets::ReadOnDemandLog(projectDir);
    std::map<std::string, std::set<std::string>> onDemandByLevel(out.onDemand.begin(), out.onDemand.end());

    std::set<std::string> used;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsSrc, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".bson") continue;

        const std::string key = it->path().lexically_relative(assetsSrc).generic_string();
        std::set<std::string> files = collector.ForLevel(it->path());

        // What Play loaded on demand there goes in from now on.
        if (const auto logged = onDemandByLevel.find(key); logged != onDemandByLevel.end())
            for (const std::string& file : logged->second)
                if (fs::exists(assetsSrc / fs::path(file))) files.insert(file);

        const size_t kept = CountKept(files);
        out.smallestSet = out.levelCount == 0 ? kept : std::min(out.smallestSet, kept);
        out.largestSet = std::max(out.largestSet, kept);
        ++out.levelCount;

        used.insert(files.begin(), files.end());
        out.sets.push_back({AssetSets::LEVEL_SET_PREFIX + key, {files.begin(), files.end()}});
    }

    const std::set<std::string> always = collector.Always(projectDir / "ProjectSettings.json");
    out.alwaysCount = CountKept(always);
    used.insert(always.begin(), always.end());
    out.sets.push_back({AssetSets::ALWAYS_SET, {always.begin(), always.end()}});

    out.unresolved = collector.Unresolved();

    if (!AssetSets::Current().stripUnusedOnExport) return;

    for (const std::string& file : collector.AllFiles()) {
        const bool strippable = AssetSets::IsImageFile(file) || AssetSets::IsSoundFile(file) || ModelLoader::IsModelFile(file);
        if (!strippable || used.contains(file) || AssetSets::IsAlwaysLoaded(file)) continue;

        const fs::path staged = stagedAssets / fs::path(file);
        std::error_code sizeError;
        const std::uintmax_t bytes = fs::file_size(staged, sizeError);
        std::error_code removeError;
        if (fs::remove(staged, removeError)) {
            out.stripped.push_back(file);
            if (!sizeError) out.strippedBytes += bytes;
        }
    }
}

static std::string Megabytes(const std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// <Project>/ExportReport.txt (never inside the export), and "@summary" lines
// for the editor's export-finished popup.
static void WriteExportReport(const fs::path& projectDir, const fs::path& destination, const AssetSetResult& result,
                              const std::vector<std::string>& modelProblems) {
    size_t onDemandCount = 0;
    for (const auto& level : result.onDemand) onDemandCount += level.second.size();

    std::vector<std::string> summary;
    summary.push_back(std::to_string(result.levelCount) + " level(s), each loading " +
                      (result.smallestSet == result.largestSet ? std::to_string(result.largestSet)
                                                               : std::to_string(result.smallestSet) + " to " + std::to_string(result.largestSet)) +
                      " texture(s) and sound(s) of its own; " + std::to_string(result.alwaysCount) + " Always Loaded.");
    if (onDemandCount > 0)
        summary.push_back(std::to_string(onDemandCount) + " asset(s) Play loaded on demand, now in their levels' sets.");
    if (!result.unresolved.empty())
        summary.push_back(std::to_string(result.unresolved.size()) + " script name(s) look like asset paths but match no asset.");
    if (AssetSets::Current().stripUnusedOnExport)
        summary.push_back("Strip unused assets: removed " + std::to_string(result.stripped.size()) + " file(s), " + Megabytes(result.strippedBytes) + ".");
    else summary.push_back("Strip unused assets is off: every asset was exported.");
    if (!modelProblems.empty()) summary.push_back(std::to_string(modelProblems.size()) + " model dependency problem(s).");

    const fs::path reportPath = projectDir / "ExportReport.txt";
    std::ofstream report(reportPath, std::ios::trunc);

    const std::time_t now = std::time(nullptr);
    char when[64] = "";
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&now));

    report << "Tilky Engine export report\n" << when << "\nExported to " << destination.string() << "\n\n";
    for (const std::string& line : summary) report << line << "\n";

    report << "\n== Per-level asset sets ==\n"
              "An exported game keeps in memory only the textures and sounds of the level it is in, plus Always Loaded ones.\n"
              "Anything else loads the moment it's used, which can stall a frame. A name a script builds at runtime\n"
              "(\"Textures/\" .. name) can't be seen at export: mark those assets (or their folder) Always Loaded in the Asset Browser.\n";
    for (const AssetFS::FileSet& set : result.sets) {
        std::set<std::string> kept;
        for (const std::string& file : set.files)
            if (AssetSets::IsImageFile(file) || AssetSets::IsSoundFile(file)) kept.insert(file);
        report << "\n" << set.name << " (" << kept.size() << ")\n";
        for (const std::string& file : kept) report << "    " << file << "\n";
    }

    report << "\n== Script names that match no asset ==\n";
    if (result.unresolved.empty()) report << "None.\n";
    for (const std::string& name : result.unresolved) report << "    " << name << "\n";

    report << "\n== Loaded on demand during Play ==\n"
              "Added to their levels' sets in this export. Delete " << AssetSets::OnDemandLogPath(projectDir).string() << " to forget them.\n";
    if (onDemandCount == 0) report << "None.\n";
    for (const auto& [level, assets] : result.onDemand) {
        report << "    " << level << "\n";
        for (const std::string& asset : assets) report << "        " << asset << "\n";
    }

    report << "\n== Strip unused assets ==\n";
    if (!AssetSets::Current().stripUnusedOnExport) report << "Off (Project Settings).\n";
    else if (result.stripped.empty()) report << "Nothing was unused.\n";
    for (const std::string& file : result.stripped) report << "    " << file << "\n";

    if (!modelProblems.empty()) {
        report << "\n== Model dependency problems ==\n";
        for (const std::string& problem : modelProblems) report << "    " << problem << "\n";
    }

    report.close();
    if (report) std::cout << "Wrote the export report to " << reportPath << "\n";
    else std::cout << "Couldn't write the export report to " << reportPath << "\n";

    for (const std::string& line : summary) std::cout << "@summary " << line << "\n";
    std::cout << "@report " << reportPath.string() << std::endl;
}

// Where one platform's export goes.
struct ExportTarget {
    const ExportPlatform* platform = nullptr; // null: an old-style export for this machine
    Runtime runtime;
    fs::path root;    // its folder, or the .app
    fs::path dataDir; // project.tilky, Assets, EngineAssets...
    fs::path exeDir;
    fs::path libDir;
    std::string executableName;
};

// Marks a folder holding several platforms' exports, so a later export knows
// it may clear it (see IsEmptyOrPreviousExport in MapEditorUI.cpp).
static constexpr const char* kExportMarker = ".tilky_export";

int main(int argc, char** argv) {
    static constexpr const char* usage =
        "Usage: tilky_exporter <project_metadata_path> <destination_path> <standalone_exe_path> "
        "[--renderer=opengl|vulkan] [--platforms=windows-x64,linux-x64,macos-arm64]\n";

    if (argc < 4) {
        std::cerr << usage;
        return 1;
    }

    std::string renderer;
    std::vector<const ExportPlatform*> platforms;
    bool platformsGiven = false;

    for (int i = 4; i < argc; ++i) {
        const std::string option = argv[i];

        if (option.rfind("--renderer=", 0) == 0) {
            renderer = option.substr(11);
            if (renderer != "opengl" && renderer != "vulkan") {
                std::cerr << "Unknown option " << option << " (expected --renderer=opengl or --renderer=vulkan)\n";
                return 1;
            }
        }
        else if (option.rfind("--platforms=", 0) == 0) {
            platformsGiven = true;
            std::string list = option.substr(12);
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const std::string id = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!id.empty()) {
                    const ExportPlatform* platform = FindPlatform(id);
                    if (platform == nullptr) {
                        std::cerr << "Unknown platform " << id << "\n" << usage;
                        return 1;
                    }
                    if (std::find(platforms.begin(), platforms.end(), platform) == platforms.end()) platforms.push_back(platform);
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        else {
            std::cerr << "Unknown option " << option << "\n" << usage;
            return 1;
        }
    }

    if (platformsGiven && platforms.empty()) {
        std::cerr << "Export failed: no platform was picked\n";
        return 1;
    }

    const fs::path metadataPath     = argv[1];
    const fs::path destinationPath  = argv[2];
    const fs::path standaloneExePath = argv[3];

    const fs::path projectDir   = metadataPath.parent_path();
    const fs::path standaloneDir = standaloneExePath.parent_path();
    const std::string gameName = GameName(metadataPath);

    // Before the export folder gets cleared.
    if (!HasStartLevel(metadataPath, projectDir / "Assets")) {
        std::cerr << "Export failed: the project's Levels list is empty. "
                     "Drag a level into it in Project Settings.\n";
        return 1;
    }

    // Windows alone (or no --platforms, as before platforms existed) goes
    // straight into the folder. Otherwise each platform gets a subfolder, and
    // Linux/macOS a .zip beside it that keeps the executable runnable.
    const bool flat = !platformsGiven || (platforms.size() == 1 && platforms.front()->windows);

    std::vector<ExportTarget> targets;

    if (!platformsGiven) {
        ExportTarget target;
        target.runtime = Runtime{standaloneDir, standaloneExePath};
        target.root = target.dataDir = target.exeDir = target.libDir = destinationPath;
#ifdef _WIN32
        target.executableName = "Standalone.exe";
#else
        target.executableName = "Standalone";
#endif
        targets.push_back(target);
    }
    else {
        for (const ExportPlatform* platform : platforms) {
            const std::optional<Runtime> runtime = FindRuntime(*platform, standaloneDir, standaloneExePath);
            if (!runtime) {
                std::cerr << "Export failed: this engine has no " << platform->folder << " runtime (" << (standaloneDir / "Runtimes" / platform->id).string()
                          << "). Engine versions installed by the launcher include every platform; a local build only has its own.\n";
                return 1;
            }

            ExportTarget target;
            target.platform = platform;
            target.runtime = *runtime;
            target.executableName = platform->windows ? "Standalone.exe" : "Standalone";

            const fs::path folder = flat ? destinationPath : destinationPath / platform->folder;
            if (platform->macos) {
                target.root = folder / (gameName + ".app");
                target.dataDir = target.root / "Contents" / "Resources";
                target.exeDir = target.root / "Contents" / "MacOS";
                target.libDir = target.root / "Contents" / "Frameworks";
            }
            else target.root = target.dataDir = target.exeDir = target.libDir = folder;

            targets.push_back(target);
        }
    }

    // Progress: the data once as before, copied again per extra platform,
    // each runtime, and each zip's contents.
    MeasureFolderWork(projectDir / "Assets", true);
    MeasureFolderWork(standaloneDir / "EngineAssets", false);
    {
        const std::uint64_t dataBytes = FolderBytes(projectDir / "Assets") + FolderBytes(standaloneDir / "EngineAssets");
        packWork = dataBytes;
        progressTotal += dataBytes * targets.size(); // the pack written, then copied per extra platform

        for (const ExportTarget& target : targets) {
            const std::uint64_t runtimeBytes = RuntimeBytes(target.runtime);
            progressTotal += runtimeBytes;
            if (target.platform != nullptr && !target.platform->windows) progressTotal += dataBytes + runtimeBytes;
        }
    }
    ReportProgress();

    if (!RecreateExportDirectory(destinationPath)) return 1;

    if (!flat) {
        std::ofstream marker(destinationPath / kExportMarker, std::ios::trunc);
        marker << "Tilky Engine game export. Exporting again into this folder replaces everything in it.\n";
    }

    std::vector<std::string> modelProblems;

    // The data is built once in a staging folder, packed into one encrypted
    // Data.pak, then copied for every other platform. No loose file is left
    // in the export (the staging folder goes, also when the export fails).
    const fs::path stagingDir = destinationPath / ".tilky_pack_staging";
    struct StagingCleanup {
        fs::path folder;
        ~StagingCleanup() {
            std::error_code ec;
            fs::remove_all(folder, ec);
        }
    } stagingCleanup{stagingDir};

    // Before the data: the model check reads levels, and their prefab
    // instances are looked up in the project's Assets.
    const bool projectLoaded = ProjectManager::LoadProjectMetaData(metadataPath);

    if (!ExportGameData(metadataPath, projectDir, standaloneDir, stagingDir, renderer, modelProblems)) return 1;

    // UI layouts are found through the project; the sets need their IDs.
    AssetSetResult assetSets;
    if (projectLoaded) {
        UILayouts::Rescan();
        if (!AssetSets::LoadFromProjectSettings(projectDir / "ProjectSettings.json"))
            std::cout << "Couldn't read the asset loading settings; using the defaults\n";
        BuildAssetSets(projectDir, stagingDir / "Assets", assetSets);
        std::cout << "Worked out the asset sets of " << assetSets.levelCount << " level(s)\n";
    }
    else std::cout << "Couldn't read " << metadataPath << " for the asset sets: the game will load every asset at start\n";

    // One key per export, patched into each runtime below.
    const AssetFS::Key packKey = AssetFS::NewKey();
    {
        std::error_code ec;
        fs::create_directories(targets.front().dataDir, ec);

        const fs::path pakPath = targets.front().dataDir / AssetFS::PACK_FILE_NAME;
        std::string packError;
        if (!AssetFS::WritePack(stagingDir, pakPath, packKey, assetSets.sets, packError)) {
            std::cerr << "Export failed: " << packError << "\n";
            return 1;
        }
        AddProgress(packWork);
        std::cout << "Packed the game data into " << pakPath << "\n";

        for (size_t i = 1; i < targets.size(); ++i) {
            fs::create_directories(targets[i].dataDir, ec);
            if (!CopyFileChecked(pakPath, targets[i].dataDir / AssetFS::PACK_FILE_NAME)) return 1;
            std::cout << "Copied the game data for " << targets[i].platform->folder << "\n";
        }
    }

    {
        std::error_code ec;
        fs::remove_all(stagingDir, ec);
    }

    for (const ExportTarget& target : targets) {
        // Runtime libraries (DLLs; MoltenVK on macOS)
        std::error_code ec;
        fs::create_directories(target.libDir, ec);
        if (!CopyAllDlls(target.runtime.directory, target.libDir)) return 1;

        // The game executable
        // TODO: make it so the .exe name is the same as the game name
        const fs::path executable = target.exeDir / target.executableName;
        if (!CopyFileChecked(target.runtime.executable, executable)) return 1;
        MarkExecutable(executable);
        std::cout << "Copied standalone executable from " << target.runtime.executable
                  << " to " << executable << "\n";

        // The key Data.pak opens with.
        {
            std::string keyError;
            size_t slotOffset = 0;
            size_t slotSize = 0;
            if (!AssetFS::PatchKeyIntoExecutable(executable, packKey, keyError, &slotOffset, &slotSize)) {
                std::cerr << "Export failed: " << keyError << "\n";
                return 1;
            }
            if (target.platform != nullptr && target.platform->macos && !RehashMachOPages(executable, slotOffset, slotSize, keyError)) {
                std::cerr << "Export failed: " << keyError << "\n";
                return 1;
            }
            MarkExecutable(executable);
        }

        if (target.platform != nullptr && target.platform->macos) {
            const fs::path iconSource = standaloneDir / "LauncherAssets" / "Fox.png";
            const bool hasIcon = WriteMacIcon(iconSource, target.dataDir / "AppIcon.icns");
            if (!hasIcon) std::cout << "No app icon: couldn't make one from " << iconSource << "\n";

            if (!WriteInfoPlist(target.root / "Contents" / "Info.plist", gameName, hasIcon)) {
                std::cerr << "Export failed: could not write Info.plist\n";
                return 1;
            }
            std::cout << "Made the macOS app " << target.root << "\n";
        }
    }

    // Linux/macOS zips, so the executable bit survives (Windows folders drop it).
    for (const ExportTarget& target : targets) {
        if (target.platform == nullptr || target.platform->windows) continue;

        const bool mac = target.platform->macos;
        const fs::path zipPath = destinationPath / (gameName + "-" + target.platform->folder + ".zip");
        const std::string topName = mac ? gameName + ".app" : gameName;
        const std::set<std::string> executables = {mac ? "Contents/MacOS/Standalone" : "Standalone"};

        if (!WriteZip(target.root, topName, zipPath, executables)) return 1;
        std::cout << "Wrote " << zipPath << "\n";
    }

    progressDone = progressTotal;
    ReportProgress();

    WriteExportReport(projectDir, destinationPath, assetSets, modelProblems);

    if (!modelProblems.empty()) {
        std::cerr << "\nExport completed, but " << modelProblems.size()
                  << " model dependency problem(s) were found. Affected models render without these files:\n";

        for (const std::string& problem : modelProblems) std::cerr << "  - " << problem << "\n";

        return 0;
    }

    std::cout << "Export completed successfully.\n";
    return 0;
}
