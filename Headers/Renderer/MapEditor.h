//
// Created by berke on 4/13/2026.
//

#ifndef WOLFY_ENGINE_MAPEDITOR_H
#define WOLFY_ENGINE_MAPEDITOR_H

#include <vector>
#include "../Objects/Wall.h"
#include "../Objects/Sector.h"

namespace MapEditor {
    inline std::vector<Wall> walls;
    inline std::vector<Sector> sectors;
    void AddWall(const Wall &wall);
    void AddSector(const Sector &sector);
    void TriangulateSectors();
}

#endif //WOLFY_ENGINE_MAPEDITOR_H