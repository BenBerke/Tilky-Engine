//
// Created by berke on 9/18/2026.
//

#ifndef TILKY_ENGINE_TAGREGISTRY_HPP
#define TILKY_ENGINE_TAGREGISTRY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace TagRegistry
{
    using TagId = uint16_t;
    using TagMap = std::unordered_map<std::string, TagId>;

    std::optional<TagId> Find(const std::string& name);
    TagId GetOrCreate(const std::string& name);

    // Strict creation for UI-driven tag creation: fails (returns nullopt,
    // registry left untouched) if name is empty or already registered,
    // rather than GetOrCreate's idempotent "return the existing ID" behavior.
    std::optional<TagId> Create(const std::string& name);

    // Renames a tag in place, preserving its ID. Fails cleanly (returns
    // false, registry left untouched) if oldName isn't registered, newName
    // is empty, or newName is already registered under a different name.
    bool Rename(const std::string& oldName, const std::string& newName);

    bool Delete(const std::string& name);
    bool Exists(const std::string& name);

    const TagMap& GetAll();
    uint32_t GetNextId();

    void Load(TagMap tags, uint32_t nextId);
    void Clear();
}

#endif //TILKY_ENGINE_TAGREGISTRY_HPP