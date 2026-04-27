//
// Created by berke on 4/13/2026.
//

#ifndef WOLFY_ENGINE_MAPEDITOR_H
#define WOLFY_ENGINE_MAPEDITOR_H

#include <string>
#include <vector>
#include <SDL3/SDL_render.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "../Objects/Wall.hpp"
#include "../Objects/Sector.hpp"

namespace MapEditor {
    inline SDL_Window* window;
    inline SDL_Renderer* renderer;
    inline TTF_TextEngine* textEngine;
    inline TTF_Font* font;

    inline std::vector<Wall> walls;
    inline std::vector<Sector> sectors;
    void AddWall(const Wall &wall);
    void AddSector(const Sector &sector);
    void TriangulateSectors();

    void Start();
    void Update();
    bool QuitRequested();
    void Destroy();

    bool LoadLevel(const std::string& level);
    inline Vector2 playerStartPos;
}

#endif //WOLFY_ENGINE_MAPEDITOR_H