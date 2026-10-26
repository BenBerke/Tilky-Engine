#include "Headers/Objects/Prefab.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Project/ProjectManager.hpp"

using json = nlohmann::json;

namespace {
    // ── Per-type access ───────────────────────────────────────────────────
    // What the sync code needs from each component type: its storage, its
    // level.bson key, and the fields that always belong to the instance.

    template<typename T> struct Traits;

#define TILKY_PREFAB_TRAITS(Type, Bit, StorageName, Key) \
    template<> struct Traits<Type> { \
        static constexpr int bit = Bit; \
        static constexpr const char* key = Key; \
        static auto& Storage(Level& level) { return level.StorageName; } \
        static const auto& Storage(const Level& level) { return level.StorageName; } \
    };

    TILKY_PREFAB_TRAITS(ComponentTransform, CMP_TRANSFORM, transforms, "transforms")
    TILKY_PREFAB_TRAITS(ComponentSprite, CMP_SPRITE, sprites, "sprites")
    TILKY_PREFAB_TRAITS(ComponentAudioSource, CMP_AUDIO_SOURCE, audioSources, "audioSources")
    TILKY_PREFAB_TRAITS(ComponentScript, CMP_SCRIPT, scripts, "scripts")
    TILKY_PREFAB_TRAITS(ComponentPlayerController, CMP_PLAYER_CONTROLLER, playerControllers, "playerControllers")
    TILKY_PREFAB_TRAITS(ComponentCamera, CMP_CAMERA, cameras, "cameras")
    TILKY_PREFAB_TRAITS(ComponentCollider, CMP_COLLIDER, colliders, "colliders")
    TILKY_PREFAB_TRAITS(ComponentRigidbody, CMP_RIGIDBODY, rigidbodies, "rigidbodies")
    TILKY_PREFAB_TRAITS(ComponentModel, CMP_MODEL, models, "models")
    TILKY_PREFAB_TRAITS(ComponentFlipbook, CMP_FLIPBOOK, flipbooks, "flipbooks")
    TILKY_PREFAB_TRAITS(ComponentUITransform, CMP_UI_TRANSFORM, ui_transforms, "uiTransforms")
    TILKY_PREFAB_TRAITS(ComponentUISprite, CMP_UI_SPRITE, ui_sprites, "uiSprites")
    TILKY_PREFAB_TRAITS(ComponentUIText, CMP_UI_TEXT, ui_texts, "uiTexts")

#undef TILKY_PREFAB_TRAITS

    // Calls f(T{}) once per component type.
    template<typename F>
    void ForEachComponentType(F&& f) {
#define TILKY_PREFAB_VISIT(Type, Bit, Storage, LabelKey) f(static_cast<Type*>(nullptr));
        TILKY_COMPONENTS(TILKY_PREFAB_VISIT)
#undef TILKY_PREFAB_VISIT
    }

    // The fields that place the entity, so never follow the prefab.
    template<typename T>
    bool IsInstanceOwnedField(const std::string& field) {
        if constexpr (std::is_same_v<T, ComponentTransform>)
            return field == "position" || field == "rotation" || field == "sectorIndex";
        else if constexpr (std::is_same_v<T, ComponentUITransform>)
            return field == "position" || field == "rotation";
        else return false;
    }

    template<typename T>
    std::uint64_t InstanceIDOf(const T& component) { return component.instanceID; }

    template<typename T>
    std::vector<std::uint64_t> InstancesOf(const Level& level, const ID owner) {
        std::vector<std::uint64_t> result;
        if constexpr (std::is_same_v<T, ComponentScript>) {
            for (const ComponentScript& script : level.scripts.components)
                if (script.ownerID == owner) result.push_back(script.instanceID);
        }
        else {
            for (const ComponentInstanceID id : Traits<T>::Storage(level).InstancesOf(owner)) result.push_back(id);
        }
        return result;
    }

    template<typename T>
    T* FindInstance(Level& level, const std::uint64_t instanceID) {
        if constexpr (std::is_same_v<T, ComponentScript>) return level.scripts.GetByID(instanceID);
        else return Traits<T>::Storage(level).GetInstance(instanceID);
    }

    template<typename T>
    const T* FindInstance(const Level& level, const std::uint64_t instanceID) {
        if constexpr (std::is_same_v<T, ComponentScript>) return level.scripts.GetByID(instanceID);
        else return Traits<T>::Storage(level).GetInstance(instanceID);
    }

    // Adds a copy of `data` to the entity, with a new instance ID. Returns the ID.
    template<typename T>
    std::uint64_t AddInstance(Level& level, Entity& entity, T data) {
        data.ownerID = entity.id;
        entity.componentsMask.set(Traits<T>::bit);

        if constexpr (std::is_same_v<T, ComponentScript>) {
            ComponentScript& script = level.scripts.Add(entity.id);
            const ScriptInstanceID id = script.instanceID;
            script = std::move(data);
            script.instanceID = id;
            return id;
        }
        else {
            data.instanceID = INVALID_COMPONENT_INSTANCE_ID;
            return Traits<T>::Storage(level).InsertLoaded(data).instanceID;
        }
    }

