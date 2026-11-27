#include "Headers/Project/AssetSets.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Engine/AssetFS.hpp"
#include "Headers/Engine/BuildVariant.hpp"
#include "Headers/Objects/UILayouts.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"

namespace AssetSets {
namespace {
    using json = nlohmann::json;

    std::string Lower(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string ExtensionOf(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
        return Lower(path.substr(dot));
    }

    std::string WithoutExtension(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot == 0) return path;
        return path.substr(0, dot);
    }

    std::string FileNameOf(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string Normalized(std::string name) {
        std::ranges::replace(name, '\\', '/');
        while (name.starts_with("./")) name.erase(0, 2);
        return name;
    }

    const std::unordered_set<std::string>& KnownExtensions() {
        static const std::unordered_set<std::string> extensions = [] {
            std::unordered_set<std::string> list = {".png", ".jpg", ".jpeg", ".wav", ".mp3", ".ogg", ".flac", ".lua", ".prefab",
                                                    ".uilayout", ".fpk", ".bson", ".ttf", ".otf"};
            for (const std::string& model : ModelLoader::SupportedExtensions()) list.insert(model);
            return list;
        }();
        return extensions;
    }

    // Every string literal in Lua source (comments skipped).
    std::vector<std::string> LuaStringLiterals(const std::string& source) {
        std::vector<std::string> literals;
        const size_t size = source.size();

        // [[...]], [==[...]==]: `at` is on the first '['. Returns the end, or npos.
        const auto longBracket = [&](size_t at, std::string* out) -> size_t {
            size_t level = 0;
            size_t i = at + 1;
            while (i < size && source[i] == '=') { ++level; ++i; }
            if (i >= size || source[i] != '[') return std::string::npos;
            const std::string close = "]" + std::string(level, '=') + "]";
            const size_t end = source.find(close, i + 1);
            if (end == std::string::npos) return size;
            if (out != nullptr) *out = source.substr(i + 1, end - i - 1);
            return end + close.size();
        };

        size_t i = 0;
        while (i < size) {
            const char c = source[i];

            if (c == '-' && i + 1 < size && source[i + 1] == '-') {
                if (i + 2 < size && source[i + 2] == '[') {
                    if (const size_t end = longBracket(i + 2, nullptr); end != std::string::npos) { i = end; continue; }
                }
                const size_t newline = source.find('\n', i);
                i = newline == std::string::npos ? size : newline + 1;
                continue;
            }

            if (c == '[') {
                std::string literal;
                if (const size_t end = longBracket(i, &literal); end != std::string::npos) {
                    literals.push_back(std::move(literal));
                    i = end;
                    continue;
                }
                ++i;
                continue;
            }

            if (c == '"' || c == '\'') {
                std::string literal;
                size_t j = i + 1;
                for (; j < size && source[j] != c && source[j] != '\n'; ++j) {
                    if (source[j] == '\\' && j + 1 < size) {
                        const char escaped = source[++j];
                        literal += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
                        continue;
                    }
                    literal += source[j];
                }
                literals.push_back(std::move(literal));
                i = j + 1;
                continue;
            }

            ++i;
        }

        return literals;
    }

    // ---- Settings ----

    Settings settings;

    // ---- The level now playing ----

    struct Session {
        bool haveSet = false;
        bool simulated = false;
        std::string levelKey;
        std::unordered_set<std::string> set;
        std::unordered_set<std::string> warned;
    } session;

    std::uint64_t sessionGeneration = 1;
}

// ============================================================================
// Settings
// ============================================================================

Settings& Current() { return settings; }

bool LoadFromProjectSettings(const fs::path& settingsPath) {
    settings = {};

    try {
        if (!AssetFS::Exists(settingsPath)) return true;

        std::string text;
        if (!AssetFS::ReadFile(settingsPath, text)) {
            spdlog::error("Failed to open project settings: {}", settingsPath.string());
            return false;
        }

        const json root = json::parse(text);
        const auto it = root.find("assetLoading");
        if (it == root.end() || !it->is_object()) return true;

        const json& data = *it;
        if (const auto list = data.find("alwaysLoaded"); list != data.end() && list->is_array())
            for (const json& entry : *list)
                if (entry.is_string()) settings.alwaysLoaded.push_back(Normalized(entry.get<std::string>()));

        settings.stripUnusedOnExport = data.value("stripUnusedOnExport", false);
        settings.loadingScreenLayout = data.value("loadingScreenLayout", std::uint64_t{0});
        settings.loadingProgressVariable = data.value("loadingProgressVariable", std::uint32_t{0});
        return true;
    }
    catch (const std::exception& error) {
        spdlog::error("Project settings: couldn't read \"assetLoading\": {}", error.what());
        return false;
    }
}

void WriteToProjectSettings(json& out) {
    out["assetLoading"] = {
        {"alwaysLoaded", settings.alwaysLoaded},
        {"stripUnusedOnExport", settings.stripUnusedOnExport},
        {"loadingScreenLayout", settings.loadingScreenLayout},
        {"loadingProgressVariable", settings.loadingProgressVariable},
    };
}

bool IsListedAlwaysLoaded(const std::string& assetPath) {
    const std::string path = Normalized(assetPath);
    return std::ranges::find(settings.alwaysLoaded, path) != settings.alwaysLoaded.end();
}

bool IsAlwaysLoaded(const std::string& assetPath) {
    const std::string path = Normalized(assetPath);
    return std::ranges::any_of(settings.alwaysLoaded, [&](const std::string& listed) {
        return path == listed || (path.size() > listed.size() && path.starts_with(listed) && path[listed.size()] == '/');
    });
}

void SetAlwaysLoaded(const std::string& assetPath, const bool alwaysLoaded) {
    const std::string path = Normalized(assetPath);
    std::erase(settings.alwaysLoaded, path);
    if (alwaysLoaded) settings.alwaysLoaded.push_back(path);
    std::ranges::sort(settings.alwaysLoaded);
}

// ============================================================================
// Collector
// ============================================================================

struct Collector::State {
    fs::path assetsRoot;

