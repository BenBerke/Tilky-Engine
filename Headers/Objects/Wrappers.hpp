//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_WRAPPERS_HPP
#define TILKY_ENGINE_WRAPPERS_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Vector/Vector2.hpp"

struct ScriptTransform {
    Level* level = nullptr;
    EntityID ownerID = static_cast<EntityID>(-1);

    [[nodiscard]] ComponentTransform* GetComponent() const {
        if (level == nullptr) {
            return nullptr;
        }

        return level->transforms.Get(ownerID);
    }

    [[nodiscard]] Vector2 GetPosition() const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return {0.0f, 0.0f};
        }

        return transform->position;
    }

    void SetPosition(const Vector2& position) const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return;
        }

        transform->position = position;
    }

    [[nodiscard]] float GetX() const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return 0.0f;
        }

        return transform->position.x;
    }

    void SetX(const float x) const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return;
        }

        transform->position.x = x;
    }

    [[nodiscard]] float GetY() const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return 0.0f;
        }

        return transform->position.y;
    }

    void SetY(const float y) const {
        ComponentTransform* transform = GetComponent();

        if (transform == nullptr) {
            return;
        }

        transform->position.y = y;
    }
};

struct ScriptEntity {
    Level* level = nullptr;
    EntityID ownerID = static_cast<EntityID>(-1);

    [[nodiscard]] EntityID GetID() const {
        return ownerID;
    }

    [[nodiscard]] bool HasTransform() const {
        return level != nullptr && level->transforms.Get(ownerID) != nullptr;
    }

    [[nodiscard]] ScriptTransform GetTransform() const {
        return {level,ownerID};
    }
};

#endif //TILKY_ENGINE_WRAPPERS_HPP