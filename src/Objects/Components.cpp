//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Components.hpp"
#include <spdlog/spdlog.h>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Entity.hpp"

void ComponentTransform::SetPosition(const Vector3 &position) {
    this->position = position;
    isDirty = true;
}

void ComponentTransform::AddPosition(const Vector3& position) {
    SetPosition(this->position + position);
}

float ComponentTransform::GetObjectBottomHeight(const std::vector<Sector>& sectors) {
    if (this->sectorIndex < 0 || this->sectorIndex >= static_cast<int>(sectors.size())) [[unlikely]] return 0.0f;

    const Sector& sector = sectors[this->sectorIndex];

    const float sectorHeight = sector.ceilingHeight - sector.floorHeight;

    this->floor = std::clamp(this->floor, 0, std::max(1, sector.floorCount) - 1);

    return sector.floorHeight + sectorHeight * static_cast<float>(floor);
}

bool ComponentTransform::UpdateObjectSectorAndFloor(std::vector<Sector>& sectors, Entity* owner) {
    if (owner == nullptr || sectors.empty()) {
        sectorIndex = -1;
        floor = 0;
        absHeight = position.z;
        return false;
    }

    const int oldSector = sectorIndex;

    const bool oldSectorValid =
        oldSector >= 0 &&
        oldSector < static_cast<int>(sectors.size());

    float worldHeight = position.z;

    if (oldSectorValid) {
        const Sector& oldSectorRef = sectors[oldSector];

        const int oldSafeFloorCount =
            std::clamp(oldSectorRef.floorCount, 1, MAX_FLOOR_COUNT);

        const int oldFloor =
            std::clamp(static_cast<int>(floor), 0, oldSafeFloorCount - 1);

        const float oldStoreyHeight =
            oldSectorRef.ceilingHeight - oldSectorRef.floorHeight;

        const float oldFloorWorldHeight =
            oldSectorRef.floorHeight + oldStoreyHeight * static_cast<float>(oldFloor);

        worldHeight = oldFloorWorldHeight + position.z;
    } else {
        worldHeight = absHeight;
    }

    const int newSector = MapQueries::FindSectorContainingPoint(
        sectors,
        { position.x, position.y }
    );

    if (newSector < 0 || newSector >= static_cast<int>(sectors.size())) {
        if (oldSectorValid) std::erase(sectors[oldSector].entitiesInside, owner);

        sectorIndex = -1;
        floor = 0;
        absHeight = worldHeight;
        return false;
    }

    if (oldSectorValid && oldSector != newSector) std::erase(sectors[oldSector].entitiesInside, owner);

    sectorIndex = newSector;

    Sector& sector = sectors[newSector];

    if (std::ranges::find(sector.entitiesInside, owner) == sector.entitiesInside.end()) {
        sector.entitiesInside.push_back(owner);
    }

    const int safeFloorCount = std::clamp(sector.floorCount, 1, MAX_FLOOR_COUNT);

    const float storeyHeight = sector.ceilingHeight - sector.floorHeight;

    int newFloor = 0;

    if (storeyHeight > 0.0001f) {
        newFloor = static_cast<int>(
            std::floor((worldHeight - sector.floorHeight) / storeyHeight)
        );
    }

    newFloor = std::clamp(newFloor, 0, safeFloorCount - 1);

    floor = newFloor;

    const float newFloorWorldHeight = sector.floorHeight + storeyHeight * static_cast<float>(newFloor);

    position.z = worldHeight - newFloorWorldHeight;

    position.z = std::clamp(position.z, 0.0f, std::max(0.0f, storeyHeight));

    absHeight = newFloorWorldHeight + position.z;

    return true;
}

void ComponentAudioSource::PlaySound() const {
    SoundManager::PlaySoundOnSource(name, soundIndex);
}

void ComponentAudioSource::SetSourcePitch(const float pitch) const {
    SoundManager::SetSourcePitch(name, pitch);
}

void ComponentAudioSource::SetSourceGain(const float gain) const {
    SoundManager::SetSourceGain(name, gain);
}

void ComponentAudioSource::SetSourceLooping(const bool looping) const {
    SoundManager::SetSourceLooping(name, looping);
}

void ComponentAudioSource::SetSourcePosition(const Vector3& position) const {
    SoundManager::SetSourcePosition(name, position);
}

void ComponentRigidbody::AddVelocity(const Vector3 velocity) {
    this->velocity += velocity;
}

void ComponentRigidbody::ApplyFriction(const float friction) {
    const float dt = GameTime::deltaTime;
    const float _f = friction + this->friction;

    auto applyAxis = [&](float& v) {
        if (v > 0.0f) v = std::max(0.0f, v - _f * dt);
        else if (v < 0.0f) v = std::min(0.0f, v + _f * dt);
    };

    applyAxis(velocity.x);
    applyAxis(velocity.y);
}

void ComponentRigidbody::ApplyAirResistance(float airResistance) {
    //todo implement
}

void ComponentRigidbody::ApplyGravity(const float gravity) {
    if (isStatic) return;

    velocity.z -= gravity * gravityScale * GameTime::deltaTime;
}
