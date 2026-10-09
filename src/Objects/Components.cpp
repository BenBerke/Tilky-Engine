//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Components.hpp"

#include "Headers/Engine/GameTime.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Objects/Entity.hpp"

void ComponentTransform::SetPosition(const Vector3 &position) {
    this->position = position;
    isDirty = true;
}

Vector3 ComponentTransform::LocalToWorld(const Vector3& offset) const {
    if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) return position;
    return position + QuaternionMath::Rotate(rotation, offset);
}

void ComponentTransform::AddPosition(const Vector3& position) {
    SetPosition(this->position + position);
}

bool ComponentTransform::UpdateObjectSectorAndFloor(std::vector<Sector>& sectors) {
    if (this->ownerID == INVALID_ID || sectors.empty()) {
        sectorIndex = -1;
        relativeHeight = position.y;
        return false;
    }

    const int oldSector = sectorIndex;

    const bool oldSectorValid = oldSector >= 0 && oldSector < static_cast<int>(sectors.size());

    const int newSector = MapQueries::FindSectorContainingPoint(
        sectors,
        { position.x, position.z }
    );

    if (newSector < 0 || newSector >= static_cast<int>(sectors.size())) {
        if (oldSectorValid)
            std::erase(sectors[oldSector].entitiesInside, this->ownerID);

        sectorIndex = -1;
        relativeHeight = position.y;
        return false;
    }

    if (oldSectorValid && oldSector != newSector)
        std::erase(sectors[oldSector].entitiesInside, this->ownerID);

    sectorIndex = newSector;

    Sector& sector = sectors[newSector];

    if (std::ranges::find(sector.entitiesInside, this->ownerID) == sector.entitiesInside.end())
        sector.entitiesInside.push_back(this->ownerID);

    sectorId = sector.id;

    return true;
}

void ComponentAudioSource::PlaySound() {
    if (soundFileName.empty()) return;
    isHeld = false;
    SoundManager::PlaySoundOnSource(name, soundFileName);
}

void ComponentAudioSource::StopSound() {
    isHeld = true;
    SoundManager::StopSource(name);
}

void ComponentAudioSource::PauseSound() {
    isHeld = true;
    SoundManager::PauseSource(name);
}

void ComponentAudioSource::ResumeSound() {
    if (SoundManager::ResumeSource(name)) isHeld = false;
}

bool ComponentAudioSource::IsPlaying() const {
    return SoundManager::IsSourcePlaying(name);
}

void ComponentAudioSource::SetSourcePitch(const float _pitch) const {
    SoundManager::SetSourcePitch(this->name, _pitch);
}

void ComponentAudioSource::SetSourceGain(const float _gain) const {
    SoundManager::SetSourceGain(this->name, _gain);
}

void ComponentAudioSource::SetSourceLooping(const bool _looping) const {
    SoundManager::SetSourceLooping(this->name, _looping);
}

void ComponentAudioSource::SetSourceReferenceDistance(const float distance) const {
    SoundManager::SetSourceReferenceDistance(this->name, distance);
}

void ComponentAudioSource::SetSourceMaxDistance(const float distance) const {
    SoundManager::SetSourceMaxDistance(this->name, distance);
}

void ComponentAudioSource::SetSourceRollOffFactor(const float factor) const {
    SoundManager::SetSourceRollOffFactor(this->name, factor);
}

void ComponentAudioSource::SetSourceInnerConeAngle(const float angle) const {
    SoundManager::SetSourceInnerConeAngle(this->name, angle);
}

void ComponentAudioSource::SetSourceOuterConeAngle(const float angle) const {
    SoundManager::SetSourceOuterConeAngle(this->name, angle);
}

void ComponentAudioSource::SetSourceOuterGain(const float _outerGain) const {
    SoundManager::SetSourceOuterGain(this->name, _outerGain);
}

void ComponentAudioSource::SetSourcePosition(const Vector3& position) const {
    SoundManager::SetSourcePosition(this->name, position);
}

void ComponentRigidbody::AddVelocity(const Vector3 &_velocity) {
    this->velocity += _velocity;
}

void ComponentRigidbody::ApplyFriction(const float _friction, const float dt) {
    const float _f = _friction + this->friction;
    auto applyAxis = [&](float& v) {
        if (v > 0.0f) v = std::max(0.0f, v - _f * dt);
        else if (v < 0.0f) v = std::min(0.0f, v + _f * dt);
    };
    applyAxis(this->velocity.x);
    applyAxis(this->velocity.z);
}


void ComponentRigidbody::ApplyGravity(const float gravity, const float dt) {
    if (isStatic) return;
    velocity.y -= gravity * gravityScale * dt;
}
void ComponentRigidbody::ApplyAirResistance(float _airResistance, const float dt) {
    //todo implement
}
