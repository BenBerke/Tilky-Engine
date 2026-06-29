//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_WRAPPERS_HPP
#define TILKY_ENGINE_WRAPPERS_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"


// ---------------------------------------------------------
// Audio Source
// ---------------------------------------------------------

struct ScriptAudioSource {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentAudioSource* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->audioSources.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] std::string GetName() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return {};
        return audio->name;
    }

    [[nodiscard]] int GetSoundIndex() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return -1;
        return audio->soundIndex;
    }

    void SetSoundIndex(const int index) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->soundIndex = index;
    }

    [[nodiscard]] float GetPitch() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 1.0f;
        return audio->pitch;
    }

    void SetPitch(const float pitch) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->pitch = pitch;
        audio->SetSourcePitch(pitch);
    }

    [[nodiscard]] float GetGain() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 1.0f;
        return audio->gain;
    }

    void SetGain(const float gain) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->gain = gain;
        audio->SetSourceGain(gain);
    }

    [[nodiscard]] bool GetLooping() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return false;
        return audio->looping;
    }

    void SetLooping(const bool looping) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->looping = looping;
        audio->SetSourceLooping(looping);
    }

    [[nodiscard]] bool GetPlayOnStart() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return false;
        return audio->playOnStart;
    }

    void SetPlayOnStart(const bool playOnStart) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->playOnStart = playOnStart;
    }

    [[nodiscard]] float GetReferenceDistance() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 1.0f;
        return audio->referenceDistance;
    }

    void SetReferenceDistance(const float distance) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->referenceDistance = distance;
    }

    [[nodiscard]] float GetMaxDistance() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 10000.0f;
        return audio->maxDistance;
    }

    void SetMaxDistance(const float distance) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->maxDistance = distance;
    }

    [[nodiscard]] float GetRollOffFactor() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 1.0f;
        return audio->rollOffFactor;
    }

    void SetRollOffFactor(const float factor) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->rollOffFactor = factor;
    }

    [[nodiscard]] float GetInnerConeAngle() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 360.0f;
        return audio->innerConeAngle;
    }

    void SetInnerConeAngle(const float angle) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->innerConeAngle = angle;
    }

    [[nodiscard]] float GetOuterConeAngle() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 360.0f;
        return audio->outerConeAngle;
    }

    void SetOuterConeAngle(const float angle) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->outerConeAngle = angle;
    }

    [[nodiscard]] float GetOuterGain() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return 0.0f;
        return audio->outerGain;
    }

    void SetOuterGain(const float gain) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->outerGain = gain;
    }

    void PlaySound() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->PlaySound();
    }

    void SetSourcePosition(const Vector3& position) const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;
        audio->SetSourcePosition(position);
    }
};

// ---------------------------------------------------------
// Transform
// ---------------------------------------------------------

struct ScriptTransform {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentTransform* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->transforms.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] Vector3 GetPosition() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return {0.0f, 0.0f, 0.0f};
        return transform->position;
    }

    void SetPosition(const Vector3& position) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->SetPosition(position);
    }

    void AddPosition(const Vector3& position) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->AddPosition(position);
    }

    [[nodiscard]] Vector3 GetScale() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return {0.0f, 0.0f, 0.0f};
        return transform->scale;
    }

    void SetScale(const Vector3& scale) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->scale = scale;
        transform->isDirty = true;
    }

    [[nodiscard]] float GetRelativeHeight() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return 0.0f;
        return transform->relativeHeight;
    }

    void SetRelativeHeight(const float height) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->relativeHeight = height;
    }

    [[nodiscard]] Vector2 GetForward() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return {1.0f, 0.0f};
        return transform->forward;
    }

    void SetForward(const Vector2& forward) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->forward = forward;
    }

    [[nodiscard]] int GetSectorIndex() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return -1;
        return transform->sectorIndex;
    }

    [[nodiscard]] bool GetIsDirty() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return false;
        return transform->isDirty;
    }

    void SetIsDirty(const bool dirty) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->isDirty = dirty;
    }
};

// ---------------------------------------------------------
// Sprite
// ---------------------------------------------------------

struct ScriptSprite {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    static constexpr int TEXTURE_SLOT_COUNT = 8;

