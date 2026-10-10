//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_WRAPPERS_HPP
#define TILKY_ENGINE_WRAPPERS_HPP

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

#include <sol/error.hpp>
#include <spdlog/spdlog.h>

#include "Headers/Math/Constants.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/TagRegistry.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptRuntime.hpp"
#include "Headers/Runtime/Gameplay/FlipbookSystem.hpp"
#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

// ---------------------------------------------------------
// Audio Source
// ---------------------------------------------------------

struct ScriptAudioSource {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);
    // Which of the owner's AudioSources this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentAudioSource* GetComponent() const {
        if (level == nullptr) return nullptr;

        return level->audioSources.GetInstance(instanceID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] Vector3 GetOffset() const {
        const ComponentAudioSource* component = GetComponent();
        if (component == nullptr) return {0.0f, 0.0f, 0.0f};
        return component->offset;
    }

    void SetOffset(const Vector3& offset) const {
        ComponentAudioSource* component = GetComponent();
        if (component == nullptr) return;
        component->offset = offset;
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
        audio->SetSourceReferenceDistance(distance);
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
        audio->SetSourceMaxDistance(distance);
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
        audio->SetSourceRollOffFactor(factor);
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
        audio->SetSourceInnerConeAngle(angle);
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
        audio->SetSourceOuterConeAngle(angle);
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
        audio->SetSourceOuterGain(gain);
    }

    void PlaySound() const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->PlaySound();
    }

    void StopSound() const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->StopSound();
    }

    void PauseSound() const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->PauseSound();
    }

    void ResumeSound() const {
        ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return;

        audio->ResumeSound();
    }

    [[nodiscard]] bool IsPlaying() const {
        const ComponentAudioSource* audio = GetComponent();
        if (audio == nullptr) return false;

        return audio->IsPlaying();
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

    // Turns the entity so its local +Z faces point. Does nothing if point is
    // on top of the entity.
    void LookAt(const Vector3& point, const bool yawOnly) const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;

        LookDirection(point - transform->position, yawOnly);
    }

    // Turns the entity so its local +Z faces along direction. Does nothing
    // for a direction with nothing to face.
    void LookDirection(const Vector3& direction, const bool yawOnly) const {
        ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return;

        if (!QuaternionMath::LookRotation(direction, yawOnly, transform->rotation)) return;

        transform->isDirty = true;
    }

    // ---- Directions, from rotation ----
    // Right is local -X: with +Z ahead and +Y up, that is the screen's right
    // (see Matrix4::LookAt), and the way the player strafes with D.

    [[nodiscard]] Vector3 GetForward() const {
        return RotateLocal(QuaternionMath::LocalForward());
    }

    [[nodiscard]] Vector3 GetRight() const {
        return RotateLocal({-1.0f, 0.0f, 0.0f});
    }

    [[nodiscard]] Vector3 GetUp() const {
        return RotateLocal({0.0f, 1.0f, 0.0f});
    }

    // ---- Distance / direction to a point ----
    // The 2D versions ignore height (y). Directions are unit length, or zero
    // when the point is on top of this transform.

    [[nodiscard]] float DistanceTo(const Vector3& point) const {
        return Vector3Math::Length(point - GetPosition());
    }

    [[nodiscard]] float DistanceTo2D(const Vector3& point) const {
        Vector3 delta = point - GetPosition();
        delta.y = 0.0f;
        return Vector3Math::Length(delta);
    }

    [[nodiscard]] Vector3 DirectionTo(const Vector3& point) const {
        return Vector3Math::Normalized(point - GetPosition());
    }

    [[nodiscard]] Vector3 DirectionTo2D(const Vector3& point) const {
        Vector3 delta = point - GetPosition();
        delta.y = 0.0f;
        return Vector3Math::Normalized(delta);
    }

private:
    [[nodiscard]] Vector3 RotateLocal(const Vector3& local) const {
        const ComponentTransform* transform = GetComponent();
        if (transform == nullptr) return local;
        return QuaternionMath::Rotate(transform->rotation, local);
    }
};

// ---------------------------------------------------------
// Sprite
// ---------------------------------------------------------

struct ScriptSprite {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);
    // Which of the owner's Sprites this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

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

        return level->sprites.GetInstance(instanceID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] Vector3 GetOffset() const {
        const ComponentSprite* component = GetComponent();
        if (component == nullptr) return {0.0f, 0.0f, 0.0f};
        return component->offset;
    }

    void SetOffset(const Vector3& offset) const {
        ComponentSprite* component = GetComponent();
        if (component == nullptr) return;
        component->offset = offset;
    }

    [[nodiscard]] static bool IsValidSlot(const int slot) {
        return slot >= 0 && slot < TEXTURE_SLOT_COUNT;
    }

    [[nodiscard]] bool GetIsActive() const {
        const ComponentSprite* sprite = GetComponent();
        return sprite != nullptr && sprite->isActive;
    }

    void SetIsActive(const bool active) const {
        ComponentSprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        sprite->isActive = active;
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
    // Which of the owner's Rigidbodys this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentRigidbody* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->rigidbodies.GetInstance(instanceID);
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

    // velocity += impulse / mass, so heavier bodies move less. A mass of 0
    // or less is treated as 1.
    void AddImpulse(const Vector3& impulse) const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;

        const float mass = rb->mass > 0.0f ? rb->mass : 1.0f;
        rb->AddVelocity(impulse / mass);
    }

    void Stop() const {
        ComponentRigidbody* rb = GetComponent();
        if (rb == nullptr) return;
        rb->velocity = {0.0f, 0.0f, 0.0f};
    }
};

// ---------------------------------------------------------
// Model
// ---------------------------------------------------------

