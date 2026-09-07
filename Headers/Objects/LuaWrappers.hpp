//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_WRAPPERS_HPP
#define TILKY_ENGINE_WRAPPERS_HPP

#include <filesystem>

#include <sol/error.hpp>

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"

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

    [[nodiscard]] std::string GetSoundFileName() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return {};

        return audio->soundFileName;
    }

    void SetSoundFileName(const std::string& fileName) const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->soundFileName = fileName;
    }

    void ClearSoundFileName() const {
        SetSoundFileName("");
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

    [[nodiscard]] Vector4 GetRotation() const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return {0.0f, 0.0f, 0.0f, 1.0f};


        return {transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w};
    }

    void SetRotation(const Vector4& rotation) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;

        transform->rotation = Quaternion{rotation.x, rotation.y, rotation.z, rotation.w}.Normalized();

        transform->isDirty = true;
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
        return sideCount == SIDECOUNT_SINGLE || sideCount == SIDECOUNT_90 || sideCount == SIDECOUNT_45;
    }

    [[nodiscard]] Vector4 GetColor() const {
        const ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return {1.0f, 1.0f, 1.0f, 1.0f};

        return sprite->color;
    }

    void SetColor(const Vector4& color) const {
        ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return;

        sprite->color = color;
    }

    [[nodiscard]] std::string GetTextureFileName(const int slot) const {
        const ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return {};
        if (!IsValidSlot(slot)) return {};

        return sprite->textureFileNames[slot];
    }

    void SetTextureFileName(
        const int slot,
        const std::string& fileName
    ) const {
        ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return;
        if (!IsValidSlot(slot)) return;

        sprite->textureFileNames[slot] = fileName;
    }

    [[nodiscard]] int GetSideCount() const {
        const ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return SIDECOUNT_SINGLE;

        return static_cast<int>(sprite->sideCount);
    }

    void SetSideCount(const int sideCount) const {
        ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return;
        if (!IsValidSideCount(sideCount)) return;

        sprite->sideCount = static_cast<SideCount>(sideCount);
    }

    void ClearTextureFileName(const int slot) const {
        SetTextureFileName(slot, "");
    }

    void ClearAllTextureFileNames() const {
        ComponentSprite* sprite = GetComponent();

        if (sprite == nullptr) return;

        sprite->textureFileNames.fill("");
    }

    [[nodiscard]] std::string GetNorthTextureFileName() const {
        return GetTextureFileName(SLOT_N);
    }

    [[nodiscard]] std::string GetNorthEastTextureFileName() const {
        return GetTextureFileName(SLOT_NE);
    }

    [[nodiscard]] std::string GetEastTextureFileName() const {
        return GetTextureFileName(SLOT_E);
    }

    [[nodiscard]] std::string GetSouthEastTextureFileName() const {
        return GetTextureFileName(SLOT_SE);
    }

    [[nodiscard]] std::string GetSouthTextureFileName() const {
        return GetTextureFileName(SLOT_S);
    }

    [[nodiscard]] std::string GetSouthWestTextureFileName() const {
        return GetTextureFileName(SLOT_SW);
    }

    [[nodiscard]] std::string GetWestTextureFileName() const {
        return GetTextureFileName(SLOT_W);
    }

    [[nodiscard]] std::string GetNorthWestTextureFileName() const {
        return GetTextureFileName(SLOT_NW);
    }

    void SetNorthTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_N, fileName);
    }

    void SetNorthEastTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_NE, fileName);
    }

    void SetEastTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_E, fileName);
    }

    void SetSouthEastTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_SE, fileName);
    }

    void SetSouthTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_S, fileName);
    }

    void SetSouthWestTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_SW, fileName);
    }

    void SetWestTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_W, fileName);
    }

    void SetNorthWestTextureFileName(const std::string& fileName) const {
        SetTextureFileName(SLOT_NW, fileName);
    }

    void SetIsStatic(const bool isStatic) const {
        ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        sprite->isStatic = isStatic;
    }

    [[nodiscard]] bool IsStatic() const {
        const ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return false;
        return sprite->isStatic;
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

    [[nodiscard]] bool GetIsGrounded() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return false;
        return rb->isGrounded;
    }

    void SetIsGrounded(const bool isGrounded) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->isGrounded = isGrounded;
    }

    [[nodiscard]] Vector3 GetGroundNormal() const {
        const ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return Vector3(0.0f, 0.0f, 0.0f);
        return rb->groundNormal;
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

    [[nodiscard]] std::string GetTextureIndex() const {
        const ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return "";
        return sprite->texture;
    }

    void SetTextureIndex(const int index) const {
        ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        sprite->texture = index;
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
// Behaviour reference (another script instance, anywhere in the level)
// ---------------------------------------------------------

// Forward declaration: ScriptBehaviourRef::GetGameObject() returns a
// ScriptEntity by value and ScriptEntity::GetScript() returns a
// ScriptBehaviourRef by value, so the two are defined with only forward
// declarations of each other's methods here; the actual bodies of the
// cross-referencing methods are inline definitions placed after both structs
// are complete (see "Behaviour <-> GameObject cross-reference definitions"
// below ScriptEntity's closing brace).
struct ScriptEntity;

// A safe handle to one specific script (Behaviour) instance, identified by
// its globally-unique ScriptInstanceID rather than by filename - so it stays
// unambiguous even when the target GameObject has several scripts attached,
// including duplicates of the same script. Field and function access
// (ref.someField, ref:SomeMethod()) is forwarded straight into the target
// instance's own Lua environment via LuaScriptRuntime, so calling a public
// function on a referenced Behaviour needs no owner-ID or filename lookup on
// the Lua side - see the "Behaviour" usertype registration in LuaSystem.cpp
// (sol::meta_function::index / new_index bound to LuaGet/LuaSet).
struct ScriptBehaviourRef {
    Level* level = nullptr;
    ID ownerID = INVALID_ENTITY_ID;
    ScriptInstanceID instanceID = INVALID_SCRIPT_INSTANCE_ID;

    [[nodiscard]] bool IsValid() const {
        return LuaScriptRuntime::IsInstanceValid(ownerID, instanceID);
    }

    [[nodiscard]] ScriptEntity GetGameObject() const;

    // The referenced script's own enabled flag - independent of the
    // GameObject's enabled flag (Entity::enabled).
    [[nodiscard]] bool GetEnabled() const {
        if (level == nullptr) return false;
        return LuaScriptRuntime::GetInstanceEnabled(*level, ownerID, instanceID);
    }

    void SetEnabled(const bool value) const {
        if (level == nullptr) return;
        LuaScriptRuntime::SetInstanceEnabled(*level, ownerID, instanceID, value);
    }

    // Dynamic field/function access into the target instance's environment -
    // bound as sol::meta_function::index / new_index, not called directly
    // from C++.
    //
    // A custom __index/__newindex on a sol2 usertype fully replaces its
    // default property dispatch, so "isValid"/"gameObject"/"enabled" are
    // handled here by name rather than also being registered as ordinary
    // usertype properties (which sol2 would then never see) - see the
    // "Behaviour" usertype registration in LuaSystem.cpp, which binds only
    // these two functions and nothing else.
    //
    // Declared only here (like GetGameObject() above) and defined out-of-line
    // after ScriptEntity is a complete type - LuaGet constructs a
    // sol::object from a ScriptEntity returned by value, which needs
    // ScriptEntity's full definition, not just the forward declaration
    // visible at this point in the file.
    [[nodiscard]] sol::object LuaGet(const std::string& key, sol::this_state state) const;
    void LuaSet(const std::string& key, sol::object value) const;
};

// ---------------------------------------------------------
// Entity (GameObject)
// ---------------------------------------------------------

// Tilky's GameObject facade. Lua never sees the raw ECS entity or its
// component storages - every field here is either a plain value or another
// safe {Level*, ID} handle, and every accessor null-checks before touching
// the level. Registered to Lua as "GameObject" (see LuaEntityBindings.cpp);
// the C++ type name stays ScriptEntity to minimize churn across the engine
// side of the codebase.
struct ScriptEntity {
    Level* level = nullptr;
    ID ownerID = INVALID_ENTITY_ID;

    [[nodiscard]] Entity* GetEntity() const {
        if (level == nullptr || ownerID == INVALID_ENTITY_ID) return nullptr;
        return level->GetEntity(ownerID);
    }

    [[nodiscard]] ID GetID() const {
        return ownerID;
    }

    [[nodiscard]] bool IsValid() const {
        return GetEntity() != nullptr;
    }

    [[nodiscard]] std::string GetName() const {
        const Entity* entity = GetEntity();
        return entity == nullptr ? std::string{} : entity->name;
    }

    void SetName(const std::string& name) const {
        Entity* entity = GetEntity();
        if (entity == nullptr) return;
        entity->name = name;
    }

    // GameObject-level active state. Disabling a GameObject effectively
    // disables every attached script's ticking (Update/FixedUpdate skipped,
    // OnDisable/OnEnable fired) without touching each script's own `enabled`
    // flag - see Entity::enabled and ScriptBehaviourRef::GetEnabled/SetEnabled
    // for the per-script flag.
    [[nodiscard]] bool GetEnabled() const {
        const Entity* entity = GetEntity();
        return entity != nullptr && entity->enabled;
    }

    void SetEnabled(const bool value) const {
        Entity* entity = GetEntity();
        if (entity == nullptr) return;
        entity->enabled = value;
    }

    // Queues this GameObject for destruction. Safe to call from anywhere in
    // a script's lifecycle (Start/Update/FixedUpdate/etc.) - the actual
    // removal (component teardown, OnDestroy on every attached script, then
    // erasing the entity) happens once, after every script has finished
    // running this frame. See LuaScriptRuntime::QueueEntityDestroy.
    void Destroy() const {
        if (ownerID == INVALID_ENTITY_ID) return;
        LuaScriptRuntime::QueueEntityDestroy(ownerID);
    }

    [[nodiscard]] bool HasTransform() const {
        return level != nullptr && level->transforms.Has(ownerID);
    }

    [[nodiscard]] bool HasSprite() const {
        return level != nullptr && level->sprites.Has(ownerID);
    }

    [[nodiscard]] bool HasAudioSource() const {
        return level != nullptr && level->audioSources.Has(ownerID);
    }

    [[nodiscard]] bool HasScript() const {
        return level != nullptr && level->scripts.HasAny(ownerID);
    }

    // True if this GameObject has an attached script whose asset id
    // (ComponentScript::fileName, a project-relative path without extension -
    // see LuaScriptSystem's script identity notes) ends in `scriptName`.
    // Matching on the final path segment means both "Health" and
    // "Player/Health" find a script stored at "Scripts/Player/Health.lua".
    // Use GetScriptById for an unambiguous lookup when several same-named
    // scripts might be attached.
    [[nodiscard]] bool HasScriptNamed(const std::string& scriptName) const {
        if (level == nullptr) return false;

        const std::string wanted = std::filesystem::path(scriptName).filename().string();

        for (const ComponentScript* script : level->scripts.GetAll(ownerID))
            if (std::filesystem::path(script->fileName).filename().string() == wanted) return true;

        return false;
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

    [[nodiscard]] ScriptAudioSource GetAudioSource() const {
        return {level, ownerID};
    }

    // Looks up an attached script (Behaviour) by asset id, matching only the
    // final path segment - see HasScriptNamed. Returns an invalid
    // ScriptBehaviourRef (IsValid() == false) if no match is attached. When
    // several same-named scripts are attached, this returns the first one
    // found; use GetScriptById for an unambiguous lookup, or GetScripts() to
    // enumerate every instance.
    [[nodiscard]] ScriptBehaviourRef GetScript(const std::string& scriptName) const {
        if (level == nullptr) return {level, ownerID, INVALID_SCRIPT_INSTANCE_ID};

        const std::string wanted = std::filesystem::path(scriptName).filename().string();

        for (const ComponentScript* script : level->scripts.GetAll(ownerID)) {
            if (std::filesystem::path(script->fileName).filename().string() == wanted)
                return {level, ownerID, script->instanceID};
        }

        return {level, ownerID, INVALID_SCRIPT_INSTANCE_ID};
    }

    // Looks up an attached script (Behaviour) by its exact, globally-unique
    // ScriptInstanceID - the unambiguous form of GetScript(name), and what a
    // serialized Behaviour-reference field resolves through.
    [[nodiscard]] ScriptBehaviourRef GetScriptById(const ScriptInstanceID instanceId) const {
        return {level, ownerID, instanceId};
    }

    // Every script attached to this GameObject, as Behaviour references.
    [[nodiscard]] std::vector<ScriptBehaviourRef> GetScripts() const {
        std::vector<ScriptBehaviourRef> result;

        if (level == nullptr) return result;

        for (const ComponentScript* script : level->scripts.GetAll(ownerID))
            result.push_back({level, ownerID, script->instanceID});

        return result;
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

// ---------------------------------------------------------
// Behaviour <-> GameObject cross-reference definitions
//
// ScriptBehaviourRef and ScriptEntity refer to each other by value, so this
// one method has to be defined out-of-line, here, after both types are
// complete.
// ---------------------------------------------------------

inline ScriptEntity ScriptBehaviourRef::GetGameObject() const {
    return {level, ownerID};
}

inline sol::object ScriptBehaviourRef::LuaGet(const std::string& key, const sol::this_state state) const {
    const sol::state_view luaView(state);

    if (key == "isValid") return sol::make_object(luaView, IsValid());
    if (key == "gameObject") return sol::make_object(luaView, GetGameObject());
    if (key == "enabled") return sol::make_object(luaView, GetEnabled());

    return LuaScriptRuntime::GetInstanceField(ownerID, instanceID, key, state);
}

inline void ScriptBehaviourRef::LuaSet(const std::string& key, sol::object value) const {
    if (key == "enabled") {
        SetEnabled(value.as<bool>());
        return;
    }

    // isValid/gameObject are read-only; a stray write to them is ignored
    // rather than silently poking a same-named field into the target
    // script's environment.
    if (key == "isValid" || key == "gameObject") return;

    LuaScriptRuntime::SetInstanceField(ownerID, instanceID, key, std::move(value));
}

// ---------------------------------------------------------
// Wall
// ---------------------------------------------------------
struct ScriptWall {
    Level* level = nullptr;
    ID wallID = INVALID_ID;

    [[nodiscard]] Wall* GetWall() const {
        if (level == nullptr || wallID == INVALID_ID) return nullptr;

        const auto it = level->wallIDToIndex.find(wallID);
        if (it == level->wallIDToIndex.end()) return nullptr;

        const size_t index = static_cast<size_t>(it->second);
        if (index >= level->walls.size()) return nullptr;

        return &level->walls[index];
    }

    [[nodiscard]] ID GetID() const {
        return wallID;
    }

    [[nodiscard]] bool IsValid() const {
        return GetWall() != nullptr;
    }

    [[nodiscard]] Vector2 GetStart() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->start;
    }

    [[nodiscard]] Vector2 GetEnd() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->end;
    }

    [[nodiscard]] Vector4 GetColor() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->color;
    }

    void SetColor(const Vector4& value) const {
        Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        wall->color = value;
    }

    [[nodiscard]] Vector2 GetTextureOffset() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->textureOffset;
    }

    void SetTextureOffset(const Vector2& value) const {
        Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        wall->textureOffset = value;
    }

    [[nodiscard]] std::string GetTextureFileName() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->textureFileName;
    }

    void SetTextureFileName(const std::string& value) const {
        Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        wall->textureFileName = value;
    }

    void ClearTextureFileName() const {
        SetTextureFileName("");
    }

    [[nodiscard]] ID GetFrontSector() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->frontSector;
    }

    [[nodiscard]] ID GetBackSector() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->backSector;
    }

    [[nodiscard]] Vector2 GetDir() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->dir;
    }

    [[nodiscard]] Vector2 GetNormal() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->normal;
    }

    [[nodiscard]] float GetLength() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid WallRef");
        return wall->length;
    }
};