    std::vector<std::string> files;
    std::unordered_map<std::string, std::string> exact;              // path -> path
    std::unordered_map<std::string, std::string> exactLower;         // lower path -> path
    std::unordered_map<std::string, std::vector<std::string>> stem;  // path without extension
    std::unordered_map<std::string, std::vector<std::string>> named; // file name, with and without extension
    std::unordered_map<std::uint64_t, std::string> layouts;          // UI layout ID -> path

    std::set<std::string> unresolved;

    // One walk's state.
    std::set<std::string>* result = nullptr;
    std::unordered_set<std::string> visited;

    void Add(const std::string& file) {
        // A level names others (Load Level): they bring their own sets.
        if (ExtensionOf(file) == ".bson") return;
        if (!result->insert(file).second) return;
        Walk(file);
    }

    // A string from a level, prefab, layout or flipbook: the whole path, extension optional.
    void MatchExact(const std::string& raw) {
        if (raw.empty() || raw.size() > 400) return;
        const std::string name = Normalized(raw);

        if (const auto it = exact.find(name); it != exact.end()) { Add(it->second); return; }
        if (const auto it = exactLower.find(Lower(name)); it != exactLower.end()) { Add(it->second); return; }
        if (const auto it = stem.find(name); it != stem.end()) {
            for (const std::string& file : it->second) Add(file);
            return;
        }
        MatchPrefix(name);
    }

    // A name that is only the start of asset paths ("UI/Faces/", "UI/Digits/Big "):
    // a script builds the rest at runtime, so every asset it starts is taken.
    // Only with a folder in it, so a word like "S" doesn't take half the project.
    bool MatchPrefix(const std::string& name) {
        if (name.find('/') == std::string::npos) return false;

        bool matched = false;
        for (auto it = std::ranges::lower_bound(files, name); it != files.end() && it->starts_with(name); ++it) {
            matched = true;
            Add(*it);
        }
        return matched;
    }

    // A script's literal: also just the end of the path, from a folder boundary.
    void MatchLoose(const std::string& raw, const std::string& script) {
        if (raw.empty() || raw.size() > 400 || raw.find('\n') != std::string::npos) return;
        const std::string name = Normalized(raw);

        bool matched = false;
        const auto add = [&](const std::string& file) { matched = true; Add(file); };

        if (const auto it = exact.find(name); it != exact.end()) add(it->second);
        else if (const auto it2 = stem.find(name); it2 != stem.end())
            for (const std::string& file : it2->second) add(file);

        if (!matched) matched = MatchPrefix(name);

        if (!matched) {
            if (const auto it = named.find(FileNameOf(name)); it != named.end()) {
                for (const std::string& file : it->second) {
                    const std::string bare = WithoutExtension(file);
                    const auto endsAt = [&](const std::string& path) {
                        return path == name || (path.size() > name.size() && path.ends_with(name) && path[path.size() - name.size() - 1] == '/');
                    };
                    if (endsAt(file) || endsAt(bare)) add(file);
                }
            }
        }

        if (!matched && (name.find('/') != std::string::npos || KnownExtensions().contains(ExtensionOf(name))) &&
            name.find(' ') == std::string::npos && name.find("://") == std::string::npos)
            unresolved.insert(script + ": \"" + raw + "\"");
    }

