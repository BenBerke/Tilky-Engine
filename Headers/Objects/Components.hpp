//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_COMPONENTS_HPP
#define TILKY_ENGINE_COMPONENTS_HPP
#include <algorithm>
#include <type_traits>
#include <vector>
#include <unordered_map>

#include "../Math/Vector/Vector2.hpp"
#include "../Objects/Sector.hpp"
#include "EntityTypes.hpp"
#include "ScriptPublicType.hpp"
#include "Headers/Math/Matrix/Matrix4.hpp"
#include <span>

#include "Headers/Math/Quaternion/Quaternion.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"

enum ComponentType {
    CMP_TRANSFORM,
    CMP_SPRITE,
    CMP_AUDIO_SOURCE,
    CMP_SCRIPT,
    CMP_PLAYER_CONTROLLER,
    CMP_CAMERA,
    CMP_COLLIDER,
    CMP_RIGIDBODY,
    CMP_MODEL,
    CMP_FLIPBOOK,

    CMP_NORMAL_COUNT,

    CMP_UI_TRANSFORM, // Transform must always be the first UI component otherwise UIEditorDrawUI breaks (legacy comment)
    CMP_UI_SPRITE,
    CMP_UI_TEXT,

    CMP_COUNT,
};


// region UI Components

// UI Text's Font Size when nothing else is set: the size all text used
// before Font Size existed, at the default 1080 UI Reference Height.
inline constexpr float DEFAULT_UI_FONT_SIZE = 48.0f;

struct ComponentUIText {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    std::string text;

    // Font file relative to Assets, with its extension (e.g. "Fonts/title.ttf").
    // Empty = the engine's default font.
    std::string font;

    // Glyph size in pixels at the project's UI Reference Height; the drawn
    // size scales with the window's height.
    float fontSize = DEFAULT_UI_FONT_SIZE;
};

struct ComponentUISprite {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    std::string texture;

    bool isActive = true; // false = not drawn
};

struct ComponentUITransform {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

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

// Static 3D model drawn at the owner's ComponentTransform. fileName is
// relative to the project's Assets folder, extension included (same as
// texture references), e.g. "Models/crate.glb". Entities using the same file
// share its GPU data; see OpenGLModel.cpp.
struct ComponentModel {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    // Local position relative to the owner's Transform, turned with its
    // rotation. Lets several of these sit at different spots on one entity.
    Vector3 offset = {0.0f, 0.0f, 0.0f};

    std::string fileName;
};

struct ComponentRigidbody {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    bool isStatic = false;
    float mass = 1.0f;
    float gravityScale = 9.8f;
    float friction  = 1.0f;

    Vector3 velocity = {.0f, .0f, .0f};

    bool isGrounded = false;
    Vector3 groundNormal = {.0f, 1.0f, .0f};

    void AddVelocity(const Vector3 &_velocity);
    void ApplyFriction(float _friction, float dt);
    void ApplyAirResistance(float resistance, float dt);
    void ApplyGravity(float gravity, float dt);
};

enum ColliderType {
    COLLIDERTYPE_SPHERE,
    COLLIDERTYPE_BOX
};
struct ComponentCollider {
    // Sphere Collider
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    // Local position relative to the owner's Transform, turned with its
    // rotation. Lets several of these sit at different spots on one entity.
    Vector3 offset = {0.0f, 0.0f, 0.0f};

    ColliderType type = COLLIDERTYPE_SPHERE;

    bool isActive = true;
    bool isTrigger = false;

    // Type = Box -> Use these values as AABB; Type = Sphere -> Use size.x as the radius
    Vector3 scale = {1.0f, 1.0f, 1.0f};

    float stepSize = 0.0f;
};

struct ComponentPlayerController {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    // Physical player/camera-body position.
    // x = world/map X
    // y = world eye height
    // z = world/map Y

    bool isActive = false;

    float speed = 46.0f;
    float runningSpeed = 90.0f;

    float jumpPower = 100.0f;

    unsigned int jumpBufferMs = 5;

    // Eye height above the current floor.
    float eyeHeight = 12.0f;

