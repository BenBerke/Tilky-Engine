#pragma once

#include <vector>

#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"

struct Level;
struct Matrix4;
struct ComponentTransform;

namespace MapQueries {
    int FindSectorContainingPoint(
        const std::vector<Sector>& sectors,
        Vector2 position,
        int hintSector = -1
    );

    // Height of one of `sector`'s floors or ceilings at `point`, slope
    // included. Slopes run from an edge of the sector's bounding rectangle,
    // as in the renderer and PhysicsSystem.
    [[nodiscard]] float SurfaceHeightAt(const Sector& sector, const SectorSurface& surface, Vector2 point);

    // The floor of `sector` whose floor-to-ceiling span at `point` is nearest
    // `height` (0 away when inside it). -1 when the sector has no floors.
    [[nodiscard]] int FindFloorAtHeight(const Sector& sector, Vector2 point, float height);

    // Puts the transform's feet on the floor it is in (FindFloorAtHeight) of
    // the sector under it. False, leaving it alone, outside every sector.
    bool SnapToFloor(Level& level, ComponentTransform& transform);

    void RebuildSectorIDLookup(Level& level);
    void RebuildWallIDLookup(Level& level);
    void RebuildLevelLookups(Level& level);

    Sector* GetSectorByID(Level& level, ID sectorID);
    const Sector* GetSectorByID(const Level& level, ID sectorID);

    Wall* GetWallByID(Level& level, ID wallID);
    const Wall* GetWallByID(const Level& level, ID wallID);

    void AssignWallsToSectors(Level& level);
    void AssignNeighborsToSectors(Level& level);
    void RebuildSectorRuntimeLinks(Level& level);

    void BuildPvs(Level& level);

    // Every sector the camera can see into from `cameraSectorIndex` (an index
    // into level.sectors, as ComponentTransform::sectorIndex stores), its own
    // sector included. Floods out through portal walls, shrinking a screen
    // rectangle at each one, so the result is conservative: a listed sector
    // can still be hidden, but a visible one is never left out. Openings use
    // the current floor and ceiling heights, so a closed door hides what is
    // behind it. A floor or ceiling whose color alpha is below 1 is
    // see-through and never closes an opening.
    //
    // viewProjection maps a world point to clip space as
    // viewProjection * (x, y, z, 1), i.e. projection * view. screenWidth and
    // screenHeight are the viewport in pixels; rectangles are kept in whole
    // pixels.
    //
    // `out` is cleared, then filled in level.sectors order. With an invalid
    // sector index or screen size nothing can be culled, so every sector is
    // listed. Needs RebuildSectorRuntimeLinks (sector.walls).
    void GetVisibleSectors(
        const Level& level,
        int cameraSectorIndex,
        const Matrix4& viewProjection,
        float screenWidth,
        float screenHeight,
        std::vector<const Sector*>& out
    );

    inline std::array<Vector3, 4> CalculateWallQuad3D(
        const Wall& wall,
        const float bottomHeight,
        const float topHeight
    ) {
        return std::array {
            Vector3 {wall.start.x, bottomHeight, wall.start.y},
            Vector3 {wall.end.x, bottomHeight,wall.end.y},
            Vector3 {wall.end.x, topHeight,wall.end.y},
            Vector3 {wall.start.x, topHeight,wall.start.y}
        };
    }

    inline bool PushWallQuad3D(Wall& wall, const float bottomHeight, const float topHeight, const float minWallHeight) {
        if (topHeight <= bottomHeight + minWallHeight) return false;
        if (wall.quad3DCount >= static_cast<int>(wall.quads3D.size())) return false;

        wall.quads3D[wall.quad3DCount++] = CalculateWallQuad3D(wall, bottomHeight, topHeight);
        wall.RebuildQuadAabbs();

        return true;
    }
}