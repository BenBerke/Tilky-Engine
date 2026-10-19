#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Objects/Level.hpp"

// The entity inspector's "Snap to Wall", in the map editor and the runtime
// editor alike. Editor-only, but kept in the level (Level::wallSnaps) and
// saved in its file's metadata, so snaps last until disabled. A snapped
// entity stays on its wall's line (clamped to the wall's ends), `gap` off the
// chosen side, facing straight out from it (yaw only - it can still be tilted
// or rolled). Its height stays free.
namespace WallSnap {
    inline constexpr float DEFAULT_GAP = WallSnapData::DEFAULT_GAP;

    using Snap = WallSnapData; // side: +1 or -1 times WallNormal
    using Snaps = std::unordered_map<ID, Snap>; // by entity

    // Every snapped entity of the open level. Undo steps that change snaps
    // swap this whole.
    Snaps& All();

    [[nodiscard]] const Snap* Find(ID entityID);

    // ---- Choosing the wall ------------------------------------------------
    // Started by the button; the next right-click on a wall finishes it.

    [[nodiscard]] bool IsPicking();

    // `primary` lands where the wall is clicked; the rest of `entities` at
    // their own nearest point. Shows the "Select a Wall" toast.
    void BeginPicking(const std::vector<ID>& entities, ID primary);
    void CancelPicking();

    // Cancels once the selection is no longer the one the pick started with.
    void CancelPickingIfSelectionChanged(const std::vector<ID>& selection);

    // Snaps the picked entities to `wall` and ends the pick. The primary goes
    // to `point` on the map (and to `height` when set), the others to their
    // nearest point. They go on the side `viewer` is on, and their sprites
    // become static so they lie flat against the wall. Returns how many
    // snapped.
    int FinishPicking(Level& level, const Wall& wall, Vector2 point, std::optional<float> height, Vector2 viewer);

    // ---- Snapped entities -------------------------------------------------

    void Disable(const std::vector<ID>& entities);
    int SwitchSides(Level& level, const std::vector<ID>& entities);
    void SetGap(Level& level, const std::vector<ID>& entities, float gap);

    // Puts every snapped entity back on its wall - call once a frame, after
    // anything that moves entities. Drops snaps whose entity or wall is gone.
    // Returns whether any entity moved.
    bool Enforce(Level& level);
}
