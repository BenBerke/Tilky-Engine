#include "Headers/Objects/EntityTransfer.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace {
    using EntityTransfer::ReferenceVisitor;

    void VisitEntity(const ReferenceVisitor& visitor, ID& id) {
        if (visitor.entity && id != INVALID_ID) visitor.entity(id);
    }

    void VisitComponent(const ReferenceVisitor& visitor, const int type, ID& owner, std::uint64_t& instance) {
        if (owner == INVALID_ID) return;

        if (visitor.component) visitor.component(type, owner, instance);
        else VisitEntity(visitor, owner);
    }

    void VisitCall(const ReferenceVisitor& visitor, DirectCall& call) {
        if (call.targetID == INVALID_ID) return;

        switch (call.target) {
            case CallTarget::ENTITY: VisitEntity(visitor, call.targetID); break;
            case CallTarget::SECTOR: if (visitor.sector) visitor.sector(call.targetID); break;
            case CallTarget::WALL: if (visitor.wall) visitor.wall(call.targetID); break;
            case CallTarget::LINEDEF: if (visitor.linedef) visitor.linedef(call.targetID); break;
            default: break;
        }
    }

    void VisitTeleport(const ReferenceVisitor& visitor, TeleportSettings& teleport) {
        if (teleport.targetID == INVALID_ID) return;

        if (teleport.target == TeleportTarget::ENTITY) VisitEntity(visitor, teleport.targetID);
        else if (teleport.target == TeleportTarget::SECTOR && visitor.sector) visitor.sector(teleport.targetID);
    }

    // A flipbook drives a Sprite of its own entity, or a UI Sprite on a UI entity.
    void VisitFlipbook(const ReferenceVisitor& visitor, ComponentFlipbook& flipbook, const bool uiEntity) {
        if (flipbook.spriteInstanceID == INVALID_COMPONENT_INSTANCE_ID) return;

        if (visitor.ownComponent) visitor.ownComponent(uiEntity ? CMP_UI_SPRITE : CMP_SPRITE, flipbook.spriteInstanceID);
    }

    void VisitPrefabLink(const ReferenceVisitor& visitor, Entity& entity) {
        if (!visitor.ownComponent) return;
        for (PrefabComponentLink& link : entity.prefabLink.components) visitor.ownComponent(link.componentType, link.instanceID);
    }

    // Puts one component into its storage as it is (its IDs included).
    template<typename Storage, typename T>
    std::uint64_t InsertComponent(Storage& storage, const T& component) {
        if constexpr (std::is_same_v<T, ComponentScript>) {
            ComponentScript& script = storage.Add(component.ownerID, component.instanceID);
            const ScriptInstanceID id = script.instanceID;
            script = component;
            script.instanceID = id;
            return id;
        }
        else return storage.InsertLoaded(component).instanceID;
    }

    // Copies of the owner's components, in its order.
    template<typename Storage, typename T>
    void CopyOwned(const Storage& storage, const ID owner, std::vector<T>& out) {
        if constexpr (std::is_same_v<T, ComponentScript>) {
            for (const ComponentScript& script : storage.components)
                if (script.ownerID == owner) out.push_back(script);
        }
        else {
            for (const T* component : storage.GetAll(owner)) out.push_back(*component);
        }
    }

    std::unordered_map<ID, ComponentMask> MasksOf(const EntityTransfer::Bundle& bundle) {
        std::unordered_map<ID, ComponentMask> masks;

#define TILKY_BUNDLE_MASK(Type, Bit, Storage, LabelKey) \
        for (const Type& component : bundle.Storage) masks[component.ownerID].set(Bit);
        TILKY_COMPONENTS(TILKY_BUNDLE_MASK)
#undef TILKY_BUNDLE_MASK

        return masks;
    }
}

namespace EntityTransfer {
    void IdMap::Merge(const IdMap& other) {
        for (const auto& [from, to] : other.entities) entities[from] = to;
        for (const auto& [from, to] : other.components) components[from] = to;
    }

    Bundle Copy(const Level& level, const std::vector<ID>& entityIDs) {
        Bundle bundle;

        for (const ID id : entityIDs) {
            const Entity* entity = level.GetEntity(id);
            if (entity == nullptr) continue;

            bundle.entities.push_back(*entity);

#define TILKY_BUNDLE_COPY(Type, Bit, Storage, LabelKey) CopyOwned(level.Storage, id, bundle.Storage);
            TILKY_COMPONENTS(TILKY_BUNDLE_COPY)
#undef TILKY_BUNDLE_COPY
        }

        return bundle;
    }