    static constexpr int SLOT_N  = 0;
    static constexpr int SLOT_NE = 1;
    static constexpr int SLOT_E  = 2;
    static constexpr int SLOT_SE = 3;
    static constexpr int SLOT_S  = 4;
    static constexpr int SLOT_SW = 5;
    static constexpr int SLOT_W  = 6;
    static constexpr int SLOT_NW = 7;

    [[nodiscard]] ComponentSprite* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->sprites.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] static bool IsValidSlot(const int slot) {
        return slot >= 0 && slot < TEXTURE_SLOT_COUNT;
    }

    [[nodiscard]] static bool IsValidSideCount(const int sideCount) {
        return sideCount == SIDECOUNT_SINGLE ||
               sideCount == SIDECOUNT_90 ||
               sideCount == SIDECOUNT_45;
    }

    [[nodiscard]] int GetTextureIndex(const int slot) const {
        const ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return -1;
        if (!IsValidSlot(slot)) return -1;

        return sprite->textureIndices[slot];
    }

    void SetTextureIndex(const int slot, const int index) const {
        ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        if (!IsValidSlot(slot)) return;

        sprite->textureIndices[slot] = index;
    }

    [[nodiscard]] int GetSideCount() const {
        const ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return SIDECOUNT_SINGLE;

        return sprite->sideCount;
    }

    void SetSideCount(const int sideCount) const {
        ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        if (!IsValidSideCount(sideCount)) return;

        sprite->sideCount = static_cast<SideCount>(sideCount);
    }

    void ClearTextureIndex(const int slot) const {
        SetTextureIndex(slot, -1);
    }

    void ClearAllTextureIndices() const {
        ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return;

        sprite->textureIndices.fill(-1);
    }

    [[nodiscard]] int GetNorthTextureIndex() const {return GetTextureIndex(SLOT_N);}
    [[nodiscard]] int GetNorthEastTextureIndex() const {return GetTextureIndex(SLOT_NE);}
    [[nodiscard]] int GetEastTextureIndex() const {return GetTextureIndex(SLOT_E);}
    [[nodiscard]] int GetSouthEastTextureIndex() const {return GetTextureIndex(SLOT_SE);}
    [[nodiscard]] int GetSouthTextureIndex() const {return GetTextureIndex(SLOT_S);}
    [[nodiscard]] int GetSouthWestTextureIndex() const {return GetTextureIndex(SLOT_SW);}
    [[nodiscard]] int GetWestTextureIndex() const {return GetTextureIndex(SLOT_W);}
    [[nodiscard]] int GetNorthWestTextureIndex() const {return GetTextureIndex(SLOT_NW);}

    void SetNorthTextureIndex(const int index) const {SetTextureIndex(SLOT_N, index);}
    void SetNorthEastTextureIndex(const int index) const {SetTextureIndex(SLOT_NE, index);}
    void SetEastTextureIndex(const int index) const {SetTextureIndex(SLOT_E, index);}
    void SetSouthEastTextureIndex(const int index) const { SetTextureIndex(SLOT_SE, index);}
    void SetSouthTextureIndex(const int index) const { SetTextureIndex(SLOT_S, index);}
    void SetSouthWestTextureIndex(const int index) const { SetTextureIndex(SLOT_SW, index);}
    void SetWestTextureIndex(const int index) const {SetTextureIndex(SLOT_W, index);}
    void SetNorthWestTextureIndex(const int index) const {SetTextureIndex(SLOT_NW, index);}
};

// ---------------------------------------------------------
// Decal
// ---------------------------------------------------------

struct ScriptDecal {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentDecal* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->decals.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] int GetWallIndex() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return -1;
        return decal->wallIndex;
    }

    void SetWallIndex(const int index) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->wallIndex = index;
    }

    [[nodiscard]] float GetVerticalPos() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return 0.0f;
        return decal->verticalPos;
    }

    void SetVerticalPos(const float pos) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->verticalPos = pos;
    }

    [[nodiscard]] float GetHorizontalPos() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return -1.0f;
        return decal->horizontalPos;
    }

    void SetHorizontalPos(const float pos) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->horizontalPos = pos;
    }

    [[nodiscard]] float GetWallNormalOffset() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return 0.0f;
        return decal->wallNormalOffset;
    }

    void SetWallNormalOffset(const float offset) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->wallNormalOffset = offset;
    }

    [[nodiscard]] float GetWallT() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return 0.5f;
        return decal->wallT;
    }

    void SetWallT(const float t) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->wallT = t;
    }

    [[nodiscard]] float GetBaseHeight() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return 0.0f;
        return decal->baseHeight;
    }

    void SetBaseHeight(const float height) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->baseHeight = height;
    }

    [[nodiscard]] bool GetAbsHeight() const {
        const ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return false;
        return decal->absHeight;
    }

    void SetAbsHeight(const bool abs) const {
        ComponentDecal* decal = GetComponent();
        if (decal == nullptr) return;
        decal->absHeight = abs;
    }
};

