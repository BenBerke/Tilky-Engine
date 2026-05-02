//
// Created by berke on 5/2/2026.
//

#ifndef WOLFY_ENGINE_LEVELMANAGER_H
#define WOLFY_ENGINE_LEVELMANAGER_H

#include <vector>

#include "Headers/Objects/Level.hpp"

namespace LevelManager {
    extern std::vector<Level> loadedLevels;
    extern int currentLevelIndex;

    Level& CurrentLevel();
    bool HasCurrentLevel();
}

#endif // WOLFY_ENGINE_LEVELMANAGER_H