    void VisitEvents(std::vector<Event>& events, std::vector<Dispatch>& dispatches, const ReferenceVisitor& visitor) {
        for (Event& event : events)
            for (DirectCall& call : event.calls) VisitCall(visitor, call);

        for (Dispatch& dispatch : dispatches) {
            for (DispatchFlow& flow : dispatch.flows) VisitCall(visitor, flow.call);

            std::visit([&visitor]<typename P>(P& payload) {
                if constexpr (std::is_same_v<P, PayloadDispatchRelay>) VisitCall(visitor, payload.call);
                else if constexpr (std::is_base_of_v<TeleportSettings, P>) VisitTeleport(visitor, payload);
                else if constexpr (std::is_same_v<P, PayloadDispatchEntityPathfinder>) {
                    if (payload.targetID == INVALID_ID) return;
                    if (payload.action == PathfinderAction::FOLLOW_ENTITY) VisitEntity(visitor, payload.targetID);
                    else if (payload.action == PathfinderAction::GO_TO_SECTOR && visitor.sector) visitor.sector(payload.targetID);
                }
                else if constexpr (std::is_same_v<P, PayloadDispatchEntityMonsterAI>) {
                    if (payload.action == MonsterAIAction::WAKE_ON_ENTITY) VisitEntity(visitor, payload.targetID);
                }
            }, dispatch.payload);
        }
    }

    void VisitScriptValues(std::unordered_map<std::string, ScriptValue>& values, const ReferenceVisitor& visitor) {
        for (auto& [name, value] : values) {
            if (auto* entityRef = std::get_if<EntityRefValue>(&value)) VisitEntity(visitor, entityRef->entityId);
            else if (auto* componentRef = std::get_if<ComponentRefValue>(&value)) {
                std::uint64_t instance = componentRef->instanceId;
                VisitComponent(visitor, componentRef->componentType, componentRef->entityId, instance);
                componentRef->instanceId = instance;
            }
            else if (auto* behaviourRef = std::get_if<BehaviourRefValue>(&value)) {
                std::uint64_t instance = behaviourRef->instanceId;
                VisitComponent(visitor, CMP_SCRIPT, behaviourRef->entityId, instance);
                behaviourRef->instanceId = instance;
            }
            else if (auto* wallRef = std::get_if<WallRefValue>(&value)) {
                if (visitor.wall && wallRef->wallId != INVALID_ID) visitor.wall(wallRef->wallId);
            }
            else if (auto* sectorRef = std::get_if<SectorRefValue>(&value)) {
                if (visitor.sector && sectorRef->sectorId != INVALID_ID) visitor.sector(sectorRef->sectorId);
            }
        }
    }

    void VisitBundle(Bundle& bundle, const ReferenceVisitor& visitor) {
        for (Entity& entity : bundle.entities) {
            VisitEvents(entity.events, entity.dispatches, visitor);
            VisitPrefabLink(visitor, entity);
        }

        for (ComponentScript& script : bundle.scripts) VisitScriptValues(script.publicValues, visitor);

        for (ComponentFlipbook& flipbook : bundle.flipbooks) {
            const bool uiEntity = std::ranges::any_of(bundle.ui_transforms, [&flipbook](const ComponentUITransform& transform) {
                return transform.ownerID == flipbook.ownerID;
            });
            VisitFlipbook(visitor, flipbook, uiEntity);
        }

        for (ComponentPathfinder& pathfinder : bundle.pathfinders) VisitEntity(visitor, pathfinder.targetEntityID);
    }

    void VisitEntities(Level& level, const std::vector<ID>& entityIDs, const ReferenceVisitor& visitor) {
        const std::unordered_set<ID> owners(entityIDs.begin(), entityIDs.end());

        for (const ID id : entityIDs) {
            Entity* entity = level.GetEntity(id);
            if (entity == nullptr) continue;

            VisitEvents(entity->events, entity->dispatches, visitor);
            VisitPrefabLink(visitor, *entity);
        }

        for (ComponentScript& script : level.scripts.components)
            if (owners.contains(script.ownerID)) VisitScriptValues(script.publicValues, visitor);

        for (ComponentFlipbook& flipbook : level.flipbooks.components)
            if (owners.contains(flipbook.ownerID)) VisitFlipbook(visitor, flipbook, level.ui_transforms.Has(flipbook.ownerID));

        for (ComponentPathfinder& pathfinder : level.pathfinders.components)
            if (owners.contains(pathfinder.ownerID)) VisitEntity(visitor, pathfinder.targetEntityID);
    }