struct ScriptModel {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);
    // Which of the owner's Models this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentModel* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->models.GetInstance(instanceID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] Vector3 GetOffset() const {
        const ComponentModel* component = GetComponent();
        if (component == nullptr) return {0.0f, 0.0f, 0.0f};
        return component->offset;
    }

    void SetOffset(const Vector3& offset) const {
        ComponentModel* component = GetComponent();
        if (component == nullptr) return;
        component->offset = offset;
    }

    [[nodiscard]] std::string GetFileName() const {
        const ComponentModel* model = GetComponent();
        if (model == nullptr) return {};
        return model->fileName;
    }

    // The renderer reads fileName every frame, so the new model shows up on
    // the next frame (loaded on first use, shared with every other user).
    void SetFileName(const std::string& fileName) const {
        ComponentModel* model = GetComponent();
        if (model == nullptr) return;
        model->fileName = fileName;
    }

    void ClearFileName() const {
        SetFileName("");
    }
};

// ---------------------------------------------------------
// Flipbook
// ---------------------------------------------------------

struct ScriptFlipbook {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);
    // Which of the owner's Flipbooks this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentFlipbook* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->flipbooks.GetInstance(instanceID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] std::string GetFlipbookFileName() const {
        const ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return {};
        return flipbook->flipbookFileName;
    }

    void SetFlipbookFileName(const std::string& fileName) const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        FlipbookSystem::SetFlipbookFileName(*flipbook, fileName);
    }

    [[nodiscard]] float GetSpeed() const {
        const ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return 1.0f;
        return flipbook->speed;
    }

    void SetSpeed(const float speed) const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        flipbook->speed = speed;
    }

    [[nodiscard]] bool IsPlaying() const {
        const ComponentFlipbook* flipbook = GetComponent();
        return flipbook != nullptr && flipbook->playing;
    }

    // Empty fileName = the current file. See FlipbookSystem::Play.
    void Play(const std::string& fileName, const bool restart) const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        FlipbookSystem::Play(*flipbook, fileName, restart);
    }

    void Pause() const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        FlipbookSystem::Pause(*flipbook);
    }

    void Resume() const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        FlipbookSystem::Resume(*flipbook);
    }

    void Stop() const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        FlipbookSystem::Stop(*flipbook);
    }

    // An unknown name is reported (with the calling script's file and line)
    // and leaves the frame as it was, rather than stopping the script.
    void SetFrame(const std::string& frameName, const sol::this_state state) const {
        ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return;
        if (FlipbookSystem::SetFrame(*flipbook, frameName)) return;

        lua_State* L = state;
        luaL_where(L, 1);
        const std::string where = lua_tostring(L, -1);
        lua_pop(L, 1);

        const std::string message = where + "Flipbook:SetFrame: '" + flipbook->flipbookFileName +
                                    "' has no frame called '" + frameName + "'";
        spdlog::error("{}", message);
        EditorFunctions::Print(message, Vector3{200.0f, 60.0f, 60.0f}, 15.0f);
    }

    [[nodiscard]] std::string GetFrame() const {
        const ComponentFlipbook* flipbook = GetComponent();
        if (flipbook == nullptr) return {};
        return FlipbookSystem::GetFrameName(*flipbook);
    }
};

// ---------------------------------------------------------
// Collider
// ---------------------------------------------------------

struct ScriptCollider {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);
    // Which of the owner's Colliders this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentCollider* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->colliders.GetInstance(instanceID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] Vector3 GetOffset() const {
        const ComponentCollider* component = GetComponent();
        if (component == nullptr) return {0.0f, 0.0f, 0.0f};
        return component->offset;
    }

    void SetOffset(const Vector3& offset) const {
        ComponentCollider* component = GetComponent();
        if (component == nullptr) return;
        component->offset = offset;
    }

    [[nodiscard]] ColliderType GetType() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return COLLIDERTYPE_SPHERE;
        return collider->type;
    }

    void SetType(const ColliderType type) const {
        if (level == nullptr) return;
        level->colliders.SetType(instanceID, type);
    }

    [[nodiscard]] bool GetIsActive() const {
        const ComponentCollider* collider = GetComponent();
        if (collider == nullptr) return false;
        return collider->isActive;
    }

    void SetIsActive(const bool active) const {
        if (level == nullptr) return;
        level->colliders.SetActive(instanceID, active);
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
    // Which of the owner's PlayerControllers this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentPlayerController* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->playerControllers.GetInstance(instanceID);
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

        if (active) level->ActivatePlayerController(*pc);
        else pc->isActive = false;
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

    [[nodiscard]] float GetAcceleration() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 80.0f;
        return pc->acceleration;
    }

    void SetAcceleration(const float acceleration) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->acceleration = acceleration;
    }

    [[nodiscard]] float GetDeceleration() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 60.0f;
        return pc->deceleration;
    }

    void SetDeceleration(const float deceleration) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->deceleration = deceleration;
    }

    [[nodiscard]] float GetAirControl() const {
        const ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return 0.3f;
        return pc->airControl;
    }

    void SetAirControl(const float airControl) const {
        ComponentPlayerController* pc = GetComponent();
        if (pc == nullptr) return;
        pc->airControl = airControl;
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
    // Which of the owner's Cameras this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentCamera* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->cameras.GetInstance(instanceID);
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

        if (active) level->ActivateCamera(*camera);
        else camera->isActive = false;
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

    // Where the camera sees from: the owner's Transform, raised by the eye
    // height of an active PlayerController on the same entity. This is the
    // body's eye, without the renderer's stair smoothing.
    [[nodiscard]] Vector3 GetEyePosition() const {
        if (level == nullptr) return {0.0f, 0.0f, 0.0f};

        const ComponentTransform* transform = level->transforms.Get(ownerID);
        if (transform == nullptr) return {0.0f, 0.0f, 0.0f};

        Vector3 eye = transform->position;

        for (const ComponentPlayerController* controller : level->playerControllers.GetAll(ownerID))
            if (controller->isActive) eye.y += controller->eyeHeight;

        return eye;
    }

    // Unit vector through the middle of the screen. Built from yaw/pitch
    // rather than read from `forward`, which only updates when a frame is drawn.
    [[nodiscard]] Vector3 GetViewForward() const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return {0.0f, 0.0f, 1.0f};
        return CameraSystem::GetCameraForwardEngineSpace(camera->yaw, camera->pitch);
    }

    // World point -> screen, normalized: (0, 0) top-left, (1, 1) bottom-right,
    // like UITransform anchors. Points off to the side give values outside
    // 0..1. nullopt when the point is behind the camera.
    [[nodiscard]] std::optional<Vector2> WorldToScreen(const Vector3& point) const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return std::nullopt;

        Vector3 forward, right, up;
        GetViewBasis(*camera, forward, right, up);

        const Vector3 delta = point - GetEyePosition();
        const float depth = Vector3Math::Dot(delta, forward);
        if (depth <= Constants::Epsilon) return std::nullopt;

        const float tanHalfFov = std::tan(camera->fov * 0.5f * Constants::DegToRad);
        const float ndcX = Vector3Math::Dot(delta, right) / (depth * tanHalfFov * camera->aspectRatio);
        const float ndcY = Vector3Math::Dot(delta, up) / (depth * tanHalfFov);

        return Vector2{(ndcX + 1.0f) * 0.5f, (1.0f - ndcY) * 0.5f};
    }

    // Screen point (normalized, as WorldToScreen) -> unit direction of the
    // ray from the eye through it.
    [[nodiscard]] Vector3 ScreenToDirection(const float x, const float y) const {
        const ComponentCamera* camera = GetComponent();
        if (camera == nullptr) return {0.0f, 0.0f, 1.0f};

        Vector3 forward, right, up;
        GetViewBasis(*camera, forward, right, up);

        const float tanHalfFov = std::tan(camera->fov * 0.5f * Constants::DegToRad);
        const float ndcX = x * 2.0f - 1.0f;
        const float ndcY = 1.0f - y * 2.0f;

        return Vector3Math::Normalized(
            forward + right * (ndcX * tanHalfFov * camera->aspectRatio) + up * (ndcY * tanHalfFov)
        );
    }

