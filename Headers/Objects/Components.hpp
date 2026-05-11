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
#include "Headers/Map/MapQueries.hpp"
#include "EntityTypes.hpp"

enum ComponentType {
    CMP_TRANSFORM,
    CMP_SPRITE,
    CMP_PLAYER_SPAWN,
    CMP_DECAL,

    CMP_COUNT,
};

// Stores things related to the entity's whereabouts
// Every entity MUST have a transform component
struct ComponentTransform {
    EntityID ownerID = -1;

    Vector2 position = {.0f, .0f};
    int floor = 0;
    Vector2 scale = {32.0f, 32.0f};

    int sectorIndex = -1;

    float GetObjectBottomHeight(const std::vector<Sector>& sectors) {
        if (this->sectorIndex < 0 ||
            this->sectorIndex >= static_cast<int>(sectors.size())) {
            return 0.0f;
            }

        const Sector& sector = sectors[this->sectorIndex];

        const float sectorHeight = sector.ceilingHeight - sector.floorHeight;

        this->floor = std::clamp(
            this->floor,
            0,
            std::max(1, sector.floorCount) - 1
        );

        return sector.floorHeight + sectorHeight * static_cast<float>(floor);
    }
    void UpdateObjectSector(const std::vector<Sector>& sectors) {
        const int newSector = MapQueries::FindSectorContainingPoint(
            sectors,
            this->position
        );

        if (newSector == -1) {
            return;
        }

        this->sectorIndex = newSector;

        const Sector& sector = sectors[newSector];

        this->floor = std::clamp(
            this->floor,
            0,
            std::max(1, sector.floorCount) - 1
        );
    }
};

// Stores things related to the entity's visuals
struct ComponentSprite {
    EntityID ownerID = -1;

    int textureIndex;
};

struct ComponentPlayerSpawn {
    EntityID ownerID = -1;
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