    void VisitGeometry(Level& level, const ReferenceVisitor& visitor) {
        for (Wall& wall : level.walls) VisitEvents(wall.events, wall.dispatches, visitor);
        for (Linedef& linedef : level.linedefs) VisitEvents(linedef.events, linedef.dispatches, visitor);

        for (Sector& sector : level.sectors) {
            VisitEvents(sector.events, sector.dispatches, visitor);
            for (SectorScript& script : sector.scripts) VisitScriptValues(script.publicValues, visitor);
        }

        VisitEvents(level.worldSettings.events, level.worldSettings.dispatches, visitor);
    }

    ReferenceVisitor Remapper(const IdMap& map) {
        ReferenceVisitor visitor;

        visitor.entity = [&map](ID& id) {
            if (const auto it = map.entities.find(id); it != map.entities.end()) id = it->second;
        };

        // Only components of a remapped entity: instance IDs of other
        // entities' components mean something else.
        visitor.component = [&map](const int type, ID& owner, std::uint64_t& instance) {
            const auto ownerIt = map.entities.find(owner);
            if (ownerIt == map.entities.end()) return;
            owner = ownerIt->second;

            // One per entity: the entity says which.
            if (type == CMP_TRANSFORM || type == CMP_UI_TRANSFORM) return;

            const auto it = map.components.find({type, instance});
            instance = it != map.components.end() ? it->second : 0;
        };

        // Instance IDs are unique per type in a level, so the one a map
        // lists is this entity's own.
        visitor.ownComponent = [&map](const int type, std::uint64_t& instance) {
            if (const auto it = map.components.find({type, instance}); it != map.components.end()) instance = it->second;
        };

        return visitor;
    }

    void Insert(Level& level, const Bundle& bundle) {
        const std::unordered_map<ID, ComponentMask> masks = MasksOf(bundle);

        for (const Entity& source : bundle.entities) {
            Entity& entity = level.entities.emplace_back(source);
            entity.attachedLevelId = level.id;

            const auto mask = masks.find(source.id);
            entity.componentsMask = mask != masks.end() ? mask->second : ComponentMask{};

            level.nextEntityID = std::max(level.nextEntityID, source.id + 1);
        }

#define TILKY_BUNDLE_INSERT(Type, Bit, Storage, LabelKey) \
        for (const Type& component : bundle.Storage) InsertComponent(level.Storage, component);
        TILKY_COMPONENTS(TILKY_BUNDLE_INSERT)
#undef TILKY_BUNDLE_INSERT
    }

    IdMap InsertAsNew(Level& level, const Bundle& bundle) {
        IdMap map;

        ID next = level.nextEntityID;
        for (const Entity& entity : bundle.entities) {
            while (level.GetEntity(next) != nullptr) ++next;
            map.entities[entity.id] = next++;
        }

        const std::unordered_map<ID, ComponentMask> masks = MasksOf(bundle);
        std::vector<ID> inserted;

        for (const Entity& source : bundle.entities) {
            Entity& entity = level.entities.emplace_back(source);
            entity.id = map.entities.at(source.id);
            entity.attachedLevelId = level.id;

            const auto mask = masks.find(source.id);
            entity.componentsMask = mask != masks.end() ? mask->second : ComponentMask{};

            level.nextEntityID = std::max(level.nextEntityID, entity.id + 1);
            inserted.push_back(entity.id);
        }

#define TILKY_BUNDLE_INSERT_NEW(Type, Bit, Storage, LabelKey) \
        for (Type component : bundle.Storage) { \
            const std::uint64_t oldID = component.instanceID; \
            component.ownerID = map.entities.at(component.ownerID); \
            if constexpr (std::is_same_v<Type, ComponentScript>) component.instanceID = INVALID_SCRIPT_INSTANCE_ID; \
            else component.instanceID = INVALID_COMPONENT_INSTANCE_ID; \
            map.components[{Bit, oldID}] = InsertComponent(level.Storage, component); \
        }
        TILKY_COMPONENTS(TILKY_BUNDLE_INSERT_NEW)
#undef TILKY_BUNDLE_INSERT_NEW

        // References between the copies (still in the old IDs) now point at
        // the copies. Only references are rewritten: the components' owners
        // already have the new IDs.
        VisitEntities(level, inserted, Remapper(map));

        return map;
    }

    std::vector<ID> AllEntityIDs(const Level& level) {
        std::vector<ID> ids;
        ids.reserve(level.entities.size());
        for (const Entity& entity : level.entities) ids.push_back(entity.id);
        return ids;
    }
}