    template<typename T>
    void RemoveInstance(Level& level, Entity& entity, const std::uint64_t instanceID) {
        if constexpr (std::is_same_v<T, ComponentScript>) level.scripts.Remove(instanceID);
        else Traits<T>::Storage(level).RemoveInstance(instanceID);

        if (InstancesOf<T>(level, entity.id).empty()) entity.componentsMask.reset(Traits<T>::bit);
    }

    template<typename T>
    void MoveInstance(Level& level, const std::uint64_t instanceID, const size_t index) {
        Traits<T>::Storage(level).MoveOnOwner(instanceID, index);
    }

    // Writes `data`'s saved fields over an existing component. Its owner and
    // instance ID stay; collider type/active state go through the storage
    // so its active/inactive regions stay sorted.
    template<typename T>
    void AssignInstance(Level& level, const std::uint64_t instanceID, const T& data) {
        T* existing = FindInstance<T>(level, instanceID);
        if (existing == nullptr) return;

        T updated = data;
        updated.ownerID = existing->ownerID;
        updated.instanceID = existing->instanceID;

        if constexpr (std::is_same_v<T, ComponentCollider>) {
            updated.type = existing->type;
            updated.isActive = existing->isActive;
            *existing = updated;
            level.colliders.SetType(instanceID, data.type);
            level.colliders.SetActive(instanceID, data.isActive);
        }
        else *existing = std::move(updated);
    }

    // ── Reference remapping ──────────────────────────────────────────────
    // A prefab's components can point at each other (a Flipbook at its
    // Sprite, a script field at its own entity or one of its components).
    // Between the prefab and an instance those IDs differ; IdMap translates
    // one side's IDs to the other's. References to anything else are kept.

    struct IdMap {
        ID fromEntity = INVALID_ID;
        ID toEntity = INVALID_ID;
        std::map<std::pair<int, std::uint64_t>, std::uint64_t> instances;

        // 0 (no component) when `from` has no counterpart.
        [[nodiscard]] std::uint64_t Map(const int type, const std::uint64_t from) const {
            const auto it = instances.find({type, from});
            return it == instances.end() ? 0 : it->second;
        }
    };

    void RemapScriptValue(ScriptValue& value, const IdMap& map) {
        if (auto* entityRef = std::get_if<EntityRefValue>(&value)) {
            if (entityRef->entityId == map.fromEntity) entityRef->entityId = map.toEntity;
        }
        else if (auto* componentRef = std::get_if<ComponentRefValue>(&value)) {
            if (componentRef->entityId == map.fromEntity) {
                componentRef->entityId = map.toEntity;
                componentRef->instanceId = map.Map(componentRef->componentType, componentRef->instanceId);
            }
        }
        else if (auto* behaviourRef = std::get_if<BehaviourRefValue>(&value)) {
            if (behaviourRef->entityId == map.fromEntity) {
                behaviourRef->entityId = map.toEntity;
                behaviourRef->instanceId = map.Map(CMP_SCRIPT, behaviourRef->instanceId);
            }
        }
    }

    template<typename T>
    void RemapRefs(T& component, const IdMap& map) {
        if constexpr (std::is_same_v<T, ComponentScript>) {
            for (auto& [name, value] : component.publicValues) RemapScriptValue(value, map);
        }
        else if constexpr (std::is_same_v<T, ComponentFlipbook>) {
            // A world entity's flipbook drives a Sprite, a UI entity's a UI Sprite.
            std::uint64_t mapped = map.Map(CMP_SPRITE, component.spriteInstanceID);
            if (mapped == 0) mapped = map.Map(CMP_UI_SPRITE, component.spriteInstanceID);
            component.spriteInstanceID = mapped;
        }
    }

    // instance -> prefab (`toPrefab`) or prefab -> instance, from the link.
    IdMap MakeIdMap(const Entity& instance, const Entity& prefabRoot, const bool toPrefab) {
        IdMap map;
        map.fromEntity = toPrefab ? instance.id : prefabRoot.id;
        map.toEntity = toPrefab ? prefabRoot.id : instance.id;

        for (const PrefabComponentLink& link : instance.prefabLink.components) {
            if (toPrefab) map.instances[{link.componentType, link.instanceID}] = link.prefabInstanceID;
            else map.instances[{link.componentType, link.prefabInstanceID}] = link.instanceID;
        }
        return map;
    }

    // A component's saved fields with its references in the prefab's IDs,
    // so an instance and its prefab compare equal when nothing was changed.
    template<typename T>
    json ComparableJson(T component, const IdMap* toPrefab) {
        if (toPrefab != nullptr) RemapRefs(component, *toPrefab);
        return LevelSerialization::ComponentFieldsToJson(component);
    }

