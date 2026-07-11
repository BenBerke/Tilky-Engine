//
// Created by berke on 4/30/2026.
//

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Level.hpp"

namespace MapQueries {
    int FindSectorContainingPoint(const std::vector<Sector> &sectors,
                                  const Vector2 position,
                                  const int hintSector)
    {
        // Fast path: still in the same sector (overwhelmingly common)
        if (hintSector >= 0 && hintSector < static_cast<int>(sectors.size())) {
            if (Geometry::IsPointInPolygon(sectors[hintSector].vertices, position)) return hintSector;

            // Check neighbours before doing a full scan
            for (const Sector* nb : sectors[hintSector].neighbors) {
                if (nb == nullptr) continue;
                const int ni = static_cast<int>(nb - sectors.data());
                if (Geometry::IsPointInPolygon(sectors[ni].vertices, position)) return ni;
            }
        }

        // Full scan fallback — only hits when entity moves far in one frame
        int bestSector = -1;
        float bestArea = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(sectors.size()); ++i) {
            if (!Geometry::IsPointInPolygon(sectors[i].vertices, position)) continue;
            const float area = Geometry::PolygonAreaAbs(sectors[i].vertices);
            if (area < bestArea) {
                bestArea = area;
                bestSector = i;
            }
        }

        return bestSector;
    }

    void AssignWallsToSectors(Level& level) {
        for (Sector& sector : level.sectors) sector.walls.clear();

        RebuildSectorIDLookup(level);

        for (Wall& wall : level.walls) {
            Sector* frontSector = GetSectorByID(level, wall.frontSector);
            Sector* backSector = GetSectorByID(level, wall.backSector);

            if (frontSector != nullptr) frontSector->walls.push_back(&wall);

            if (backSector != nullptr && backSector != frontSector) backSector->walls.push_back(&wall);
        }
    }

    void AssignNeighborsToSectors(Level& level) {
        for (Sector& sector : level.sectors) sector.neighbors.clear();

        RebuildSectorIDLookup(level);

        for (Sector& sector : level.sectors) {
            for (Wall* wall : sector.walls) {
                if (wall == nullptr) continue;

                ID neighborSectorID = INVALID_ID;

                if (wall->frontSector == sector.id) neighborSectorID = wall->backSector;
                else if (wall->backSector == sector.id) neighborSectorID = wall->frontSector;
                else continue;


                // ID is unsigned (uint32_t), so "neighborSectorID < 0" could never be
                // true - a boundary wall (no sector on the other side) was only ever
                // being filtered out downstream, by GetSectorByID() returning nullptr
                // for INVALID_ID. Comparing against the sentinel directly makes the
                // intent explicit instead of relying on that as the sole backstop.
                if (neighborSectorID == INVALID_ID || neighborSectorID == sector.id) continue;


                Sector* neighbor = GetSectorByID(level, neighborSectorID);

                if (neighbor == nullptr) continue;

                if (std::ranges::find(sector.neighbors, neighbor) == sector.neighbors.end())
                    sector.neighbors.push_back(neighbor);
            }
        }
    }

    void RebuildSectorIDLookup(Level &level) {
        level.sectorIDToIndex.clear();

        for (int i = 0; i < static_cast<int>(level.sectors.size()); ++i)
            level.sectorIDToIndex[level.sectors[i].id] = i;
    }

    void RebuildWallIDLookup(Level &level) {
        level.wallIDToIndex.clear();

        for (int i = 0; i < static_cast<int>(level.walls.size()); ++i)
            level.wallIDToIndex[level.walls[i].id] = i;
    }

    void RebuildLevelLookups(Level &level) {
        RebuildSectorIDLookup(level);
        RebuildWallIDLookup(level);
    }

    Sector* GetSectorByID(Level &level, const ID sectorID) {
        const auto it = level.sectorIDToIndex.find(sectorID);

        if (it == level.sectorIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.sectors.size())) return nullptr;

        return &level.sectors[index];
    }

    const Sector* GetSectorByID(const Level &level, const ID sectorID) {
        const auto it = level.sectorIDToIndex.find(sectorID);

        if (it == level.sectorIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.sectors.size())) return nullptr;

        return &level.sectors[index];
    }

    Wall* GetWallByID(Level &level, const ID wallID) {
        const auto it = level.wallIDToIndex.find(wallID);

        if (it == level.wallIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.walls.size())) return nullptr;

        return &level.walls[index];
    }

    const Wall* GetWallByID(const Level &level, const ID wallID) {
        const auto it = level.wallIDToIndex.find(wallID);

        if (it == level.wallIDToIndex.end()) return nullptr;

        const int index = it->second;

        if (index < 0 || index >= static_cast<int>(level.walls.size())) return nullptr;

        return &level.walls[index];
    }

    void RebuildSectorRuntimeLinks(Level& level) {
        RebuildLevelLookups(level);
        AssignWallsToSectors(level);
        AssignNeighborsToSectors(level);
    }
}