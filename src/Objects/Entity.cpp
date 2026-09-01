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
T *Entity::GetComponent() {
    Level &level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentScript>) {
        if (!level.scripts.HasAny(id))
            return nullptr;

        return level.scripts.GetFirstByOwner(id);
    } else {
#define ENTITY_GET_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
if constexpr (std::is_same_v<T, Type>) { \
if (!HasComponentBit(componentsMask, Bit)) return nullptr; \
return level.Storage.Get(id); \
} else

        TILKY_COMPONENTS(ENTITY_GET_COMPONENT_CASE)
        {
            static_assert(
                AlwaysFalseV<T>,
                "Unsupported component type in Entity::GetComponent<T>()"
            );

            return nullptr;
        }

#undef ENTITY_GET_COMPONENT_CASE
    }
}


template<typename T>
T* Entity::AddComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentScript>) {
        AddComponentBit(componentsMask, CMP_SCRIPT);
        return &level.scripts.Add(id);
    } else {

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
            static_assert(
                AlwaysFalseV<T>,
                "Unsupported component type in Entity::AddComponent<T>()"
            );

            return nullptr;
        }

#undef ENTITY_ADD_COMPONENT_CASE
    }
}


template<typename T>
bool Entity::RemoveComponent() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentScript>) {
        const bool removed = level.scripts.RemoveAll(id);

        if (!level.scripts.HasAny(id))
            RemoveComponentBit(componentsMask, CMP_SCRIPT);

        return removed;
    } else {

#define ENTITY_REMOVE_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
if constexpr (std::is_same_v<T, Type>) { \
if (!HasComponentBit(componentsMask, Bit)) return false; \
if (level.Storage.Remove(id)) { \
RemoveComponentBit(componentsMask, Bit); \
return true; \
} \
return false; \
} else

        TILKY_COMPONENTS(ENTITY_REMOVE_COMPONENT_CASE)
        {
            static_assert(
                AlwaysFalseV<T>,
                "Unsupported component type in Entity::RemoveComponent<T>()"
            );

            return false;
        }

#undef ENTITY_REMOVE_COMPONENT_CASE
    }
}


template<typename T>
bool Entity::HasComponent() {
    if constexpr (std::is_same_v<T, ComponentScript>) {
        return LevelManager::CurrentLevel().scripts.HasAny(id);
    } else {

#define ENTITY_HAS_COMPONENT_CASE(Type, Bit, Storage, LabelKey) \
if constexpr (std::is_same_v<T, Type>) { \
return HasComponentBit(componentsMask, Bit); \
} else

        TILKY_COMPONENTS(ENTITY_HAS_COMPONENT_CASE)
        {
            static_assert(
                AlwaysFalseV<T>,
                "Unsupported component type in Entity::HasComponent<T>()"
            );

            return false;
        }

#undef ENTITY_HAS_COMPONENT_CASE
    }
}

void Entity::Start() {
    // A place holder. Probably should remain empty
    // Might be useful for those who want to create their own fork of the engine
}

void Entity::Update() {
}

ComponentScript& Entity::AddScript() {
    Level& level = LevelManager::CurrentLevel();

    AddComponentBit(componentsMask, CMP_SCRIPT);
    return level.scripts.Add(id);
}

ComponentScript* Entity::GetScript(const ScriptInstanceID instanceID) {
    Level& level = LevelManager::CurrentLevel();

    ComponentScript* script = level.scripts.GetByID(instanceID);

    if (script == nullptr || script->ownerID != id)
        return nullptr;

    return script;
}

std::vector<ComponentScript*> Entity::GetScripts() {
    return LevelManager::CurrentLevel().scripts.GetAll(id);
}

bool Entity::RemoveScript(const ScriptInstanceID instanceID) {
    Level& level = LevelManager::CurrentLevel();

    ComponentScript* script = level.scripts.GetByID(instanceID);

    if (script == nullptr || script->ownerID != id) return false;

    if (!level.scripts.Remove(instanceID)) return false;

    if (!level.scripts.HasAny(id)) RemoveComponentBit(componentsMask, CMP_SCRIPT);

    return true;
}

bool Entity::RemoveAllScripts() {
    Level& level = LevelManager::CurrentLevel();

    const bool removed = level.scripts.RemoveAll(id);
    RemoveComponentBit(componentsMask, CMP_SCRIPT);

    return removed;
}

// Explicit template instantiations
#define ENTITY_INSTANTIATE_COMPONENT(Type, Bit, Storage, LabelKey) \
    template Type* Entity::GetComponent<Type>(); \
    template Type* Entity::AddComponent<Type>(); \
    template bool Entity::RemoveComponent<Type>(); \
    template bool Entity::HasComponent<Type>();

TILKY_COMPONENTS(ENTITY_INSTANTIATE_COMPONENT)

#undef ENTITY_INSTANTIATE_COMPONENT