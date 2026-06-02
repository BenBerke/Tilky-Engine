//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_COMPONENTS_HPP
#define TILKY_ENGINE_COMPONENTS_HPP
#include <algorithm>
#include <vector>
#include <unordered_map>

#include "../Math/Vector/Vector2.hpp"
#include "../Objects/Sector.hpp"
#include "EntityTypes.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"


enum ComponentType {
    CMP_TRANSFORM,
    CMP_SPRITE,
    CMP_DECAL,
    CMP_AUDIO_SOURCE,
    CMP_SCRIPT,
    CMP_PLAYER_CONTROLLER,
    CMP_CAMERA,
    CMP_COLLIDER,
    CMP_RIGIDBODY,

    CMP_NORMAL_COUNT,

    CMP_UI_TRANSFORM, // Transform must always be the first UI component otherwise UIEditorDrawUI breaks
    CMP_UI_SPRITE,
    CMP_UI_TEXT,

    CMP_COUNT,
};


// region UI Components

struct ComponentUIText {
    EntityID ownerID = static_cast<EntityID>(-1);

    std::string text;
};

struct ComponentUISprite {
    EntityID ownerID = static_cast<EntityID>(-1);

    int textureIndex{};
};

struct ComponentUITransform {
    EntityID ownerID = static_cast<EntityID>(-1);

    Vector2 anchorMin = {0.5f, 0.5f};
    Vector2 anchorMax = {0.5f, 0.5f};

    Vector2 pivot = {0.5f, 0.5f};

    Vector2 position = {0.0f, 0.0f};

    Vector2 scale = {1.0f, 1.0f};
    float rotation = 0.0f;

    Vector2 resolvedPosition = {};
    Vector2 resolvedSize = {};
};

// endregion

struct ComponentRigidbody {
    EntityID ownerID = static_cast<EntityID>(-1);

    bool isStatic = false;
    float mass = 1.0f;

    Vector3 velocity = {.0f, .0f, .0f};

    void AddVelocity(Vector3 velocity);
    void ApplyFriction(float friction);
    void ApplyAirResistance(float resistance);
    void ApplyGravity(float gravity);
};

enum ColliderType {
    COLLIDERTYPE_SPHERE,
    COLLIDERTYPE_BOX
};

struct ComponentCollider {
    // Sphere Collider
    EntityID ownerID = static_cast<EntityID>(-1);

    ColliderType type = COLLIDERTYPE_SPHERE;

    bool isActive = true;
    bool isTrigger = false;

    // Type = Box -> Use these values as AABB; Type = Sphere -> Use size.x as the radius
    Vector3 scale = {1.0f, 1.0f, 1.0f};

    float stepSize = 0.0f;
};
struct ComponentPlayerController {
    EntityID ownerID = static_cast<EntityID>(-1);

    // Physical player/camera-body position.
    // x = world/map X
    // y = world eye height
    // z = world/map Y

    bool isActive = false;

    // Movement velocity.
    Vector3 velocity = {0.0f, 0.0f, 0.0f};

    float speed = 46.0f;
    float runningSpeed = 90.0f;

    // Eye height above the current floor.
    float eyeHeight = 12.0f;

    float stepSize = 8.0f;

    float currentSpeed = 0.0f;
    float currentEyeHeight = 0.0f;

    float friction = 0.8f;
    float sensitivityX = .5f, sensitivityY = .5f;

    bool noClip = false;
};

struct ComponentCamera {
    EntityID ownerID = static_cast<EntityID>(-1);

    bool isActive = true;

    // yaw = left/right rotation.
    float yaw = 0.0f;

    // pitch = up/down rotation.
    float pitch = 0.0f;

    Vector3 forward = {0.0f, 0.0f, 1.0f};
    Vector3 target = {0.0f, 0.0f, 1.0f};

    Matrix4 view = Matrix4::Identity();
    Matrix4 projection = Matrix4::Identity();

    float fov = 90.0f;
    float aspectRatio = 1680.0f / 960.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
};

