#include "Headers/Map/Gateways.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Level.hpp"

namespace Gateways {
    namespace {
        // Feet this far under the opening's bottom still go through: a body
        // resting on the floor sits right at it, give or take rounding.
        constexpr float FEET_SLACK = 0.5f;

        // A wall's sides: the sector on each (-1: none) and the normal
        // pointing into it. [0] is the front sector's side, [1] the back's.
        // frontSector is on the wall's left (Wall::normal's side).
        struct Sides {
            int sector[2] = {-1, -1};
            Vector2 inward[2];

            [[nodiscard]] int Count() const { return (sector[0] >= 0) + (sector[1] >= 0); }
        };

        Sides WallSides(const Level& level, const Wall& wall) {
            Sides sides;
            if (wall.length <= Constants::Epsilon) return sides;

            const auto indexOf = [&level](const ID id) {
                const Sector* sector = MapQueries::GetSectorByID(level, id);
                return sector == nullptr || sector->floors.empty() ? -1 : static_cast<int>(sector - level.sectors.data());
            };

            sides.sector[0] = indexOf(wall.frontSector);
            sides.sector[1] = indexOf(wall.backSector);
            sides.inward[0] = wall.normal;
            sides.inward[1] = {-wall.normal.x, -wall.normal.y};

            // Checked against the map itself, in case a hand-edited wall has
            // its sectors the other way round.
            const Vector2 middle = (wall.start + wall.end) * 0.5f;
            const float probe = std::min(0.05f, wall.length * 0.25f);

            for (int side = 0; side < 2; ++side) {
                const int sector = sides.sector[side];
                if (sector < 0 || sides.sector[0] == sides.sector[1]) continue;

                const Vector2 inside = {middle.x + sides.inward[side].x * probe, middle.y + sides.inward[side].y * probe};
                if (MapQueries::FindSectorContainingPoint(level.sectors, inside, sector) == sector) continue;

                const Vector2 behind = {middle.x - sides.inward[side].x * probe, middle.y - sides.inward[side].y * probe};
                if (MapQueries::FindSectorContainingPoint(level.sectors, behind, sector) == sector) {
                    std::swap(sides.inward[0], sides.inward[1]);
                    break;
                }
            }

            return sides;
        }

        // The room's floor-to-ceiling span along [startT, endT] of the wall:
        // the highest floor and the lowest ceiling at either end, so a sloped
        // room still gives a rectangle that fits inside it. Its first slab.
        void RoomSpan(const Sector& sector, const Wall& wall, const float startT, const float endT,
                      float& bottom, float& top) {
            const SectorFloor& floor = sector.floors.front();
            const Vector2 a = wall.start + (wall.end - wall.start) * startT;
            const Vector2 b = wall.start + (wall.end - wall.start) * endT;

            bottom = std::max(MapQueries::SurfaceHeightAt(sector, floor.floor, a), MapQueries::SurfaceHeightAt(sector, floor.floor, b));
            top = std::min(MapQueries::SurfaceHeightAt(sector, floor.ceiling, a), MapQueries::SurfaceHeightAt(sector, floor.ceiling, b));
        }

        // The span open to every sector the wall has: on a two-sided wall,
        // the part between them you could see or walk through.
        void WallSpan(const Level& level, const Sides& sides, const Wall& wall, const float startT, const float endT,
                      float& bottom, float& top) {
            bottom = -std::numeric_limits<float>::infinity();
            top = std::numeric_limits<float>::infinity();

            for (const int sector : sides.sector) {
                if (sector < 0) continue;

                float sectorBottom, sectorTop;
                RoomSpan(level.sectors[sector], wall, startT, endT, sectorBottom, sectorTop);
                bottom = std::max(bottom, sectorBottom);
                top = std::min(top, sectorTop);
            }
        }

        bool SideAllowed(const Wall& wall, const int side) {
            switch (wall.gatewaySide) {
                case GatewaySide::Front: return side == 0;
                case GatewaySide::Back: return side == 1;
                case GatewaySide::Both: break;
            }
            return true;
        }
    }

    Vector2 Opening::StartPoint() const { return wall->start + (wall->end - wall->start) * startT; }
    Vector2 Opening::EndPoint() const { return wall->start + (wall->end - wall->start) * endT; }

    Vector3 Opening::MapPoint(const Vector3& point) const {
        const float x = point.x - centre.x;
        const float z = point.z - centre.y;

        return {
            exitCentre.x + x * cosTurn - z * sinTurn,
            point.y + heightOffset,
            exitCentre.y + x * sinTurn + z * cosTurn
        };
    }

    Vector3 Opening::MapDirection(const Vector3& direction) const {
        return {
            direction.x * cosTurn - direction.z * sinTurn,
            direction.y,
            direction.x * sinTurn + direction.z * cosTurn
        };
    }

    // forward (sin y, cos y) turned by t is (sin(y - t), cos(y - t)).
    float Opening::MapYaw(const float yawDegrees) const {
        float yaw = std::fmod(yawDegrees - turnDegrees, 360.0f);
        if (yaw < 0.0f) yaw += 360.0f;
        return yaw;
    }

    float Opening::SideDistance(const Vector2& point) const {
        return (point.x - centre.x) * inward.x + (point.y - centre.y) * inward.y;
    }

