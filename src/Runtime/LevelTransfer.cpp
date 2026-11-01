#include "Headers/Runtime/LevelTransfer.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Objects/EntityTransfer.hpp"
#include "Headers/Objects/UILayouts.hpp"

namespace {
    using EntityTransfer::Bundle;
    using EntityTransfer::IdMap;
    using EntityTransfer::ReferenceVisitor;

    template<typename T> struct Access;

#define TILKY_TRANSFER_ACCESS(Type, Bit, StorageName, LabelKey) \
    template<> struct Access<Type> { \
        static constexpr int bit = Bit; \
        static auto& Storage(Level& level) { return level.StorageName; } \
        static const auto& Storage(const Level& level) { return level.StorageName; } \
    };
    TILKY_COMPONENTS(TILKY_TRANSFER_ACCESS)
#undef TILKY_TRANSFER_ACCESS

    // Calls f(static_cast<T*>(nullptr)) once per component type.
    template<typename F>
    void ForEachComponentType(F&& f) {
#define TILKY_TRANSFER_VISIT(Type, Bit, Storage, LabelKey) f(static_cast<Type*>(nullptr));
        TILKY_COMPONENTS(TILKY_TRANSFER_VISIT)
#undef TILKY_TRANSFER_VISIT
    }

    // The owner's components of type T, in its order.
    template<typename T>
    std::vector<std::uint64_t> InstancesOf(const Level& level, const ID owner) {
        std::vector<std::uint64_t> ids;

        if constexpr (std::is_same_v<T, ComponentScript>) {
            for (const ComponentScript& script : level.scripts.components)
                if (script.ownerID == owner) ids.push_back(script.instanceID);
        }
        else {
            for (const ComponentInstanceID id : Access<T>::Storage(level).InstancesOf(owner)) ids.push_back(id);
        }

        return ids;
    }

    template<typename T>
    T* FindInstance(Level& level, const std::uint64_t instanceID) {
        if constexpr (std::is_same_v<T, ComponentScript>) return level.scripts.GetByID(instanceID);
        else return Access<T>::Storage(level).GetInstance(instanceID);
    }

    // A copy of `source` on `owner`, with a new instance ID. Returns the ID.
    template<typename T>
    std::uint64_t AddComponent(Level& level, const ID owner, T source) {
        if (Entity* entity = level.GetEntity(owner)) entity->componentsMask.set(Access<T>::bit);

        source.ownerID = owner;

        if constexpr (std::is_same_v<T, ComponentScript>) {
            ComponentScript& script = level.scripts.Add(owner);
            const ScriptInstanceID id = script.instanceID;
            script = std::move(source);
            script.instanceID = id;
            return id;
        }
        else {
            source.instanceID = INVALID_COMPONENT_INSTANCE_ID;
            return Access<T>::Storage(level).InsertLoaded(source).instanceID;
        }
    }

    // `target` takes `source`'s values. It stays the carried entity's
    // component: owner, instance ID, its own takeFromLevelCopy and the
    // owner's enabled state are kept, and so are the live engine resources
    // behind it (an Audio Source's sound source).
    template<typename T>
    void TakeValues(Level& level, T& target, const T& source) {
        T updated = source;
        updated.ownerID = target.ownerID;
        updated.instanceID = target.instanceID;
        updated.takeFromLevelCopy = target.takeFromLevelCopy;
        if constexpr (requires { updated.ownerEnabled; }) updated.ownerEnabled = target.ownerEnabled;

        if constexpr (std::is_same_v<T, ComponentAudioSource>) {
            updated.name = target.name;
            updated.isHeld = target.isHeld;
            updated.applied = {}; // sent to the sound source again
        }

        if constexpr (std::is_same_v<T, ComponentCollider>) {
            // Type and active state through the storage, which keeps its
            // partition sorted.
            const ComponentInstanceID id = target.instanceID;
            updated.type = target.type;
            updated.isActive = target.isActive;
            target = updated;
            level.colliders.SetType(id, source.type);
            level.colliders.SetActive(id, source.isActive);
        }
        else target = std::move(updated);
    }

