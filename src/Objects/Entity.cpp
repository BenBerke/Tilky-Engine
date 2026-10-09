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
std::vector<T*> Entity::GetComponents() {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentScript>) {
        return level.scripts.GetAll(id);
    } else {
#define ENTITY_GET_COMPONENTS_CASE(Type, Bit, Storage, LabelKey) \
if constexpr (std::is_same_v<T, Type>) { \
return level.Storage.GetAll(id); \
} else

        TILKY_COMPONENTS(ENTITY_GET_COMPONENTS_CASE)
        {
            static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::GetComponents<T>()");
            return {};
        }

#undef ENTITY_GET_COMPONENTS_CASE
    }
}


// Called through T so the storage call is only compiled for the matching type.
template<typename T, typename Storage>
static T* OwnedInstance(Storage& storage, const ComponentInstanceID instanceID, const ID ownerID) {
    T* component = storage.GetInstance(instanceID);
    return component != nullptr && component->ownerID == ownerID ? component : nullptr;
}

template<typename T>
T* Entity::GetComponentInstance(const ComponentInstanceID instanceID) {
    Level& level = LevelManager::CurrentLevel();

    // Script instance IDs are ScriptInstanceIDs (the same integer type).
    if constexpr (std::is_same_v<T, ComponentScript>) {
        return GetScript(instanceID);
    } else {
#define ENTITY_GET_INSTANCE_CASE(Type, Bit, Storage, LabelKey) \
if constexpr (std::is_same_v<T, Type>) { \
return OwnedInstance<T>(level.Storage, instanceID, id); \
} else

        TILKY_COMPONENTS(ENTITY_GET_INSTANCE_CASE)
        {
            static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::GetComponentInstance<T>()");
            return nullptr;
        }

#undef ENTITY_GET_INSTANCE_CASE
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
if constexpr (IsSingleComponent<T>) { \
if (T* existing = level.Storage.Get(id)) return existing; \
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
RemoveComponentBit(componentsMask, Bit); \
return level.Storage.RemoveAll(id); \
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


// Clears the entity's mask bit once its last component of the type is gone.
template<typename T, typename Storage>
static bool RemoveOwnedInstance(Storage& storage, const ComponentInstanceID instanceID, const ID ownerID,
                                ComponentMask& mask, const int bit) {
    if (OwnedInstance<T>(storage, instanceID, ownerID) == nullptr) return false;
    storage.RemoveInstance(instanceID);
    if (!storage.Has(ownerID)) RemoveComponentBit(mask, bit);
    return true;
}

template<typename T>
bool Entity::RemoveComponentInstance(const ComponentInstanceID instanceID) {
    Level& level = LevelManager::CurrentLevel();

    if constexpr (std::is_same_v<T, ComponentScript>) {
        return RemoveScript(instanceID);
    } else {
#define ENTITY_REMOVE_INSTANCE_CASE(Type, Bit, Storage, LabelKey) \
if constexpr (std::is_same_v<T, Type>) { \
return RemoveOwnedInstance<T>(level.Storage, instanceID, id, componentsMask, Bit); \
} else

        TILKY_COMPONENTS(ENTITY_REMOVE_INSTANCE_CASE)
        {
            static_assert(AlwaysFalseV<T>, "Unsupported component type in Entity::RemoveComponentInstance<T>()");
            return false;
        }

#undef ENTITY_REMOVE_INSTANCE_CASE
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
    template std::vector<Type*> Entity::GetComponents<Type>(); \
    template Type* Entity::AddComponent<Type>(); \
    template bool Entity::RemoveComponent<Type>(); \
    template bool Entity::HasComponent<Type>();

#define ENTITY_INSTANTIATE_INSTANCE_FUNCTIONS(Type, Bit, Storage, LabelKey) \
    template Type* Entity::GetComponentInstance<Type>(ComponentInstanceID); \
    template bool Entity::RemoveComponentInstance<Type>(ComponentInstanceID);

TILKY_COMPONENTS(ENTITY_INSTANTIATE_COMPONENT)
TILKY_COMPONENTS(ENTITY_INSTANTIATE_INSTANCE_FUNCTIONS)

#undef ENTITY_INSTANTIATE_COMPONENT
#undef ENTITY_INSTANTIATE_INSTANCE_FUNCTIONS