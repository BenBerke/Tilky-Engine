#include "Headers/Objects/Entity.hpp"

#include <type_traits>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"

template<typename T>
T* Entity::GetComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentTransform>) return level.transforms.Get(id);
    else if constexpr (std::is_same_v<T, ComponentSprite>) return level.sprites.Get(id);
    else if constexpr (std::is_same_v<T, ComponentDecal>) return level.decals.Get(id);
    else if constexpr (std::is_same_v<T, ComponentPlayerSpawn>) return level.playerSpawns.Get(id);
    else if constexpr (std::is_same_v<T, ComponentAudioSource>) return level.audioSources.Get(id);
    else if constexpr (std::is_same_v<T, ComponentScript>) return level.scripts.Get(id);
    else return nullptr;
}

template<typename T>
T* Entity::AddComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentTransform>) return &level.transforms.Add(id);
    else if constexpr (std::is_same_v<T, ComponentSprite>) return &level.sprites.Add(id);
    else if constexpr (std::is_same_v<T, ComponentDecal>) return &level.decals.Add(id);
    else if constexpr (std::is_same_v<T, ComponentPlayerSpawn>) return &level.playerSpawns.Add(id);
    else if constexpr (std::is_same_v<T, ComponentAudioSource>) return &level.audioSources.Add(id);
    else if constexpr (std::is_same_v<T, ComponentScript>) return &level.scripts.Add(id);
    else return nullptr;
}

template<typename T>
bool Entity::RemoveComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentTransform>) return level.transforms.Remove(id);
    else if constexpr (std::is_same_v<T, ComponentSprite>) return level.sprites.Remove(id);
    else if constexpr (std::is_same_v<T, ComponentDecal>) return level.decals.Remove(id);
    else if constexpr (std::is_same_v<T, ComponentPlayerSpawn>) return level.playerSpawns.Remove(id);
    else if constexpr (std::is_same_v<T, ComponentAudioSource>) return level.audioSources.Remove(id);
    else if constexpr (std::is_same_v<T, ComponentScript>) return level.scripts.Remove(id);
    else return false;
}

template<typename T>
bool Entity::HasComponent() {
    return GetComponent<T>() != nullptr;
}

void Entity::Start() {
}

void Entity::Update() {

}

// Explicit template instantiations.
// These are required because the template definitions are in a .cpp file.
template ComponentTransform* Entity::GetComponent<ComponentTransform>();
template ComponentSprite* Entity::GetComponent<ComponentSprite>();
template ComponentDecal* Entity::GetComponent<ComponentDecal>();
template ComponentPlayerSpawn* Entity::GetComponent<ComponentPlayerSpawn>();
template ComponentAudioSource* Entity::GetComponent<ComponentAudioSource>();
template ComponentScript* Entity::GetComponent<ComponentScript>();

template ComponentTransform* Entity::AddComponent<ComponentTransform>();
template ComponentSprite* Entity::AddComponent<ComponentSprite>();
template ComponentDecal* Entity::AddComponent<ComponentDecal>();
template ComponentPlayerSpawn* Entity::AddComponent<ComponentPlayerSpawn>();
template ComponentAudioSource* Entity::AddComponent<ComponentAudioSource>();
template ComponentScript* Entity::AddComponent<ComponentScript>();

template bool Entity::RemoveComponent<ComponentTransform>();
template bool Entity::RemoveComponent<ComponentSprite>();
template bool Entity::RemoveComponent<ComponentDecal>();
template bool Entity::RemoveComponent<ComponentPlayerSpawn>();
template bool Entity::RemoveComponent<ComponentAudioSource>();
template bool Entity::RemoveComponent<ComponentScript>();

template bool Entity::HasComponent<ComponentTransform>();
template bool Entity::HasComponent<ComponentSprite>();
template bool Entity::HasComponent<ComponentDecal>();
template bool Entity::HasComponent<ComponentPlayerSpawn>();
template bool Entity::HasComponent<ComponentAudioSource>();
template bool Entity::HasComponent<ComponentScript>();