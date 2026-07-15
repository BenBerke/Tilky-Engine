#pragma once

#include <string>
#include <vector>

#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector3.hpp"
#include "../Objects/Wall.hpp"
#include "../Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"

struct Level;
using ID = uint32_t;

namespace Editor {
    extern Vector3 playerStartPos;
    extern int backgroundTextureIndex;

    extern std::vector<std::string> maps;
    extern std::string currentMap;

    void Start();
    void Update();
    void Destroy();

    bool ShutdownRequested();
    bool QuitRequested();
    bool PlayRequested();
    bool SwitchToRuntimeEditorRequested();
    bool LoadLevel(const std::string& levelName);
    void RefreshLevelSoundsFromFolder();

    void AddWall(const Wall& wall);
    void AddSector(const Sector& sector);

    void CreateSector(
        const std::vector<Vector2>& vertices,
        float ceilHeight,
        float floorHeight,
        Vector3 ceilColor,
        const Vector3 &floorColor,
        int ceilTextureIndex,
        int floorTextureIndex
    );

    void TriangulateSectors();
}
