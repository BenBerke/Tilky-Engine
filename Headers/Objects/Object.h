//
// Created by berke on 4/27/2026.
//

#ifndef WOLFY_ENGINE_OBJECT_H
#define WOLFY_ENGINE_OBJECT_H
#include "Headers/Map/MapQueries.h"
#include "Headers/Math/Vector/Vector2.hpp"

enum ObjectType {
    OBJ_PLAYER_SPAWN,
    OBJ_SPRITE,
    OBJ_DECAL,

    OBJ_COUNT,
};

struct Object {
    int id;
    ObjectType type;
    Vector2 position;
    int textureIndex;

    int sectorIndex = -1;
    int floor = 0;

    float height = 32.0f;
    float width = 32.0f;

    // For decals
    int wallIndex = -1;
    float zOffset = 0;
    float wallOffset = -1.0f;
    float wallT = 0.5f;
    float decalBaseHeight = 0.0f; // fixed world height of the wall floor when decal was placed

    static float GetObjectBottomHeight(const Object& object, const std::vector<Sector>& sectors) {
        if (object.sectorIndex < 0 ||
            object.sectorIndex >= static_cast<int>(sectors.size())) {
            return 0.0f;
            }

        const Sector& sector = sectors[object.sectorIndex];

        const float sectorHeight =
            sector.ceilingHeight - sector.floorHeight;

        const int floor = std::clamp(
            object.floor,
            0,
            std::max(1, sector.floorCount) - 1
        );

        return sector.floorHeight + sectorHeight * static_cast<float>(floor);
    }
    static void UpdateObjectSector(Object& object, const std::vector<Sector>& sectors) {
        const int newSector = MapQueries::FindSectorContainingPoint(
            sectors,
            object.position
        );

        if (newSector == -1) {
            return;
        }

        object.sectorIndex = newSector;

        const Sector& sector = sectors[newSector];

        object.floor = std::clamp(
            object.floor,
            0,
            std::max(1, sector.floorCount) - 1
        );
    }
};


#endif //WOLFY_ENGINE_OBJECT_H