// ---------------------------------------------------------
// Rigidbody
// ---------------------------------------------------------

struct ScriptRigidbody {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentRigidbody* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->rigidbodies.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] bool GetIsStatic() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return false;
        return rb->isStatic;
    }

    void SetIsStatic(const bool isStatic) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->isStatic = isStatic;
    }

    [[nodiscard]] float GetMass() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return 1.0f;
        return rb->mass;
    }

    void SetMass(const float mass) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->mass = mass;
    }

    [[nodiscard]] float GetGravityScale() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return 9.8f;
        return rb->gravityScale;
    }

    void SetGravityScale(const float gravityScale) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->gravityScale = gravityScale;
    }

    [[nodiscard]] float GetFriction() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return 1.0f;
        return rb->friction;
    }

    void SetFriction(const float friction) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->friction = friction;
    }

    [[nodiscard]] Vector3 GetVelocity() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return {0.0f, 0.0f, 0.0f};
        return rb->velocity;
    }

    void SetVelocity(const Vector3& velocity) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->velocity = velocity;
    }

    void AddVelocity(const Vector3& velocity) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->AddVelocity(velocity);
    }
};

// ---------------------------------------------------------
// Collider
// ---------------------------------------------------------

struct ScriptCollider {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentCollider* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->colliders.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] ColliderType GetType() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return COLLIDERTYPE_SPHERE;
        return collider->type;
    }

    void SetType(const ColliderType type) const {
        if (level == nullptr) return;
        level->colliders.SetType(ownerID, type);
    }

    [[nodiscard]] bool GetIsActive() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return false;
        return collider->isActive;
    }

    void SetIsActive(const bool active) const {
        if (level == nullptr) return;
        level->colliders.SetActive(ownerID, active);
    }

    [[nodiscard]] bool GetIsTrigger() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return false;
        return collider->isTrigger;
    }

    void SetIsTrigger(const bool trigger) const {
        ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return;
        collider->isTrigger = trigger;
    }

    [[nodiscard]] Vector3 GetScale() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return {1.0f, 1.0f, 1.0f};
        return collider->scale;
    }

    void SetScale(const Vector3& scale) const {
        ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return;
        collider->scale = scale;
    }

    [[nodiscard]] float GetStepSize() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return 0.0f;
        return collider->stepSize;
    }

    void SetStepSize(const float stepSize) const {
        ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return;
        collider->stepSize = stepSize;
    }
};

// ---------------------------------------------------------
// Player Controller
// ---------------------------------------------------------

struct ScriptPlayerController {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentPlayerController* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->playerControllers.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] bool GetIsActive() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return false;
        return pc->isActive;
    }

    void SetIsActive(const bool active) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->isActive = active;
    }

    [[nodiscard]] float GetSpeed() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 46.0f;
        return pc->speed;
    }

    void SetSpeed(const float speed) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->speed = speed;
    }

    [[nodiscard]] float GetRunningSpeed() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 90.0f;
        return pc->runningSpeed;
    }

    void SetRunningSpeed(const float speed) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->runningSpeed = speed;
    }

    [[nodiscard]] float GetJumpPower() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 100.0f;
        return pc->jumpPower;
    }

    void SetJumpPower(const float jumpPower) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->jumpPower = jumpPower;
    }

    [[nodiscard]] float GetEyeHeight() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 12.0f;
        return pc->eyeHeight;
    }

    void SetEyeHeight(const float eyeHeight) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->eyeHeight = eyeHeight;
    }

    [[nodiscard]] float GetFriction() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 0.8f;
        return pc->friction;
    }

    void SetFriction(const float friction) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->friction = friction;
    }

    [[nodiscard]] float GetSensitivityX() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 0.5f;
        return pc->sensitivityX;
    }

    void SetSensitivityX(const float sensitivity) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->sensitivityX = sensitivity;
    }

    [[nodiscard]] float GetSensitivityY() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 0.5f;
        return pc->sensitivityY;
    }

    void SetSensitivityY(const float sensitivity) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->sensitivityY = sensitivity;
    }

    [[nodiscard]] bool GetNoClip() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return false;
        return pc->noClip;
    }

    void SetNoClip(const bool noClip) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->noClip = noClip;
    }

    [[nodiscard]] Vector3 GetVelocity() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return {0.0f, 0.0f, 0.0f};
        return pc->velocity;
    }

    [[nodiscard]] float GetCurrentSpeed() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 0.0f;
        return pc->currentSpeed;
    }

    [[nodiscard]] float GetCurrentEyeHeight() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 0.0f;
        return pc->currentEyeHeight;
    }
};