    std::string ComponentPath(const char* key, const std::uint64_t prefabInstanceID, const std::string& field) {
        return std::string(key) + "/" + std::to_string(prefabInstanceID) + "/" + field;
    }

    bool HasOverride(const PrefabLink& link, const std::string& path) {
        return std::ranges::find(link.overrides, path) != link.overrides.end();
    }

    // Calls f(field path, instance value or null, prefab value or null) for
    // every comparable field of one linked component.
    template<typename F>
    void ForEachField(const char* key, const std::uint64_t prefabInstanceID, const json& instanceJson,
                      const json& prefabJson, F&& f, bool (*instanceOwned)(const std::string&)) {
        static const json null;

        std::vector<std::string> fields;
        for (const auto& [field, value] : prefabJson.items()) fields.push_back(field);
        for (const auto& [field, value] : instanceJson.items())
            if (!prefabJson.contains(field)) fields.push_back(field);

        for (const std::string& field : fields) {
            if (instanceOwned(field)) continue;

            const json& instanceValue = instanceJson.contains(field) ? instanceJson[field] : null;
            const json& prefabValue = prefabJson.contains(field) ? prefabJson[field] : null;

            // A script's public fields are overridden one by one.
            if (field == "publicValues" && instanceValue.is_object() && prefabValue.is_object()) {
                std::vector<std::string> names;
                for (const auto& [name, value] : prefabValue.items()) names.push_back(name);
                for (const auto& [name, value] : instanceValue.items())
                    if (!prefabValue.contains(name)) names.push_back(name);

                for (const std::string& name : names)
                    f(ComponentPath(key, prefabInstanceID, "publicValues/" + name),
                      instanceValue.contains(name) ? instanceValue[name] : null,
                      prefabValue.contains(name) ? prefabValue[name] : null);
                continue;
            }

            f(ComponentPath(key, prefabInstanceID, field), instanceValue, prefabValue);
        }
    }

    const PrefabComponentLink* FindLinkByPrefabID(const PrefabLink& link, const int type, const std::uint64_t prefabInstanceID) {
        for (const PrefabComponentLink& c : link.components)
            if (c.componentType == type && c.prefabInstanceID == prefabInstanceID) return &c;
        return nullptr;
    }

    bool IsRemoved(const PrefabLink& link, const int type, const std::uint64_t prefabInstanceID) {
        return std::ranges::find(link.removed, PrefabRemovedComponent{type, prefabInstanceID}) != link.removed.end();
    }

    // ── Library ──────────────────────────────────────────────────────────

    struct LibraryEntry {
        std::unique_ptr<Level> prefab; // null: the file couldn't be loaded
    };

    std::unordered_map<std::string, LibraryEntry>& LoadedPrefabs() {
        static std::unordered_map<std::string, LibraryEntry> loaded;
        return loaded;
    }

    std::unordered_map<std::string, std::unique_ptr<Level>>& ScriptCopies() {
        static std::unordered_map<std::string, std::unique_ptr<Level>> copies;
        return copies;
    }

    // ── Sync ─────────────────────────────────────────────────────────────

    template<typename T>
    void RefreshComponentOverrides(Level& level, Entity& instance, const Level& prefab, const IdMap& toPrefab,
                                   std::vector<std::string>& overrides, std::vector<PrefabRemovedComponent>& removed) {
        constexpr int bit = Traits<T>::bit;
        const Entity& root = Prefab::RootOf(prefab);

        for (const std::uint64_t prefabID : InstancesOf<T>(prefab, root.id)) {
            const PrefabComponentLink* link = FindLinkByPrefabID(instance.prefabLink, bit, prefabID);
            const T* instanceComponent = link != nullptr ? FindInstance<T>(level, link->instanceID) : nullptr;

            if (instanceComponent == nullptr) {
                removed.push_back({bit, prefabID});
                continue;
            }

            const json instanceJson = ComparableJson(*instanceComponent, &toPrefab);
            const json prefabJson = ComparableJson(*FindInstance<T>(prefab, prefabID), nullptr);

            ForEachField(Traits<T>::key, prefabID, instanceJson, prefabJson,
                [&overrides](const std::string& path, const json& instanceValue, const json& prefabValue) {
                    if (instanceValue != prefabValue) overrides.push_back(path);
                }, &IsInstanceOwnedField<T>);
        }
    }

