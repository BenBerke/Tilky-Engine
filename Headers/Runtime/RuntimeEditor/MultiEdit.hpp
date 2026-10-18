#ifndef TILKY_ENGINE_MULTIEDIT_HPP
#define TILKY_ENGINE_MULTIEDIT_HPP

#include <nlohmann/json.hpp>

#include "Headers/Objects/Level.hpp"

// Mirrors an inspector edit made on one object (the primary of a runtime
// editor multi-selection) onto other objects of the same type.
//
// Snapshot the primary before its inspector draws and again after; if the
// two differ, Mirror*Edit writes only the fields that changed onto each
// target and leaves every other field of the target as it was. Fields are
// compared through the save format (LevelSerialization), so anything that
// is saved is mirrored without a hand-written list. Identity and geometry
// (IDs, wall endpoints, sector corners and sides) are never copied.
namespace MultiEdit {
    nlohmann::json WallState(const Wall& wall);
    void MirrorWallEdit(const nlohmann::json& before, const nlohmann::json& after, Wall& target);

    nlohmann::json SectorState(const Sector& sector);
    void MirrorSectorEdit(const nlohmann::json& before, const nlohmann::json& after, Sector& target);

    // Name, tags and every component's fields. A component is matched by
    // type and by its position among that entity's components of that type;
    // a component the target doesn't have is skipped, never added.
    nlohmann::json EntityState(Level& level, ID entityID);
    void MirrorEntityEdit(Level& level, const nlohmann::json& before, const nlohmann::json& after, ID targetID);
}

#endif