// ---------------------------------------------------------
// Sector floor
// ---------------------------------------------------------

struct ScriptSectorFloor {
    Level* level = nullptr;
    ID sectorID = INVALID_ID;
    int floorIndex = -1;

    [[nodiscard]] Sector* GetSector() const {
        if (level == nullptr || sectorID == INVALID_ID) return nullptr;

        const auto it = level->sectorIDToIndex.find(sectorID);
        if (it == level->sectorIDToIndex.end()) return nullptr;

        const size_t sectorIndex = static_cast<size_t>(it->second);
        if (sectorIndex >= level->sectors.size()) return nullptr;

        return &level->sectors[sectorIndex];
    }

    [[nodiscard]] SectorFloor* GetSectorFloor() const {
        Sector* sector = GetSector();

        if (sector == nullptr ||
            floorIndex < 0 ||
            floorIndex >= static_cast<int>(sector->floors.size())) {
            return nullptr;
        }

        return &sector->floors[floorIndex];
    }

    [[nodiscard]] int GetIndex() const {
        return floorIndex + 1;
    }

    [[nodiscard]] bool IsValid() const {
        return GetSectorFloor() != nullptr;
    }

    [[nodiscard]] float GetFloorHeight() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        return floor->floor.height;
    }

    void SetFloorHeight(const float value) const {
        Sector* sector = GetSector();
        SectorFloor* floor = GetSectorFloor();

        if (sector == nullptr || floor == nullptr) {
            throw sol::error("Invalid SectorFloorRef");
        }

        if (value >= floor->ceiling.height) {
            throw sol::error("Sector floor height must be below its ceiling");
        }

        if (floorIndex > 0 &&
            value < sector->floors[floorIndex - 1].ceiling.height) {
            throw sol::error("Sector floor interval overlaps the previous interval");
        }

        floor->floor.height = value;
    }

    [[nodiscard]] float GetCeilingHeight() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        return floor->ceiling.height;
    }

    void SetCeilingHeight(const float value) const {
        Sector* sector = GetSector();
        SectorFloor* floor = GetSectorFloor();

        if (sector == nullptr || floor == nullptr) {
            throw sol::error("Invalid SectorFloorRef");
        }

        if (value <= floor->floor.height) {
            throw sol::error("Sector ceiling height must be above its floor");
        }

        if (floorIndex + 1 < static_cast<int>(sector->floors.size()) &&
            value > sector->floors[floorIndex + 1].floor.height) {
            throw sol::error("Sector floor interval overlaps the next interval");
        }

        floor->ceiling.height = value;
    }

    [[nodiscard]] Vector4 GetFloorColor() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        return floor->floor.color;
    }

    void SetFloorColor(const Vector4& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        floor->floor.color = value;
    }

    [[nodiscard]] Vector4 GetCeilingColor() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        return floor->ceiling.color;
    }

    void SetCeilingColor(const Vector4& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        floor->ceiling.color = value;
    }

    [[nodiscard]] std::string GetFloorTexture() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        return floor->floor.texture;
    }

    void SetFloorTexture(const std::string& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        floor->floor.texture = value;
    }

    void ClearFloorTexture() const {
        SetFloorTexture("");
    }

    [[nodiscard]] std::string GetCeilingTexture() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        return floor->ceiling.texture;
    }

    void SetCeilingTexture(const std::string& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloorRef");
        floor->ceiling.texture = value;
    }

    void ClearCeilingTexture() const {
        SetCeilingTexture("");
    }
};

