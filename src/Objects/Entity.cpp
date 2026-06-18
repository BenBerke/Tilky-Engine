#include "Headers/Objects/Entity.hpp"

#include <type_traits>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"

static bool HasComponentBit(const ComponentMask& mask, const int componentId) {
    return mask.test(componentId);
}

static void AddComponentBit(ComponentMask& mask, const int componentId) {
    mask.set(componentId);
}

static void RemoveComponentBit(ComponentMask& mask, const int componentId) {
    mask.reset(componentId);
}

template<typename>
inline constexpr bool AlwaysFalseV = false;

template<typename T>
T* Entity::GetComponent() {
    Level& level = LevelManager::CurrentLevel();

#define ENTITY_GET_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    if constexpr (std::is_same_v<T, Type>) { \
        if (!HasComponentBit(componentsMask, Bit)) return nullptr; \
        return level.Storage.Get(id); \
    } else

    TILKY_COMPONENTS(ENTITY_GET_COMPONENT_CASE)
    {
        static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::GetComponent<T>()");
        return nullptr;
    }

#undef ENTITY_GET_COMPONENT_CASE
}


template<typename T>
T* Entity::AddComponent() {
    Level& level = LevelManager::CurrentLevel();

#define ENTITY_ADD_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    if constexpr (std::is_same_v<T, Type>) { \
        if (HasComponentBit(componentsMask, Bit)) { \
            return level.Storage.Get(id); \
        } \
        AddComponentBit(componentsMask, Bit); \
        return &level.Storage.Add(id); \
    } else

    TILKY_COMPONENTS(ENTITY_ADD_COMPONENT_CASE)
    {
        static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::AddComponent<T>()");
        return nullptr;
    }

#undef ENTITY_ADD_COMPONENT_CASE
}


template<typename T>
bool Entity::RemoveComponent() {
    Level& level = LevelManager::CurrentLevel();

#define ENTITY_REMOVE_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    if constexpr (std::is_same_v<T, Type>) { \
        if (!HasComponentBit(componentsMask, Bit)) return false; \
        if (level.Storage.Remove(id)) [[likely]] { \
            RemoveComponentBit(componentsMask, Bit); \
            return true; \
        } \
        return false; \
    } else

    TILKY_COMPONENTS(ENTITY_REMOVE_COMPONENT_CASE)
    {
        static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::RemoveComponent<T>()");
        return false;
    }

#undef ENTITY_REMOVE_COMPONENT_CASE
}


template<typename T>
bool Entity::HasComponent() {
#define ENTITY_HAS_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
    if constexpr (std::is_same_v<T, Type>) { \
        return HasComponentBit(componentsMask, Bit); \
    } else

    TILKY_COMPONENTS(ENTITY_HAS_COMPONENT_CASE)
    {
        static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::HasComponent<T>()");
        return false;
    }

#undef ENTITY_HAS_COMPONENT_CASE
}

void Entity::Start() {
}

void Entity::Update() {
}

// Explicit template instantiations
#define ENTITY_INSTANTIATE_COMPONENT(Type, Bit, Storage, LabelKey) \
    template Type* Entity::GetComponent<Type>(); \
    template Type* Entity::AddComponent<Type>(); \
    template bool Entity::RemoveComponent<Type>(); \
    template bool Entity::HasComponent<Type>();

TILKY_COMPONENTS(ENTITY_INSTANTIATE_COMPONENT)

#undef ENTITY_INSTANTIATE_COMPONENT