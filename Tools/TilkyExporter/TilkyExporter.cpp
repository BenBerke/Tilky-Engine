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
#include <cctype>
#include <cstdint>
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

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Prefab.hpp"
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

    // Copy Shaders/
    const fs::path shadersSrc  = standaloneDir / "Shaders";
    const fs::path shadersDest = dataDir / "Shaders";
    if (!CopyDirRecursive(shadersSrc, shadersDest)) return false;
    std::cout << "Copied shaders from " << shadersSrc
              << " to " << shadersDest << "\n";

    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// Where one platform's export goes.
struct ExportTarget {
    const ExportPlatform* platform = nullptr; // null: an old-style export for this machine
    Runtime runtime;
    fs::path root;    // its folder, or the .app
    fs::path dataDir; // project.tilky, Assets, Shaders...
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
    MeasureFolderWork(standaloneDir / "Shaders", false);
    {
        const std::uint64_t dataBytes = FolderBytes(projectDir / "Assets") + FolderBytes(standaloneDir / "EngineAssets") + FolderBytes(standaloneDir / "Shaders");
        progressTotal += dataBytes * (targets.size() - 1);

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

    // The data is built once, then copied for every other platform.
    if (!ExportGameData(metadataPath, projectDir, standaloneDir, targets.front().dataDir, renderer, modelProblems)) return 1;

    for (size_t i = 1; i < targets.size(); ++i) {
        if (!CopyDirRecursive(targets.front().dataDir, targets[i].dataDir)) return 1;
        std::cout << "Copied the game data for " << targets[i].platform->folder << "\n";
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

    if (!modelProblems.empty()) {
        std::cerr << "\nExport completed, but " << modelProblems.size()
                  << " model dependency problem(s) were found. Affected models render without these files:\n";

        for (const std::string& problem : modelProblems) std::cerr << "  - " << problem << "\n";

        return 0;
    }

    std::cout << "Export completed successfully.\n";
    return 0;
}
