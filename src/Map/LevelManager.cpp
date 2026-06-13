//
// Created by berke on 5/2/2026.
//

#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Headers/Map/LevelSerialization.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace LevelManager {
    std::vector<Level> loadedLevels;
    int currentLevelIndex = -1;

    bool HasCurrentLevel() { return currentLevelIndex >= 0 && currentLevelIndex < static_cast<int>(loadedLevels.size()); }

    Level& CurrentLevel() { return loadedLevels[currentLevelIndex];}

    void ClearLoadedLevels() {
        loadedLevels.clear();
        currentLevelIndex = -1;
    }

    bool LoadLevelFromFile(const fs::path& levelFile) {
        Level loadedLevel;
        std::string errorMessage;

        if (!LevelSerialization::LoadLevelFromFile(
                levelFile,
                loadedLevel,
                nullptr,
                &errorMessage
            )) {
            std::cerr << errorMessage << "\n";
            return false;
        }

        ClearLoadedLevels();

        loadedLevels.push_back(std::move(loadedLevel));
        currentLevelIndex = 0;

        return true;
    }

    bool LoadLevelByName(const std::string& levelName) {
        return LoadLevelFromFile(LevelSerialization::BuildLevelPath(levelName));
    }

    bool LoadFirstProjectLevel() {
        const fs::path levelsPath = ProjectManager::GetLevelsPath();

        if (!fs::exists(levelsPath) || !fs::is_directory(levelsPath)) {
            std::cerr << "Project levels folder does not exist: "
                      << levelsPath.string()
                      << "\n";
            return false;
        }

        std::vector<fs::path> levelFiles;

        for (const fs::directory_entry& entry : fs::directory_iterator(levelsPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() == ".bson") {
                levelFiles.push_back(entry.path());
            }
        }

        if (levelFiles.empty()) {
            std::cerr << "No .bson level files found in: "
                      << levelsPath.string()
                      << "\n";
            return false;
        }

        std::ranges::sort(levelFiles);

        return LoadLevelFromFile(levelFiles.front());
    }

    void TriangulateCurrentLevelSectors() {
        if (!HasCurrentLevel()) {
            return;
        }

        Level& level = CurrentLevel();

        for (Sector& sector : level.sectors) {
            sector.triangles.clear();

            if (sector.vertices.size() < 3) {
                continue;
            }

            sector.triangles = Geometry::Triangulate(sector.vertices);
        }
    }
}