    // Merges the level copy `copyID` into the carried entity `carriedID`
    // and removes the copy. `merged` gets copy -> carried for the entity and
    // each of its components.
    void MergeCopy(Level& level, const ID carriedID, const ID copyID, IdMap& merged, std::vector<ScriptInstanceID>& restart) {
        ForEachComponentType([&]<typename T>(T*) {
            constexpr int bit = Access<T>::bit;

            const std::vector<std::uint64_t> copyIDs = InstancesOf<T>(level, copyID);
            const std::vector<std::uint64_t> keptIDs = InstancesOf<T>(level, carriedID);
            std::vector<bool> paired(keptIDs.size(), false);

            for (size_t i = 0; i < copyIDs.size(); ++i) {
                // A copy: adding a component can move the storage.
                const T source = *FindInstance<T>(level, copyIDs[i]);

                std::optional<size_t> match;
                if constexpr (std::is_same_v<T, ComponentScript>) {
                    // The copy's n-th script of a file goes with the carried
                    // entity's n-th script of that file.
                    for (size_t k = 0; k < keptIDs.size(); ++k) {
                        if (paired[k]) continue;
                        if (const ComponentScript* kept = level.scripts.GetByID(keptIDs[k]); kept && kept->fileName == source.fileName) {
                            match = k;
                            break;
                        }
                    }
                }
                else if (i < keptIDs.size()) match = i;

                if (!match) {
                    merged.components[{bit, copyIDs[i]}] = AddComponent<T>(level, carriedID, source);
                    continue;
                }

                paired[*match] = true;
                merged.components[{bit, copyIDs[i]}] = keptIDs[*match];

                T& target = *FindInstance<T>(level, keptIDs[*match]);
                if (!target.takeFromLevelCopy) continue;

                if constexpr (std::is_same_v<T, ComponentScript>) {
                    target.enabled = source.enabled;
                    target.publicValues = source.publicValues;
                    target.schemaHash = source.schemaHash;
                    restart.push_back(target.instanceID);
                }
                else TakeValues<T>(level, target, source);
            }
        });

        merged.entities[copyID] = carriedID;
        level.DestroyEntity(copyID);
    }

    // What a carried entity points at that stays behind is cleared: those
    // IDs mean other things in the next level.
    void ClearOutsideReferences(Bundle& carried, const std::unordered_set<ID>& kept, const std::string& nextLevel) {
        int cleared = 0;

        ReferenceVisitor visitor;
        visitor.entity = [&](ID& id) {
            if (kept.contains(id)) return;
            id = INVALID_ID;
            ++cleared;
        };
        visitor.component = [&](int, ID& owner, std::uint64_t& instance) {
            if (kept.contains(owner)) return;
            owner = INVALID_ID;
            instance = 0;
            ++cleared;
        };
        visitor.sector = visitor.wall = visitor.linedef = [&](ID& id) {
            id = INVALID_ID;
            ++cleared;
        };

        for (Entity& entity : carried.entities) {
            cleared = 0;
            EntityTransfer::VisitEvents(entity.events, entity.dispatches, visitor);

            if (cleared > 0)
                spdlog::warn("Entity '{}' (#{}) was carried into level '{}': {} of its event and dispatch targets were "
                             "in the old level and now point at nothing", entity.name, entity.id, nextLevel, cleared);
        }

        // A running script's fields already hold their live values; these
        // are only what a restarted script starts with.
        for (ComponentScript& script : carried.scripts) EntityTransfer::VisitScriptValues(script.publicValues, visitor);
    }

    bool HasLayoutEntity(const Level& level, const std::uint64_t layoutID) {
        return std::ranges::any_of(level.entities, [layoutID](const Entity& entity) { return entity.layoutID == layoutID; });
    }

    void InstantiateMissingLayouts(Level& level) {
        for (const std::uint64_t layoutID : level.worldSettings.uiLayouts)
            if (!HasLayoutEntity(level, layoutID)) UILayouts::Instantiate(level, layoutID);

        UILayouts::SortUIDrawOrder(level);
    }
}

namespace LevelTransfer {
    std::vector<ID> CarriedEntities(const Level& current, const std::vector<std::uint64_t>& nextLayouts) {
        std::vector<ID> carried;

        for (const Entity& entity : current.entities) {
            const bool keptLayout = entity.layoutID != 0 && std::ranges::find(nextLayouts, entity.layoutID) != nextLayouts.end();
            if (entity.persistent || keptLayout) carried.push_back(entity.id);
        }

        return carried;
    }

