// Headers/Objects/Types.hpp

#ifndef TILKY_ENGINE_TYPES_HPP
#define TILKY_ENGINE_TYPES_HPP

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using ID = uint32_t;
using LevelID = ID;
using UIElementID = ID;

using TagId = uint16_t;
using EventId = TagId;
using DispatchId = EventId;
using EventVariableID = ID;

constexpr ID INVALID_ID = std::numeric_limits<ID>::max();

// Set on the IDs of project-wide event variables (GlobalEventVariables),
// clear on a level's own (WorldSettings). Both count up from 1, so the bit is
// what tells a condition which table to read.
constexpr EventVariableID GLOBAL_EVENT_VARIABLE_BIT = EventVariableID{1} << 31;

[[nodiscard]] constexpr bool IsGlobalEventVariable(const EventVariableID id) noexcept {
    return (id & GLOBAL_EVENT_VARIABLE_BIT) != 0;
}
constexpr ID INVALID_ENTITY_ID = INVALID_ID;

using ScriptInstanceID = std::uint64_t;
constexpr ScriptInstanceID INVALID_SCRIPT_INSTANCE_ID = 0;

// Identifies one component among all components of its type in a level
// (see ComponentStorage). Saved with the level.
using ComponentInstanceID = std::uint64_t;
constexpr ComponentInstanceID INVALID_COMPONENT_INSTANCE_ID = 0;

// Ties one of a prefab instance's components to the component in the
// .prefab it came from. instanceID/prefabInstanceID are ComponentInstanceIDs,
// or ScriptInstanceIDs for scripts (componentType == CMP_SCRIPT).
struct PrefabComponentLink {
    int componentType = -1;
    std::uint64_t instanceID = 0;
    std::uint64_t prefabInstanceID = 0;

    friend bool operator==(const PrefabComponentLink&, const PrefabComponentLink&) = default;
};

// A prefab component the instance doesn't have (it was removed on the instance).
struct PrefabRemovedComponent {
    int componentType = -1;
    std::uint64_t prefabInstanceID = 0;

    friend bool operator==(const PrefabRemovedComponent&, const PrefabRemovedComponent&) = default;
};

// Set on an entity placed from a .prefab (see Prefab.hpp). Everything not
// listed in `overrides`, and every linked component, follows the prefab.
// Components without a link were added on the instance.
struct PrefabLink {
    std::string prefab; // asset reference, e.g. "Prefabs/Goblin.prefab"; empty = not an instance
    std::vector<PrefabComponentLink> components;
    std::vector<PrefabRemovedComponent> removed;
    // Field paths whose value differs from the prefab - see Prefab.hpp.
    std::vector<std::string> overrides;

    [[nodiscard]] bool IsInstance() const { return !prefab.empty(); }
};

#endif // TILKY_ENGINE_TYPES_HPP