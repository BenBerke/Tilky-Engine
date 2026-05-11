//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVELMANAGER_H
#define TILKY_ENGINE_LEVELMANAGER_H

#include <vector>

#include "Headers/Objects/Level.hpp"

namespace LevelManager {
    extern std::vector<Level> loadedLevels;
    extern int currentLevelIndex;

    Level& CurrentLevel();
    bool HasCurrentLevel();
}

#endif // TILKY_ENGINE_LEVELMANAGER_H