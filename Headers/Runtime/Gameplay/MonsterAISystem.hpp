//
// Created by berke on 11/2/2026.
//

#ifndef TILKY_ENGINE_MONSTERAISYSTEM_HPP
#define TILKY_ENGINE_MONSTERAISYSTEM_HPP

#include <functional>

#include "Headers/Objects/Level.hpp"

/// Doom's monster thinking, without the attacks themselves: A_Look (noticing a
/// target by sight or sound), A_Chase with P_CheckMeleeRange and
/// P_CheckMissileRange (deciding to attack), P_NoiseAlert (sound spreading
/// through sectors) and the part of P_DamageMobj that decides pain and
/// infighting. What it decides comes out as notices: LevelSystem turns them
/// into events and script callbacks, and those do the actual attacking.
///
/// Only an entity's first active Monster AI thinks. Only runs during Play and
/// Standalone (LevelSystem), after scripts and before the Pathfinders.
namespace MonsterAISystem {
    enum class NoticeKind {
        TARGET_SPOTTED,
        TARGET_LOST,
        MELEE_READY,
        MISSILE_READY,
        PAIN,
        ACTIVE
    };

    // `otherID` is the target (the attacker for PAIN), INVALID_ID if none.
    using NoticeHandler = std::function<void(ID ownerID, NoticeKind kind, ID otherID)>;

    // Clears the sectors' heard noises and every Monster AI's state. Call once
    // at Play start. `onNotice` hears every decision until the next Start.
    void Start(Level& level, NoticeHandler onNotice = {});

    // `deltaTime` is scaled, so timeScale 0 holds them. Notices come after
    // every Monster AI has thought.
    void Update(Level& level, float deltaTime);

    // Doom's P_NoiseAlert: from sector `sectorIndex` (out through portals with
    // room in them, past at most one Blocks Sound wall), every sector
    // remembers `targetID` as what it last heard.
    void NoiseAlert(Level& level, ID targetID, int sectorIndex);

    // The first enabled entity with the Player tag, INVALID_ID if none.
    [[nodiscard]] ID FindPlayer(const Level& level);

    // --- Shared by the dispatch and the Lua API. What they cause is reported
    // at once, to the handler given to Start. ------------------------------

    // Starts chasing `targetID`, or switches to it if already chasing.
    void Wake(Level& level, ComponentMonsterAI& ai, ID targetID);

    // Back to idle, target forgotten.
    void Sleep(Level& level, ComponentMonsterAI& ai);

    // What taking damage does: rolls Pain Chance, and turns it on `attackerID`
    // (INVALID_ID for none) unless it is locked on another target.
    void Hurt(Level& level, ComponentMonsterAI& ai, ID attackerID);

    // True when the owner could see `targetID` now: range, field of view and
    // line of sight (P_CheckSight).
    [[nodiscard]] bool CanSee(Level& level, const ComponentMonsterAI& ai, ID targetID);
}

#endif //TILKY_ENGINE_MONSTERAISYSTEM_HPP
