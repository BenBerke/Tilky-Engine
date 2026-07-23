#pragma once

#include <string>
#include <vector>
#include <Headers/Objects/Sector.hpp>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Wall.hpp"

struct Level;

// Wall-graph-first sector topology.
//
// This module owns turning a hand-drawn polyline into real map geometry:
// it inserts the drawn points into the level's existing wall graph
// (splitting/reusing walls as needed), retraces every enclosed face in
// the *entire* resulting graph, and reconciles those faces against the
// sectors that existed before the call so unrelated/unchanged sectors
// keep their stable IDs and properties. See MapTopology.cpp for the
// staged writeup (it mirrors the design doc this module was built from).
//
// This module has no dependency on the editor - it only knows about
// Level/Wall/Sector. MapEditorInternal::ApplyDrawnGeometry (declared in
// EditorInternal.hpp) is the thin editor-facing wrapper that adapts
// MapEditorInternal::PendingSectorParams to NewSectorParams below and
// handles undo/selection as an editor concern.
namespace MapTopology {
    // World-space epsilon used for every "same point" / "on this
    // segment" / "collinear" test in this module. Deliberately well
    // below a single grid unit so it never welds two vertices the user
    // actually meant to keep apart, while still comfortably absorbing
    // the float error that intersection math introduces.
    inline constexpr float kEpsilon = 0.015f;

    bool PointsEquivalent(const Vector2& a, const Vector2& b, float epsilon = kEpsilon);

    // Strict interior containment - false when `p` is at/near `a` or `b`.
    // Use PointsEquivalent against the endpoints for that case instead.
    bool PointOnSegmentInterior(const Vector2& p, const Vector2& a, const Vector2& b, float epsilon = kEpsilon);

    Vector2 ClosestPointOnSegment(const Vector2& p, const Vector2& a, const Vector2& b);

    // Scans every wall for the closest point that lies within
    // `maxDistance` of `point`, restricted to each wall's *interior*
    // (an existing endpoint is intentionally not a candidate here - the
    // editor already snaps to endpoints on its own, with priority over
    // this). Used to let a click near the side of a wall snap onto it,
    // so committing the chain later splits that wall there. Returns
    // false and leaves `outPoint` untouched if nothing is in range.
    bool ClosestPointOnAnyWall(
        const std::vector<Wall>& walls,
        const Vector2& point,
        float maxDistance,
        Vector2* outPoint
    );

    // Cosmetic template for a brand new sector. Mirrors
    // MapEditorInternal::PendingSectorParams field-for-field without
    // this module depending on the editor.
    struct NewSectorParams {
        std::string wallTexture;

        std::vector<SectorFloor> floors = {
            {
                {0.0f, {255.0f, 255.0f, 255.0f}, {}},
                {40.0f, {255.0f, 255.0f, 255.0f}, {}}
            }
        };

        float lightValue = 255.0f;
        Vector3 wallColor = {255.0f, 255.0f, 255.0f};
    };

    struct ApplyResult {
        bool success = false;

        // True if the resulting graph encloses at least one more sector
        // than `level` had before the call (i.e. the edit actually
        // carved out new space, rather than just adding/splitting walls
        // that don't enclose anything new yet).
        bool createdNewFace = false;

        // Every sector ID present in the result that did not exist in
        // `level` before the call - freshly-created sectors, plus any
        // split-off siblings of a sector that got subdivided. Empty on
        // failure. The editor uses this to decide what to select.
        std::vector<ID> affectedSectorIDs;

        // Human-readable outcome (success or rejection reason), safe to
        // log or surface directly in a UI toast.
        std::string message;
    };

    // Inserts `drawnPoints` into `level`'s wall graph and rebuilds every
    // sector from the resulting topology.
    //
    // `drawnPoints` is always an *open* polyline: consecutive pairs only,
    // with no implicit closing edge from the last point back to the
    // first. Repeat the first point at the end if you want to close a
    // loop back on itself - see MapEditorInternal::FinishSectorSelection
    // for the canonical example.
    //
    // Atomic: on rejection (fewer than two distinct points, a
    // self-intersecting input chain, etc.) `level` is left completely
    // untouched and `success` is false. On success, level.walls,
    // level.sectors, level.nextWallID and level.nextSectorID are fully
    // replaced with the rebuilt topology and
    // MapQueries::RebuildSectorRuntimeLinks(level) has already been
    // called - the caller only still owns any undo snapshot and
    // selection follow-up beyond `affectedSectorIDs`.
    ApplyResult ApplyDrawnGeometry(
        Level& level,
        const std::vector<Vector2>& drawnPoints,
        const NewSectorParams& params
    );

    // Runs the exact same algorithm as ApplyDrawnGeometry without ever
    // touching `level`, returning whether it would enclose at least one
    // new sector. Used by the editor's click handler to decide, before
    // committing, whether ending the in-progress chain on existing
    // geometry actually closes something yet or whether the chain should
    // just keep growing.
    bool WouldEncloseNewFace(const Level& level, const std::vector<Vector2>& drawnPoints);
}