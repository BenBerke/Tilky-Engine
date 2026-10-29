//
// Created by berke on 9/18/2026.
//

#ifndef TILKY_ENGINE_TAGREGISTRY_HPP
#define TILKY_ENGINE_TAGREGISTRY_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Objects/Types.hpp"

namespace TagRegistry
{
    using TagMap = std::unordered_map<std::string, TagId>;

    // One registry, one ID and name space, two kinds. NORMAL tags label
    // sectors, walls and entities, and are what tag filters test. EVENT
    // tags only link events to dispatches. The editor lists them apart.
    enum class TagKind : uint8_t { NORMAL, EVENT };

    // Added to every project when the editor loads its settings. A normal
    // tag like any other.
    inline constexpr const char* PLAYER_TAG = "Player";

    // Sectors, walls and entities store only tag IDs; the registry is the
    // one place names live, kept as two maps (name -> ID and ID -> name).
    std::optional<TagId> Find(const std::string& name);
    // The name of a registered tag, or nullopt if `id` isn't registered
    // (e.g. a tag deleted while another level still held it).
    std::optional<std::string> GetName(TagId id);
    // An existing tag keeps its kind.
    TagId GetOrCreate(const std::string& name, TagKind kind = TagKind::NORMAL);

    // Strict creation for UI-driven tag creation: fails (returns nullopt,
    // registry left untouched) if name is empty or already registered,
    // rather than GetOrCreate's idempotent "return the existing ID" behavior.
    std::optional<TagId> Create(const std::string& name, TagKind kind = TagKind::NORMAL);

    // NORMAL for an unregistered ID.
    TagKind GetKind(TagId id);
    // The tags of one kind, in ID (creation) order.
    std::vector<std::pair<std::string, TagId>> GetAllOfKind(TagKind kind);
    // `base`, or `base 2`, `base 3`... - the first name no tag has yet.
    std::string UniqueName(const std::string& base);

    // Creates the built-in tags the project doesn't have. True when it added any.
    bool EnsureBuiltInTags();

    // Renames a tag in place, preserving its ID. Fails cleanly (returns
    // false, registry left untouched) if oldName isn't registered, newName
    // is empty, or newName is already registered under a different name.
    bool Rename(const std::string& oldName, const std::string& newName);

    bool Delete(const std::string& name);
    bool Exists(const std::string& name);

    const TagMap& GetAll();
    uint32_t GetNextId();
    // Sorted, for saving.
    std::vector<TagId> GetEventTagIds();

    void Load(TagMap tags, uint32_t nextId, const std::vector<TagId>& eventTags = {});
    // Loads the tags from a project's ProjectSettings.json. A missing file
    // means a project without tags: the registry is cleared and this returns
    // true. Used by both the editor and the standalone game.
    bool LoadFromProjectSettings(const std::filesystem::path& settingsPath);
    void Clear();
}

#endif //TILKY_ENGINE_TAGREGISTRY_HPP