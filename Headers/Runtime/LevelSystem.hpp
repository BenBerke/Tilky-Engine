//
// Created by berke on 5/26/2026.
//

#ifndef TILKY_ENGINE_LEVELUPDATE_H
#define TILKY_ENGINE_LEVELUPDATE_H

#include "Headers/Objects/Level.hpp"

namespace LevelSystem {
    void Start(Level& level);
    void Update(Level& level);

    ComponentCamera* GetActiveCamera(Level& level);
}

#endif //TILKY_ENGINE_LEVELUPDATE_H