private:
    // Same axes as Matrix4::LookAt. Right comes from yaw alone so it stays
    // defined when looking straight up or down.
    static void GetViewBasis(const ComponentCamera& camera, Vector3& forward, Vector3& right, Vector3& up) {
        forward = CameraSystem::GetCameraForwardEngineSpace(camera.yaw, camera.pitch);

        const float yawRadians = camera.yaw * Constants::DegToRad;
        right = {-std::cos(yawRadians), 0.0f, std::sin(yawRadians)};

        up = Vector3Math::Cross(right, forward);
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
    // Which of the owner's UISprites this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentUISprite* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->ui_sprites.GetInstance(instanceID);
    }

    [[nodiscard]] bool IsValid() const {
        return GetComponent() != nullptr;
    }

    [[nodiscard]] std::string GetTextureIndex() const {
        const ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return "";
        return sprite->texture;
    }

    // Despite the name this is the texture's path (relative to Assets, with
    // extension), the same string the UI editor stores.
    void SetTextureIndex(const std::string& texture) const {
        ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        sprite->texture = texture;
    }

    [[nodiscard]] bool GetIsActive() const {
        const ComponentUISprite* sprite = GetComponent();
        return sprite != nullptr && sprite->isActive;
    }

    void SetIsActive(const bool active) const {
        ComponentUISprite* sprite = GetComponent();
        if (sprite == nullptr) return;
        sprite->isActive = active;
    }
};

// ---------------------------------------------------------
// UI Text
// ---------------------------------------------------------

struct ScriptUIText {
    Level* level = nullptr;
    ID ownerID = static_cast<ID>(-1);
    // Which of the owner's UITexts this is (an entity can have several).
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    [[nodiscard]] ComponentUIText* GetComponent() const {
        if (level == nullptr) return nullptr;
        return level->ui_texts.GetInstance(instanceID);
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

// Forward declaration: ScriptBehaviourRef::GetEntity() returns a
// ScriptEntity by value and ScriptEntity::GetScript() returns a
// ScriptBehaviourRef by value, so the two are defined with only forward
// declarations of each other's methods here; the actual bodies of the
// cross-referencing methods are inline definitions placed after both structs
// are complete (see "Behaviour <-> Entity cross-reference definitions"
// below ScriptEntity's closing brace).
struct ScriptEntity;

// A safe handle to one specific script (Behaviour) instance, identified by
// its globally-unique ScriptInstanceID rather than by filename - so it stays
// unambiguous even when the target Entity has several scripts attached,
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

    [[nodiscard]] ScriptEntity GetEntity() const;

    // The referenced script's own enabled flag - independent of the
    // Entity's enabled flag (Entity::enabled).
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
    // default property dispatch, so "isValid"/"entity"/"enabled" are
    // handled here by name rather than also being registered as ordinary
    // usertype properties (which sol2 would then never see) - see the
    // "Behaviour" usertype registration in LuaSystem.cpp, which binds only
    // these two functions and nothing else.
    //
    // Declared only here (like GetEntity() above) and defined out-of-line
    // after ScriptEntity is a complete type - LuaGet constructs a
    // sol::object from a ScriptEntity returned by value, which needs
    // ScriptEntity's full definition, not just the forward declaration
    // visible at this point in the file.
    [[nodiscard]] sol::object LuaGet(const std::string& key, sol::this_state state) const;
    void LuaSet(const std::string& key, sol::object value) const;
};

// ---------------------------------------------------------
// Entity (Entity)
// ---------------------------------------------------------

// Tilky's Entity facade. Lua never sees the raw ECS entity or its
// component storages - every field here is either a plain value or another
// safe {Level*, ID} handle, and every accessor null-checks before touching
// the level. Registered to Lua as "Entity" (see LuaEntityBindings.cpp);
// the C++ type name stays ScriptEntity to minimize churn across the engine
// side of the codebase.
struct ScriptEntity {
    Level* level = nullptr;
    ID ownerID = INVALID_ENTITY_ID;

