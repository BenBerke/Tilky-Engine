//
// Created by berke on 10/12/2026.
//

#ifndef TILKY_ENGINE_VECTORINDEXCACHE_HPP
#define TILKY_ENGINE_VECTORINDEXCACHE_HPP

#include <cstddef>
#include <unordered_map>
#include <vector>

// Key -> index into a vector whose elements carry their own unique key (an
// entity's id, a script's instanceID), for lookups that used to scan the
// whole vector every call.
//
// Never trusted blindly: a cached index only counts if the element there
// still has that key. On a miss it falls back to the scan and rebuilds, so
// code that edits the vector directly (the level loader, erase_if, undo)
// costs one rebuild instead of going wrong. Owners only need to Clear() or
// Set() where it's cheap, to skip that rebuild.
template<typename Key>
class VectorIndexCache {
public:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    // Index of the first element whose keyOf(element) == key, or npos.
    template<typename T, typename KeyOf>
    std::size_t Find(const std::vector<T>& items, const Key key, KeyOf keyOf) const {
        if (const auto it = indexByKey.find(key);
            it != indexByKey.end() && it->second < items.size() && keyOf(items[it->second]) == key)
            return it->second;

        for (std::size_t i = 0; i < items.size(); ++i) {
            if (keyOf(items[i]) != key) continue;

            Rebuild(items, keyOf);
            return i;
        }

        return npos;
    }

    // The element at `index` (just appended) has key `key`.
    void Set(const Key key, const std::size_t index) { indexByKey[key] = index; }

    // After an erase or reorder; the next lookup rebuilds.
    void Clear() { indexByKey.clear(); }

private:
    template<typename T, typename KeyOf>
    void Rebuild(const std::vector<T>& items, KeyOf keyOf) const {
        indexByKey.clear();
        indexByKey.reserve(items.size());

        // try_emplace: the first element with a key wins, same as the scan.
        for (std::size_t i = 0; i < items.size(); ++i) indexByKey.try_emplace(keyOf(items[i]), i);
    }

    mutable std::unordered_map<Key, std::size_t> indexByKey;
};

#endif //TILKY_ENGINE_VECTORINDEXCACHE_HPP