struct ComponentScript {
    EntityID ownerID = static_cast<EntityID>(-1);

    std::string fileName = "";

    bool enabled = true;
};

struct ComponentAudioSource {
    EntityID ownerID = static_cast<EntityID>(-1);

    std::string name; // OpenAL source name, e.g. "entity_4_audio"
    int soundIndex = -1;

    float pitch = 1.0f;
    float gain = 1.0f;
    bool looping = false;
    bool playOnStart = false;

    // Distance Attenuation (How volume drops over distance)
    float referenceDistance = 1.0f;   // Distance where gain is at its max
    float maxDistance = 10000.0f;      // Distance where attenuation stops
    float rollOffFactor = 1.0f;       // How fast the sound fades (1.0 is "real")

    // Sound Cone (Directional audio behavior)
    float innerConeAngle = 360.0f;    // Inside this yaw, sound is full volume
    float outerConeAngle = 360.0f;    // Outside this, volume is 'outerGain'
    float outerGain = 0.0f;           // Volume multiplier outside the cone

    void PlaySound() const;

    void SetSourcePitch(float pitch) const;

    void SetSourceGain(float gain) const;

    void SetSourceLooping(bool looping) const;

    void SetSourcePosition(const Vector3& position) const;

};

// Stores things related to the entity's whereabouts
// Every entity MUST have a transform component
struct Sector;
struct ComponentTransform {
    EntityID ownerID = -1;
    //                 World X, World Z, Height relative to the sector's floor
    Vector3 position = {.0f, .0f, .0f};
    Vector2 forward = {1.0f, .0f};
    int floor = 0;
    Vector2 scale = {32.0f, 32.0f};

    int sectorIndex = -1;
    bool isDirty;

    void AddPosition(const Vector3& position);
    void SetPosition(const Vector3& position);
    float GetObjectBottomHeight(const std::vector<Sector>& sectors);
    bool UpdateObjectSectorAndFloor(std::vector<Sector>& sectors, Entity* owner);
};

// Stores things related to the entity's visuals
struct ComponentSprite {
    EntityID ownerID = -1;

    int textureIndex;
};

// MUST have a sprite component to work properly
struct ComponentDecal {
    EntityID ownerID = -1;

    int wallIndex = -1;

    float verticalPos = 0; // Vertical Position
    float horizontalPos = -1.0f; // Horizontal position
    float wallNormalOffset = 0.0f;  // Distance away from the wall

    float wallT = 0.5f; // Horizontal percentage among the wall

    float baseHeight = 0.0f; // fixed world height of the wall floor when decal was placed
    bool absHeight = false; // Move with the wall or not.
};

template<typename T>
struct ComponentStorage {
    std::vector<T> components;
    std::unordered_map<EntityID, size_t> entityToIndex;

    T* Get(const EntityID id) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return nullptr;
        return &components[it->second];
    }

    const T* Get(EntityID id) const {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return nullptr;
        return &components[it->second];
    }

    T& Add(const EntityID id) {
        if (T* existing = Get(id)) return *existing;
        T component {};
        component.ownerID = id;

        const size_t index = components.size();
        components.push_back(component);
        entityToIndex[id] = index;

        return components.back();
    }

    bool Remove(const EntityID id) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return false;

        const size_t removeIndex = it->second;
        const size_t lastIndex = components.size() - 1;

        if (removeIndex != lastIndex) {
            components[removeIndex] = components[lastIndex];

            const EntityID movedOwnerID = components[removeIndex].ownerID;
            entityToIndex[movedOwnerID] = removeIndex;
        }

        components.pop_back();
        entityToIndex.erase(id);

        return true;
    }

    bool Has(const EntityID id) const {
        return entityToIndex.contains(id);
    }

    void Clear() {
        components.clear();
        entityToIndex.clear();
    }

    T& InsertLoaded(const T& component) {
        const size_t index = components.size();

        components.push_back(component);
        entityToIndex[component.ownerID] = index;

        return components.back();
    }
};

#endif //TILKY_ENGINE_COMPONENTS_HPP