//
// Created by berke on 5/2/2026.
//

#include "Headers/Map/LevelManager.hpp"

namespace LevelManager {
    std::vector<Level> loadedLevels;
    int currentLevelIndex = -1;

    bool HasCurrentLevel() {
        return currentLevelIndex >= 0 &&
               currentLevelIndex < static_cast<int>(loadedLevels.size());
    }

    Level& CurrentLevel() {
        return loadedLevels[currentLevelIndex];
    }
}