// ---------------------------------------------------------
// Sector
// ---------------------------------------------------------

struct ScriptSector {
    Level* level = nullptr;
    ID sectorID = INVALID_ID;

    [[nodiscard]] Sector* GetSector() const {
        if (level == nullptr || sectorID == INVALID_ID) return nullptr;

        const auto it = level->sectorIDToIndex.find(sectorID);
        if (it == level->sectorIDToIndex.end()) return nullptr;

        const size_t index = static_cast<size_t>(it->second);
        if (index >= level->sectors.size()) return nullptr;

        return &level->sectors[index];
    }

    [[nodiscard]] ID GetID() const {
        return sectorID;
    }

    [[nodiscard]] bool IsValid() const {
        return GetSector() != nullptr;
    }

    [[nodiscard]] Vector3 GetLight() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        return sector->light;
    }

    void SetLight(const Vector3 &value) const {
        Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        sector->light = value;
    }

    [[nodiscard]] int GetFloorCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        return static_cast<int>(sector->floors.size());
    }

    [[nodiscard]] ScriptSectorFloor GetFloor(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->floors.size())) {
            throw sol::error("Sector floor index out of range");
        }

        return {
            .level = level,
            .sectorID = sectorID,
            .floorIndex = index
        };
    }

    [[nodiscard]] int GetVertexCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        return static_cast<int>(sector->vertices.size());
    }

    [[nodiscard]] Vector2 GetVertex(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->vertices.size())) {
            throw sol::error("Sector vertex index out of range");
        }

        return sector->vertices[index];
    }

    [[nodiscard]] int GetWallCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        return static_cast<int>(sector->walls.size());
    }

    [[nodiscard]] ScriptWall GetWall(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->walls.size())) {
            throw sol::error("Sector wall index out of range");
        }

        const Wall* wall = sector->walls[index];

        if (wall == nullptr) {
            return {
                .level = level,
                .wallID = INVALID_ID
            };
        }

        return {
            .level = level,
            .wallID = wall->id
        };
    }

    [[nodiscard]] int GetEntityCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        return static_cast<int>(sector->entitiesInside.size());
    }

    [[nodiscard]] ScriptEntity GetEntity(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->entitiesInside.size())) {
            throw sol::error("Sector entity index out of range");
        }

        return {
            .level = level,
            .ownerID = sector->entitiesInside[index]
        };
    }

    [[nodiscard]] int GetNeighborCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");
        return static_cast<int>(sector->neighbors.size());
    }

    [[nodiscard]] ScriptSector GetNeighbor(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid SectorRef");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->neighbors.size())) {
            throw sol::error("Sector neighbor index out of range");
        }

        const Sector* neighbor = sector->neighbors[index];

        if (neighbor == nullptr) {
            return {
                .level = level,
                .sectorID = INVALID_ID
            };
        }

        return {
            .level = level,
            .sectorID = neighbor->id
        };
    }
};

#endif // TILKY_ENGINE_WRAPPERS_HPP