    // The instance ID of this entity's first component in `storage`, or
    // INVALID_COMPONENT_INSTANCE_ID (an invalid handle) when it has none.
    template<typename Storage>
    [[nodiscard]] ComponentInstanceID FirstInstance(Storage Level::* storage) const {
        if (level == nullptr) return INVALID_COMPONENT_INSTANCE_ID;
        const auto* component = (level->*storage).Get(ownerID);
        return component == nullptr ? INVALID_COMPONENT_INSTANCE_ID : component->instanceID;
    }

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

    // Entity-level active state. Disabling an Entity effectively
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

    // Queues this Entity for destruction. Safe to call from anywhere in
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

    // True if this Entity has an attached script whose asset id
    // (ComponentScript::fileName, an Assets-relative path without extension -
    // see LuaScriptSystem's script identity notes) ends in `scriptName`.
    // Matching on the final path segment means both "Health" and
    // "Player/Health" find a script stored at "Assets/Scripts/Player/Health.lua".
    // Use GetScriptById for an unambiguous lookup when several same-named
    // scripts might be attached.
    [[nodiscard]] bool HasScriptNamed(const std::string& scriptName) const {
        if (level == nullptr) return false;

        const std::string wanted = std::filesystem::path(scriptName).filename().string();

        for (const ComponentScript* script : level->scripts.GetAll(ownerID))
            if (std::filesystem::path(script->fileName).filename().string() == wanted) return true;

        return false;
    }

    // Tags are assigned only from the editor (Project Settings + the
    // Sector/Wall/Entity inspectors) - scripts can query them (HasTag,
    // tagCount, GetTag) but never add/remove/rename one, so gameplay code
    // can't drift an Entity's tags out of sync with what a level
    // designer set.
    [[nodiscard]] bool HasTag(const std::string& tag) const {
        const Entity* entity = GetEntity();
        if (entity == nullptr) return false;

        const auto tagId = TagRegistry::Find(tag);
        if (!tagId.has_value()) return false;

        return std::ranges::find(entity->tagIds, *tagId) != entity->tagIds.end();
    }

    [[nodiscard]] int GetTagCount() const {
        const Entity* entity = GetEntity();
        return entity == nullptr ? 0 : static_cast<int>(entity->tags.size());
    }

    [[nodiscard]] std::string GetTag(const int luaIndex) const {
        const Entity* entity = GetEntity();
        if (entity == nullptr) throw sol::error("Invalid Entity");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(entity->tags.size()))
            throw sol::error("Entity tag index out of range");

        return entity->tags[index];
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

    [[nodiscard]] bool HasModel() const {
        return level != nullptr && level->models.Has(ownerID);
    }