    void WalkJson(const json& value) {
        switch (value.type()) {
            case json::value_t::string: MatchExact(value.get_ref<const std::string&>()); break;
            case json::value_t::number_unsigned:
            case json::value_t::number_integer:
                if (const auto it = layouts.find(value.get<std::uint64_t>()); it != layouts.end()) Add(it->second);
                break;
            case json::value_t::array:
            case json::value_t::object:
                for (const json& child : value) WalkJson(child);
                break;
            default: break;
        }
    }

    void WalkBson(const fs::path& path) {
        std::vector<unsigned char> bytes;
        if (!AssetFS::ReadFile(path, bytes)) return;
        try {
            WalkJson(json::from_bson(bytes));
        }
        catch (const std::exception&) {} // not ours to report: loading it does
    }

    void Walk(const std::string& file) {
        if (!visited.insert(file).second) return;

        const fs::path path = assetsRoot / fs::path(file);
        const std::string extension = ExtensionOf(file);

        if (extension == ".bson" || extension == ".prefab" || extension == ".uilayout" || extension == ".fpk") {
            WalkBson(path);
        }
        else if (extension == ".lua") {
            std::string source;
            if (AssetFS::ReadFile(path, source))
                for (const std::string& literal : LuaStringLiterals(source)) MatchLoose(literal, file);
        }
        else if (ModelLoader::IsModelFile(path)) {
            std::vector<ModelLoader::ModelDependency> dependencies;
            std::string error;
            if (!ModelLoader::CollectDependencies(path, assetsRoot, dependencies, error)) return;
            for (const ModelLoader::ModelDependency& dependency : dependencies) {
                if (dependency.status != ModelLoader::DependencyStatus::Found) continue;
                const fs::path relative = dependency.resolvedPath.lexically_normal().lexically_relative(assetsRoot.lexically_normal());
                if (!relative.empty() && *relative.begin() != "..") Add(relative.generic_string());
            }
        }
    }
};

Collector::Collector(const fs::path& assetsRoot) : state(std::make_unique<State>()) {
    State& s = *state;
    s.assetsRoot = assetsRoot;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string file = it->path().lexically_relative(assetsRoot).generic_string();
        s.files.push_back(file);
        s.exact.emplace(file, file);
        s.exactLower.emplace(Lower(file), file);
        s.stem[WithoutExtension(file)].push_back(file);
        s.named[FileNameOf(file)].push_back(file);
        const std::string bareName = FileNameOf(WithoutExtension(file));
        if (bareName != FileNameOf(file)) s.named[bareName].push_back(file);
    }
    std::ranges::sort(s.files);