    template<typename T>
    void SyncComponents(Level& level, Entity& instance, const Level& prefab) {
        constexpr int bit = Traits<T>::bit;
        const Entity& root = Prefab::RootOf(prefab);
        PrefabLink& link = instance.prefabLink;

        const std::vector<std::uint64_t> prefabIDs = InstancesOf<T>(prefab, root.id);

        // Links to prefab components that no longer exist: the prefab
        // removed them, so the instance loses them too. Links whose instance
        // component is gone were removed on the instance.
        std::erase_if(link.components, [&](const PrefabComponentLink& c) {
            if (c.componentType != bit) return false;

            if (std::ranges::find(prefabIDs, c.prefabInstanceID) == prefabIDs.end()) {
                if (FindInstance<T>(level, c.instanceID) != nullptr) RemoveInstance<T>(level, instance, c.instanceID);
                return true;
            }

            if (FindInstance<T>(level, c.instanceID) == nullptr) {
                if (!IsRemoved(link, bit, c.prefabInstanceID)) link.removed.push_back({bit, c.prefabInstanceID});
                return true;
            }
            return false;
        });

        std::erase_if(link.removed, [&](const PrefabRemovedComponent& r) {
            return r.componentType == bit && std::ranges::find(prefabIDs, r.prefabInstanceID) == prefabIDs.end();
        });

        // New prefab components the instance never had.
        std::vector<std::uint64_t> added;
        for (const std::uint64_t prefabID : prefabIDs) {
            if (FindLinkByPrefabID(link, bit, prefabID) != nullptr || IsRemoved(link, bit, prefabID)) continue;

            // Transform/UITransform: there's only ever one, so link the one
            // the entity has instead of adding another.
            std::uint64_t instanceID;
            if constexpr (IsSingleComponent<T>) {
                const std::vector<std::uint64_t> existing = InstancesOf<T>(level, instance.id);
                if (existing.empty()) continue;
                instanceID = existing.front();
            }
            else instanceID = AddInstance<T>(level, instance, *FindInstance<T>(prefab, prefabID));

            link.components.push_back({bit, instanceID, prefabID});
            added.push_back(instanceID);
        }

        // Every link is in place now, so references can be translated.
        const IdMap toPrefab = MakeIdMap(instance, root, true);
        const IdMap toInstance = MakeIdMap(instance, root, false);

        for (const std::uint64_t prefabID : prefabIDs) {
            const PrefabComponentLink* c = FindLinkByPrefabID(link, bit, prefabID);
            if (c == nullptr) continue;

            const T* instanceComponent = FindInstance<T>(level, c->instanceID);
            const T* prefabComponent = FindInstance<T>(prefab, prefabID);
            const bool isNew = std::ranges::find(added, c->instanceID) != added.end();

            json merged = ComparableJson(*instanceComponent, &toPrefab);
            const json prefabJson = ComparableJson(*prefabComponent, nullptr);

            ForEachField(Traits<T>::key, prefabID, json(merged), prefabJson,
                [&](const std::string& path, const json&, const json& prefabValue) {
                    if (!isNew && HasOverride(link, path)) return;

                    // path is "<key>/<id>/<field>" or ".../publicValues/<name>"
                    const std::string field = path.substr(path.find('/', path.find('/') + 1) + 1);
                    json* target = &merged;
                    std::string leaf = field;
                    if (const size_t slash = field.find('/'); slash != std::string::npos) {
                        target = &merged[field.substr(0, slash)];
                        leaf = field.substr(slash + 1);
                    }

                    if (prefabValue.is_null()) target->erase(leaf);
                    else (*target)[leaf] = prefabValue;
                }, &IsInstanceOwnedField<T>);

            T updated = *instanceComponent; // keeps runtime-only state
            if (!LevelSerialization::ComponentFieldsFromJson(merged, updated)) continue;
            RemapRefs(updated, toInstance);
            AssignInstance<T>(level, c->instanceID, updated);
        }

        // Order: the prefab's components in the prefab's order, then the
        // ones added on the instance.
        if constexpr (!IsSingleComponent<T>) {
            std::vector<std::uint64_t> order;
            for (const std::uint64_t prefabID : prefabIDs)
                if (const PrefabComponentLink* c = FindLinkByPrefabID(link, bit, prefabID)) order.push_back(c->instanceID);
            for (const std::uint64_t id : InstancesOf<T>(level, instance.id))
                if (std::ranges::find(order, id) == order.end()) order.push_back(id);

            for (size_t i = 0; i < order.size(); ++i) MoveInstance<T>(level, order[i], i);
        }
    }

    // Calls f(T*) for the component type whose CMP_* value is `bit`.
    template<typename F>
    void WithComponentType(const int bit, F&& f) {
        ForEachComponentType([&]<typename T>(T* tag) { if (Traits<T>::bit == bit) f(tag); });
    }

    int ComponentTypeOfKey(const std::string& key) {
        int result = -1;
        ForEachComponentType([&]<typename T>(T*) { if (key == Traits<T>::key) result = Traits<T>::bit; });
        return result;
    }

    // `field` is "color" or "publicValues/speed".
    void SetJsonField(json& object, const std::string& field, const json& value) {
        json* target = &object;
        std::string leaf = field;
        if (const size_t slash = field.find('/'); slash != std::string::npos) {
            target = &object[field.substr(0, slash)];
            leaf = field.substr(slash + 1);
        }

        if (value.is_null()) target->erase(leaf);
        else (*target)[leaf] = value;
    }