    [[nodiscard]] bool HasFlipbook() const {
        return level != nullptr && level->flipbooks.Has(ownerID);
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

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptSprite GetSprite() const {
        return {level, ownerID, FirstInstance(&Level::sprites)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptAudioSource GetAudioSource() const {
        return {level, ownerID, FirstInstance(&Level::audioSources)};
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

    // Every script attached to this Entity, as Behaviour references.
    [[nodiscard]] std::vector<ScriptBehaviourRef> GetScripts() const {
        std::vector<ScriptBehaviourRef> result;

        if (level == nullptr) return result;

        for (const ComponentScript* script : level->scripts.GetAll(ownerID))
            result.push_back({level, ownerID, script->instanceID});

        return result;
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptPlayerController GetPlayerController() const {
        return {level, ownerID, FirstInstance(&Level::playerControllers)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptCamera GetCamera() const {
        return {level, ownerID, FirstInstance(&Level::cameras)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptCollider GetCollider() const {
        return {level, ownerID, FirstInstance(&Level::colliders)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptRigidbody GetRigidbody() const {
        return {level, ownerID, FirstInstance(&Level::rigidbodies)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptModel GetModel() const {
        return {level, ownerID, FirstInstance(&Level::models)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptFlipbook GetFlipbook() const {
        return {level, ownerID, FirstInstance(&Level::flipbooks)};
    }

    [[nodiscard]] ScriptUITransform GetUITransform() const {
        return {level, ownerID};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptUISprite GetUISprite() const {
        return {level, ownerID, FirstInstance(&Level::ui_sprites)};
    }

    // The first one; GetComponents lists them all.
    [[nodiscard]] ScriptUIText GetUIText() const {
        return {level, ownerID, FirstInstance(&Level::ui_texts)};
    }
};

// ---------------------------------------------------------
// Behaviour <-> Entity cross-reference definitions
//
// ScriptBehaviourRef and ScriptEntity refer to each other by value, so this
// one method has to be defined out-of-line, here, after both types are
// complete.
// ---------------------------------------------------------

inline ScriptEntity ScriptBehaviourRef::GetEntity() const {
    return {level, ownerID};
}

inline sol::object ScriptBehaviourRef::LuaGet(const std::string& key, const sol::this_state state) const {
    const sol::state_view luaView(state);

    if (key == "isValid") return sol::make_object(luaView, IsValid());
    if (key == "entity") return sol::make_object(luaView, GetEntity());
    if (key == "enabled") return sol::make_object(luaView, GetEnabled());

    return LuaScriptRuntime::GetInstanceField(ownerID, instanceID, key, state);
}

inline void ScriptBehaviourRef::LuaSet(const std::string& key, sol::object value) const {
    if (key == "enabled") {
        SetEnabled(value.as<bool>());
        return;
    }

    // isValid/entity are read-only; a stray write to them is ignored
    // rather than silently poking a same-named field into the target
    // script's environment.
    if (key == "isValid" || key == "entity") return;

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
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->start;
    }

    [[nodiscard]] Vector2 GetEnd() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->end;
    }

    [[nodiscard]] Vector4 GetColor() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->color;
    }

    void SetColor(const Vector4& value) const {
        Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        wall->color = value;
    }

    [[nodiscard]] WallSurface& GetSurface(const WallSurfaceSlot slot) const {
        Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->Surface(slot);
    }

    [[nodiscard]] bool IsPortal() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->IsPortal();
    }

    [[nodiscard]] std::string GetTopTexture() const { return GetSurface(WallSurfaceSlot::Top).texture; }
    void SetTopTexture(const std::string& value) const { GetSurface(WallSurfaceSlot::Top).texture = value; }
    void ClearTopTexture() const { SetTopTexture(""); }

    [[nodiscard]] std::string GetBottomTexture() const { return GetSurface(WallSurfaceSlot::Bottom).texture; }
    void SetBottomTexture(const std::string& value) const { GetSurface(WallSurfaceSlot::Bottom).texture = value; }
    void ClearBottomTexture() const { SetBottomTexture(""); }

    [[nodiscard]] Vector2 GetTopTextureOffset() const { return GetSurface(WallSurfaceSlot::Top).textureOffset; }
    void SetTopTextureOffset(const Vector2& value) const { GetSurface(WallSurfaceSlot::Top).textureOffset = value; }

    [[nodiscard]] Vector2 GetBottomTextureOffset() const { return GetSurface(WallSurfaceSlot::Bottom).textureOffset; }
    void SetBottomTextureOffset(const Vector2& value) const { GetSurface(WallSurfaceSlot::Bottom).textureOffset = value; }

    [[nodiscard]] Vector2 GetTopTextureScale() const { return GetSurface(WallSurfaceSlot::Top).textureScale; }
    void SetTopTextureScale(const Vector2& value) const { GetSurface(WallSurfaceSlot::Top).textureScale = value; }

    [[nodiscard]] Vector2 GetBottomTextureScale() const { return GetSurface(WallSurfaceSlot::Bottom).textureScale; }
    void SetBottomTextureScale(const Vector2& value) const { GetSurface(WallSurfaceSlot::Bottom).textureScale = value; }

    [[nodiscard]] int GetTopAnchor() const { return static_cast<int>(GetSurface(WallSurfaceSlot::Top).anchor); }
    void SetTopAnchor(const int value) const { GetSurface(WallSurfaceSlot::Top).anchor = ToAnchor(value); }

    [[nodiscard]] int GetBottomAnchor() const { return static_cast<int>(GetSurface(WallSurfaceSlot::Bottom).anchor); }
    void SetBottomAnchor(const int value) const { GetSurface(WallSurfaceSlot::Bottom).anchor = ToAnchor(value); }

    static WallTextureAnchor ToAnchor(const int value) {
        const std::optional<WallTextureAnchor> anchor = WallTextureAnchorFromInt(value);
        if (!anchor) throw sol::error("Wall anchor expects a WallAnchor value, e.g. WallAnchor.TopEdge");
        return *anchor;
    }

    [[nodiscard]] bool HasTag(const std::string& tag) const
    {
        const Wall* wall = GetWall();

        if (wall == nullptr) throw sol::error("Invalid Wall");

        const auto tagId = TagRegistry::Find(tag);

        if (!tagId.has_value()) return false;

        return std::ranges::find(wall->tagIds, *tagId) != wall->tagIds.end();
    }

    // Tags are assigned only from the editor (Project Settings + the
    // Sector/Wall/Entity inspectors) - scripts can query them (HasTag,
    // tagCount, GetTag) but never add/remove/rename one, so gameplay code
    // can't drift a wall's tags out of sync with what a level designer set.
    [[nodiscard]] int GetTagCount() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return static_cast<int>(wall->tags.size());
    }

    [[nodiscard]] std::string GetTag(const int luaIndex) const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(wall->tags.size()))
            throw sol::error("Wall tag index out of range");

        return wall->tags[index];
    }

    [[nodiscard]] ID GetFrontSector() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->frontSector;
    }

    [[nodiscard]] ID GetBackSector() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->backSector;
    }

    [[nodiscard]] Vector2 GetDir() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->dir;
    }

    [[nodiscard]] Vector2 GetNormal() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
        return wall->normal;
    }

    [[nodiscard]] float GetLength() const {
        const Wall* wall = GetWall();
        if (wall == nullptr) throw sol::error("Invalid Wall");
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

    // Every setter below makes its write through here, so the change also
    // reaches the sector's children, same as an edit in the inspector
    // (Level::PropagateSectorChanges).
    template<typename Write>
    void WriteAndPropagate(Write&& write) const {
        Sector* sector = GetSector();
        if (sector == nullptr || GetSectorFloor() == nullptr) throw sol::error("Invalid SectorFloor");

        if (sector->children.empty()) {
            write();
            return;
        }

        const std::vector<SectorFloor> floorsBefore = sector->floors;
        write();
        level->PropagateSectorChanges(sectorID, floorsBefore, sector->light);
    }

    [[nodiscard]] float GetFloorHeight() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return floor->floor.height;
    }

    void SetFloorHeight(const float value) const {
        Sector* sector = GetSector();
        SectorFloor* floor = GetSectorFloor();

        if (sector == nullptr || floor == nullptr) throw sol::error("Invalid SectorFloor");
        if (value >= floor->ceiling.height) throw sol::error("Sector floor height must be below its ceiling");
        if (floorIndex > 0 &&
            value < sector->floors[floorIndex - 1].ceiling.height) {
            throw sol::error("Sector floor interval overlaps the previous interval");
        }

        WriteAndPropagate([&] { floor->floor.height = value; });
    }

    [[nodiscard]] float GetCeilingHeight() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return floor->ceiling.height;
    }

    void SetCeilingHeight(const float value) const {
        Sector* sector = GetSector();
        SectorFloor* floor = GetSectorFloor();

        if (sector == nullptr || floor == nullptr) throw sol::error("Invalid SectorFloor");
        if (value <= floor->floor.height) throw sol::error("Sector ceiling height must be above its floor");
        if (floorIndex + 1 < static_cast<int>(sector->floors.size()) &&
            value > sector->floors[floorIndex + 1].floor.height) {
            throw sol::error("Sector floor interval overlaps the next interval");
        }

        WriteAndPropagate([&] { floor->ceiling.height = value; });
    }

    [[nodiscard]] Vector4 GetFloorColor() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return floor->floor.color;
    }

    void SetFloorColor(const Vector4& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        WriteAndPropagate([&] { floor->floor.color = value; });
    }

    [[nodiscard]] Vector4 GetCeilingColor() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return floor->ceiling.color;
    }

    void SetCeilingColor(const Vector4& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        WriteAndPropagate([&] { floor->ceiling.color = value; });
    }

    [[nodiscard]] std::string GetFloorTexture() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return floor->floor.texture;
    }

    void SetFloorTexture(const std::string& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        WriteAndPropagate([&] { floor->floor.texture = value; });
    }

    void ClearFloorTexture() const {
        SetFloorTexture("");
    }

    [[nodiscard]] std::string GetCeilingTexture() const {
        const SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return floor->ceiling.texture;
    }

    void SetCeilingTexture(const std::string& value) const {
        SectorFloor* floor = GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        WriteAndPropagate([&] { floor->ceiling.texture = value; });
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

    // Sector::name itself - no separate Lua-side copy.
    [[nodiscard]] std::string GetName() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return sector->name;
    }

    void SetName(const std::string& value) const {
        Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        sector->name = value;
    }

    // Shortcut for GetFloor(1).floorHeight: the first floor interval's floor,
    // which is also the one the rest of the engine treats as "the" sector
    // floor (floors[0]). Goes through ScriptSectorFloor so it shares that
    // type's validation (must stay below its ceiling / not overlap the
    // previous interval) instead of duplicating it. Sectors with several
    // floors reach the others through GetFloor(n).
    [[nodiscard]] float GetFloorHeight() const {
        return GetFloor(1).GetFloorHeight();
    }

    void SetFloorHeight(const float value) const {
        GetFloor(1).SetFloorHeight(value);
    }

    [[nodiscard]] Vector3 GetLight() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return sector->light;
    }

