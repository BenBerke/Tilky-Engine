//
// Created by berke on 5/2/2026.
//

#ifndef WOLFY_ENGINE_COMPONENTS_HPP
#define WOLFY_ENGINE_COMPONENTS_HPP
#include <algorithm>
#include <cstdint>
#include <vector>

#include "../Math/Vector/Vector2.hpp"
#include "../Objects/Sector.hpp"
#include "Headers/Map/MapQueries.hpp"

using EntityID = uint32_t;

struct ComponentTransform {
    EntityID ownerID = -1;

    Vector2 position = {.0f, .0f};
    int sectorIndex = -1;
    int floor = 0;
    Vector2 scale = {32.0f, 32.0f};

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

struct ComponentSprite {
    EntityID ownerID = -1;

    int textureIndex;
};

struct ComponentPlayerSpawn {
    EntityID ownerID = -1;
};

struct ComponentDecal {
    EntityID ownerID = -1;

    int wallIndex = -1;

    float zOffset = 0; // Vertical Position
    float wallOffset = -1.0f; // Horizontal position
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

#endif //WOLFY_ENGINE_COMPONENTS_HPP