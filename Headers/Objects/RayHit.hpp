//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_RAYHIT_HPP
#define TILKY_ENGINE_RAYHIT_HPP

#include "../Math/Vector/Vector3.hpp"

struct Wall;
struct Entity;
struct Sector;

enum class RayHitType {
    None,
    Entity,
    Wall,
    SectorFloor,
    SectorCeiling
};

struct RayHit {
    Entity* entity = nullptr;
    Wall* wall = nullptr;
    Sector* sector = nullptr;

    RayHitType type = RayHitType::None;

    Vector3 position;
    float distance = .0f;

    [[nodiscard]] bool HitEntity() const { return type == RayHitType::Entity && entity != nullptr; }
    [[nodiscard]] bool HitWall() const { return type == RayHitType::Wall && wall != nullptr; }
    [[nodiscard]] bool HitSectorFloor() const { return type == RayHitType::SectorFloor && sector != nullptr; }
    [[nodiscard]] bool HitSectorCeiling() const { return type == RayHitType::SectorCeiling && sector != nullptr; }
};

#endif //TILKY_ENGINE_RAYHIT_HPP