    json GetJsonField(const json& object, const std::string& field) {
        const json* source = &object;
        std::string leaf = field;
        if (const size_t slash = field.find('/'); slash != std::string::npos) {
            const std::string parent = field.substr(0, slash);
            if (!object.contains(parent)) return nullptr;
            source = &object[parent];
            leaf = field.substr(slash + 1);
        }
        return source->contains(leaf) ? (*source)[leaf] : json(nullptr);
    }

    // Copies fields of one of the instance's linked components into the
    // matching component of `edited` (a copy of the prefab). All of them
    // when `fields` is empty.
    template<typename T>
    void CopyFieldsToPrefab(const Level& level, const Entity& instance, Level& edited, const PrefabComponentLink& link,
                            const std::vector<std::string>& fields) {
        const T* instanceComponent = FindInstance<T>(level, link.instanceID);
        T* prefabComponent = FindInstance<T>(edited, link.prefabInstanceID);
        if (instanceComponent == nullptr || prefabComponent == nullptr) return;

        const IdMap toPrefab = MakeIdMap(instance, Prefab::RootOf(edited), true);
        const json instanceJson = ComparableJson(*instanceComponent, &toPrefab);
        json prefabJson = ComparableJson(*prefabComponent, nullptr);

        if (fields.empty()) {
            for (const auto& [field, value] : instanceJson.items())
                if (!IsInstanceOwnedField<T>(field)) prefabJson[field] = value;
        }
        else {
            for (const std::string& field : fields) SetJsonField(prefabJson, field, GetJsonField(instanceJson, field));
        }

        T updated = *prefabComponent;
        if (LevelSerialization::ComponentFieldsFromJson(prefabJson, updated))
            AssignInstance<T>(edited, link.prefabInstanceID, updated);
    }

    // Adds one of the instance's added components to `edited` and links it.
    template<typename T>
    void AddComponentToPrefab(const Level& level, Entity& instance, Level& edited, const std::uint64_t instanceID) {
        const T* instanceComponent = FindInstance<T>(level, instanceID);
        if (instanceComponent == nullptr) return;

        Entity& root = edited.entities.front();
        T data = *instanceComponent;
        RemapRefs(data, MakeIdMap(instance, root, true));

        const std::uint64_t prefabID = AddInstance<T>(edited, root, data);
        instance.prefabLink.components.push_back({Traits<T>::bit, instanceID, prefabID});
    }

    // Writes `edited` to the prefab's file and brings the level's instances up to date.
    bool SaveAndReplace(Level& level, const std::string& reference, const Level& edited, std::string* errorMessage) {
        if (!LevelSerialization::SavePrefabToFile(ProjectManager::GetAssetsPath() / reference, edited, errorMessage)) return false;
        Prefab::Replace(reference, edited, level);
        return true;
    }

    const PrefabComponentLink* FindLinkByInstanceID(const PrefabLink& link, const int type, const std::uint64_t instanceID) {
        for (const PrefabComponentLink& c : link.components)
            if (c.componentType == type && c.instanceID == instanceID) return &c;
        return nullptr;
    }

    // Applies one override path to `edited`.
    void ApplyPathToPrefab(const Level& level, const Entity& instance, Level& edited, const std::string& path) {
        Entity& root = edited.entities.front();

        if (path == "entity/enabled") { root.enabled = instance.enabled; return; }
        if (path == "entity/tags") { root.tagIds = instance.tagIds; return; }

        const size_t first = path.find('/');
        const size_t second = first == std::string::npos ? std::string::npos : path.find('/', first + 1);
        if (second == std::string::npos) return;

        const int type = ComponentTypeOfKey(path.substr(0, first));
        const std::uint64_t prefabID = std::stoull(path.substr(first + 1, second - first - 1));
        const std::string field = path.substr(second + 1);

        const PrefabComponentLink* link = FindLinkByPrefabID(instance.prefabLink, type, prefabID);
        if (link == nullptr) return;

        WithComponentType(type, [&]<typename T>(T*) { CopyFieldsToPrefab<T>(level, instance, edited, *link, {field}); });
    }

