#pragma once

#include <vector>

#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"

struct Level;
namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector>& sectors, Vector2 position);

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
}