//
// Created by berke on 9/18/2026.
//

#include "Headers/TagRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace
{
    TagRegistry::TagMap tagToId;
    uint32_t nextTagId = 0;
}

std::optional<TagRegistry::TagId> TagRegistry::Find(const std::string& name) {
    const auto iterator = tagToId.find(name);

    if (iterator == tagToId.end()) return std::nullopt;

    return iterator->second;
}

TagRegistry::TagId TagRegistry::GetOrCreate(const std::string& name) {
    if (const auto id = Find(name)) return *id;

    if (nextTagId > std::numeric_limits<TagId>::max()) throw std::runtime_error("Maximum number of tags reached");

    const auto id = static_cast<TagId>(nextTagId++);
    tagToId.emplace(name, id);

    return id;
}

std::optional<TagRegistry::TagId> TagRegistry::Create(const std::string& name) {
    if (name.empty()) return std::nullopt;
    if (Exists(name)) return std::nullopt;

    if (nextTagId > std::numeric_limits<TagId>::max()) throw std::runtime_error("Maximum number of tags reached");

    const auto id = static_cast<TagId>(nextTagId++);
    tagToId.emplace(name, id);

    return id;
}

bool TagRegistry::Rename(const std::string& oldName, const std::string& newName) {
    if (newName.empty()) return false;
    if (oldName == newName) return true;

    const auto iterator = tagToId.find(oldName);
    if (iterator == tagToId.end()) return false;

    if (tagToId.contains(newName)) return false;

    const TagId id = iterator->second;
    tagToId.erase(iterator);
    tagToId.emplace(newName, id);

    return true;
}

bool TagRegistry::Delete(const std::string& name) {
    // Do not decrease nextTagId. Deleted IDs should not be reused.
    return tagToId.erase(name) > 0;
}

bool TagRegistry::Exists(const std::string& name) {
    return tagToId.contains(name);
}

const TagRegistry::TagMap& TagRegistry::GetAll() {
    return tagToId;
}

uint32_t TagRegistry::GetNextId() {
    return nextTagId;
}

void TagRegistry::Load(TagMap tags, const uint32_t nextId) {
    tagToId = std::move(tags);
    nextTagId = nextId;
}

bool TagRegistry::LoadFromProjectSettings(const std::filesystem::path& settingsPath) {
    try {
        if (!std::filesystem::exists(settingsPath)) {
            Clear();
            return true;
        }

        std::ifstream file(settingsPath);

        if (!file.is_open()) {
            spdlog::error("Failed to open project settings: {}", settingsPath.string());
            return false;
        }

        nlohmann::json settings;
        file >> settings;

        const int formatVersion = settings.at("formatVersion").get<int>();

        if (formatVersion != 1) {
            spdlog::error("Unsupported project settings version: {}", formatVersion);
            return false;
        }

        auto tags = settings.at("tagToId").get<TagMap>();

        // Calculate the minimum safe next ID. This prevents ID collisions
        // if nextTagId is missing or contains an incorrect lower value.
        uint32_t minimumNextId = 0;

        for (const auto& [name, id] : tags)
            minimumNextId = std::max(minimumNextId, static_cast<uint32_t>(id) + 1u);

        const uint32_t savedNextId = settings.value("nextTagId", minimumNextId);

        Load(std::move(tags), std::max(savedNextId, minimumNextId));

        return true;
    }
    catch (const std::exception& error) {
        spdlog::error("Failed to load project settings: {}", error.what());
        return false;
    }
}

void TagRegistry::Clear() {
    tagToId.clear();
    nextTagId = 0;
}