    void SetLight(const Vector3 &value) const {
        Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

        const Vector3 lightBefore = sector->light;
        sector->light = value;

        // Setting the light directly overrides a FadeLight in progress.
        sector->lightFade.active = false;

        // Passes the change on to child sectors, same as the inspector.
        level->PropagateSectorChanges(sectorID, sector->floors, lightBefore);
    }

    [[nodiscard]] int GetFloorCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return static_cast<int>(sector->floors.size());
    }

    [[nodiscard]] ScriptSectorFloor GetFloor(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

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

    // Floor/ceiling movement. The moving itself is SectorFloor's
    // (SurfaceMove / UpdateMovement, ticked by Level::UpdateSectorMovement),
    // these only validate the Lua arguments and start or query it.
    [[nodiscard]] SectorFloor& GetMovableFloor(const int luaIndex) const {
        SectorFloor* floor = GetFloor(luaIndex).GetSectorFloor();
        if (floor == nullptr) throw sol::error("Invalid SectorFloor");
        return *floor;
    }

    static void CheckMoveArguments(const float speedOrSeconds, const float gap, const bool isDuration) {
        if (isDuration ? speedOrSeconds < 0.0f : speedOrSeconds <= 0.0f)
            throw sol::error(isDuration ? "Move time must not be negative" : "Move speed must be above 0");
        if (gap < 0.0f) throw sol::error("Move gap must not be negative");
    }

    void MoveFloorToCeiling(const int luaIndex, const float speed, const float gap) const {
        CheckMoveArguments(speed, gap, false);
        GetMovableFloor(luaIndex).MoveFloorToCeiling(speed, gap);
    }

    void MoveCeilingToFloor(const int luaIndex, const float speed, const float gap) const {
        CheckMoveArguments(speed, gap, false);
        GetMovableFloor(luaIndex).MoveCeilingToFloor(speed, gap);
    }

    void MoveFloorToCeilingOverTime(const int luaIndex, const float seconds, const float gap) const {
        CheckMoveArguments(seconds, gap, true);
        GetMovableFloor(luaIndex).MoveFloorToCeilingOverTime(seconds, gap);
    }

    void MoveCeilingToFloorOverTime(const int luaIndex, const float seconds, const float gap) const {
        CheckMoveArguments(seconds, gap, true);
        GetMovableFloor(luaIndex).MoveCeilingToFloorOverTime(seconds, gap);
    }

    [[nodiscard]] bool IsMoving(const int luaIndex) const {
        return GetMovableFloor(luaIndex).IsMoving();
    }

    void StopMoving(const int luaIndex) const {
        GetMovableFloor(luaIndex).StopMoving();
    }

    [[nodiscard]] int GetVertexCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return static_cast<int>(sector->vertices.size());
    }

