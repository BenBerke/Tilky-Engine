#pragma once

#include <vector>

#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"

struct Level;
namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector> &sectors, Vector2 position, int hintSector = -1);

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

    inline std::array<Vector3, 4> CalculateWallQuad3D(const Wall &wall, const float bottomHeight,const float topHeight) {
        return std::array<Vector3, 4>{
            Vector3{wall.start.x, wall.start.y, bottomHeight},
            Vector3{wall.end.x, wall.end.y, bottomHeight},
            Vector3{wall.end.x, wall.end.y, topHeight},
            Vector3{wall.start.x, wall.start.y, topHeight}
        };
    }

    inline bool PushWallQuad3D(Wall &wall, const float bottomHeight, const float topHeight, const float minWallHeight) {
        if (topHeight <= bottomHeight + minWallHeight) return false;
        if (wall.quad3DCount >= static_cast<int>(wall.quads3D.size())) return false;

        wall.quads3D[wall.quad3DCount++] = CalculateWallQuad3D(wall, bottomHeight, topHeight);

        wall.RebuildQuadAabbs();

        return true;
    }
}