    // Copies `source` (an entity of `from`) into `to` as a new entity with
    // the same components in the same order, references to itself pointed
    // at the copy. `links` gets one {type, copy's ID, source's ID} entry per
    // component. Returns the new entity's ID.
    ID CopyEntity(Level& to, const Level& from, const Entity& source, std::vector<PrefabComponentLink>& links) {
        const bool uiEntity = from.ui_transforms.Has(source.id);

        // CreateEntity gives the new entity its Transform (or UITransform);
        // the source's is copied into it below.
        const ID newID = to.CreateEntity(uiEntity);
        Entity& entity = *to.GetEntity(newID);
        entity.name = source.name;
        entity.enabled = source.enabled;
        entity.tagIds = source.tagIds;

        IdMap toCopy;
        toCopy.fromEntity = source.id;
        toCopy.toEntity = newID;

        ForEachComponentType([&]<typename T>(T*) {
            for (const std::uint64_t sourceID : InstancesOf<T>(from, source.id)) {
                const T& data = *FindInstance<T>(from, sourceID);
                std::uint64_t copyID;

                if constexpr (IsSingleComponent<T>) {
                    copyID = InstancesOf<T>(to, newID).front();
                    AssignInstance<T>(to, copyID, data);
                }
                else copyID = AddInstance<T>(to, entity, data);

                toCopy.instances[{Traits<T>::bit, sourceID}] = copyID;
                links.push_back({Traits<T>::bit, copyID, sourceID});
            }
        });

        // Self-references now that every component exists.
        for (const PrefabComponentLink& link : links) {
            if (link.componentType == CMP_SCRIPT) {
                if (ComponentScript* script = to.scripts.GetByID(link.instanceID)) RemapRefs(*script, toCopy);
            }
            else if (link.componentType == CMP_FLIPBOOK) {
                if (ComponentFlipbook* flipbook = to.flipbooks.GetInstance(link.instanceID)) RemapRefs(*flipbook, toCopy);
            }
        }

        return newID;
    }
}

namespace Prefab {
    const Level* Get(const std::string& reference) {
        if (reference.empty()) return nullptr;

        auto& loaded = LoadedPrefabs();
        if (const auto found = loaded.find(reference); found != loaded.end()) return found->second.prefab.get();

        auto prefab = std::make_unique<Level>();
        std::string error;

        if (!LevelSerialization::LoadPrefabFromFile(ProjectManager::GetAssetsPath() / reference, *prefab, &error)) {
            spdlog::error("Prefab '{}' could not be loaded: {}", reference, error);
            prefab.reset();
        }

        return loaded.emplace(reference, LibraryEntry{std::move(prefab)}).first->second.prefab.get();
    }

    void Replace(const std::string& reference, const Level& newPrefab, Level& level) {
        const Level* oldPrefab = Get(reference);

        for (Entity& entity : level.entities) {
            if (entity.prefabLink.prefab != reference) continue;
            if (oldPrefab != nullptr) RefreshOverrides(level, entity, *oldPrefab);
            SyncInstance(level, entity, newPrefab);
        }

        LoadedPrefabs()[reference] = LibraryEntry{std::make_unique<Level>(newPrefab)};
    }

    Level* GetScriptCopy(const std::string& reference) {
        auto& copies = ScriptCopies();
        if (const auto found = copies.find(reference); found != copies.end()) return found->second.get();

        const Level* prefab = Get(reference);
        if (prefab == nullptr) return nullptr;
        return copies.emplace(reference, std::make_unique<Level>(*prefab)).first->second.get();
    }

    std::string ScriptCopyReference(const Level* level) {
        for (const auto& [reference, copy] : ScriptCopies())
            if (copy.get() == level) return reference;
        return {};
    }

    void ClearScriptCopies() {
        ScriptCopies().clear();
    }

    void Invalidate(const std::string& reference) {
        LoadedPrefabs().erase(reference);
    }

    void Clear() {
        LoadedPrefabs().clear();
    }

    const Entity& RootOf(const Level& prefab) {
        return prefab.entities.front();
    }

    Level MakeEmpty(const std::string& name, const bool uiEntity) {
        Level prefab;
        prefab.name = name;
        const ID id = prefab.CreateEntity(uiEntity);
        prefab.GetEntity(id)->name = name;
        return prefab;
    }

    ID Instantiate(Level& level, const Level& prefab, const std::string& reference) {
        std::vector<PrefabComponentLink> links;
        const ID newID = CopyEntity(level, prefab, RootOf(prefab), links);

        if (!reference.empty()) {
            Entity& entity = *level.GetEntity(newID);
            entity.prefabLink.prefab = reference;
            entity.prefabLink.components = std::move(links);
        }

        return newID;
    }

    bool CreateFromEntity(Level& level, const ID entityID, const std::string& reference, std::string* errorMessage) {
        Entity* source = level.GetEntity(entityID);
        if (source == nullptr) {
            if (errorMessage) *errorMessage = "the entity no longer exists";
            return false;
        }

        // links: {type, component in the prefab, component on the entity}.
        Level prefab;
        prefab.name = std::filesystem::path(reference).stem().string();

        std::vector<PrefabComponentLink> links;
        const ID rootID = CopyEntity(prefab, level, *source, links);

        // Where an instance stands is its own, so the prefab sits at the origin.
        if (ComponentTransform* transform = prefab.transforms.Get(rootID)) {
            transform->SetPosition({0.0f, 0.0f, 0.0f});
            transform->rotation = Quaternion::Identity();
        }

        if (!LevelSerialization::SavePrefabToFile(ProjectManager::GetAssetsPath() / reference, prefab, errorMessage)) return false;

        Invalidate(reference);

        // The entity becomes an instance of the file it was saved as.
        source = level.GetEntity(entityID);
        source->prefabLink = {};
        source->prefabLink.prefab = reference;
        for (const PrefabComponentLink& link : links)
            source->prefabLink.components.push_back({link.componentType, link.prefabInstanceID, link.instanceID});

        if (const Level* saved = Get(reference)) RefreshOverrides(level, *source, *saved);

        return true;
    }

