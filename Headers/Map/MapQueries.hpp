//
// Created by berke on 4/30/2026.
//

#ifndef TILKY_ENGINE_MAPQUERIES_H
#define TILKY_ENGINE_MAPQUERIES_H

#include "Headers/Objects/Sector.hpp"

namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector> &sectors, const Vector2 position);
    void AssignWallsToSectors(std::vector<Sector>& sectors, const std::vector<Wall> &walls);
    void AssignNeighborsToSectors(std::vector<Sector>& sectors);
}

#endif //TILKY_ENGINE_MAPQUERIES_H