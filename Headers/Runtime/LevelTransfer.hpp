#ifndef TILKY_ENGINE_LEVELTRANSFER_HPP
#define TILKY_ENGINE_LEVELTRANSFER_HPP

#include <cstdint>
#include <vector>

#include "Headers/Objects/Level.hpp"

// What a level switch keeps (Game.LoadLevel, the Load Level dispatch).
//
// Carried: every entity with Persist Between Levels (Entity::persistent),
// and every entity of a UI layout the next level lists too. They keep their
// IDs, components, running scripts (with their Lua state and Invokes),
// sounds and event trigger counts. The next level's own entities make room:
// one whose entity or component ID a carried one already uses is given a
// new one, and everything pointing at it follows.
//
// A carried entity with a persistence key is merged with the next level's
// entity that has the same key (its level copy), which is then removed:
// what pointed at the copy points at the carried entity. Each of the copy's
// components is paired with the carried entity's component of its type in
// order (scripts: the same script file, in order); where the carried one's
// takeFromLevelCopy is set it takes the copy's values (a script restarts
// with them), components only the copy has are added.
//
// A carried entity's references to what stays behind (entities, walls,
// sectors, linedefs of the old level) are cleared, with a warning.
namespace LevelTransfer {
    struct Result {
        // The carried entities, in the new level.
        std::vector<ID> carried;
        // Carried scripts that took their level copy's values: they restart.
        std::vector<ScriptInstanceID> restartScripts;
    };

    // The entities of the running level that go on into a level listing
    // these UI layouts.
    [[nodiscard]] std::vector<ID> CarriedEntities(const Level& current, const std::vector<std::uint64_t>& nextLayouts);

    // Turns `next`, just loaded from its file, into the level to run: the
    // carried entities of `current` added, level copies merged, its UI
    // layouts that weren't carried created, the UI sorted into draw order.
    // Entities new to the level get LevelEpoch::current: bump it first.
    Result Merge(const Level& current, const std::vector<ID>& carried, Level& next);

    // The first level of a play session: creates its UI layouts' entities
    // and sorts the UI into draw order.
    void AddLayouts(Level& level);
}

#endif //TILKY_ENGINE_LEVELTRANSFER_HPP