    for (const UILayouts::Info& layout : UILayouts::List()) {
        const fs::path relative = layout.path.lexically_normal().lexically_relative(assetsRoot.lexically_normal());
        if (!relative.empty() && *relative.begin() != "..") s.layouts.emplace(layout.id, relative.generic_string());
    }
}

Collector::~Collector() = default;

std::set<std::string> Collector::ForLevel(const fs::path& levelFile) {
    std::set<std::string> result;
    state->result = &result;
    state->visited.clear();

    const std::string key = levelFile.lexically_normal().lexically_relative(state->assetsRoot.lexically_normal()).generic_string();
    state->visited.insert(key);
    state->WalkBson(levelFile);

    state->result = nullptr;
    return result;
}

std::set<std::string> Collector::Always(const fs::path& projectSettingsFile) {
    std::set<std::string> result;
    state->result = &result;
    state->visited.clear();

    for (const std::string& listed : settings.alwaysLoaded) {
        for (const std::string& file : state->files)
            if (file == listed || (file.size() > listed.size() && file.starts_with(listed) && file[listed.size()] == '/')) {
                // A level in an Always Loaded folder keeps its own set: what
                // it uses isn't Always Loaded because of where it's saved.
                if (ExtensionOf(file) == ".bson") continue;
                if (result.insert(file).second) state->Walk(file);
            }
    }

    std::string text;
    if (AssetFS::ReadFile(projectSettingsFile, text)) {
        try {
            state->WalkJson(json::parse(text));
        }
        catch (const std::exception&) {}
    }

    state->result = nullptr;
    return result;
}

const std::set<std::string>& Collector::Unresolved() const { return state->unresolved; }
const std::vector<std::string>& Collector::AllFiles() const { return state->files; }

std::string LevelKey(const fs::path& levelFile) {
    return levelFile.lexically_normal().lexically_relative(ProjectManager::GetAssetsPath().lexically_normal()).generic_string();
}

bool IsImageFile(const std::string& assetPath) {
    const std::string extension = ExtensionOf(Normalized(assetPath));
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

bool IsSoundFile(const std::string& assetPath) {
    const std::string extension = ExtensionOf(Normalized(assetPath));
    return extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac";
}

// ============================================================================
// The running game
// ============================================================================

bool HasPackedSets() {
    return std::ranges::any_of(AssetFS::PackedSets(), [](const AssetFS::FileSet& set) { return set.name == ALWAYS_SET; });
}

std::set<std::string> PackedNeeds(const std::string& levelKey) {
    std::set<std::string> needs;
    const std::string levelSet = LEVEL_SET_PREFIX + levelKey;

    for (const AssetFS::FileSet& set : AssetFS::PackedSets())
        if (set.name == ALWAYS_SET || set.name == levelSet) needs.insert(set.files.begin(), set.files.end());

    return needs;
}

void BeginLevel(const std::string& levelKey, const bool simulate) {
    ++sessionGeneration;
    session.levelKey = levelKey;
    session.set.clear();
    session.haveSet = false;
    session.simulated = simulate;

    if (simulate) {
        const fs::path assets = ProjectManager::GetAssetsPath();
        Collector collector(assets);
        const std::set<std::string> level = collector.ForLevel(assets / fs::path(levelKey));
        const std::set<std::string> always = collector.Always(ProjectManager::GetProjectFolder() / "ProjectSettings.json");
        session.set.insert(level.begin(), level.end());
        session.set.insert(always.begin(), always.end());
        session.haveSet = true;
        return;
    }

    if (!HasPackedSets()) return;
    const std::set<std::string> needs = PackedNeeds(levelKey);
    session.set.insert(needs.begin(), needs.end());
    session.haveSet = true;
}

void EndSession() {
    ++sessionGeneration;
    session = {};
}

bool HasCurrentSet() { return session.haveSet; }
std::uint64_t SessionGeneration() { return sessionGeneration; }

bool InCurrentSet(const std::string& assetPath) {
    return !session.haveSet || session.set.contains(Normalized(assetPath));
}

fs::path OnDemandLogPath(const fs::path& projectFolder) {
    return projectFolder / ".tilky" / "OnDemandAssets.json";
}

std::vector<std::pair<std::string, std::set<std::string>>> ReadOnDemandLog(const fs::path& projectFolder) {
    std::vector<std::pair<std::string, std::set<std::string>>> log;

    std::ifstream file(OnDemandLogPath(projectFolder), std::ios::binary);
    if (!file) return log;

    try {
        const json data = json::parse(file);
        if (!data.is_object()) return log;
        for (const auto& [level, assets] : data.items()) {
            std::set<std::string> names;
            if (assets.is_array())
                for (const json& asset : assets)
                    if (asset.is_string()) names.insert(asset.get<std::string>());
            log.emplace_back(level, std::move(names));
        }
    }
    catch (const std::exception& error) {
        spdlog::warn("Couldn't read {}: {}", OnDemandLogPath(projectFolder).string(), error.what());
    }

    return log;
}

void NoteOnDemandUse(const std::string& assetPath, const char* kind) {
    if (!session.haveSet || assetPath.empty()) return;

    const std::string name = Normalized(assetPath);
    if (session.set.contains(name) || !session.warned.insert(name).second) return;

    spdlog::warn("{} '{}' isn't in level '{}''s asset set, so the exported game loads it the moment it's used (a short stall). "
                 "Mark it Always Loaded in the Asset Browser if a script names it at runtime.",
                 kind, name, session.levelKey);

    if (!session.simulated) return;

    // Remembered for the next export: it puts these in the level's set and lists them in the report.
    const fs::path projectFolder = ProjectManager::GetProjectFolder();
    json data = json::object();
    for (const auto& [level, assets] : ReadOnDemandLog(projectFolder)) data[level] = assets;

    std::set<std::string> assets = data.contains(session.levelKey) ? data[session.levelKey].get<std::set<std::string>>() : std::set<std::string>{};
    assets.insert(name);
    data[session.levelKey] = assets;

    const fs::path path = OnDemandLogPath(projectFolder);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out) out << data.dump(2);
}
}