// ---------------------------------------------------------
// Camera
// ---------------------------------------------------------

struct ScriptCamera {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentCamera* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->cameras.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] bool GetIsActive() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return false;
        return camera->isActive;
    }

    void SetIsActive(const bool active) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->isActive = active;
    }

    [[nodiscard]] float GetYaw() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return 0.0f;
        return camera->yaw;
    }

    void SetYaw(const float yaw) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->yaw = yaw;
    }

    [[nodiscard]] float GetPitch() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return 0.0f;
        return camera->pitch;
    }

    void SetPitch(const float pitch) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->pitch = pitch;
    }

    [[nodiscard]] float GetFov() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return 90.0f;
        return camera->fov;
    }

    void SetFov(const float fov) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->fov = fov;
    }

    [[nodiscard]] float GetAspectRatio() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return 1680.0f / 960.0f;
        return camera->aspectRatio;
    }

    void SetAspectRatio(const float aspectRatio) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->aspectRatio = aspectRatio;
    }

    [[nodiscard]] float GetNearPlane() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return 0.1f;
        return camera->nearPlane;
    }

    void SetNearPlane(const float nearPlane) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->nearPlane = nearPlane;
    }

    [[nodiscard]] float GetFarPlane() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return 10000.0f;
        return camera->farPlane;
    }

    void SetFarPlane(const float farPlane) const {
        ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return;
        camera->farPlane = farPlane;
    }

    [[nodiscard]] Vector3 GetForward() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return {0.0f, 0.0f, 1.0f};
        return camera->forward;
    }

    [[nodiscard]] Vector3 GetTarget() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return {0.0f, 0.0f, 1.0f};
        return camera->target;
    }
};

// ---------------------------------------------------------
// Script Component
// ---------------------------------------------------------

struct ScriptScript {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentScript* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->scripts.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] std::string GetFileName() const {
        const ComponentScript* script = GetComponent();
        if (script == nullptr) return {};
        return script->fileName;
    }

    void SetFileName(const std::string& fileName) const {
        ComponentScript* script = GetComponent();
        if (script == nullptr) return;
        script->fileName = fileName;
    }

    [[nodiscard]] bool GetEnabled() const {
        const ComponentScript* script = GetComponent();
        if (script == nullptr) return false;
        return script->enabled;
    }

    void SetEnabled(const bool enabled) const {
        ComponentScript* script = GetComponent();
        if (script == nullptr) return;
        script->enabled = enabled;
    }
};

// ---------------------------------------------------------
// UI Transform
// ---------------------------------------------------------

struct ScriptUITransform {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentUITransform* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->ui_transforms.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] Vector2 GetAnchorMin() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {0.5f, 0.5f};
        return transform->anchorMin;
    }

    void SetAnchorMin(const Vector2& anchor) const {
        ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->anchorMin = anchor;
    }

    [[nodiscard]] Vector2 GetAnchorMax() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {0.5f, 0.5f};
        return transform->anchorMax;
    }

    void SetAnchorMax(const Vector2& anchor) const {
        ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->anchorMax = anchor;
    }

    [[nodiscard]] Vector2 GetPivot() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {0.5f, 0.5f};
        return transform->pivot;
    }

    void SetPivot(const Vector2& pivot) const {
        ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->pivot = pivot;
    }

    [[nodiscard]] Vector2 GetPosition() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {0.0f, 0.0f};
        return transform->position;
    }

    void SetPosition(const Vector2& position) const {
        ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->position = position;
    }

    [[nodiscard]] Vector2 GetScale() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {1.0f, 1.0f};
        return transform->scale;
    }

    void SetScale(const Vector2& scale) const {
        ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->scale = scale;
    }

    [[nodiscard]] float GetRotation() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return 0.0f;
        return transform->rotation;
    }

    void SetRotation(const float rotation) const {
        ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return;
        transform->rotation = rotation;
    }

    [[nodiscard]] Vector2 GetResolvedPosition() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {};
        return transform->resolvedPosition;
    }

    [[nodiscard]] Vector2 GetResolvedSize() const {
        const ComponentUITransform* transform = GetComponent();
        if (transform == nullptr) return {};
        return transform->resolvedSize;
    }
};

