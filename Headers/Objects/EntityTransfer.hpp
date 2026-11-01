#ifndef TILKY_ENGINE_ENTITYTRANSFER_HPP
#define TILKY_ENGINE_ENTITYTRANSFER_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Objects/Level.hpp"

// Moving entities between levels: UI layouts copied into a level, Paste UI,
// and the entities a level switch carries (see LevelTransfer). Entities and
// components are identified by IDs that are only unique inside one level,
// so moving them means picking IDs and rewriting what points at them.
namespace EntityTransfer {
    // Some entities and every component they own, each type in its owner's
    // order, cut loose from any level.
    struct Bundle {
        std::vector<Entity> entities;

#define TILKY_BUNDLE_STORAGE(Type, Bit, Storage, LabelKey) std::vector<Type> Storage;
        TILKY_COMPONENTS(TILKY_BUNDLE_STORAGE)
#undef TILKY_BUNDLE_STORAGE
    };

    // Copies of the entities that exist, in this order, with their components.
    [[nodiscard]] Bundle Copy(const Level& level, const std::vector<ID>& entityIDs);

    // Old ID -> new ID. Components are keyed by (CMP_*, instance ID); a
    // script's is its ScriptInstanceID under CMP_SCRIPT.
    struct IdMap {
        std::unordered_map<ID, ID> entities;
        std::map<std::pair<int, std::uint64_t>, std::uint64_t> components;

        [[nodiscard]] bool Empty() const { return entities.empty() && components.empty(); }
        // Adds every entry of `other`.
        void Merge(const IdMap& other);
    };

    // What to do with each reference found in some data. Every member is
    // optional. A component reference goes to `component` (to `entity`, for
    // its owner, without one).
    struct ReferenceVisitor {
        std::function<void(ID& entityID)> entity;
        std::function<void(int componentType, ID& ownerID, std::uint64_t& instanceID)> component;
        // An entity's references to its own components: a flipbook's sprite,
        // the prefab link's components.
        std::function<void(int componentType, std::uint64_t& instanceID)> ownComponent;
        std::function<void(ID& sectorID)> sector;
        std::function<void(ID& wallID)> wall;
        std::function<void(ID& linedefID)> linedef;
    };

    // Event calls, flow steps, relays and teleports.
    void VisitEvents(std::vector<Event>& events, std::vector<Dispatch>& dispatches, const ReferenceVisitor& visitor);
    // Entity / Component / Behaviour / Wall / Sector script fields.
    void VisitScriptValues(std::unordered_map<std::string, ScriptValue>& values, const ReferenceVisitor& visitor);
    // An entity's events and dispatches, plus every component's references
    // (script fields, a flipbook's sprite) and its prefab link's components.
    void VisitBundle(Bundle& bundle, const ReferenceVisitor& visitor);
    // The same, for these entities of a level.
    void VisitEntities(Level& level, const std::vector<ID>& entityIDs, const ReferenceVisitor& visitor);
    // What isn't on an entity: walls', linedefs', sectors' and the level's
    // own events and dispatches, and sector scripts' fields.
    void VisitGeometry(Level& level, const ReferenceVisitor& visitor);

    // Rewrites references to what `map` lists; others are kept.
    [[nodiscard]] ReferenceVisitor Remapper(const IdMap& map);

    // Puts the bundle into the level with the IDs it has. Every entity ID
    // and component instance ID in it must be free there.
    void Insert(Level& level, const Bundle& bundle);

    // Puts the bundle into the level as new entities, with new entity and
    // component IDs. References between the bundle's own entities and
    // components follow; references to anything else are kept as they are.
    // Returns old -> new for every entity and component.
    IdMap InsertAsNew(Level& level, const Bundle& bundle);

    // Every entity of the level, in its order.
    [[nodiscard]] std::vector<ID> AllEntityIDs(const Level& level);
}

#endif //TILKY_ENGINE_ENTITYTRANSFER_HPP