    float sensitivityX = .5f, sensitivityY = .5f;

    //todo TILKYTODO Max-Min pitch is acting weirdly
    float minPitch = -89.0f, maxPitch = 89.0f;
    float minYaw =  .0f, maxYaw = 360.0f;

    bool noClip = false;

    float acceleration = 80.f;
    float deceleration = 60.0f;
    float airControl = .3f;

    // Read only, do not change
    Vector3 velocity = {0.0f, 0.0f, 0.0f};

    float currentSpeed = 0.0f;
    float currentEyeHeight = 0.0f;
};

struct ComponentCamera {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    bool isActive = true;

    // yaw = left/right rotation.
    float yaw = 0.0f;

    // pitch = up/down rotation.
    float pitch = 0.0f;

    float fov = 90.0f;
    float aspectRatio = 1680.0f / 960.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;

    bool smoothStep = true; // When true, smoothly move up/down steps instead of teleporting
    float smoothingStrength = 4.8f;

    // Read-only, do not change in game via scripts
    float smoothStepStartY = 0.0f;
    float smoothStepTargetY = 0.0f;
    float stepOffsetY = 0.0f;
    bool isStepping = false;
    float previousTransformY = 0.0f;
    bool hasPreviousTransformY = false;

    Vector3 forward = {0.0f, 0.0f, 1.0f};
    Vector3 target = {0.0f, 0.0f, 1.0f};

    Matrix4 view = Matrix4::Identity();
    Matrix4 projection = Matrix4::Identity();
};

// A script attached to an entity. Everything except the owner lives in
// ScriptAttachmentData (ScriptPublicType.hpp), shared with sector scripts.
struct ComponentScript : ScriptAttachmentData {
    ID ownerID = static_cast<ID>(-1);
};

class ScriptComponentStorage {
public:
    std::vector<ComponentScript> components;

    ComponentScript& Add(const ID ownerID, const ScriptInstanceID requestedID = INVALID_SCRIPT_INSTANCE_ID) {
        ScriptInstanceID instanceID = requestedID;

        if (instanceID == INVALID_SCRIPT_INSTANCE_ID || GetByID(instanceID) != nullptr)instanceID = GenerateID();
        else if (instanceID >= nextInstanceID) nextInstanceID = instanceID + 1;

        ComponentScript& script = components.emplace_back();
        script.ownerID = ownerID;
        script.instanceID = instanceID;

        return script;
    }

    ComponentScript* GetByID(const ScriptInstanceID instanceID) {
        for (ComponentScript& script : components)  if (script.instanceID == instanceID) return &script;

        return nullptr;
    }

    [[nodiscard]] const ComponentScript* GetByID(const ScriptInstanceID instanceID) const {
        for (const ComponentScript& script : components) if (script.instanceID == instanceID) return &script;

        return nullptr;
    }

    ComponentScript* GetFirstByOwner(const ID ownerID) {
        for (ComponentScript& script : components) if (script.ownerID == ownerID) return &script;

        return nullptr;
    }

    ComponentScript* Get(const ID ownerID) {
        return GetFirstByOwner(ownerID);
    }

    [[nodiscard]] const ComponentScript* Get(const ID ownerID) const {
        for (const ComponentScript& script : components)  if (script.ownerID == ownerID) return &script;

        return nullptr;
    }

    std::vector<ComponentScript*> GetAll(const ID ownerID) {
        std::vector<ComponentScript*> result;

        for (ComponentScript& script : components) if (script.ownerID == ownerID) result.push_back(&script);

        return result;
    }

    [[nodiscard]] bool HasAny(const ID ownerID) const {
        for (const ComponentScript& script : components) if (script.ownerID == ownerID) return true;
        return false;
    }

    bool Remove(const ScriptInstanceID instanceID) {
        const auto it = std::find_if(
            components.begin(),
            components.end(),
            [instanceID](const ComponentScript& script) {
                return script.instanceID == instanceID;
            }
        );

        if (it == components.end()) return false;

        components.erase(it);
        return true;
    }