    [[nodiscard]] Vector2 GetVertex(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->vertices.size())) {
            throw sol::error("Sector vertex index out of range");
        }

        return sector->vertices[index];
    }

    [[nodiscard]] int GetWallCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return static_cast<int>(sector->walls.size());
    }

    [[nodiscard]] ScriptWall GetWall(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

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
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return static_cast<int>(sector->entitiesInside.size());
    }

    [[nodiscard]] ScriptEntity GetEntity(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

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
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return static_cast<int>(sector->neighbors.size());
    }

    [[nodiscard]] ScriptSector GetNeighbor(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

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

    // Tags are assigned only from the editor (Project Settings + the
    // Sector/Wall/Entity inspectors) - scripts can query them (HasTag,
    // tagCount, GetTag) but never add/remove/rename one, so gameplay code
    // can't drift a sector's tags out of sync with what a level designer set.
    [[nodiscard]] bool HasTag(const std::string& tag) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

        const auto tagId = TagRegistry::Find(tag);
        if (!tagId.has_value()) return false;

        return std::ranges::find(sector->tagIds, *tagId) != sector->tagIds.end();
    }

    [[nodiscard]] int GetTagCount() const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return static_cast<int>(sector->tags.size());
    }

    [[nodiscard]] std::string GetTag(const int luaIndex) const {
        const Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");

        const int index = luaIndex - 1;

        if (index < 0 || index >= static_cast<int>(sector->tags.size()))
            throw sol::error("Sector tag index out of range");

        return sector->tags[index];
    }

    // ---------------------------------------------------------------------
    // Everything below backs the Sector functions listed in docs/sector.md.
    // ---------------------------------------------------------------------

    [[nodiscard]] Sector& RequireSector() const {
        Sector* sector = GetSector();
        if (sector == nullptr) throw sol::error("Invalid Sector");
        return *sector;
    }

    // ---- Occupancy ------------------------------------------------------
    // Read from Sector::entitiesInside, which
    // ComponentTransform::UpdateObjectSectorAndFloor keeps current as
    // entities move. An entity is inside exactly one sector - the innermost
    // one containing its (x, z) position, height ignored - so an entity
    // standing in a child sector (a pillar, a platform) is not inside the
    // parent. Entities without a Transform are never inside any sector.

    [[nodiscard]] bool ContainsEntity(const ScriptEntity& entity) const {
        const Sector& sector = RequireSector();
        return std::ranges::find(sector.entitiesInside, entity.ownerID) != sector.entitiesInside.end();
    }

    [[nodiscard]] bool ContainsEntityWithTag(const std::string& tag) const {
        const Sector& sector = RequireSector();

        return std::ranges::any_of(sector.entitiesInside, [&](const ID id) {
            return ScriptEntity{level, id}.HasTag(tag);
        });
    }

    [[nodiscard]] std::vector<ScriptEntity> GetEntities() const {
        const Sector& sector = RequireSector();

        std::vector<ScriptEntity> result;
        result.reserve(sector.entitiesInside.size());
        for (const ID id : sector.entitiesInside) result.push_back({level, id});
        return result;
    }

    [[nodiscard]] std::vector<ScriptEntity> GetEntitiesWithTag(const std::string& tag) const {
        const Sector& sector = RequireSector();

        std::vector<ScriptEntity> result;
        for (const ID id : sector.entitiesInside) {
            const ScriptEntity entity{level, id};
            if (entity.HasTag(tag)) result.push_back(entity);
        }
        return result;
    }

    [[nodiscard]] int CountEntities() const {
        return GetEntityCount();
    }

    [[nodiscard]] int CountEntitiesWithTag(const std::string& tag) const {
        return static_cast<int>(GetEntitiesWithTag(tag).size());
    }

    [[nodiscard]] bool IsEmpty() const {
        return RequireSector().entitiesInside.empty();
    }

    // ---- Moving to a set height -----------------------------------------

    void MoveFloorTo(const int luaIndex, const float height, const float speed) const {
        CheckMoveArguments(speed, 0.0f, false);
        GetMovableFloor(luaIndex).MoveFloorTo(height, speed);
    }

    void MoveCeilingTo(const int luaIndex, const float height, const float speed) const {
        CheckMoveArguments(speed, 0.0f, false);
        GetMovableFloor(luaIndex).MoveCeilingTo(height, speed);
    }

    void MoveFloorToOverTime(const int luaIndex, const float height, const float seconds) const {
        CheckMoveArguments(seconds, 0.0f, true);
        GetMovableFloor(luaIndex).MoveFloorToOverTime(height, seconds);
    }

    void MoveCeilingToOverTime(const int luaIndex, const float height, const float seconds) const {
        CheckMoveArguments(seconds, 0.0f, true);
        GetMovableFloor(luaIndex).MoveCeilingToOverTime(height, seconds);
    }

    [[nodiscard]] bool IsFloorMoving(const int luaIndex) const {
        return GetMovableFloor(luaIndex).IsFloorMoving();
    }

    [[nodiscard]] bool IsCeilingMoving(const int luaIndex) const {
        return GetMovableFloor(luaIndex).IsCeilingMoving();
    }

    // ---- Light fading -----------------------------------------------------

    void FadeLight(const Vector3& color, const float seconds) const {
        if (seconds < 0.0f) throw sol::error("Fade time must not be negative");

        Sector& sector = RequireSector();
        sector.lightFade = {true, sector.light, color, 0.0f, seconds};
    }

    [[nodiscard]] bool IsLightFading() const {
        return RequireSector().lightFade.active;
    }

    // ---- Shape ------------------------------------------------------------
    // All in map space: Vector2 x/y are world x/z. Measured from the
    // triangles, so holes (child sectors) are not part of the area.

    [[nodiscard]] float GetArea() const {
        float area = 0.0f;
        for (const Triangle& triangle : RequireSector().triangles) area += TriangleArea(triangle);
        return area;
    }

    // Area-weighted centroid. Can fall outside the sector for L-shapes and
    // rings, like any centroid.
    [[nodiscard]] Vector2 GetCenter() const {
        const Sector& sector = RequireSector();

        float totalArea = 0.0f;
        float x = 0.0f;
        float y = 0.0f;

        for (const Triangle& triangle : sector.triangles) {
            const float area = TriangleArea(triangle);
            totalArea += area;
            x += area * (triangle.a.x + triangle.b.x + triangle.c.x) / 3.0f;
            y += area * (triangle.a.y + triangle.b.y + triangle.c.y) / 3.0f;
        }

        if (totalArea <= 0.0f) {
            // Degenerate sector: fall back to the average vertex.
            if (sector.vertices.empty()) return {0.0f, 0.0f};
            for (const Vector2& vertex : sector.vertices) { x += vertex.x; y += vertex.y; }
            const auto count = static_cast<float>(sector.vertices.size());
            return {x / count, y / count};
        }

        return {x / totalArea, y / totalArea};
    }

    [[nodiscard]] std::tuple<Vector2, Vector2> GetBounds() const {
        Vector2 minimum, maximum;
        ComputeBounds(RequireSector(), minimum, maximum);
        return {minimum, maximum};
    }

    // Uniformly distributed point inside the sector. The three randoms are
    // in [0, 1]; the Lua binding feeds them from the mathT random generator
    // so RandomSeed covers this too.
    [[nodiscard]] Vector2 PointInside(const float pick, float u, float v) const {
        const Sector& sector = RequireSector();
        if (sector.triangles.empty()) throw sol::error("Sector has no area");

        const float totalArea = GetArea();
        float remaining = pick * totalArea;
        const Triangle* chosen = &sector.triangles.back();

        for (const Triangle& triangle : sector.triangles) {
            remaining -= TriangleArea(triangle);
            if (remaining <= 0.0f) { chosen = &triangle; break; }
        }

        if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }

        return {
            chosen->a.x + (chosen->b.x - chosen->a.x) * u + (chosen->c.x - chosen->a.x) * v,
            chosen->a.y + (chosen->b.y - chosen->a.y) * u + (chosen->c.y - chosen->a.y) * v
        };
    }

    // ---- Distance to the outline -------------------------------------------
    // Map space (x, z), height ignored. Measured to the outer boundary and to
    // every hole (child sector), and 0 while the entity is inside. Standing
    // in a child sector counts as outside, same as ContainsEntity.

    [[nodiscard]] float DistanceToSector(const ScriptEntity& entity) const {
        return std::sqrt(DistanceToSectorSquared(entity));
    }

    [[nodiscard]] float DistanceToSectorSquared(const ScriptEntity& entity) const {
        const ComponentTransform* transform = entity.GetTransform().GetComponent();
        if (transform == nullptr) throw sol::error("Entity has no Transform");

        const Sector& sector = RequireSector();
        const Vector2 point{transform->position.x, transform->position.z};
        if (Geometry::IsPointInPolygon(sector.vertices, sector.innerLoops, point)) return 0.0f;

        float best = LoopDistanceSquared(sector.vertices, point);
        for (const std::vector<Vector2>& hole : sector.innerLoops)
            best = std::min(best, LoopDistanceSquared(hole, point));
        return best;
    }

    // ---- Heights at a point (slopes included) ------------------------------

    [[nodiscard]] float GetFloorHeightAt(const Vector2& point, const int luaIndex) const {
        Vector2 minimum, maximum;
        ComputeBounds(RequireSector(), minimum, maximum);
        return SurfaceHeightAt(GetMovableFloor(luaIndex).floor, minimum, maximum, point);
    }

    [[nodiscard]] float GetCeilingHeightAt(const Vector2& point, const int luaIndex) const {
        Vector2 minimum, maximum;
        ComputeBounds(RequireSector(), minimum, maximum);
        return SurfaceHeightAt(GetMovableFloor(luaIndex).ceiling, minimum, maximum, point);
    }