    int Compute(const Level& level, const Wall& wall, std::array<Opening, 2>& out, Problem* problem) {
        const auto fail = [&](const Problem reason) {
            if (problem != nullptr) *problem = reason;
            return 0;
        };

        if (wall.gatewayTarget == INVALID_ID) return fail(Problem::NO_TARGET);
        if (wall.gatewayTarget == wall.id) return fail(Problem::SAME_WALL);

        const Wall* exit = MapQueries::GetWallByID(level, wall.gatewayTarget);
        if (exit == nullptr) return fail(Problem::NO_TARGET);

        const Sides sides = WallSides(level, wall);
        const Sides exitSides = WallSides(level, *exit);

        if (sides.Count() == 0) return fail(Problem::NO_ROOM);
        if (exitSides.Count() == 0) return fail(Problem::TARGET_NO_ROOM);

        // As long as the shorter wall, centred on both.
        const float length = std::min(wall.length, exit->length);
        const float margin = (wall.length - length) * 0.5f / wall.length;
        const float exitMargin = (exit->length - length) * 0.5f / exit->length;

        float bottom, top, exitBottom, exitTop;
        WallSpan(level, sides, wall, margin, 1.0f - margin, bottom, top);
        WallSpan(level, exitSides, *exit, exitMargin, 1.0f - exitMargin, exitBottom, exitTop);

        // Floors line up, so it is as tall as the shorter room.
        const float height = std::min(top - bottom, exitTop - exitBottom);
        if (height <= Constants::Epsilon) return fail(Problem::CLOSED);

        const bool twoSided = sides.Count() == 2;
        const bool exitTwoSided = exitSides.Count() == 2;
        int count = 0;

        for (int side = 0; side < 2; ++side) {
            if (sides.sector[side] < 0) continue;
            if (twoSided && !SideAllowed(wall, side)) continue;

            // A one-sided target: into its room. A two-sided one: the same
            // doorway, so out on the side across from the one gone in from.
            const int exitSide = exitTwoSided ? 1 - side : (exitSides.sector[0] >= 0 ? 0 : 1);

            Opening& opening = out[static_cast<size_t>(count)];
            opening = Opening{};
            opening.wall = &wall;
            opening.exit = exit;
            opening.fromBack = side == 1;
            opening.sectorIndex = sides.sector[side];
            opening.exitSectorIndex = exitSides.sector[exitSide];
            opening.inward = sides.inward[side];
            opening.exitInward = exitSides.inward[exitSide];

            opening.startT = margin;
            opening.endT = 1.0f - margin;
            opening.bottom = bottom;
            opening.top = bottom + height;
            opening.heightOffset = exitBottom - bottom;

            opening.centre = (wall.start + wall.end) * 0.5f;
            opening.exitCentre = (exit->start + exit->end) * 0.5f;

            // Walking in goes against `inward`; walking out goes along the
            // exit's. The turn takes one to the other: angle(-exitInward) -
            // angle(inward).
            float turn = std::atan2(-opening.exitInward.y, -opening.exitInward.x) - std::atan2(opening.inward.y, opening.inward.x);
            constexpr float PI = 3.14159265358979f;
            if (turn > PI) turn -= 2.0f * PI;
            if (turn <= -PI) turn += 2.0f * PI;
            opening.cosTurn = std::cos(turn);
            opening.sinTurn = std::sin(turn);
            opening.turnDegrees = turn * Constants::RadToDeg;

            ++count;
        }

        if (problem != nullptr) *problem = Problem::NONE;
        return count;
    }

    void Collect(const Level& level, std::vector<Opening>& out) {
        out.clear();

        std::array<Opening, 2> openings;
        for (const Wall& wall : level.walls) {
            if (wall.gatewayTarget == INVALID_ID) continue;

            const int count = Compute(level, wall, openings);
            out.insert(out.end(), openings.begin(), openings.begin() + count);
        }
    }

    bool AnyLinked(const Level& level) {
        return std::ranges::any_of(level.walls, [](const Wall& wall) { return wall.gatewayTarget != INVALID_ID; });
    }

    bool FindCrossing(const std::vector<Opening>& openings, const Vector3& from, const Vector3& to,
                      const Opening*& crossed, float& moveT) {
        crossed = nullptr;
        moveT = 2.0f;

        for (const Opening& opening : openings) {
            const float before = opening.SideDistance({from.x, from.z});
            const float after = opening.SideDistance({to.x, to.z});

            // From its room's side to behind it.
            if (before < 0.0f || after >= 0.0f) continue;

            const float t = before / (before - after);
            if (t >= moveT) continue;

            const Vector3 at = from + (to - from) * t;

            const Vector2 start = opening.StartPoint();
            const Vector2 end = opening.EndPoint();
            const Vector2 span = end - start;
            const float spanLengthSq = span.x * span.x + span.y * span.y;
            if (spanLengthSq <= Constants::Epsilon) continue;

            const float along = ((at.x - start.x) * span.x + (at.z - start.y) * span.y) / spanLengthSq;
            if (along < 0.0f || along > 1.0f) continue;

            if (at.y < opening.bottom - FEET_SLACK || at.y >= opening.top) continue;

            crossed = &opening;
            moveT = t;
        }

        return crossed != nullptr;
    }
}