    // Moves a script to position newIndex among its owner's scripts. Scripts
    // keep their per-owner order in `components` itself.
    bool MoveOnOwner(const ScriptInstanceID instanceID, const size_t newIndex) {
        const ComponentScript* script = GetByID(instanceID);
        if (script == nullptr) return false;
        const ID ownerID = script->ownerID;

        std::vector<size_t> ownerIndices;
        for (size_t i = 0; i < components.size(); ++i) if (components[i].ownerID == ownerID) ownerIndices.push_back(i);
        if (newIndex >= ownerIndices.size()) return false;

        const size_t from = static_cast<size_t>(script - components.data());
        const size_t to = ownerIndices[newIndex];
        if (from == to) return true;

        ComponentScript moved = std::move(components[from]);
        components.erase(components.begin() + static_cast<std::ptrdiff_t>(from));
        components.insert(components.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
        return true;
    }

    bool RemoveAll(const ID ownerID) {
        const std::size_t previousSize = components.size();

        std::erase_if(components,
            [ownerID](const ComponentScript& script) {
                return script.ownerID == ownerID;
            }
        );

        return components.size() != previousSize;
    }

    void Clear() {
        components.clear();
        nextInstanceID = 1;
    }

private:
    ScriptInstanceID nextInstanceID = 1;

    ScriptInstanceID GenerateID() {
        while (GetByID(nextInstanceID) != nullptr) ++nextInstanceID;

        return nextInstanceID++;
    }
};
// OpenAL Audio source. What sound it will play can change during gameplay.
struct ComponentAudioSource {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    // Local position relative to the owner's Transform, turned with its
    // rotation. Lets several of these sit at different spots on one entity.
    Vector3 offset = {0.0f, 0.0f, 0.0f};

    std::string name; // OpenAL source name, e.g. "entity_4_audio_2" (owner, instance)
    std::string soundFileName;

    float pitch = 1.0f;
    float gain = 1.0f;
    bool looping = false;
    bool playOnStart = false;

    // Distance Attenuation (How volume drops over distance)
    float referenceDistance = 1.0f;   // Distance where gain is at its max
    float maxDistance = 10000.0f;      // Distance where attenuation stops
    float rollOffFactor = 1.0f;       // How fast the sound fades (1.0 is "real")

    // Sound Cone (Directional audio behavior)
    // The cone points along the owner's transform rotation (local +Z). Angles are the full
    // cone width in degrees, 360 means no cone.
    float innerConeAngle = 360.0f;    // Inside this cone, sound is full volume
    float outerConeAngle = 360.0f;    // Outside this, volume is 'outerGain'
    float outerGain = 0.0f;           // Volume multiplier outside the cone

    // Runtime only, not saved. Set by StopSound/PauseSound so AudioSystem
    // doesn't restart a looping source the script silenced; PlaySound and
    // ResumeSound clear it.
    bool isHeld = false;

    void PlaySound();
    void StopSound();
    void PauseSound();
    void ResumeSound();
    [[nodiscard]] bool IsPlaying() const;

    void SetSourcePitch(float _pitch) const;

    void SetSourceGain(float _gain) const;

    void SetSourceLooping(bool _looping) const;

    void SetSourceReferenceDistance(float distance) const;

    void SetSourceMaxDistance(float distance) const;

    void SetSourceRollOffFactor(float factor) const;

    void SetSourceInnerConeAngle(float angle) const;

    void SetSourceOuterConeAngle(float angle) const;

    void SetSourceOuterGain(float _outerGain) const;

    void SetSourcePosition(const Vector3& position) const;

};

// Stores things related to the entity's whereabouts
// Every entity MUST have a transform component
struct Sector;
struct ComponentTransform {
    ID ownerID = -1;
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    /*
     * relativeHeight = height relative to the current sector floor
     *
     * Transform origin is the object's feet
     */
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Quaternion rotation = Quaternion::Identity();

    float relativeHeight = 0.0f;

    Vector3 scale = {32.0f, 32.0f, 32.0f};

    int sectorIndex = -1; // Stores the index of the sector in level.sectors vector. Allows for fast lookup
    ID sectorId = -1; // Stores the absoulete ID of the sector, this should be for things like scripting
    bool isDirty = false;

