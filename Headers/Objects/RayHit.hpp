//
// Created by berke on 6/2/2026.
//

#ifndef TILKY_ENGINE_RAYHIT_HPP
#define TILKY_ENGINE_RAYHIT_HPP

#include <cstdint>

#include "../Math/Vector/Vector3.hpp"

// What a ray is for (Lua: RayType). A typed ray fires the Receive Ray events
// of whatever it hits; a GENERAL one (every engine query, and scripts unless
// they pass a type) never fires anything.
enum class RayType : uint8_t {
    GENERAL = 0,
    INTERACT = 1,
    LINE_OF_SIGHT = 2,
    BULLET = 3,
    CUSTOM = 4,

    COUNT
};

struct Wall;
struct Entity;
enum class WallSurfaceSlot;
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

    int sectorFloorIndex = -1;

    // Unit length, facing back along the ray. For an entity, straight back along
    // the ray, because colliders don't report a surface.
    Vector3 normal = {0.0f, 1.0f, 0.0f};

    // A wall hit only: which piece was hit, and its height where the ray hit it.
    WallSurfaceSlot pieceSlot{};
    float pieceBottom = 0.0f;
    float pieceTop = 0.0f;

    [[nodiscard]] bool HitEntity() const { return type == RayHitType::Entity && entity != nullptr; }
    [[nodiscard]] bool HitWall() const { return type == RayHitType::Wall && wall != nullptr; }
    [[nodiscard]] bool HitSectorFloor() const { return type == RayHitType::SectorFloor && sector != nullptr; }
    [[nodiscard]] bool HitSectorCeiling() const { return type == RayHitType::SectorCeiling && sector != nullptr; }
};

#endif //TILKY_ENGINE_RAYHIT_HPP