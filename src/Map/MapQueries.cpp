//
// Created by berke on 4/30/2026.
//

#include "../../Headers/Map/MapQueries.h"
#include "../../Headers/Math/Geometry/Geometry.h"
#include "Headers/Objects/Wall.hpp"

namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector>& sectors, const Vector2 position) {
        int bestSector = -1;
        float bestArea = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(sectors.size()); ++i) {
            if (!Geometry::IsPointInPolygon(sectors[i].vertices, position)) {
                continue;
            }

            const float area = Geometry::PolygonAreaAbs(sectors[i].vertices);

            if (area < bestArea) {
                bestArea = area;
                bestSector = i;
            }
        }

        return bestSector;
    }

    void AssignWallsToSectors(std::vector<Sector>& sectors, const std::vector<Wall>& walls) {
        for (Sector& sector : sectors) {
            sector.walls.clear();
        }

        for (const Wall& wall : walls) {
            if (wall.frontSector >= 0 &&
                wall.frontSector < static_cast<int>(sectors.size())) {
                sectors[wall.frontSector].walls.push_back(wall);
                }

            if (wall.backSector >= 0 &&
                wall.backSector < static_cast<int>(sectors.size()) &&
                wall.backSector != wall.frontSector) {
                sectors[wall.backSector].walls.push_back(wall);
                }
        }
    }
}