    void AddPosition(const Vector3& position);
    void SetPosition(const Vector3& position);
    bool UpdateObjectSectorAndFloor(std::vector<Sector>& sectors);

    // World position of a component offset (Sprite/Model/Collider/AudioSource::offset).
    [[nodiscard]] Vector3 LocalToWorld(const Vector3& offset) const;
};

enum SideCount {
    SIDECOUNT_SINGLE,
    SIDECOUNT_45,
    SIDECOUNT_90
};

struct ComponentSprite {
    ID ownerID = -1;
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    // Local position relative to the owner's Transform, turned with its
    // rotation. Lets several of these sit at different spots on one entity.
    Vector3 offset = {0.0f, 0.0f, 0.0f};

    std::array<std::string,8> textureFileNames;

    SideCount sideCount = SIDECOUNT_SINGLE;

    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};

    bool isStatic;
    bool isActive = true; // false = not uploaded to the sprite SSBO, so not drawn
};

// Plays a flipbook (.fpk, see FlipbookAsset.hpp) on one of the owner's
// Sprites, or on a UI entity, one of its UI Sprites. It never draws
// anything: while the game runs, FlipbookSystem copies the current frame's
// textures into that sprite's textureFileNames (a UI Sprite gets slot 0).
// The sprite's side count is left alone - the flipbook uses whatever the
// sprite has.
struct ComponentFlipbook {
    ID ownerID = static_cast<ID>(-1);
    ComponentInstanceID instanceID = INVALID_COMPONENT_INSTANCE_ID;

    // Assets-relative .fpk reference, extension included, e.g. "Animations/walk.fpk".
    std::string flipbookFileName;

    // The owner's sprite this drives - a ComponentSprite, or a
    // ComponentUISprite when the owner is a UI entity. INVALID (or a sprite
    // that's gone) means the owner's first one.
    ComponentInstanceID spriteInstanceID = INVALID_COMPONENT_INSTANCE_ID;

    float speed = 1.0f; // playback rate multiplier, 1 = the flipbook's own timing

    // Runtime only, not saved. Driven by FlipbookSystem and the Lua API.
    int currentFrame = 0;
    float frameTime = 0.0f;     // seconds spent on currentFrame
    int pingPongDirection = 1;  // +1 forward, -1 backward (PingPong only)
    bool playing = false;
    bool applyPending = false;  // copy currentFrame to the sprite on the next update
    bool eventPending = false;  // fire currentFrame's event on the next update
};

// Transform and UITransform place the entity, so there's only ever one.
// Every other component can be added any number of times.
template<typename T>
inline constexpr bool IsSingleComponent = std::is_same_v<T, ComponentTransform> || std::is_same_v<T, ComponentUITransform>;

// Holds every component of one type. An entity can own several (except
// Transform/UITransform - see Entity::AddComponent); each gets an instanceID
// that is unique within this storage and never reused while the level is
// loaded, so references to "this exact AudioSource" survive siblings being
// removed or reordered. `components` is in no particular order - systems
// that just process everything iterate it directly. Per-owner order (which
// one is "first", the inspector order) is kept in ownerToInstances.
template<typename T>
struct ComponentStorage {
    std::vector<T> components;

    // The owner's first component, or nullptr.
    T* Get(const ID ownerID) {
        const auto it = ownerToInstances.find(ownerID);
        if (it == ownerToInstances.end() || it->second.empty()) return nullptr;
        return GetInstance(it->second.front());
    }

    const T* Get(const ID ownerID) const {
        const auto it = ownerToInstances.find(ownerID);
        if (it == ownerToInstances.end() || it->second.empty()) return nullptr;
        return GetInstance(it->second.front());
    }

    T* GetInstance(const ComponentInstanceID instanceID) {
        const auto it = instanceToIndex.find(instanceID);
        if (it == instanceToIndex.end()) return nullptr;
        return &components[it->second];
    }

    const T* GetInstance(const ComponentInstanceID instanceID) const {
        const auto it = instanceToIndex.find(instanceID);
        if (it == instanceToIndex.end()) return nullptr;
        return &components[it->second];
    }

