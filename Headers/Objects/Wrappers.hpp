//
// Created by berke on 5/15/2026.
//

#ifndef TILKY_ENGINE_WRAPPERS_HPP
#define TILKY_ENGINE_WRAPPERS_HPP

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Math/Vector/Vector2.hpp"

struct ScriptTransform {
    ComponentTransform* transform = nullptr;

    [[nodiscard]] Vector2 GetPosition() const {
        if (transform == nullptr) return {0.0f, 0.0f};
        return transform->position;
    }

     void SetPosition(const Vector2& position) const {
        if (transform == nullptr) return;
        transform->position = position;
    }

    [[nodiscard]] float GetX() const {
        if (transform == nullptr) return 0.0f;
        return transform->position.x;
    }

    [[nodiscard]] float GetY() const {
        if (transform == nullptr) return 0.0f;
        return transform->position.y;
    }

    void SetX(const float x) const {
        if (transform == nullptr) return;
        transform->position.x = x;
    }

    void SetY(const float y) const {
        if (transform == nullptr) return;
        transform->position.y = y;
    }
};

struct ScriptEntity {
    Level* level = nullptr;
    EntityID ownerID = static_cast<EntityID>(-1);

    [[nodiscard]] EntityID GetID() const {
        return ownerID;
    }

    [[nodiscard]] ScriptTransform GetTransform() const {
        if (level == nullptr) {
            return {};
        }

        return {
            level->transforms.Get(ownerID)
        };
    }

    [[nodiscard]] bool HasTransform() const {
        return level != nullptr && level->transforms.Get(ownerID) != nullptr;
    }
};

#endif //TILKY_ENGINE_WRAPPERS_HPP