    ID Instantiate(Level& level, const std::string& reference) {
        const Level* prefab = Get(reference);
        if (prefab == nullptr) return INVALID_ID;
        return Instantiate(level, *prefab, reference);
    }

    void RefreshOverrides(Level& level, Entity& instance, const Level& prefab) {
        PrefabLink& link = instance.prefabLink;
        const Entity& root = RootOf(prefab);

        // Links whose instance component was removed on the instance.
        std::erase_if(link.components, [&](const PrefabComponentLink& c) {
            bool exists = false;
            ForEachComponentType([&]<typename T>(T*) {
                if (Traits<T>::bit == c.componentType) exists = FindInstance<T>(level, c.instanceID) != nullptr;
            });
            return !exists;
        });

        std::vector<std::string> overrides;
        std::vector<PrefabRemovedComponent> removed;

        if (instance.enabled != root.enabled) overrides.emplace_back("entity/enabled");
        if (instance.tagIds != root.tagIds) overrides.emplace_back("entity/tags");

        const IdMap toPrefab = MakeIdMap(instance, root, true);
        ForEachComponentType([&]<typename T>(T*) {
            RefreshComponentOverrides<T>(level, instance, prefab, toPrefab, overrides, removed);
        });

        link.overrides = std::move(overrides);
        link.removed = std::move(removed);
    }

    void SyncInstance(Level& level, Entity& instance, const Level& prefab) {
        const Entity& root = RootOf(prefab);
        const PrefabLink& link = instance.prefabLink;

        if (!HasOverride(link, "entity/enabled")) instance.enabled = root.enabled;
        if (!HasOverride(link, "entity/tags")) instance.tagIds = root.tagIds;

        ForEachComponentType([&]<typename T>(T*) { SyncComponents<T>(level, instance, prefab); });
    }

    void SyncAllInstances(Level& level) {
        for (Entity& entity : level.entities) {
            if (!entity.prefabLink.IsInstance()) continue;
            if (const Level* prefab = Get(entity.prefabLink.prefab)) SyncInstance(level, entity, *prefab);
        }
    }

    void RefreshAllOverrides(Level& level) {
        for (Entity& entity : level.entities) {
            if (!entity.prefabLink.IsInstance()) continue;
            if (const Level* prefab = Get(entity.prefabLink.prefab)) RefreshOverrides(level, entity, *prefab);
        }
    }

    void Unpack(Entity& instance) {
        instance.prefabLink = {};
    }

    void RevertOverride(Level& level, Entity& instance, const std::string& path) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) return;

        RefreshOverrides(level, instance, *prefab);
        std::erase(instance.prefabLink.overrides, path);
        SyncInstance(level, instance, *prefab);
    }

    void RevertAll(Level& level, Entity& instance) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) return;

        PrefabLink& link = instance.prefabLink;

        // Components added on the instance go; removed ones come back.
        ForEachComponentType([&]<typename T>(T*) {
            if constexpr (IsSingleComponent<T>) return;
            for (const std::uint64_t id : InstancesOf<T>(level, instance.id)) {
                const bool linked = std::ranges::any_of(link.components, [&](const PrefabComponentLink& c) {
                    return c.componentType == Traits<T>::bit && c.instanceID == id;
                });
                if (!linked) RemoveInstance<T>(level, instance, id);
            }
        });

        RefreshOverrides(level, instance, *prefab);
        link.overrides.clear();
        link.removed.clear();
        SyncInstance(level, instance, *prefab);
    }
}

namespace Prefab {
    const char* ComponentKey(const int componentType) {
        const char* key = "";
        WithComponentType(componentType, [&]<typename T>(T*) { key = Traits<T>::key; });
        return key;
    }

    std::uint64_t PrefabInstanceIDOf(const Entity& instance, const int componentType, const std::uint64_t instanceID) {
        const PrefabComponentLink* link = FindLinkByInstanceID(instance.prefabLink, componentType, instanceID);
        return link != nullptr ? link->prefabInstanceID : 0;
    }

    std::vector<std::string> ComponentOverrides(const Level& level, const Entity& instance, const int componentType,
                                                const std::uint64_t instanceID) {
        std::vector<std::string> fields;

        const PrefabComponentLink* link = FindLinkByInstanceID(instance.prefabLink, componentType, instanceID);
        const Level* prefab = link != nullptr ? Get(instance.prefabLink.prefab) : nullptr;
        if (prefab == nullptr) return fields;

        WithComponentType(componentType, [&]<typename T>(T*) {
            const T* instanceComponent = FindInstance<T>(level, instanceID);
            const T* prefabComponent = FindInstance<T>(*prefab, link->prefabInstanceID);
            if (instanceComponent == nullptr || prefabComponent == nullptr) return;

            const IdMap toPrefab = MakeIdMap(instance, RootOf(*prefab), true);
            const std::string prefix = ComponentPath(Traits<T>::key, link->prefabInstanceID, "");

            ForEachField(Traits<T>::key, link->prefabInstanceID,
                ComparableJson(*instanceComponent, &toPrefab), ComparableJson(*prefabComponent, nullptr),
                [&](const std::string& path, const json& instanceValue, const json& prefabValue) {
                    if (instanceValue != prefabValue) fields.push_back(path.substr(prefix.size()));
                }, &IsInstanceOwnedField<T>);
        });

        return fields;
    }