    // All of the owner's components, in order. The pointers are only valid
    // until the next Add/Remove on this storage.
    std::vector<T*> GetAll(const ID ownerID) {
        std::vector<T*> result;
        for (const ComponentInstanceID instanceID : InstancesOf(ownerID)) result.push_back(GetInstance(instanceID));
        return result;
    }

    std::vector<const T*> GetAll(const ID ownerID) const {
        std::vector<const T*> result;
        for (const ComponentInstanceID instanceID : InstancesOf(ownerID)) result.push_back(GetInstance(instanceID));
        return result;
    }

    // The owner's instance IDs, in order. Copy it before adding or removing.
    const std::vector<ComponentInstanceID>& InstancesOf(const ID ownerID) const {
        static const std::vector<ComponentInstanceID> none;
        const auto it = ownerToInstances.find(ownerID);
        return it == ownerToInstances.end() ? none : it->second;
    }

    // Position of instanceID among its owner's components (0 = first), or -1.
    [[nodiscard]] int IndexOnOwner(const ComponentInstanceID instanceID) const {
        const T* component = GetInstance(instanceID);
        if (component == nullptr) return -1;
        const std::vector<ComponentInstanceID>& list = InstancesOf(component->ownerID);
        const auto it = std::find(list.begin(), list.end(), instanceID);
        return it == list.end() ? -1 : static_cast<int>(it - list.begin());
    }

    [[nodiscard]] bool Has(const ID ownerID) const {
        const auto it = ownerToInstances.find(ownerID);
        return it != ownerToInstances.end() && !it->second.empty();
    }

    [[nodiscard]] size_t Count(const ID ownerID) const { return InstancesOf(ownerID).size(); }

    // Always adds a new component, after the owner's existing ones.
    // requestedID is kept when it's free (loading, undo), otherwise a new one
    // is generated.
    T& Add(const ID ownerID, const ComponentInstanceID requestedID = INVALID_COMPONENT_INSTANCE_ID) {
        T component{};
        component.ownerID = ownerID;
        component.instanceID = requestedID;
        return Insert(component);
    }

    // Adds a component that already has its data (loading, copy-paste).
    // Keeps component.instanceID when it's free.
    T& InsertLoaded(const T& component) { return Insert(component); }

    bool RemoveInstance(const ComponentInstanceID instanceID) {
        const auto it = instanceToIndex.find(instanceID);
        if (it == instanceToIndex.end()) return false;

        const size_t removeIndex = it->second;
        const ID ownerID = components[removeIndex].ownerID;
        const size_t lastIndex = components.size() - 1;

        if (removeIndex != lastIndex) {
            components[removeIndex] = std::move(components[lastIndex]);
            instanceToIndex[components[removeIndex].instanceID] = removeIndex;
        }

        components.pop_back();
        instanceToIndex.erase(instanceID);
        ForgetInstance(ownerID, instanceID);

        return true;
    }

    // Removes every component the owner has. False if it had none.
    bool RemoveAll(const ID ownerID) {
        const std::vector<ComponentInstanceID> list = InstancesOf(ownerID);
        for (const ComponentInstanceID instanceID : list) RemoveInstance(instanceID);
        return !list.empty();
    }

    // Moves instanceID to position newIndex among its owner's components.
    bool MoveOnOwner(const ComponentInstanceID instanceID, const size_t newIndex) {
        const T* component = GetInstance(instanceID);
        if (component == nullptr) return false;
        std::vector<ComponentInstanceID>& list = ownerToInstances[component->ownerID];
        const auto it = std::find(list.begin(), list.end(), instanceID);
        if (it == list.end() || newIndex >= list.size()) return false;
        list.erase(it);
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(newIndex), instanceID);
        return true;
    }

    void Clear() {
        components.clear();
        instanceToIndex.clear();
        ownerToInstances.clear();
        nextInstanceID = 1;
    }

protected:
    std::unordered_map<ComponentInstanceID, size_t> instanceToIndex;
    std::unordered_map<ID, std::vector<ComponentInstanceID>> ownerToInstances;
    ComponentInstanceID nextInstanceID = 1;

