//
// Created by berke on 10/9/2026.
//

#include "Headers/Objects/FlipbookAsset.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    // Bumped when the layout changes in a way older engines can't read.
    constexpr int FLIPBOOK_FORMAT_VERSION = 1;

    void SetFlipbookError(std::string* errorMessage, const std::string& message) {
        if (errorMessage != nullptr) *errorMessage = message;
    }

    json FlipbookToJson(const FlipbookAsset& asset) {
        json frames = json::array();

        for (const FlipbookFrame& frame : asset.frames) {
            frames.push_back(json{
                {"name", frame.name},
                {"textures", frame.textures},
                {"duration", frame.duration},
                {"eventFunction", frame.eventFunction},
            });
        }

        return json{
            {"version", FLIPBOOK_FORMAT_VERSION},
            {"fps", asset.fps},
            {"loopMode", static_cast<int>(asset.loopMode)},
            {"playOnStart", asset.playOnStart},
            {"frames", frames},
        };
    }

    FlipbookAsset FlipbookFromJson(const json& data) {
        FlipbookAsset asset;

        asset.fps = data.value("fps", 10.0f);
        asset.playOnStart = data.value("playOnStart", true);

        const int loopMode = data.value("loopMode", static_cast<int>(FlipbookLoopMode::Loop));
        if (loopMode >= static_cast<int>(FlipbookLoopMode::Once) && loopMode <= static_cast<int>(FlipbookLoopMode::PingPong))
            asset.loopMode = static_cast<FlipbookLoopMode>(loopMode);

        for (const json& frameJson : data.value("frames", json::array())) {
            FlipbookFrame frame;
            frame.name = frameJson.value("name", std::string{});
            frame.duration = frameJson.value("duration", 0.0f);
            frame.eventFunction = frameJson.value("eventFunction", std::string{});

            if (frameJson.contains("textures")) frame.textures = frameJson.at("textures").get<std::array<std::string, 8>>();

            // Names are what scripts look frames up by, so a hand-edited file
            // with a blank or repeated name gets a fresh one.
            if (frame.name.empty() || asset.FindFrame(frame.name) != -1) frame.name = asset.MakeUniqueFrameName();

            asset.frames.push_back(std::move(frame));
        }

        return asset;
    }

    struct LibraryEntry {
        std::unique_ptr<FlipbookAsset> asset; // nullptr = failed to load
    };

    std::unordered_map<std::string, LibraryEntry>& LoadedFlipbooks() {
        static std::unordered_map<std::string, LibraryEntry> loaded;
        return loaded;
    }
}

float FlipbookAsset::FrameDuration(const int index) const {
    if (index >= 0 && index < static_cast<int>(frames.size()) && frames[index].duration > 0.0f)
        return frames[index].duration;

    return fps > 0.0f ? 1.0f / fps : 0.1f;
}

bool FlipbookAsset::StepFrame(int& frame, int& direction) const {
    const int frameCount = static_cast<int>(frames.size());
    const int last = frameCount - 1;

    if (frameCount == 0) return false;

    switch (loopMode) {
        case FlipbookLoopMode::Once:
            if (frame >= last) return false;
            ++frame;
            return true;

        case FlipbookLoopMode::Loop:
            frame = (frame + 1) % frameCount;
            return true;

        case FlipbookLoopMode::PingPong: {
            if (frameCount == 1) return true;

            int next = frame + direction;

            if (next > last) {
                direction = -1;
                next = last - 1;
            }
            else if (next < 0) {
                direction = 1;
                next = 1;
            }

            frame = next;
            return true;
        }
    }

    return false;
}

int FlipbookAsset::FindFrame(const std::string_view name) const {
    for (int i = 0; i < static_cast<int>(frames.size()); ++i)
        if (frames[i].name == name) return i;

    return -1;
}

std::string FlipbookAsset::MakeUniqueFrameName() const {
    for (int number = static_cast<int>(frames.size());; ++number) {
        std::string name = "Frame" + std::to_string(number);
        if (FindFrame(name) == -1) return name;
    }
}

namespace FlipbookIO {
    bool Load(const fs::path& file, FlipbookAsset& outAsset, std::string* errorMessage) {
        std::ifstream stream(file, std::ios::binary);

        if (!stream.is_open()) {
            SetFlipbookError(errorMessage, "Could not open flipbook file: " + file.string());
            return false;
        }

        const std::vector<std::uint8_t> bsonData{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };

        try {
            outAsset = FlipbookFromJson(json::from_bson(bsonData));
        }
        catch (const std::exception& e) {
            SetFlipbookError(errorMessage, "Failed to read flipbook " + file.string() + ": " + e.what());
            return false;
        }

        return true;
    }