    std::vector<std::string> EntityOverrides(const Entity& instance) {
        std::vector<std::string> fields;
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) return fields;

        const Entity& root = RootOf(*prefab);
        if (instance.enabled != root.enabled) fields.emplace_back("enabled");
        if (instance.tagIds != root.tagIds) fields.emplace_back("tags");
        return fields;
    }

    bool ApplyOverride(Level& level, Entity& instance, const std::string& path, std::string* errorMessage) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) {
            if (errorMessage) *errorMessage = "The prefab " + instance.prefabLink.prefab + " could not be loaded.";
            return false;
        }

        Level edited = *prefab;
        ApplyPathToPrefab(level, instance, edited, path);
        return SaveAndReplace(level, instance.prefabLink.prefab, edited, errorMessage);
    }

    bool ApplyComponent(Level& level, Entity& instance, const int componentType, const std::uint64_t instanceID,
                        std::string* errorMessage) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) {
            if (errorMessage) *errorMessage = "The prefab " + instance.prefabLink.prefab + " could not be loaded.";
            return false;
        }

        Level edited = *prefab;

        WithComponentType(componentType, [&]<typename T>(T*) {
            if (const PrefabComponentLink* link = FindLinkByInstanceID(instance.prefabLink, componentType, instanceID))
                CopyFieldsToPrefab<T>(level, instance, edited, *link, {});
            else if constexpr (!IsSingleComponent<T>)
                AddComponentToPrefab<T>(level, instance, edited, instanceID);
        });

        return SaveAndReplace(level, instance.prefabLink.prefab, edited, errorMessage);
    }

    void RevertComponent(Level& level, Entity& instance, const int componentType, const std::uint64_t instanceID) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) return;

        const PrefabComponentLink* link = FindLinkByInstanceID(instance.prefabLink, componentType, instanceID);

        if (link == nullptr) {
            WithComponentType(componentType, [&]<typename T>(T*) {
                if constexpr (!IsSingleComponent<T>) RemoveInstance<T>(level, instance, instanceID);
            });
            return;
        }

        RefreshOverrides(level, instance, *prefab);
        const std::string prefix = ComponentPath(ComponentKey(componentType), link->prefabInstanceID, "");
        std::erase_if(instance.prefabLink.overrides, [&](const std::string& path) { return path.starts_with(prefix); });
        SyncInstance(level, instance, *prefab);
    }

    void RestoreComponent(Level& level, Entity& instance, const int componentType, const std::uint64_t prefabInstanceID) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) return;

        RefreshOverrides(level, instance, *prefab);
        std::erase(instance.prefabLink.removed, PrefabRemovedComponent{componentType, prefabInstanceID});
        SyncInstance(level, instance, *prefab);
    }

    bool ApplyAll(Level& level, Entity& instance, std::string* errorMessage) {
        const Level* prefab = Get(instance.prefabLink.prefab);
        if (prefab == nullptr) {
            if (errorMessage) *errorMessage = "The prefab " + instance.prefabLink.prefab + " could not be loaded.";
            return false;
        }

        RefreshOverrides(level, instance, *prefab);

        Level edited = *prefab;
        Entity& root = edited.entities.front();

        for (const std::string& path : instance.prefabLink.overrides) ApplyPathToPrefab(level, instance, edited, path);

        // Removed on the instance: removed from the prefab.
        for (const PrefabRemovedComponent& removed : instance.prefabLink.removed)
            WithComponentType(removed.componentType, [&]<typename T>(T*) {
                if constexpr (!IsSingleComponent<T>)
                    if (FindInstance<T>(edited, removed.prefabInstanceID) != nullptr) RemoveInstance<T>(edited, root, removed.prefabInstanceID);
            });

        // Added on the instance: added to the prefab.
        ForEachComponentType([&]<typename T>(T*) {
            if constexpr (!IsSingleComponent<T>) {
                for (const std::uint64_t id : InstancesOf<T>(level, instance.id))
                    if (FindLinkByInstanceID(instance.prefabLink, Traits<T>::bit, id) == nullptr)
                        AddComponentToPrefab<T>(level, instance, edited, id);
            }
        });

        // Removed components are gone from the prefab now, so nothing to remember.
        instance.prefabLink.removed.clear();

        return SaveAndReplace(level, instance.prefabLink.prefab, edited, errorMessage);
    }
}
