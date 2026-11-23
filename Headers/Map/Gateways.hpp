#ifndef TILKY_ENGINE_GATEWAYS_HPP
#define TILKY_ENGINE_GATEWAYS_HPP

#include <array>
#include <vector>

#include "Headers/Objects/Wall.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

struct Level;
struct Sector;

// A Gateway is a wall linked to another wall (Wall::gatewayTarget): its
// opening shows what is on the other side of the target, and whatever walks
// or shoots into it comes out of the target's opening. Two-way is just the
// target linking back.
//
// The two walls line up on their middles and on their floors, with no
// scaling: the opening is as long as the shorter wall and as tall as the
// shorter room, and the rest of each wall stays solid. On a wall with a
// sector on each side the opening is the part open to both of them.
//
// Each side a gateway is entered from is its own Opening. A one-sided wall
// has one, its room's. A two-sided wall has one per side Wall::gatewaySide
// allows. Out of a one-sided target you come into its room; a two-sided
// target is the same doorway as the gateway: going through the gateway from
// its front sector comes out of the target into the target's back sector,
// and the other way round.
namespace Gateways {
    struct Opening {
        const Wall* wall = nullptr; // the gateway
        const Wall* exit = nullptr; // where it leads
        int sectorIndex = -1;       // the room the gateway is entered from
        int exitSectorIndex = -1;   // the room it comes out into
        bool fromBack = false;      // entered from the gateway wall's back sector

        // Unit map normals: into the room it is entered from, and into the
        // room it comes out into.
        Vector2 inward;
        Vector2 exitInward;

        // The opening along the gateway wall, as fractions from its start.
        float startT = 0.0f;
        float endT = 1.0f;

        // World heights of the opening's bottom and top on the gateway wall.
        float bottom = 0.0f;
        float top = 0.0f;

        // Middles of the two walls (the points that line up) and how much
        // higher the exit's floor is than the gateway's.
        Vector2 centre;
        Vector2 exitCentre;
        float heightOffset = 0.0f;

        // The turn from the gateway's side to the exit's, in map X/Z:
        // v' = (v.x cos - v.z sin, v.x sin + v.z cos).
        float cosTurn = 1.0f;
        float sinTurn = 0.0f;
        float turnDegrees = 0.0f;

        [[nodiscard]] Vector2 StartPoint() const;
        [[nodiscard]] Vector2 EndPoint() const;

        // World point (x, height, z) on the gateway's side -> the same place
        // relative to the exit.
        [[nodiscard]] Vector3 MapPoint(const Vector3& point) const;
        // A direction or velocity: turned, never moved.
        [[nodiscard]] Vector3 MapDirection(const Vector3& direction) const;
        // A camera yaw in degrees (forward = (sin yaw, cos yaw) in X/Z).
        [[nodiscard]] float MapYaw(float yawDegrees) const;

        // Signed distance of a map point from the gateway's line, positive
        // on the side it is entered from.
        [[nodiscard]] float SideDistance(const Vector2& point) const;
    };

    // Why a linked wall has no working opening (editor status text).
    enum class Problem {
        NONE,
        NO_TARGET,          // not linked, or the target wall is gone
        SAME_WALL,          // linked to itself
        NO_ROOM,            // the gateway wall has no sector on either side
        TARGET_NO_ROOM,
        CLOSED              // no room left between floor and ceiling (a closed door)
    };

    // A wall's openings as things are now, one per side it is entered from:
    // how many (0 to 2). 0 (with `problem` set when given) when the wall
    // isn't a working gateway. Both share the same rectangle on the wall.
    int Compute(const Level& level, const Wall& wall, std::array<Opening, 2>& out, Problem* problem = nullptr);

    // Every working gateway opening in the level, in level.walls order.
    void Collect(const Level& level, std::vector<Opening>& out);

    // Does any wall in the level link to another? Cheap early-out.
    [[nodiscard]] bool AnyLinked(const Level& level);

    // The move from `from` to `to` (world points, feet height) went into a
    // gateway's opening from its room: the first one crossed, and how far
    // along the move (0..1) it was. The feet must be in the opening (a
    // little below its bottom counts, for a body resting on the floor).
    bool FindCrossing(const std::vector<Opening>& openings, const Vector3& from, const Vector3& to,
                      const Opening*& crossed, float& moveT);
}

#endif // TILKY_ENGINE_GATEWAYS_HPP