    Result Merge(const Level& current, const std::vector<ID>& carriedIDs, Level& next) {
        Result result;

        Bundle carried = EntityTransfer::Copy(current, carriedIDs);

        std::unordered_set<ID> carriedSet;
        for (const Entity& entity : carried.entities) {
            carriedSet.insert(entity.id);
            result.carried.push_back(entity.id);
        }

        ClearOutsideReferences(carried, carriedSet, next.name);

        // Every component ID the carried entities keep.
        std::set<std::pair<int, std::uint64_t>> carriedComponents;
#define TILKY_TRANSFER_CARRIED_IDS(Type, Bit, Storage, LabelKey) \
        for (const Type& component : carried.Storage) carriedComponents.insert({Bit, component.instanceID});
        TILKY_COMPONENTS(TILKY_TRANSFER_CARRIED_IDS)
#undef TILKY_TRANSFER_CARRIED_IDS

        // The next level's entities using one of those IDs move aside: out,
        // the carried ones in with their IDs, then back in with new ones.
        std::vector<ID> clashing;
        for (const Entity& entity : next.entities) {
            bool clash = carriedSet.contains(entity.id);

            ForEachComponentType([&]<typename T>(T*) {
                if (clash) return;
                for (const std::uint64_t id : InstancesOf<T>(next, entity.id))
                    if (carriedComponents.contains({Access<T>::bit, id})) clash = true;
            });

            if (clash) clashing.push_back(entity.id);
        }

        const Bundle moved = EntityTransfer::Copy(next, clashing);
        for (const ID id : clashing) next.DestroyEntity(id);

        EntityTransfer::Insert(next, carried);
        const IdMap renumbered = EntityTransfer::InsertAsNew(next, moved);

        // The next level's own data follows the new IDs. The moved entities
        // already point at each other; their references hold new IDs now,
        // which never match an old one.
        if (!renumbered.Empty()) {
            std::vector<ID> ownEntities;
            for (const Entity& entity : next.entities)
                if (!carriedSet.contains(entity.id)) ownEntities.push_back(entity.id);

            const ReferenceVisitor remap = EntityTransfer::Remapper(renumbered);
            EntityTransfer::VisitEntities(next, ownEntities, remap);
            EntityTransfer::VisitGeometry(next, remap);
        }

        // Level copies.
        IdMap merged;
        std::unordered_set<ID> usedCopies;

        for (const Entity& source : carried.entities) {
            if (source.persistenceKey.empty()) continue;

            const bool uiEntity = next.ui_transforms.Has(source.id);
            ID copyID = INVALID_ID;

            for (const Entity& candidate : next.entities) {
                if (carriedSet.contains(candidate.id) || usedCopies.contains(candidate.id)) continue;
                if (candidate.persistenceKey != source.persistenceKey) continue;
                if (next.ui_transforms.Has(candidate.id) != uiEntity) continue;

                copyID = candidate.id;
                break;
            }

            if (copyID == INVALID_ID) continue;

            usedCopies.insert(copyID);
            MergeCopy(next, source.id, copyID, merged, result.restartScripts);
        }

        // What pointed at a copy points at its carried entity. Carried data
        // never holds a copy's IDs (those are the next level's), so it can
        // be visited too, which covers the components moved over from the copies.
        if (!merged.Empty()) {
            const ReferenceVisitor remap = EntityTransfer::Remapper(merged);
            EntityTransfer::VisitEntities(next, EntityTransfer::AllEntityIDs(next), remap);
            EntityTransfer::VisitGeometry(next, remap);
        }

        // A carried active camera or player controller stays the active one.
        const auto carriedActive = [&carriedSet](const auto& storage) {
            return std::ranges::any_of(storage.components, [&](const auto& component) {
                return component.isActive && carriedSet.contains(component.ownerID);
            });
        };

        if (carriedActive(next.cameras))
            for (ComponentCamera& camera : next.cameras.components)
                if (!carriedSet.contains(camera.ownerID)) camera.isActive = false;

        if (carriedActive(next.playerControllers))
            for (ComponentPlayerController& controller : next.playerControllers.components)
                if (!carriedSet.contains(controller.ownerID)) controller.isActive = false;

        // New to the running game from here on (see Entity::sinceEpoch).
        for (Entity& entity : next.entities)
            if (!carriedSet.contains(entity.id)) entity.sinceEpoch = LevelEpoch::current;

        InstantiateMissingLayouts(next);
        return result;
    }

    void AddLayouts(Level& level) {
        InstantiateMissingLayouts(level);
    }
}