// ---------------------------------------------------------
// UI Sprite
// ---------------------------------------------------------

struct ScriptUISprite {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentUISprite* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->ui_sprites.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] int GetTextureIndex() const {
        const ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return -1;
        return sprite->textureIndex;
    }

    void SetTextureIndex(const int index) const {
        ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        sprite->textureIndex = index;
    }
};

// ---------------------------------------------------------
// UI Text
// ---------------------------------------------------------

struct ScriptUIText {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);

    [[nodiscard]] ComponentUIText* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->ui_texts.Get(ownerID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] std::string GetText() const {
        const ComponentUIText* text = GetComponent();
        if (text == nullptr) return {};
        return text->text;
    }

    void SetText(const std::string& value) const {
        ComponentUIText* text = GetComponent();
        if (text == nullptr) return;
        text->text = value;
    }
};

// ---------------------------------------------------------
// Entity
// ---------------------------------------------------------

struct ScriptEntity {
    Level* level = nullptr;
    ID ownerID = INVALID_ENTITY_ID;

    [[nodiscard]] ScriptComponentRef GetScriptRef(const std::string& fileName) const {
        return {
            .ownerID = ownerID,
            .scriptFile = std::filesystem::path(fileName).stem().string()
        };
    }

    [[nodiscard]] ID GetID() const {
        return ownerID;
    }

    [[nodiscard]] bool IsValid() const {
        return level != nullptr && ownerID != INVALID_ENTITY_ID;
    }

    [[nodiscard]] bool HasTransform() const {
        return level != nullptr && level->transforms.Has(ownerID);
    }

    [[nodiscard]] bool HasSprite() const {
        return level != nullptr && level->sprites.Has(ownerID);
    }

    [[nodiscard]] bool HasDecal() const {
        return level != nullptr && level->decals.Has(ownerID);
    }

    [[nodiscard]] bool HasAudioSource() const {
        return level != nullptr && level->audioSources.Has(ownerID);
    }

    [[nodiscard]] bool HasScript() const {
        return level != nullptr && level->scripts.Has(ownerID);
    }

    [[nodiscard]] bool HasPlayerController() const {
        return level != nullptr && level->playerControllers.Has(ownerID);
    }

    [[nodiscard]] bool HasCamera() const {
        return level != nullptr && level->cameras.Has(ownerID);
    }

    [[nodiscard]] bool HasCollider() const {
        return level != nullptr && level->colliders.Has(ownerID);
    }

    [[nodiscard]] bool HasRigidbody() const {
        return level != nullptr && level->rigidbodies.Has(ownerID);
    }

    [[nodiscard]] bool HasUITransform() const {
        return level != nullptr && level->ui_transforms.Has(ownerID);
    }

    [[nodiscard]] bool HasUISprite() const {
        return level != nullptr && level->ui_sprites.Has(ownerID);
    }

    [[nodiscard]] bool HasUIText() const {
        return level != nullptr && level->ui_texts.Has(ownerID);
    }

    [[nodiscard]] ScriptTransform GetTransform() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptSprite GetSprite() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptDecal GetDecal() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptAudioSource GetAudioSource() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptScript GetScript() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptPlayerController GetPlayerController() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptCamera GetCamera() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptCollider GetCollider() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptRigidbody GetRigidbody() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptUITransform GetUITransform() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptUISprite GetUISprite() const {
        return {level, ownerID};
    }

    [[nodiscard]] ScriptUIText GetUIText() const {
        return {level, ownerID};
    }
};

#endif // TILKY_ENGINE_WRAPPERS_HPP