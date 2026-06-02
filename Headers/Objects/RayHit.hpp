//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_RAYHIT_HPP
#define TILKY_ENGINE_RAYHIT_HPP

#include "../Math/Vector/Vector3.hpp"

struct Wall;
struct Entity;

struct RayHit {
    Entity* entity = nullptr;
    Wall* wall = nullptr;

    Vector3 position;
    float distance = .0f;
};

#endif //TILKY_ENGINE_RAYHIT_HPP