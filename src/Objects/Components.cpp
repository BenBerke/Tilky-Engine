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
    if (owner == nullptr) {
        sectorIndex = -1;
        floor = 0.0f;
        return false;
    }

    if (sectors.empty()) {
        sectorIndex = -1;
        floor = 0.0f;
        return false;
    }

    const bool currentSectorValid =
        sectorIndex >= 0 &&
        sectorIndex < static_cast<int>(sectors.size());

    if (!isDirty && currentSectorValid) {
        auto& inside = sectors[sectorIndex].entitiesInside;

        if (std::find(inside.begin(), inside.end(), owner) == inside.end()) {
            inside.push_back(owner);
        }

        return true;
    }

    const int oldSector = sectorIndex;

    const int newSector = MapQueries::FindSectorContainingPoint(
        sectors,
        { position.x, position.y }
    );

    if (newSector < 0 || newSector >= static_cast<int>(sectors.size())) {
        if (currentSectorValid) {
            std::erase(sectors[oldSector].entitiesInside, owner);
        }

        sectorIndex = -1;
        floor = 0.0f;
        isDirty = false;
        return false;
    }

    if (currentSectorValid && oldSector != newSector) {
        std::erase(sectors[oldSector].entitiesInside, owner);
    }

    sectorIndex = newSector;

    auto& inside = sectors[newSector].entitiesInside;

    if (std::ranges::find(inside, owner) == inside.end()) {
        inside.push_back(owner);
    }

    floor = sectors[newSector].floorHeight;

    isDirty = false;
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

    auto applyAxis = [&](float& v) {
        if (v > 0.0f) v = std::max(0.0f, v - friction * dt);
        else if (v < 0.0f) v = std::min(0.0f, v + friction * dt);
    };

    applyAxis(velocity.x);
    applyAxis(velocity.y);
}

void ComponentRigidbody::ApplyAirResistance(float airResistance) {
    //todo implement
}

void ComponentRigidbody::ApplyGravity(const float gravity) {
    this->velocity.z -= gravity * GameTime::deltaTime;
}
