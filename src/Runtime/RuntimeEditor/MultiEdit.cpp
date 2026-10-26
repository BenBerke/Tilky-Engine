#include "Headers/Runtime/RuntimeEditor/MultiEdit.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"

using json = nlohmann::json;

namespace {
    using Change = std::pair<json::json_pointer, json>;

    // Every leaf of `after` that differs from `before`. Objects and
    // same-length arrays are walked into; anything else that differs
    // (including an array that changed length) is one change.
    void CollectChanges(const json& before, const json& after, const json::json_pointer& path, std::vector<Change>& out) {
        if (before == after) return;

        if (before.is_object() && after.is_object()) {
            for (auto it = after.begin(); it != after.end(); ++it) {
                const json::json_pointer child = path / it.key();

                if (before.contains(it.key())) CollectChanges(before.at(it.key()), it.value(), child, out);
                else out.emplace_back(child, it.value());
            }

            return;
        }

        if (before.is_array() && after.is_array() && before.size() == after.size()) {
            for (std::size_t i = 0; i < after.size(); ++i) CollectChanges(before.at(i), after.at(i), path / i, out);
            return;
        }

        out.emplace_back(path, after);
    }

    std::string TopLevelKey(const json::json_pointer& pointer) {
        const std::string text = pointer.to_string(); // "/key/..."
        const std::size_t end = text.find('/', 1);
        return text.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    }

    // Writes each change into `target` where that spot exists there too: an
    // array index past the target's end, or a parent the target lacks, is
    // skipped rather than invented.
    bool ApplyChanges(json& target, const std::vector<Change>& changes, const std::vector<std::string_view>& excludedKeys) {
        bool changed = false;

        for (const auto& [pointer, value] : changes) {
            if (pointer.empty()) continue;
            if (std::ranges::find(excludedKeys, TopLevelKey(pointer)) != excludedKeys.end()) continue;

            const json::json_pointer parentPointer = pointer.parent_pointer();
            if (!target.contains(parentPointer)) continue;

            json& parent = target[parentPointer];

            if (parent.is_array()) {
                const std::string& token = pointer.back();
                if (token.empty() || !std::ranges::all_of(token, [](const char c) { return c >= '0' && c <= '9'; })) continue;
                if (std::stoul(token) >= parent.size()) continue;
            }
            else if (!parent.is_object()) continue;

            if (target[pointer] == value) continue;

            target[pointer] = value;
            changed = true;
        }

        return changed;
    }

    std::vector<Change> Changes(const json& before, const json& after) {
        std::vector<Change> changes;
        CollectChanges(before, after, json::json_pointer{}, changes);
        return changes;
    }

    // Calls f(storageKey, level.Storage.GetAll(entityID)) once per type.
    template<typename F>
    void ForEachComponentList(Level& level, const ID entityID, F&& f) {
#define TILKY_MULTI_EDIT_VISIT(Type, Bit, Storage, LabelKey) f(#Storage, level.Storage.GetAll(entityID));
        TILKY_COMPONENTS(TILKY_MULTI_EDIT_VISIT)
#undef TILKY_MULTI_EDIT_VISIT
    }
}

namespace MultiEdit {
    json WallState(const Wall& wall) {
        return LevelSerialization::WallToJson(wall);
    }

    void MirrorWallEdit(const json& before, const json& after, Wall& target) {
        // Where the wall is and what it borders belong to that wall.
        static const std::vector<std::string_view> excluded = {"id", "start", "end", "frontSector", "backSector"};

        json targetJson = LevelSerialization::WallToJson(target);
        if (!ApplyChanges(targetJson, Changes(before, after), excluded)) return;

        Wall loaded = target;
        if (!LevelSerialization::WallFromJson(targetJson, loaded)) return;

        // Only the saved, non-excluded fields; geometry and derived data stay.
        target.name = std::move(loaded.name);
        target.tagIds = std::move(loaded.tagIds);
        target.color = loaded.color;
        target.top = std::move(loaded.top);
        target.bottom = std::move(loaded.bottom);
        target.middle = std::move(loaded.middle);
        target.middleHasCollision = loaded.middleHasCollision;
        target.middleIgnoredByRaycast = loaded.middleIgnoredByRaycast;
    }

    json SectorState(const Sector& sector) {
        return LevelSerialization::SectorToJson(sector);
    }

    void MirrorSectorEdit(const json& before, const json& after, Sector& target) {
        static const std::vector<std::string_view> excluded = {"id", "corners", "innerLoops", "parentID"};

        json targetJson = LevelSerialization::SectorToJson(target);
        const json originalScripts = targetJson.value("scripts", json::array());

        if (!ApplyChanges(targetJson, Changes(before, after), excluded)) return;

        Sector loaded;
        if (!LevelSerialization::SectorFromJson(targetJson, loaded)) return;

        // Only the saved, non-excluded fields. Everything else on a Sector -
        // its outline, triangles, neighbours, PVS, entities - stays.
        target.name = std::move(loaded.name);
        target.tagIds = std::move(loaded.tagIds);
        target.floors = std::move(loaded.floors);
        target.light = loaded.light;

        // Untouched scripts keep their live instance IDs.
        if (targetJson.value("scripts", json::array()) != originalScripts) {
            target.scripts = std::move(loaded.scripts);
            target.nextScriptInstanceID = loaded.nextScriptInstanceID;
        }
    }

    json EntityState(Level& level, const ID entityID) {
        const Entity* entity = level.GetEntity(entityID);
        if (entity == nullptr) return json{};

        json components = json::object();

        ForEachComponentList(level, entityID, [&](const char* key, const auto& instances) {
            json list = json::array();
            for (const auto* instance : instances) list.push_back(LevelSerialization::ComponentFieldsToJson(*instance));
            components[key] = std::move(list);
        });

        return {
            {"name", entity->name},
            {"tagIds", entity->tagIds},
            {"components", std::move(components)}
        };
    }

    void MirrorEntityEdit(Level& level, const json& before, const json& after, const ID targetID) {
        Entity* target = level.GetEntity(targetID);
        if (target == nullptr) return;

        const json original = EntityState(level, targetID);
        json edited = original;

        if (!ApplyChanges(edited, Changes(before, after), {})) return;

        target->name = edited.value("name", target->name);
        target->tagIds = edited.value("tagIds", target->tagIds);

        bool transformChanged = false;

        ForEachComponentList(level, targetID, [&](const char* key, const auto& instances) {
            const json& editedList = edited.at("components").at(key);
            const json& originalList = original.at("components").at(key);

            for (std::size_t i = 0; i < instances.size(); ++i) {
                if (editedList.at(i) == originalList.at(i)) continue;

                LevelSerialization::ComponentFieldsFromJson(editedList.at(i), *instances[i]);

                if (std::string_view(key) == "transforms") transformChanged = true;
            }
        });

        // The entity's sector, and with it its lighting, follows its position.
        if (transformChanged)
            if (ComponentTransform* transform = level.transforms.Get(targetID))
                transform->UpdateObjectSectorAndFloor(level.sectors);
    }
}
