//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_RAYHIT_HPP
#define TILKY_ENGINE_RAYHIT_HPP

struct Wall;
struct Vector3;
struct Entity;

struct RayHit {
     union {
         Entity* entity = nullptr;
         Wall* wall;
     } object;

    Vector3 position;
    float distance = .0f;
};

#endif //TILKY_ENGINE_RAYHIT_HPP