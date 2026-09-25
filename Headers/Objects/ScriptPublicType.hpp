//
// Created by berke on 6/23/2026.
//

#ifndef TILKY_ENGINE_SCRIPTPUBLICTYPE_HPP
#define TILKY_ENGINE_SCRIPTPUBLICTYPE_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "EntityTypes.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector4.hpp"

// Every type a serialized script field can have. Primitive/math types store
// their value directly in ScriptValue below; the reference kinds store only
// a stable ID or path (never a name, never a pointer) so they survive
// renames and (for Entity/Component/Behaviour/Wall/Sector) resolve safely to
// nil once their target no longer exists. See EntityRefValue/
// ComponentRefValue/BehaviourRefValue/AssetRefValue/WallRefValue/
// SectorRefValue.
enum class ScriptValueType : std::uint8_t {
    Int,
    Float,
    Bool,
    String,
    Vector2,
    Vector3,
    Vector4,
    Enum,       // stored as int; see ScriptPublicField::enumOptions for the name<->value table
    Entity, // -> EntityRefValue
    Component,  // -> ComponentRefValue
    Behaviour,  // -> BehaviourRefValue (another script attached somewhere in the level)
    Asset,      // -> AssetRefValue (a path-based asset reference, e.g. a texture)
    Wall,       // -> WallRefValue
    Sector      // -> SectorRefValue
};

// A serialized reference to an Entity, by stable ID only.
// Resolves to an Entity in Lua, or nil if entityId no longer exists.
struct EntityRefValue {
    ID entityId = INVALID_ID;

    friend bool operator==(const EntityRefValue&, const EntityRefValue&) = default;
};

// A serialized reference to one engine component on one entity. componentType
// is a ComponentType (Components.hpp) value, stored as int here to avoid a
// circular include - Components.hpp already includes this header for
// ScriptValue itself.
struct ComponentRefValue {
    ID entityId = INVALID_ID;
    int componentType = -1;

    friend bool operator==(const ComponentRefValue&, const ComponentRefValue&) = default;
};

// A serialized reference to one specific Behaviour (script) instance attached
// to one entity. instanceId is the ComponentScript's globally-unique
// ScriptInstanceID - never a filename - so it stays unambiguous even when the
// target entity has several scripts, including duplicates of the same script.
struct BehaviourRefValue {
    ID entityId = INVALID_ID;
    ScriptInstanceID instanceId = INVALID_SCRIPT_INSTANCE_ID;

    friend bool operator==(const BehaviourRefValue&, const BehaviourRefValue&) = default;
};

// A serialized reference to a non-component asset (currently just textures -
// see AssetKind in AssetBrowser.hpp). Stores the same kind-appropriate
// reference string AssetBrowser::ToAssetReference() already produces
// (relative path, extension rules depending on kind), so it round-trips
// through the exact same rename-propagation path textures/sounds/scripts
// already use.
struct AssetRefValue {
    std::string path;

    friend bool operator==(const AssetRefValue&, const AssetRefValue&) = default;
};

// A serialized reference to a Wall, by stable ID only.
// Resolves to a Wall in Lua, or nil if wallId no longer exists.
struct WallRefValue {
    ID wallId = INVALID_ID;

    friend bool operator==(const WallRefValue&, const WallRefValue&) = default;
};

// A serialized reference to a Sector, by stable ID only.
// Resolves to a Sector in Lua, or nil if sectorId no longer exists.
struct SectorRefValue {
    ID sectorId = INVALID_ID;

    friend bool operator==(const SectorRefValue&, const SectorRefValue&) = default;
};

using ScriptValue = std::variant<
    int,
    float,
    bool,
    std::string,
    Vector2,
    Vector3,
    Vector4,
    EntityRefValue,
    ComponentRefValue,
    BehaviourRefValue,
    AssetRefValue,
    WallRefValue,
    SectorRefValue
>;

// One named option of an Enum-typed field, e.g. `enum(Idle,Walk,Run)` parses
// to {{"Idle",0},{"Walk",1},{"Run",2}}. The underlying ScriptValue is always
// a plain int (the option's value).
struct ScriptEnumOption {
    std::string name;
    int value = 0;
};

// One field of a script's schema, parsed from its `---@field` doc comments
// (see LuaScriptSystem::ExtractSchema in LuaSystem.cpp) - never by executing
// the script. This is intentionally plain data: the editor inspector, the
// serializer, and the future LuaLS stub generator all read the same struct.
struct ScriptPublicField {
    std::string name;
    ScriptValueType type;
    ScriptValue defaultValue;
    std::string displayName;

    // Only meaningful when type == Enum. Ordered name<->value table parsed
    // from the field's `enum(...)` annotation.
    std::vector<ScriptEnumOption> enumOptions;

    // Only meaningful when type == Component. Which ComponentType
    // (Components.hpp) the field accepts, e.g. CMP_RIGIDBODY for a field
    // annotated `---@field body Rigidbody`. -1 if unresolved/invalid.
    int componentType = -1;

    // Reserved for future list/array field support (see the scripting
    // redesign notes). Always false today - the schema parser recognizes and
    // rejects `Type[]` annotations with a warning instead of misinterpreting
    // them as a single value of an unknown type.
    bool isArray = false;
};

// The owner-agnostic, serialized half of one attached script: which script
// file, whether it is enabled, and its inspector-edited public values. Both
// kinds of script owner build on it - ComponentScript (Components.hpp) adds
// an entity ownerID, and Sector::scripts (Sector.hpp) stores it as-is, keyed
// by the owning sector's stable ID. Sharing the struct is what lets the
// runtime (LuaSystem.cpp), the serializer and the inspector treat entity
// and sector scripts through one implementation.
struct ScriptAttachmentData {
    // Unique among the scripts of one owner (globally unique for entities,
    // see ScriptComponentStorage; unique per sector for sector scripts).
    ScriptInstanceID instanceID = INVALID_SCRIPT_INSTANCE_ID;

    std::string fileName;
    bool enabled = true;

    // Serialized separately for each attached script.
    std::unordered_map<std::string, ScriptValue> publicValues;

    std::uint64_t schemaHash = 0;
};

#endif //TILKY_ENGINE_SCRIPTPUBLICTYPE_HPP
