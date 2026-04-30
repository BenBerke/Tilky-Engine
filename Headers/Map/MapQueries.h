//
// Created by berke on 4/30/2026.
//

#ifndef WOLFY_ENGINE_MAPQUERIES_H
#define WOLFY_ENGINE_MAPQUERIES_H

#include "Headers/Objects/Sector.hpp"

namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector> &sectors, const Vector2 position);
    void AssignWallsToSectors(std::vector<Sector>& sectors, const std::vector<Wall> &walls);
}

#endif //WOLFY_ENGINE_MAPQUERIES_H