#include "Headers/Objects/Entity.hpp"

#include <type_traits>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"

static bool HasComponentBit(const ComponentMask& mask, const int componentId) {
    return mask.test(componentId);
}

static void AddComponentBit(ComponentMask& mask, const int componentId) {
    mask.set(componentId);
}

static void RemoveComponentBit(ComponentMask& mask, const int componentId) {
    mask.reset(componentId);
}

template<typename T>
T* Entity::GetComponent() {
    // Pass componentsMask to the checker function
    if constexpr (std::is_same_v<T, ComponentTransform>) {
        if (!HasComponentBit(componentsMask, CMP_TRANSFORM)) return nullptr;
        return LevelManager::CurrentLevel().transforms.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentSprite>) {
        if (!HasComponentBit(componentsMask, CMP_SPRITE)) return nullptr;
        return LevelManager::CurrentLevel().sprites.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentDecal>) {
        if (!HasComponentBit(componentsMask, CMP_DECAL)) return nullptr;
        return LevelManager::CurrentLevel().decals.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentAudioSource>) {
        if (!HasComponentBit(componentsMask, CMP_AUDIO_SOURCE)) return nullptr;
        return LevelManager::CurrentLevel().audioSources.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentScript>) {
        if (!HasComponentBit(componentsMask, CMP_SCRIPT)) return nullptr;
        return LevelManager::CurrentLevel().scripts.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentPlayerController>) {
        if (!HasComponentBit(componentsMask, CMP_PLAYER_CONTROLLER)) return nullptr;
        return LevelManager::CurrentLevel().playerControllers.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentCamera>) {
        if (!HasComponentBit(componentsMask, CMP_CAMERA)) return nullptr;
        return LevelManager::CurrentLevel().cameras.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentSphereCollider>) {
        if (!HasComponentBit(componentsMask, CMP_SPHERE_COLLIDER)) return nullptr;
        return LevelManager::CurrentLevel().sphereColliders.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentRigidbody>) {
        if (!HasComponentBit(componentsMask, CMP_RIGIDBODY)) return nullptr;
        return LevelManager::CurrentLevel().rigidbodies.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentUITransform>) {
        if (!HasComponentBit(componentsMask, CMP_UI_TRANSFORM)) return nullptr;
        return LevelManager::CurrentLevel().ui_transforms.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentUISprite>) {
        if (!HasComponentBit(componentsMask, CMP_UI_SPRITE)) return nullptr;
        return LevelManager::CurrentLevel().ui_sprites.Get(id);
    }
    else if constexpr (std::is_same_v<T, ComponentUIText>) {
        if (!HasComponentBit(componentsMask, CMP_UI_TEXT)) return nullptr;
        return LevelManager::CurrentLevel().ui_texts.Get(id);
    }

    return nullptr;
}

// 2. ADD COMPONENT
template<typename T>
T* Entity::AddComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentTransform>) {
        AddComponentBit(componentsMask, CMP_TRANSFORM);
        return &level.transforms.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentSprite>) {
        AddComponentBit(componentsMask, CMP_SPRITE);
        return &level.sprites.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentDecal>) {
        AddComponentBit(componentsMask, CMP_DECAL);
        return &level.decals.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentAudioSource>) {
        AddComponentBit(componentsMask, CMP_AUDIO_SOURCE);
        return &level.audioSources.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentScript>) {
        AddComponentBit(componentsMask, CMP_SCRIPT);
        return &level.scripts.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentPlayerController>) {
        AddComponentBit(componentsMask, CMP_PLAYER_CONTROLLER);
        return &level.playerControllers.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentCamera>) {
        AddComponentBit(componentsMask, CMP_CAMERA);
        return &level.cameras.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentSphereCollider>) {
        AddComponentBit(componentsMask, CMP_SPHERE_COLLIDER);
        return &level.sphereColliders.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentRigidbody>) {
        AddComponentBit(componentsMask, CMP_RIGIDBODY);
        return &level.rigidbodies.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentUITransform>) {
        AddComponentBit(componentsMask, CMP_UI_TRANSFORM);
        return &level.ui_transforms.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentUISprite>) {
        AddComponentBit(componentsMask, CMP_UI_SPRITE);
        return &level.ui_sprites.Add(id);
    }
    else if constexpr (std::is_same_v<T, ComponentUIText>) {
        AddComponentBit(componentsMask, CMP_UI_TEXT);
        return &level.ui_texts.Add(id);
    }

    return nullptr;
}

