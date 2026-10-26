#ifndef TILKY_ENGINE_PREFAB_HPP
#define TILKY_ENGINE_PREFAB_HPP

#include <string>
#include <string_view>
#include <vector>

#include "Headers/Objects/Level.hpp"

// Prefabs: one entity saved on its own in a .prefab file (level.bson's
// layout, cut to that entity - see LevelSerialization::SavePrefabToFile).
// In memory a prefab is a Level holding exactly that one entity.
//
// An entity placed from a prefab is a normal entity that also carries a
// PrefabLink (Entity::prefabLink): which .prefab it came from, which of its
// components came from which prefab component, the prefab components it
// removed, and the fields it overrides. Everything else follows the prefab:
// SyncInstance copies the prefab's current values over every field that
// isn't overridden. A field counts as overridden whenever its value differs
// from the prefab's (RefreshOverrides), so setting it back to the prefab's
// value clears it. The entity's name and its position and rotation
// (Transform, or UITransform for UI entities) are always the instance's own.
//
// Override paths: "entity/enabled", "entity/tags", "entity/events",
// "entity/dispatches" (each list as a whole), and
// "<components key>/<prefab instance ID>/<field>" using level.bson's keys,
// e.g. "sprites/2/color". A script's public fields are one level deeper:
// "scripts/3/publicValues/speed".
namespace Prefab {
    inline constexpr std::string_view kExtension = ".prefab";

    // The loaded prefab for an asset reference (Assets-relative path with
    // extension, e.g. "Prefabs/Goblin.prefab"), cached. nullptr if the file
    // is missing or broken (logged once until invalidated).
    const Level* Get(const std::string& reference);

    // Replaces the cached copy (the editor saved the file) and brings every
    // instance in `level` up to date: their overrides are worked out against
    // the old version, then everything else takes the new one.
    void Replace(const std::string& reference, const Level& newPrefab, Level& level);

    // A copy of the prefab for scripts (an Entity field holding a prefab
    // gives its entity): changing it never touches the file or the cached
    // prefab, only what Game.CreateEntity(it) spawns. Kept until
    // ClearScriptCopies (each play session starts fresh). nullptr if the
    // prefab can't be loaded.
    Level* GetScriptCopy(const std::string& reference);
    // The prefab a script copy is of, or "" when `level` isn't one.
    std::string ScriptCopyReference(const Level* level);
    void ClearScriptCopies();

    // Forgets one prefab, or all, so the next Get reloads the file.
    void Invalidate(const std::string& reference);
    void Clear();

    // The prefab's one entity.
    const Entity& RootOf(const Level& prefab);

    // A new prefab file's contents: an entity named after the file with just
    // a Transform (or a UITransform for a UI prefab).
    Level MakeEmpty(const std::string& name, bool uiEntity);

    // Copies the prefab's entity into `level` as a new entity, components in
    // the prefab's order, references to itself pointed at the copy. With
    // `reference` non-empty the copy is a linked instance of that prefab.
    // Returns the new entity's ID.
    ID Instantiate(Level& level, const Level& prefab, const std::string& reference);

    // Same, loading the prefab by reference. INVALID_ID if it can't be loaded.
    ID Instantiate(Level& level, const std::string& reference);

    // Saves a copy of the level entity as a new prefab at `reference`
    // (relative to Assets, with extension; an existing file is overwritten),
    // at the origin, and makes the entity a linked instance of it.
    bool CreateFromEntity(Level& level, ID entityID, const std::string& reference, std::string* errorMessage = nullptr);

    // Re-works out instance.prefabLink's overrides and removed components by
    // comparing the instance with `prefab`, the version it follows.
    void RefreshOverrides(Level& level, Entity& instance, const Level& prefab);

    // Copies `prefab`'s values over every field of `instance` that isn't
    // overridden, removes components whose prefab component is gone, and
    // adds prefab components the instance doesn't have (unless it removed them).
    void SyncInstance(Level& level, Entity& instance, const Level& prefab);

    // Every instance in the level: SyncInstance with its prefab's current
    // file. Called after a level loads. Instances whose prefab can't be
    // loaded keep their saved data.
    void SyncAllInstances(Level& level);

    // Every instance in the level: RefreshOverrides. Called before a save so
    // the saved override lists are current.
    void RefreshAllOverrides(Level& level);

    // Turns an instance back into a plain entity.
    void Unpack(Entity& instance);

    // Sets one overridden field back to the prefab's value.
    void RevertOverride(Level& level, Entity& instance, const std::string& path);

    // Removes every override, added component and removed component.
    void RevertAll(Level& level, Entity& instance);

    // ── For the inspector ────────────────────────────────────────────────
    // `componentType` is a CMP_* value, `instanceID` the instance's component
    // (a ScriptInstanceID for scripts).

    // The level.bson key of a component type ("sprites"), or "" if unknown.
    const char* ComponentKey(int componentType);

    // The prefab component one of the instance's components came from, or 0
    // when the instance added it.
    std::uint64_t PrefabInstanceIDOf(const Entity& instance, int componentType, std::uint64_t instanceID);

    // The fields of one linked component that differ from the prefab, as
    // the part of the override path after "<key>/<id>/" ("color",
    // "publicValues/speed"). Empty for added components.
    std::vector<std::string> ComponentOverrides(const Level& level, const Entity& instance, int componentType, std::uint64_t instanceID);

    // "enabled", "tags", "events" and/or "dispatches" when they differ from the prefab.
    std::vector<std::string> EntityOverrides(const Entity& instance);

    // Writes the instance's value of one overridden field into the .prefab
    // (saving the file), so it and every other instance follow it.
    bool ApplyOverride(Level& level, Entity& instance, const std::string& path, std::string* errorMessage = nullptr);

    // A linked component: all its overrides into the .prefab. An added one:
    // adds it to the .prefab.
    bool ApplyComponent(Level& level, Entity& instance, int componentType, std::uint64_t instanceID, std::string* errorMessage = nullptr);

    // A linked component: back to the prefab's values. An added one: removed.
    void RevertComponent(Level& level, Entity& instance, int componentType, std::uint64_t instanceID);

    // Brings back a prefab component the instance removed.
    void RestoreComponent(Level& level, Entity& instance, int componentType, std::uint64_t prefabInstanceID);

    // Every override, added and removed component into the .prefab.
    bool ApplyAll(Level& level, Entity& instance, std::string* errorMessage = nullptr);
}

#endif //TILKY_ENGINE_PREFAB_HPP
