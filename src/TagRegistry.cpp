//
// Created by berke on 9/18/2026.
//

#include "Headers/TagRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace
{
    TagRegistry::TagMap tagToId;
    std::unordered_map<TagId, std::string> idToTag;
    std::unordered_set<TagId> eventTagIds;
    uint32_t nextTagId = 0;
}

std::optional<TagId> TagRegistry::Find(const std::string& name) {
    const auto iterator = tagToId.find(name);

    if (iterator == tagToId.end()) return std::nullopt;

    return iterator->second;
}

std::optional<std::string> TagRegistry::GetName(const TagId id) {
    const auto iterator = idToTag.find(id);

    if (iterator == idToTag.end()) return std::nullopt;

    return iterator->second;
}

TagId TagRegistry::GetOrCreate(const std::string& name, const TagKind kind) {
    if (const auto id = Find(name)) return *id;

    if (nextTagId > std::numeric_limits<TagId>::max()) throw std::runtime_error("Maximum number of tags reached");

    const auto id = static_cast<TagId>(nextTagId++);
    tagToId.emplace(name, id);
    idToTag[id] = name;
    if (kind == TagKind::EVENT) eventTagIds.insert(id);

    return id;
}

std::optional<TagId> TagRegistry::Create(const std::string& name, const TagKind kind) {
    if (name.empty()) return std::nullopt;
    if (Exists(name)) return std::nullopt;

    return GetOrCreate(name, kind);
}

TagRegistry::TagKind TagRegistry::GetKind(const TagId id) {
    return eventTagIds.contains(id) ? TagKind::EVENT : TagKind::NORMAL;
}

std::vector<std::pair<std::string, TagId>> TagRegistry::GetAllOfKind(const TagKind kind) {
    std::vector<std::pair<std::string, TagId>> tags;

    for (const auto& [name, id] : tagToId)
        if (GetKind(id) == kind) tags.emplace_back(name, id);

    std::ranges::sort(tags, {}, [](const auto& entry) { return entry.second; });
    return tags;
}

std::string TagRegistry::UniqueName(const std::string& base) {
    if (!Exists(base)) return base;

    for (int number = 2;; ++number) {
        std::string name = base + " " + std::to_string(number);
        if (!Exists(name)) return name;
    }
}

bool TagRegistry::EnsureBuiltInTags() {
    if (Exists(PLAYER_TAG)) return false;

    GetOrCreate(PLAYER_TAG);
    return true;
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
    idToTag[id] = newName;

    return true;
}

bool TagRegistry::Delete(const std::string& name) {
    // Do not decrease nextTagId. Deleted IDs should not be reused.
    const auto iterator = tagToId.find(name);
    if (iterator == tagToId.end()) return false;

    idToTag.erase(iterator->second);
    eventTagIds.erase(iterator->second);
    tagToId.erase(iterator);

    return true;
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

std::vector<TagId> TagRegistry::GetEventTagIds() {
    std::vector<TagId> ids(eventTagIds.begin(), eventTagIds.end());
    std::ranges::sort(ids);
    return ids;
}

void TagRegistry::Load(TagMap tags, const uint32_t nextId, const std::vector<TagId>& eventTags) {
    tagToId = std::move(tags);
    nextTagId = nextId;

    idToTag.clear();
    for (const auto& [name, id] : tagToId) idToTag[id] = name;

    // Only IDs that are registered tags.
    eventTagIds.clear();
    for (const TagId id : eventTags)
        if (idToTag.contains(id)) eventTagIds.insert(id);
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

        const auto eventTags = settings.value("eventTags", std::vector<TagId>{});

        Load(std::move(tags), std::max(savedNextId, minimumNextId), eventTags);

        return true;
    }
    catch (const std::exception& error) {
        spdlog::error("Failed to load project settings: {}", error.what());
        return false;
    }
}

void TagRegistry::Clear() {
    tagToId.clear();
    idToTag.clear();
    eventTagIds.clear();
    nextTagId = 0;
}