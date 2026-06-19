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
    ID ownerID = static_cast<ID>(-1);

    std::string text;
};

struct ComponentUISprite {
    ID ownerID = static_cast<ID>(-1);

    int textureIndex{};
};

struct ComponentUITransform {
    ID ownerID = static_cast<ID>(-1);

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
    ID ownerID = static_cast<ID>(-1);

    bool isStatic = false;
    float mass = 1.0f;
    float gravityScale = 9.8f;
    float friction  = 1.0f;

    Vector3 velocity = {.0f, .0f, .0f};

    void AddVelocity(const Vector3 &_velocity);
    void ApplyFriction(float _friction);
    void ApplyAirResistance(float resistance);
    void ApplyGravity(float gravity);
};


enum ColliderType {
    COLLIDERTYPE_SPHERE,
    COLLIDERTYPE_BOX
};
struct ComponentCollider {
    // Sphere Collider
    ID ownerID = static_cast<ID>(-1);

    ColliderType type = COLLIDERTYPE_SPHERE;

    bool isActive = true;
    bool isTrigger = false;

    // Type = Box -> Use these values as AABB; Type = Sphere -> Use size.x as the radius
    Vector3 scale = {1.0f, 1.0f, 1.0f};

    float stepSize = 0.0f;
};

struct ComponentPlayerController {
    ID ownerID = static_cast<ID>(-1);

    // Physical player/camera-body position.
    // x = world/map X
    // y = world eye height
    // z = world/map Y

    bool isActive = false;

    float speed = 46.0f;
    float runningSpeed = 90.0f;

    float jumpPower = 100.0f;

    // Eye height above the current floor.
    float eyeHeight = 12.0f;

    float friction = 0.8f;
    float sensitivityX = .5f, sensitivityY = .5f;

    bool noClip = false;

    // Read only, do not change
    Vector3 velocity = {0.0f, 0.0f, 0.0f};

    float currentSpeed = 0.0f;
    float currentEyeHeight = 0.0f;
};

struct ComponentCamera {
    ID ownerID = static_cast<ID>(-1);

    bool isActive = true;

    // yaw = left/right rotation.
    float yaw = 0.0f;

    // pitch = up/down rotation.
    float pitch = 0.0f;

    float fov = 90.0f;
    float aspectRatio = 1680.0f / 960.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;

    // Read-only, do not change
    Vector3 forward = {0.0f, 0.0f, 1.0f};
    Vector3 target = {0.0f, 0.0f, 1.0f};

    Matrix4 view = Matrix4::Identity();
    Matrix4 projection = Matrix4::Identity();

};

// Custom Lua script written by the user
struct ComponentScript {
    ID ownerID = static_cast<ID>(-1);

    std::string fileName = "";

    bool enabled = true;
};

// OpenAL Audio source. What sound it will play can change during gameplay.
struct ComponentAudioSource {
    ID ownerID = static_cast<ID>(-1);

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

    void SetSourcePitch(float _pitch) const;

    void SetSourceGain(float _gain) const;

    void SetSourceLooping(bool _looping) const;

    void SetSourcePosition(const Vector3& position) const;

};

// Stores things related to the entity's whereabouts
// Every entity MUST have a transform component
struct Sector;
struct ComponentTransform {
    ID ownerID = -1;
    /*
     * Engine coordinate convention:
     *
     * position.x = world X
     * position.y = world Z / horizontal depth
     * position.z = absolute world height
     *
     * relativeHeight = height relative to the current sector floor
     *
     * Transform origin is the object's feet
     */
    Vector3 position = {.0f, .0f, .0f};
    float relativeHeight = .0f;
    Vector2 forward = {1.0f, .0f};
    int floor = 0;

    // Scale convention same as position. x, y = World x, z; z = World y. This does not affect Sprites but only Colliders
    Vector3 scale = {32.0f, 32.0f, 32.0f};

    int sectorIndex = -1;
    bool isDirty = false;

    void AddPosition(const Vector3& position);
    void SetPosition(const Vector3& position);
    float GetObjectBottomHeight(const std::vector<Sector>& sectors);
    bool UpdateObjectSectorAndFloor(std::vector<Sector>& sectors);
};

// Stores things related to the entity's visuals
struct ComponentSprite {
    ID ownerID = -1;

    int textureIndex;
};

// MUST have a sprite component to work properly
struct ComponentDecal {
    ID ownerID = -1;

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
    std::unordered_map<ID, size_t> entityToIndex;

