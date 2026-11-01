#include "Headers/Objects/UILayouts.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/EntityTransfer.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace {
    struct Loaded {
        std::unique_ptr<Level> layout;  // nullptr: the file couldn't be read
        nlohmann::json saved;           // as last loaded/saved, for IsDirty
    };

    std::vector<UILayouts::Info> files;
    bool scanned = false;

    std::unordered_map<std::uint64_t, Loaded> loaded;

    std::string LowerExtension(const fs::path& path) {
        std::string extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    nlohmann::json Snapshot(const Level& layout) {
        return LevelSerialization::EntitiesToJson(layout, EntityTransfer::AllEntityIDs(layout));
    }

    void EnsureScanned() {
        if (!scanned) UILayouts::Rescan();
    }

    // Fits BSON's signed 64-bit integers, never 0.
    std::uint64_t NewLayoutID() {
        static std::mt19937_64 generator{std::random_device{}()};

        while (true) {
            const std::uint64_t id = generator() & 0x3FFFFFFFFFFFFFFFull;
            if (id != 0 && UILayouts::Find(id) == nullptr) return id;
        }
    }
}

namespace UILayouts {
    const std::vector<Info>& List() {
        EnsureScanned();
        return files;
    }

    void Rescan() {
        scanned = true;

        // Which file had each ID before: a copied file keeps its original's
        // ID, and the original is the one that already had it.
        const std::vector<Info> previous = std::move(files);
        files.clear();

        if (!ProjectManager::HasProject()) return;

        const fs::path assets = ProjectManager::GetAssetsPath();
        std::vector<Info> found;
        std::error_code error;

        for (auto it = fs::recursive_directory_iterator(assets, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
            if (!it->is_regular_file() || LowerExtension(it->path()) != kExtension) continue;

            const fs::path path = it->path().lexically_normal();
            std::uint64_t id = 0;
            std::string message;

            if (!LevelSerialization::ReadUILayoutID(path, id, &message)) spdlog::error("UI layout skipped: {}", message);
            else found.push_back({id, path, path.stem().string()});
        }

        std::ranges::sort(found, [](const Info& a, const Info& b) { return a.path < b.path; });

        for (Info& info : found) {
            const auto owner = std::ranges::find(files, info.id, &Info::id);
            if (owner == files.end()) {
                files.push_back(info);
                continue;
            }

            // Two files with one ID. The one that had it before keeps it.
            const auto before = std::ranges::find(previous, info.id, &Info::id);
            const bool currentIsOriginal = before != previous.end() && before->path == info.path;
            const bool ownerIsOriginal = before != previous.end() && before->path == owner->path;

            // Without an earlier scan there's no telling which is the copy.
            if (!currentIsOriginal && !ownerIsOriginal) {
                spdlog::error("UI layouts {} and {} have the same layout ID; {} is ignored. Delete whichever is the copy.",
                              owner->path.string(), info.path.string(), info.path.string());
                continue;
            }

            const Info original = currentIsOriginal ? info : *owner;
            Info copy = currentIsOriginal ? *owner : info;
            *owner = original;

            Level layout;
            std::uint64_t ignored = 0;
            std::string message;
            std::uint64_t newID = NewLayoutID();
            while (std::ranges::find(found, newID, &Info::id) != found.end()) newID = NewLayoutID();

            if (!LevelSerialization::LoadUILayoutFromFile(copy.path, layout, ignored, &message) ||
                !LevelSerialization::SaveUILayoutToFile(copy.path, layout, newID, &message)) {
                spdlog::error("UI layout {} is a copy of {} and couldn't be given a layout ID of its own: {}",
                              copy.path.string(), original.path.string(), message);
                continue;
            }

            spdlog::info("UI layout {} is a copy of {}; it now has a layout ID of its own", copy.path.string(), original.path.string());
            copy.id = newID;
            files.push_back(copy);
        }

        std::ranges::sort(files, [](const Info& a, const Info& b) { return a.name < b.name; });

        // Deleted since: their kept copies go too.
        std::erase_if(loaded, [](const auto& entry) {
            return std::ranges::find(files, entry.first, &Info::id) == files.end();
        });
    }

    const Info* Find(const std::uint64_t id) {
        EnsureScanned();
        const auto it = std::ranges::find(files, id, &Info::id);
        return it != files.end() ? &*it : nullptr;
    }

    const Info* FindByName(const std::string& name) {
        EnsureScanned();
        const auto it = std::ranges::find(files, name, &Info::name);
        return it != files.end() ? &*it : nullptr;
    }

    Level* Get(const std::uint64_t id) {
        if (id == 0) return nullptr;

        if (const auto it = loaded.find(id); it != loaded.end()) return it->second.layout.get();

        const Info* info = Find(id);

        // Made or moved since the last scan.
        if (info == nullptr) {
            Rescan();
            info = Find(id);
        }

        if (info == nullptr) return nullptr;

        Loaded entry;
        entry.layout = std::make_unique<Level>();

        std::uint64_t fileID = 0;
        std::string message;

        if (LevelSerialization::LoadUILayoutFromFile(info->path, *entry.layout, fileID, &message)) {
            entry.saved = Snapshot(*entry.layout);
        }
        else {
            spdlog::error("UI layout '{}' could not be loaded: {}", info->name, message);
            entry.layout.reset();
        }

        return loaded.emplace(id, std::move(entry)).first->second.layout.get();
    }

    std::uint64_t IdOf(const Level* level) {
        if (level == nullptr) return 0;

        for (const auto& [id, entry] : loaded)
            if (entry.layout.get() == level) return id;

        return 0;
    }

    bool Create(const fs::path& absolutePath, std::uint64_t* outID, std::string* errorMessage) {
        const std::uint64_t id = NewLayoutID();

        Level empty;
        empty.name = absolutePath.stem().string();

        if (!LevelSerialization::SaveUILayoutToFile(absolutePath, empty, id, errorMessage)) return false;

        Rescan();
        if (outID != nullptr) *outID = id;
        return true;
    }

    bool IsDirty(const std::uint64_t id) {
        const auto it = loaded.find(id);
        return it != loaded.end() && it->second.layout && Snapshot(*it->second.layout) != it->second.saved;
    }

    std::vector<std::uint64_t> DirtyLayouts() {
        std::vector<std::uint64_t> dirty;
        for (const auto& [id, entry] : loaded)
            if (IsDirty(id)) dirty.push_back(id);
        return dirty;
    }

    bool Save(const std::uint64_t id, std::string* errorMessage) {
        const auto it = loaded.find(id);
        if (it == loaded.end() || !it->second.layout) {
            if (errorMessage) *errorMessage = "the layout isn't loaded";
            return false;
        }

        const Info* info = Find(id);
        if (info == nullptr) {
            Rescan();
            info = Find(id);
        }

        if (info == nullptr) {
            if (errorMessage) *errorMessage = "the layout's file is gone";
            return false;
        }

        if (!LevelSerialization::SaveUILayoutToFile(info->path, *it->second.layout, id, errorMessage)) return false;

        it->second.saved = Snapshot(*it->second.layout);
        return true;
    }

    void Invalidate(const std::uint64_t id) {
        loaded.erase(id);
    }

    void Clear() {
        loaded.clear();
        files.clear();
        scanned = false;
    }

    std::vector<ID> Instantiate(Level& level, const std::uint64_t id) {
        const Level* layout = Get(id);
        if (layout == nullptr) {
            if (Find(id) == nullptr) spdlog::warn("Level '{}' uses a UI layout that no longer exists (ID {})", level.name, id);
            return {};
        }

        const EntityTransfer::Bundle bundle = EntityTransfer::Copy(*layout, EntityTransfer::AllEntityIDs(*layout));
        const EntityTransfer::IdMap map = EntityTransfer::InsertAsNew(level, bundle);

        std::vector<ID> created;
        for (const Entity& source : bundle.entities) {
            Entity* entity = level.GetEntity(map.entities.at(source.id));
            entity->layoutID = id;
            entity->sinceEpoch = LevelEpoch::current;
            created.push_back(entity->id);
        }

        return created;
    }

    void SortUIDrawOrder(Level& level) {
        const std::vector<std::uint64_t>& order = level.worldSettings.uiLayouts;

        const auto layerOf = [&level, &order](const ID owner) -> std::uint32_t {
            const Entity* entity = level.GetEntity(owner);
            if (entity == nullptr || entity->layoutID == 0) return UI_LEVEL_DRAW_LAYER;

            const auto it = std::ranges::find(order, entity->layoutID);
            // A layout kept on a carried entity the level doesn't list sits
            // just under the level's own UI.
            return it != order.end() ? static_cast<std::uint32_t>(it - order.begin()) : UI_LEVEL_DRAW_LAYER - 1;
        };

        for (ComponentUISprite& sprite : level.ui_sprites.components) sprite.drawLayer = layerOf(sprite.ownerID);
        for (ComponentUIText& text : level.ui_texts.components) text.drawLayer = layerOf(text.ownerID);

        level.ui_sprites.StableSortComponents([](const ComponentUISprite& a, const ComponentUISprite& b) { return a.drawLayer < b.drawLayer; });
        level.ui_texts.StableSortComponents([](const ComponentUIText& a, const ComponentUIText& b) { return a.drawLayer < b.drawLayer; });
    }
}
