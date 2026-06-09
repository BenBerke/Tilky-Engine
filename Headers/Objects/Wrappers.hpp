//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_WRAPPERS_HPP
#define TILKY_ENGINE_WRAPPERS_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Engine/InputManager.hpp"

struct ScriptTransform {
    Level* level = nullptr;
    EntityID ownerID = static_cast<EntityID>(-1);

    [[nodiscard]] ComponentTransform* GetComponent() const {
        if (level == nullptr) {
            return nullptr;
        }

        return level->transforms.Get(ownerID);
    }

    [[nodiscard]] Vector3 GetPosition() const {
        const ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return {0.0f, 0.0f};
        }

        return transform->position;
    }

    void SetPosition(const Vector3& position) const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return;
        }

        transform->position = position;
    }

    [[nodiscard]] Vector2 GetScale() const {
        const ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return {0.0f, 0.0f};
        }

        return transform->scale;
    }

    void SetScale(const Vector2& scale) const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return;
        }

        transform->scale = scale;
    }

    [[nodiscard]] float GetFloor() const {
        const ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return 0;
        }

        return transform->floor;
    }

    void SetFloor(const float floor) const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return;
        }

        transform->floor = floor;
    }

};

struct ScriptSprite {
    Level* level = nullptr;
    EntityID ownerID = -1;

    int textureIndex{};

    [[nodiscard]] int GetTextureIndex() const { return textureIndex; }
    void SetTextureIndex(const int index) { textureIndex = index; }
};

struct ScriptDecal {
    Level* level = nullptr;
    EntityID ownerID = static_cast<EntityID>(-1);

    int wallIndex = -1;

    float verticalPos = 0; // Vertical Position
    float horizontalPos = -1.0f; // Horizontal position
    float wallNormalOffset = 0.0f;  // Distance away from the wall

    float wallT = 0.5f; // Horizontal percentage among the wall

    float baseHeight = 0.0f; // fixed world height of the wall floor when decal was placed
    bool absHeight = false; // Move with the wall or not.

    // --- Getters ---
    [[nodiscard]] Level* GetLevel() const { return level; }
    [[nodiscard]] EntityID GetOwnerID() const { return ownerID; }

    [[nodiscard]] int GetWallIndex() const { return wallIndex; }

    [[nodiscard]] float GetVerticalPos() const { return verticalPos; }
    [[nodiscard]] float GetHorizontalPos() const { return horizontalPos; }
    [[nodiscard]] float GetWallNormalOffset() const { return wallNormalOffset; }

    [[nodiscard]] float GetWallT() const { return wallT; }

    [[nodiscard]] float GetBaseHeight() const { return baseHeight; }
    [[nodiscard]] bool GetAbsHeight() const { return absHeight; }

    // --- Setters ---
    void SetLevel(Level* lvl) { level = lvl; }
    void SetOwnerID(const EntityID id) { ownerID = id; }

    void SetWallIndex(const int index) { wallIndex = index; }

    void SetVerticalPos(const float pos) { verticalPos = pos; }
    void SetHorizontalPos(const float pos) { horizontalPos = pos; }
    void SetWallNormalOffset(const float offset) { wallNormalOffset = offset; }

    void SetWallT(const float t) { wallT = t; }

    void SetBaseHeight(const float height) { baseHeight = height; }
    void SetAbsHeight(const bool abs) { absHeight = abs; }
};

struct ScriptEntity {
    Level* level = nullptr;
    EntityID ownerID = static_cast<EntityID>(-1);

    [[nodiscard]] EntityID GetID() const {
        return ownerID;
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

    [[nodiscard]] ScriptTransform GetTransform() const {
        return {level,ownerID};
    }

    [[nodiscard]] ScriptSprite GetSprite() const {
        return {level,ownerID};
    }

    [[nodiscard]] ScriptDecal GetDecal() const {
        return {level,ownerID};
    }
};

#endif //TILKY_ENGINE_WRAPPERS_HPP