    bool Save(const fs::path& file, const FlipbookAsset& asset, std::string* errorMessage) {
        std::vector<std::uint8_t> bsonData;

        try {
            bsonData = json::to_bson(FlipbookToJson(asset));
        }
        catch (const std::exception& e) {
            SetFlipbookError(errorMessage, "Failed to encode flipbook " + file.string() + ": " + e.what());
            return false;
        }

        std::ofstream stream(file, std::ios::binary | std::ios::trunc);

        if (!stream.is_open()) {
            SetFlipbookError(errorMessage, "Could not open flipbook file for saving: " + file.string());
            return false;
        }

        stream.write(reinterpret_cast<const char*>(bsonData.data()), static_cast<std::streamsize>(bsonData.size()));

        if (!stream) {
            SetFlipbookError(errorMessage, "Failed to write flipbook file: " + file.string());
            return false;
        }

        return true;
    }

    bool IsValidEventFunction(const std::string_view name, std::string* reason) {
        if (name.empty()) return true;

        const auto fail = [&](const std::string& message) {
            if (reason != nullptr) *reason = message;
            return false;
        };

        const auto isIdentifierStart = [](const unsigned char c) { return std::isalpha(c) || c == '_'; };
        const auto isIdentifierChar = [](const unsigned char c) { return std::isalnum(c) || c == '_'; };

        if (!isIdentifierStart(static_cast<unsigned char>(name.front())))
            return fail("Must start with a letter or _");

        if (!std::ranges::all_of(name, [&](const char c) { return isIdentifierChar(static_cast<unsigned char>(c)); }))
            return fail("Only letters, digits and _ are allowed");

        static const std::unordered_set<std::string_view> keywords = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
            "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
            "true", "until", "while"
        };

        if (keywords.contains(name)) return fail("'" + std::string(name) + "' is a Lua keyword");

        static const std::unordered_set<std::string_view> reserved = {
            "Start", "Update", "FixedUpdate", "OnEnable", "OnDisable", "OnDestroy",
            "OnEntityEnter", "OnEntityExit",
            "OnCollisionEnter", "OnCollision", "OnCollisionExit",
            "OnTriggerEnter", "OnTrigger", "OnTriggerExit", "OnSectorChange",
            "entity", "sector", "Global", "GameTime", "Input", "Game", "Debug", "mathT"
        };

        if (reserved.contains(name)) return fail("'" + std::string(name) + "' is reserved by the engine");

        return true;
    }

    int RenameTextureReferenceInProject(const std::string& oldReference, const std::string& newReference) {
        if (oldReference == newReference) return 0;

        const fs::path assetsPath = ProjectManager::GetAssetsPath();
        int rewritten = 0;

        std::error_code ec;
        for (fs::recursive_directory_iterator it(assetsPath, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;

            std::string extension = it->path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != kExtension) continue;

            FlipbookAsset asset;
            std::string error;

            if (!Load(it->path(), asset, &error)) {
                spdlog::warn("Texture rename: skipped flipbook that could not be read: {}", error);
                continue;
            }

            bool changed = false;

            for (FlipbookFrame& frame : asset.frames)
                for (std::string& texture : frame.textures)
                    if (texture == oldReference) {
                        texture = newReference;
                        changed = true;
                    }

            if (!changed) continue;

            if (!Save(it->path(), asset, &error)) {
                spdlog::error("Texture rename: {}", error);
                continue;
            }

            FlipbookLibrary::Invalidate(it->path().lexically_relative(assetsPath).generic_string());
            ++rewritten;
        }

        return rewritten;
    }
}

namespace FlipbookLibrary {
    const FlipbookAsset* Get(const std::string& reference) {
        if (reference.empty()) return nullptr;

        auto& loaded = LoadedFlipbooks();

        if (const auto found = loaded.find(reference); found != loaded.end()) return found->second.asset.get();

        auto asset = std::make_unique<FlipbookAsset>();
        std::string error;

        if (!FlipbookIO::Load(ProjectManager::GetAssetsPath() / reference, *asset, &error)) {
            spdlog::error("Flipbook '{}' could not be loaded: {}", reference, error);
            asset.reset();
        }

        return loaded.emplace(reference, LibraryEntry{std::move(asset)}).first->second.asset.get();
    }

    void Invalidate(const std::string& reference) {
        LoadedFlipbooks().erase(reference);
    }

    void Clear() {
        LoadedFlipbooks().clear();
    }
}