// 3. REMOVE COMPONENT
template<typename T>
bool Entity::RemoveComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentTransform>) {
        if (level.transforms.Remove(id)) [[likely]] {
            RemoveComponentBit(componentsMask, CMP_TRANSFORM);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentSprite>) {
        if (level.sprites.Remove(id)) [[likely]]{
            RemoveComponentBit(componentsMask, CMP_SPRITE);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentDecal>) {
        if (level.decals.Remove(id)) [[likely]]{
            RemoveComponentBit(componentsMask, CMP_DECAL);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentAudioSource>) {
        if (level.audioSources.Remove(id)) [[likely]]{
            RemoveComponentBit(componentsMask, CMP_AUDIO_SOURCE);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentScript>) {
        if (level.scripts.Remove(id))[[likely]] {
            RemoveComponentBit(componentsMask, CMP_SCRIPT);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentPlayerController>) {
        if (level.playerControllers.Remove(id)) [[likely]]{
            RemoveComponentBit(componentsMask, CMP_PLAYER_CONTROLLER);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentCamera>) {
        if (level.cameras.Remove(id)) [[likely]]{
            RemoveComponentBit(componentsMask, CMP_CAMERA);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentSphereCollider>) {
        if (level.sphereColliders.Remove(id)) [[likely]]{
            RemoveComponentBit(componentsMask, CMP_SPHERE_COLLIDER);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentRigidbody>) {
        if (level.rigidbodies.Remove(id)) [[likely]] {
            RemoveComponentBit(componentsMask, CMP_RIGIDBODY);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentUITransform>) {
        if (level.ui_transforms.Remove(id)) [[likely]] {
            RemoveComponentBit(componentsMask, CMP_UI_TRANSFORM);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentUISprite>) {
        if (level.ui_sprites.Remove(id)) [[likely]] {
            RemoveComponentBit(componentsMask, CMP_UI_SPRITE);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, ComponentUIText>) {
        if (level.ui_texts.Remove(id)) [[likely]] {
            RemoveComponentBit(componentsMask, CMP_UI_TEXT);
            return true;
        }
    }

    return false;
}

template<typename T>
bool Entity::HasComponent() {
    if constexpr (std::is_same_v<T, ComponentTransform>)
        return HasComponentBit(componentsMask, CMP_TRANSFORM);

    else if constexpr (std::is_same_v<T, ComponentSprite>)
        return HasComponentBit(componentsMask, CMP_SPRITE);

    else if constexpr (std::is_same_v<T, ComponentDecal>)
        return HasComponentBit(componentsMask, CMP_DECAL);

    else if constexpr (std::is_same_v<T, ComponentAudioSource>)
        return HasComponentBit(componentsMask, CMP_AUDIO_SOURCE);

    else if constexpr (std::is_same_v<T, ComponentScript>)
        return HasComponentBit(componentsMask, CMP_SCRIPT);

    else if constexpr (std::is_same_v<T, ComponentPlayerController>)
        return HasComponentBit(componentsMask, CMP_PLAYER_CONTROLLER);

    else if constexpr (std::is_same_v<T, ComponentCamera>)
        return HasComponentBit(componentsMask, CMP_CAMERA);

    else if constexpr (std::is_same_v<T, ComponentSphereCollider>)
        return HasComponentBit(componentsMask, CMP_SPHERE_COLLIDER);

    else if constexpr (std::is_same_v<T, ComponentRigidbody>)
        return HasComponentBit(componentsMask, CMP_RIGIDBODY);

    else if constexpr (std::is_same_v<T, ComponentUITransform>)
        return HasComponentBit(componentsMask, CMP_UI_TRANSFORM);

    else if constexpr (std::is_same_v<T, ComponentUISprite>)
        return HasComponentBit(componentsMask, CMP_UI_SPRITE);

    else if constexpr (std::is_same_v<T, ComponentUIText>)
        return HasComponentBit(componentsMask, CMP_UI_TEXT);

    return false;
}

void Entity::Start() {
}

void Entity::Update() {
}

// Explicit template instantiations remain down here
template ComponentTransform* Entity::GetComponent<ComponentTransform>();
template ComponentSprite* Entity::GetComponent<ComponentSprite>();
template ComponentDecal* Entity::GetComponent<ComponentDecal>();
template ComponentAudioSource* Entity::GetComponent<ComponentAudioSource>();
template ComponentScript* Entity::GetComponent<ComponentScript>();
template ComponentPlayerController* Entity::GetComponent<ComponentPlayerController>();
template ComponentCamera* Entity::GetComponent<ComponentCamera>();
template ComponentSphereCollider* Entity::GetComponent<ComponentSphereCollider>();
template ComponentRigidbody* Entity::GetComponent<ComponentRigidbody>();
template ComponentUITransform* Entity::GetComponent<ComponentUITransform>();
template ComponentUISprite* Entity::GetComponent<ComponentUISprite>();
template ComponentUIText* Entity::GetComponent<ComponentUIText>();

template ComponentTransform* Entity::AddComponent<ComponentTransform>();
template ComponentSprite* Entity::AddComponent<ComponentSprite>();
template ComponentDecal* Entity::AddComponent<ComponentDecal>();
template ComponentAudioSource* Entity::AddComponent<ComponentAudioSource>();
template ComponentScript* Entity::AddComponent<ComponentScript>();
template ComponentPlayerController* Entity::AddComponent<ComponentPlayerController>();
template ComponentCamera* Entity::AddComponent<ComponentCamera>();
template ComponentSphereCollider* Entity::AddComponent<ComponentSphereCollider>();
template ComponentRigidbody* Entity::AddComponent<ComponentRigidbody>();
template ComponentUITransform* Entity::AddComponent<ComponentUITransform>();
template ComponentUISprite* Entity::AddComponent<ComponentUISprite>();
template ComponentUIText* Entity::AddComponent<ComponentUIText>();

template bool Entity::RemoveComponent<ComponentTransform>();
template bool Entity::RemoveComponent<ComponentSprite>();
template bool Entity::RemoveComponent<ComponentDecal>();
template bool Entity::RemoveComponent<ComponentAudioSource>();
template bool Entity::RemoveComponent<ComponentScript>();
template bool Entity::RemoveComponent<ComponentPlayerController>();
template bool Entity::RemoveComponent<ComponentCamera>();
template bool Entity::RemoveComponent<ComponentSphereCollider>();
template bool Entity::RemoveComponent<ComponentRigidbody>();
template bool Entity::RemoveComponent<ComponentUITransform>();
template bool Entity::RemoveComponent<ComponentUISprite>();
template bool Entity::RemoveComponent<ComponentUIText>();

template bool Entity::HasComponent<ComponentTransform>();
template bool Entity::HasComponent<ComponentSprite>();
template bool Entity::HasComponent<ComponentDecal>();
template bool Entity::HasComponent<ComponentAudioSource>();
template bool Entity::HasComponent<ComponentScript>();
template bool Entity::HasComponent<ComponentPlayerController>();
template bool Entity::HasComponent<ComponentCamera>();
template bool Entity::HasComponent<ComponentSphereCollider>();
template bool Entity::HasComponent<ComponentRigidbody>();
template bool Entity::HasComponent<ComponentUITransform>();
template bool Entity::HasComponent<ComponentUISprite>();
template bool Entity::HasComponent<ComponentUIText>();