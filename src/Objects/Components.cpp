//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Components.hpp"

#include "Headers/Map/MapQueries.hpp"

void ComponentTransform::SetPosition(const Vector3 &position) {
    this->position = position;
    isDirty = true;
}

void ComponentTransform::AddPosition(const Vector3& position) {
    SetPosition(this->position + position);
}

float ComponentTransform::GetObjectBottomHeight(const std::vector<Sector>& sectors) {
    if (this->sectorIndex < 0 ||
        this->sectorIndex >= static_cast<int>(sectors.size())) {
        return 0.0f;
        }

    const Sector& sector = sectors[this->sectorIndex];

    const float sectorHeight = sector.ceilingHeight - sector.floorHeight;

    this->floor = std::clamp(
        this->floor,
        0,
        std::max(1, sector.floorCount) - 1
    );

    return sector.floorHeight + sectorHeight * static_cast<float>(floor);
}

void ComponentTransform::UpdateObjectSector(std::vector<Sector>& sectors) {
    if (!isDirty) return;

    const int newSector = MapQueries::FindSectorContainingPoint(sectors,{this->position.x, this->position.z});

    if (newSector == -1) [[unlikely]] return;

    if (std::ranges::find(sectors[this->sectorIndex].entitiesInside, ownerID) !=
    sectors[this->sectorIndex].entitiesInside.end()) [[likely]]
    {
        std::erase(sectors[this->sectorIndex].entitiesInside, ownerID);
    }

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