    T* Get(const ID id) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return nullptr;
        return &components[it->second];
    }

    const T* Get(ID id) const {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return nullptr;
        return &components[it->second];
    }

    T& Add(const ID id) {
        if (T* existing = Get(id)) return *existing;
        T component {};
        component.ownerID = id;

        const size_t index = components.size();
        components.push_back(component);
        entityToIndex[id] = index;

        return components.back();
    }

    bool Remove(const ID id) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return false;

        const size_t removeIndex = it->second;
        const size_t lastIndex = components.size() - 1;

        if (removeIndex != lastIndex) {
            components[removeIndex] = components[lastIndex];

            const ID movedOwnerID = components[removeIndex].ownerID;
            entityToIndex[movedOwnerID] = removeIndex;
        }

        components.pop_back();
        entityToIndex.erase(id);

        return true;
    }

    bool Has(const ID id) const {
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

struct ColliderStorage : ComponentStorage<ComponentCollider> {
    // components layout: [active spheres | active boxes | inactive]
    //                     0        firstBoxIndex  firstInactiveIndex  size
    size_t firstBoxIndex      = 0;
    size_t firstInactiveIndex = 0;

    std::span<ComponentCollider> ActiveColliders() {
        return { components.data(), firstInactiveIndex };
    }

    std::span<const ComponentCollider> ActiveColliders() const {
        return { components.data(), firstInactiveIndex };
    }

    std::span<ComponentCollider> ActiveSpheres() {
        return { components.data(), firstBoxIndex };
    }

    std::span<const ComponentCollider> ActiveSpheres() const {
        return { components.data(), firstBoxIndex };
    }

    std::span<ComponentCollider> ActiveBoxes() {
        return { components.data() + firstBoxIndex, firstInactiveIndex - firstBoxIndex };
    }

    std::span<const ComponentCollider> ActiveBoxes() const {
        return { components.data() + firstBoxIndex, firstInactiveIndex - firstBoxIndex };
    }

    std::span<ComponentCollider> InactiveColliders() {
        return { components.data() + firstInactiveIndex, components.size() - firstInactiveIndex };
    }

    std::span<const ComponentCollider> InactiveColliders() const {
        return { components.data() + firstInactiveIndex, components.size() - firstInactiveIndex };
    }

    // ── Public mutators ──────────────────────────────────────────────
    // Call instead of directly writing collider.isActive
    void SetActive(ID id, bool active) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return;
        ComponentCollider& c = components[it->second];
        if (c.isActive == active) return;
        c.isActive = active;
        active ? _activateComponent(it->second) : _deactivateComponent(it->second);
    }

    // Call instead of directly writing collider.type
    void SetType(ID id, const ColliderType type) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return;
        ComponentCollider& c = components[it->second];
        if (c.type == type) return;
        if (!c.isActive) { c.type = type; return; } // inactive: just change the field
        c.type = type;
        if (type == COLLIDERTYPE_SPHERE) _promoteToSphere(it->second);
        else _demoteToBox(it->second);
    }

    // ── Overrides that keep boundaries consistent ────────────────────
    ComponentCollider& Add(ID id) {
        if (ComponentCollider* existing = Get(id)) return *existing;

        ComponentCollider comp{};
        comp.ownerID  = id;
        // New components default to active sphere — insert at firstBoxIndex
        // so they land in the sphere region.
        const size_t insertAt = firstBoxIndex;
        components.push_back(comp);                     // append at back first
        entityToIndex[id] = components.size() - 1;

        // Then swap into the sphere slot
        _swapElements(insertAt, components.size() - 1);
        firstBoxIndex++;
        firstInactiveIndex++;

        return components[insertAt];
    }

    bool Remove(ID id) {
        const auto it = entityToIndex.find(id);
        if (it == entityToIndex.end()) return false;

        size_t idx = it->second;

        // Move active collider into inactive region first.
        if (components[idx].isActive) {
            _deactivateComponent(idx);
            idx = entityToIndex[id];
        }

        // Now idx is in inactive region. Remove by swap-with-last.
        const size_t last = components.size() - 1;

        if (idx != last) {
            components[idx] = components[last];
            entityToIndex[components[idx].ownerID] = idx;
        }

        components.pop_back();
        entityToIndex.erase(id);

        return true;
    }

    ComponentCollider& InsertLoaded(const ComponentCollider& comp) {
        ComponentStorage<ComponentCollider>::InsertLoaded(comp);

        const size_t idx = entityToIndex[comp.ownerID];

        if (comp.isActive) _activateComponent(idx);

        return components[entityToIndex[comp.ownerID]];
    }

    void Clear() {
        ComponentStorage<ComponentCollider>::Clear();
        firstBoxIndex      = 0;
        firstInactiveIndex = 0;
    }

private:
    void _swapElements(size_t a, size_t b) {
        if (a == b) return;
        std::swap(components[a], components[b]);
        entityToIndex[components[a].ownerID] = a;
        entityToIndex[components[b].ownerID] = b;
    }

    // Returns new index after promotion into sphere region
    size_t _promoteToSphere(size_t idx) {
        // idx must already be in the active-box region [firstBoxIndex, firstInactiveIndex)
        // Swap it to the boundary between sphere and box regions, then advance the boundary.
        if (idx != firstBoxIndex)
            _swapElements(idx, firstBoxIndex);
        return firstBoxIndex++;
    }

    // Demote an active sphere at idx to the box region
    void _demoteToBox(size_t idx) {
        // idx must be in [0, firstBoxIndex)
        if (firstBoxIndex == 0) return;
        _swapElements(idx, firstBoxIndex - 1);
        firstBoxIndex--;
    }

    // Move an inactive component at idx into the correct active region
    void _activateComponent(size_t idx) {
        // Swap into the inactive boundary slot, then advance firstInactiveIndex.
        if (idx != firstInactiveIndex)
            _swapElements(idx, firstInactiveIndex);
        firstInactiveIndex++;

        // Now it's in the box region. If it's a sphere, promote further.
        const size_t newIdx = firstInactiveIndex - 1;
        if (components[newIdx].type == COLLIDERTYPE_SPHERE)
            _promoteToSphere(newIdx);
    }

    // Move an active component at idx out into the inactive region
    void _deactivateComponent(size_t idx) {
        const bool isSphere = components[idx].type == COLLIDERTYPE_SPHERE;

        if (isSphere) {
            // Move out of sphere region into box region first
            _swapElements(idx, firstBoxIndex - 1);
            idx = firstBoxIndex - 1;
            firstBoxIndex--;
        }
        // Now idx is in active-box region [firstBoxIndex, firstInactiveIndex)
        // Swap to just before the inactive boundary, retract the boundary
        if (idx != firstInactiveIndex - 1)
            _swapElements(idx, firstInactiveIndex - 1);
        firstInactiveIndex--;
    }
};

#endif //TILKY_ENGINE_COMPONENTS_HPP