private:
    static float TriangleArea(const Triangle& t) {
        return std::abs((t.b.x - t.a.x) * (t.c.y - t.a.y) - (t.c.x - t.a.x) * (t.b.y - t.a.y)) * 0.5f;
    }

    // Squared distance from `point` to the nearest edge of a closed loop.
    // Infinity for an empty loop.
    static float LoopDistanceSquared(const std::vector<Vector2>& loop, const Vector2& point) {
        float best = std::numeric_limits<float>::infinity();
        if (loop.empty()) return best;

        Vector2 prev = loop.back();
        for (const Vector2& cur : loop) {
            const float ex = cur.x - prev.x;
            const float ey = cur.y - prev.y;
            const float lengthSquared = ex * ex + ey * ey;

            float t = 0.0f;
            if (lengthSquared > 0.0f)
                t = std::clamp(((point.x - prev.x) * ex + (point.y - prev.y) * ey) / lengthSquared, 0.0f, 1.0f);

            const float dx = point.x - (prev.x + ex * t);
            const float dy = point.y - (prev.y + ey * t);
            best = std::min(best, dx * dx + dy * dy);

            prev = cur;
        }

        return best;
    }

    // Slopes are measured from an edge of this rectangle. Must match
    // ComputeSectorBounds() in PhysicsSystem.cpp and GetSectorBounds() in
    // Rendering_vs.glsl (triangles first, vertices as the fallback), or the
    // height reported here won't be the height on screen.
    static void ComputeBounds(const Sector& sector, Vector2& minimum, Vector2& maximum) {
        minimum = {0.0f, 0.0f};
        maximum = {0.0f, 0.0f};

        bool first = true;
        auto include = [&](const Vector2& p) {
            if (first) { minimum = {p.x, p.y}; maximum = {p.x, p.y}; first = false; return; }
            minimum = {std::min(minimum.x, p.x), std::min(minimum.y, p.y)};
            maximum = {std::max(maximum.x, p.x), std::max(maximum.y, p.y)};
        };

        if (!sector.triangles.empty()) {
            for (const Triangle& t : sector.triangles) { include(t.a); include(t.b); include(t.c); }
        }
        else for (const Vector2& vertex : sector.vertices) include(vertex);
    }

    // Mirrors GetSurfaceHeight()/GetSlopeOffset() in PhysicsSystem.cpp.
    static float SurfaceHeightAt(const SectorSurface& surface, const Vector2& minimum,
                                 const Vector2& maximum, const Vector2& point) {
        if (surface.slopeStrength == 0.0f) return surface.height;

        const float gradient = surface.slopeStrength * Constants::DegToRad;

        switch (surface.slopeDirection) {
            case PLUS_X:  return surface.height + (point.x - minimum.x) * gradient;
            case MINUS_X: return surface.height + (maximum.x - point.x) * gradient;
            case PLUS_Z:  return surface.height + (point.y - minimum.y) * gradient;
            case MINUS_Z: return surface.height + (maximum.y - point.y) * gradient;
        }

        return surface.height;
    }
};

#endif // TILKY_ENGINE_WRAPPERS_HPP