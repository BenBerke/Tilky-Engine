//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Components.hpp"
#include <spdlog/spdlog.h>

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/MapQueries.hpp"

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


void ComponentTransform::UpdateObjectSector(std::vector<Sector>& sectors) {
    if (!isDirty && this->sectorIndex >= 0) return;

    if (sectors.empty()) [[unlikely]] return;

    const int newSector = MapQueries::FindSectorContainingPoint(sectors,{this->position.x, this->position.y});

    if (newSector == -1 || newSector >= static_cast<int>(sectors.size())) [[unlikely]] return;

    if (this->sectorIndex == newSector) [[likely]] return;

    if (sectorIndex >= 0 && sectorIndex < static_cast<int>(sectors.size())) [[likely]]
        std::erase(sectors[sectorIndex].entitiesInside, ownerID);

    this->sectorIndex = newSector;
    sectors[this->sectorIndex].entitiesInside.push_back(ownerID);

    const Sector& sector = sectors[newSector];

    this->floor = std::clamp(
        this->floor,
        0,
        std::max(1, sector.floorCount) - 1
    );
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

void ComponentRigidbody::ApplyGravity(float gravity) {
    this->velocity.z += gravity * GameTime::deltaTime;
}