    T& Insert(T component) {
        ComponentInstanceID instanceID = component.instanceID;
        if (instanceID == INVALID_COMPONENT_INSTANCE_ID || instanceToIndex.contains(instanceID)) {
            while (instanceToIndex.contains(nextInstanceID)) ++nextInstanceID;
            instanceID = nextInstanceID++;
        } else if (instanceID >= nextInstanceID) {
            nextInstanceID = instanceID + 1;
        }
        component.instanceID = instanceID;

        const ID ownerID = component.ownerID;
        components.push_back(std::move(component));
        instanceToIndex[instanceID] = components.size() - 1;
        ownerToInstances[ownerID].push_back(instanceID);

        return components.back();
    }

    void ForgetInstance(const ID ownerID, const ComponentInstanceID instanceID) {
        const auto it = ownerToInstances.find(ownerID);
        if (it == ownerToInstances.end()) return;
        std::erase(it->second, instanceID);
        if (it->second.empty()) ownerToInstances.erase(it);
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
    void SetActive(const ComponentInstanceID instanceID, const bool active) {
        const auto it = instanceToIndex.find(instanceID);
        if (it == instanceToIndex.end()) return;
        ComponentCollider& c = components[it->second];
        if (c.isActive == active) return;
        c.isActive = active;
        active ? _activateComponent(it->second) : _deactivateComponent(it->second);
    }

    // Call instead of directly writing collider.type
    void SetType(const ComponentInstanceID instanceID, const ColliderType type) {
        const auto it = instanceToIndex.find(instanceID);
        if (it == instanceToIndex.end()) return;
        ComponentCollider& c = components[it->second];
        if (c.type == type) return;
        if (!c.isActive) { c.type = type; return; } // inactive: just change the field
        c.type = type;
        if (type == COLLIDERTYPE_SPHERE) _promoteToSphere(it->second);
        else _demoteToBox(it->second);
    }

    // ── Overrides that keep boundaries consistent ────────────────────
    // New components default to an active sphere. They're appended at the
    // back (the inactive region), then activated, which walks them through
    // the inactive and box boundaries so the elements they displace stay in
    // their regions.
    ComponentCollider& Add(const ID ownerID, const ComponentInstanceID requestedID = INVALID_COMPONENT_INSTANCE_ID) {
        ComponentCollider comp{};
        comp.ownerID = ownerID;
        comp.instanceID = requestedID;
        comp.isActive = false;
        const ComponentInstanceID instanceID = Insert(comp).instanceID;
        SetActive(instanceID, true);
        return *GetInstance(instanceID);
    }

    // Keeps the loaded type/active state, placing it in the matching region.
    ComponentCollider& InsertLoaded(const ComponentCollider& loaded) {
        ComponentCollider comp = loaded;
        const bool active = comp.isActive;
        comp.isActive = false;
        const ComponentInstanceID instanceID = Insert(comp).instanceID;
        SetActive(instanceID, active);
        return *GetInstance(instanceID);
    }

    bool RemoveInstance(const ComponentInstanceID instanceID) {
        const auto it = instanceToIndex.find(instanceID);
        if (it == instanceToIndex.end()) return false;

        // Move an active collider into the inactive region first, so the
        // swap-with-last below never pulls an inactive one into an active region.
        if (components[it->second].isActive) _deactivateComponent(it->second);

        const size_t idx = instanceToIndex[instanceID];
        const ID ownerID = components[idx].ownerID;
        const size_t last = components.size() - 1;

        if (idx != last) {
            components[idx] = std::move(components[last]);
            instanceToIndex[components[idx].instanceID] = idx;
        }

        components.pop_back();
        instanceToIndex.erase(instanceID);
        ForgetInstance(ownerID, instanceID);

        return true;
    }

    bool RemoveAll(const ID ownerID) {
        const std::vector<ComponentInstanceID> list = InstancesOf(ownerID);
        for (const ComponentInstanceID instanceID : list) RemoveInstance(instanceID);
        return !list.empty();
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
        instanceToIndex[components[a].instanceID] = a;
        instanceToIndex[components[b].instanceID] = b;
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