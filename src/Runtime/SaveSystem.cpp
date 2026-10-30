#include "Headers/Runtime/SaveSystem.hpp"

#include "Headers/Project/ProjectManager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

#ifdef TILKY_STANDALONE
    constexpr const char* EXTENSION = ".bson";
#else
    constexpr const char* EXTENSION = ".json";
#endif

    struct SaveFile {
        json values = json::object();
        bool exists = false;        // on disk, or will be after the next Flush
        bool dirty = false;         // changed since the last Flush
        bool deletePending = false; // DeleteSaveFile: the next Flush removes it
    };

    std::unordered_map<std::string, SaveFile> files;

    // Folder() asks SDL in the exported game, which creates the folder;
    // once per session is enough.
    std::optional<fs::path> folder;

    fs::path PathOf(const std::string& fileName) {
        return SaveSystem::Folder() / (fileName + EXTENSION);
    }

    // A file that can't be read (or was written by a newer engine) is moved
    // aside instead of being overwritten by the next save, so nothing is
    // lost: "progress.json" becomes "progress.json.unreadable".
    void SetAside(const fs::path& path, const std::string& reason) {
        fs::path aside = path;
        aside += ".unreadable";

        std::error_code ec;
        fs::rename(path, aside, ec);

        if (ec) spdlog::error("Save file '{}' {} and couldn't be moved aside: {}", path.string(), reason, ec.message());
        else spdlog::error("Save file '{}' {}; it was renamed to '{}' and the save starts empty", path.string(), reason, aside.filename().string());
    }

    std::optional<json> ReadDocument(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;

        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

        try {
#ifdef TILKY_STANDALONE
            return json::from_bson(bytes);
#else
            return json::parse(bytes);
#endif
        }
        catch (const json::exception&) {
            return std::nullopt;
        }
    }

    // The file, read from disk the first time it is used.
    SaveFile& Load(const std::string& fileName) {
        if (const auto it = files.find(fileName); it != files.end()) return it->second;

        SaveFile& file = files[fileName];
        const fs::path path = PathOf(fileName);

        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) return file;

        const std::optional<json> document = ReadDocument(path);

        if (!document || !document->is_object() || !document->contains("values") || !(*document)["values"].is_object()) {
            SetAside(path, "can't be read");
            return file;
        }

        if (document->value("format", 0) > SaveSystem::FORMAT_VERSION) {
            SetAside(path, "was saved by a newer version of the engine");
            return file;
        }

        file.values = (*document)["values"];
        file.exists = true;
        return file;
    }

    // To a temporary file first, then over the old one, so a crash mid-write
    // leaves the old save or the new one, never half of each.
    bool Write(const fs::path& path, const json& values) {
        const json document = {{"format", SaveSystem::FORMAT_VERSION}, {"values", values}};

        fs::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return false;

#ifdef TILKY_STANDALONE
            const std::vector<std::uint8_t> bytes = json::to_bson(document);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
#else
            out << document.dump(4);
#endif
            if (!out) return false;
        }

        std::error_code ec;
        fs::rename(temporary, path, ec);
        if (!ec) return true;

        fs::remove(temporary, ec);
        return false;
    }
}

namespace SaveSystem {
    fs::path Folder() {
        if (!folder) {
#ifdef TILKY_STANDALONE
            folder = ProjectManager::GetUserSavesPath();
#else
            folder = ProjectManager::GetProjectSavesPath();
#endif
        }
        return *folder;
    }

    std::string FileNameProblem(const std::string& fileName) {
        if (fileName.empty()) return "the file name is empty";
        if (fileName.size() > 100) return "the file name is longer than 100 characters";
        if (fileName.front() == '.') return "a file name can't start with '.'";
        if (fileName.back() == '.' || fileName.back() == ' ') return "a file name can't end with '.' or a space";

        for (const char c : fileName) {
            if (c == '/' || c == '\\') return "a file name can't contain '/' or '\\' - saves have no folders";
            if (static_cast<unsigned char>(c) < 32 || std::string_view(":*?\"<>|").find(c) != std::string_view::npos)
                return std::string("a file name can't contain '") + (static_cast<unsigned char>(c) < 32 ? std::string("control characters") : std::string(1, c)) + "'";
        }

        // Windows can't make files with these names, whatever the extension.
        std::string stem = fileName.substr(0, fileName.find('.'));
        std::ranges::transform(stem, stem.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });

        static constexpr std::array<std::string_view, 4> RESERVED = {"CON", "PRN", "AUX", "NUL"};
        const bool numbered = stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9';
        if (numbered || std::ranges::find(RESERVED, stem) != RESERVED.end())
            return "\"" + fileName + "\" is a name Windows doesn't allow for files";

        return {};
    }

    const json* Find(const std::string& fileName, const std::string& name) {
        const SaveFile& file = Load(fileName);
        const auto it = file.values.find(name);
        return it == file.values.end() ? nullptr : &*it;
    }

    void Set(const std::string& fileName, const std::string& name, const std::string& type, json value) {
        SaveFile& file = Load(fileName);
        file.values[name] = {{"type", type}, {"value", std::move(value)}};
        file.exists = true;
        file.dirty = true;
        file.deletePending = false;
    }

    bool DeleteKey(const std::string& fileName, const std::string& name) {
        SaveFile& file = Load(fileName);
        if (file.values.erase(name) == 0) return false;

        file.dirty = true;
        return true;
    }

    bool DeleteSaveFile(const std::string& fileName) {
        SaveFile& file = Load(fileName);
        if (!file.exists) return false;

        file.values = json::object();
        file.exists = false;
        file.dirty = true;
        file.deletePending = true;
        return true;
    }

    bool FileExists(const std::string& fileName) {
        return Load(fileName).exists;
    }

    std::vector<std::string> GetFiles() {
        std::vector<std::string> names;

        std::error_code ec;
        for (fs::directory_iterator it(Folder(), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != EXTENSION) continue;

            const std::string name = it->path().stem().string();
            if (FileNameProblem(name).empty()) names.push_back(name);
        }

        // This frame's changes aren't on disk yet.
        for (const auto& [name, file] : files) {
            const auto it = std::ranges::find(names, name);
            if (file.exists && it == names.end()) names.push_back(name);
            if (!file.exists && it != names.end()) names.erase(it);
        }

        std::ranges::sort(names);
        return names;
    }

    void Flush() {
        bool folderReady = false;

        for (auto& [name, file] : files) {
            if (!file.dirty) continue;
            file.dirty = false;

            const fs::path path = PathOf(name);
            std::error_code ec;

            if (file.deletePending) {
                file.deletePending = false;
                fs::remove(path, ec);
                if (ec) spdlog::error("Couldn't delete save file '{}': {}", path.string(), ec.message());
                continue;
            }

            if (!folderReady) {
                fs::create_directories(Folder(), ec);
                folderReady = true;
            }

            if (!Write(path, file.values)) spdlog::error("Couldn't write save file '{}'", path.string());
        }
    }

    void Reset() {
        files.clear();
        folder.reset();
    }

    int ClearProjectSaves() {
        Reset();

        int removed = 0;
        std::error_code ec;
        for (fs::directory_iterator it(ProjectManager::GetProjectSavesPath(), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;

            std::error_code removeError;
            if (fs::remove(it->path(), removeError)) ++removed;
            else spdlog::error("Couldn't delete save file '{}': {}", it->path().string(), removeError.message());
        }

        return removed;
    }
}
