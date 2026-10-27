//
// Created by berke on 10/26/2026.
//

#include "Headers/Runtime/EventSystem.hpp"
#include "Headers/Objects/Event.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/MathFunctions.hpp"
#include "Headers/Engine/GameTime.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <tuple>
#include <type_traits>
#include <unordered_map>

namespace {
    template <class... Ts>
    struct Overloaded : Ts... { using Ts::operator()...; };

    // Where a dispatch lives, resolved against the live level when an event
    // fires so the registry never holds stale copies.
    struct DispatchHandle {
        EventOwnerKind ownerKind;
        ID ownerID;
        size_t index;
    };

    // A wall with at least one ENTITY_CROSS event. `frontIsNormalSide` says
    // which side of the line its front sector is on.
    struct CrossWall {
        ID wallID;
        bool frontIsNormalSide;
    };

    // A wall color heading for `target` (SET_COLOR), advanced every Update.
    struct ColorFade {
        ID wallID;
        Vector3 target;
        float speed; // per channel, per second
    };

    // An entity's collider overlapping a linedef, seen from above.
    struct LinedefContact {
        ID entity;
        ID linedef;

        bool operator==(const LinedefContact&) const = default;
    };

    EventSystem::Hooks hooks;

    std::unordered_multimap<TagId, DispatchHandle> dispatchRegistry;
    std::vector<CrossWall> crossWalls;
    std::vector<ColorFade> colorFades;

    // Last frame's state, diffed against this frame's to find what changed.
    std::unordered_map<ID, Vector2> lastPositions; // entity -> transform (feet) x/z
    std::unordered_map<ID, ID> lastSectors;        // entity -> sector ID, INVALID_ID outside the map
    std::unordered_map<ID, bool> lastEnabled;
    std::vector<PhysicsSystem::CollisionPair> lastEntityContacts; // sorted, unique
    std::vector<PhysicsSystem::WallContact> lastWallContacts;     // sorted, unique
    std::vector<LinedefContact> lastLinedefContacts;              // sorted, unique

    bool operator<(const PhysicsSystem::CollisionPair& a, const PhysicsSystem::CollisionPair& b) {
        return std::tie(a.a, a.b) < std::tie(b.a, b.b);
    }

    bool operator<(const PhysicsSystem::WallContact& a, const PhysicsSystem::WallContact& b) {
        return std::tie(a.entity, a.wall) < std::tie(b.entity, b.wall);
    }

    bool operator<(const LinedefContact& a, const LinedefContact& b) {
        return std::tie(a.entity, a.linedef) < std::tie(b.entity, b.linedef);
    }

    template <typename T>
    void SortUnique(std::vector<T>& values) {
        std::ranges::sort(values, [](const T& a, const T& b) { return a < b; });
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }

    // The entries of `now` that aren't in `before`. Both sorted.
    template <typename T>
    std::vector<T> Started(const std::vector<T>& now, const std::vector<T>& before) {
        std::vector<T> started;
        std::ranges::set_difference(now, before, std::back_inserter(started),
                                    [](const T& a, const T& b) { return a < b; });
        return started;
    }

    // Entity maps keep IDs of destroyed entities; drop them once they pile up.
    template <typename Map>
    void PruneDestroyed(Map& map, const Level& level) {
        if (map.size() <= level.entities.size() + 64) return;
        std::erase_if(map, [&](const auto& entry) { return level.GetEntity(entry.first) == nullptr; });
    }

    const Dispatch* ResolveDispatch(const Level& level, const DispatchHandle& handle) {
        const std::vector<Dispatch>* dispatches = nullptr;

        switch (handle.ownerKind) {
            case EventOwnerKind::LINE:
                if (const Wall* wall = MapQueries::GetWallByID(level, handle.ownerID)) dispatches = &wall->dispatches;
                break;
            case EventOwnerKind::SECTOR:
                if (const Sector* sector = level.GetSector(handle.ownerID)) dispatches = &sector->dispatches;
                break;
            case EventOwnerKind::ENTITY:
                if (const Entity* entity = level.GetEntity(handle.ownerID)) dispatches = &entity->dispatches;
                break;
            case EventOwnerKind::LINEDEF:
                if (const Linedef* linedef = level.GetLinedef(handle.ownerID)) dispatches = &linedef->dispatches;
                break;
        }

        if (!dispatches || handle.index >= dispatches->size()) return nullptr;
        return &(*dispatches)[handle.index];
    }

    std::vector<Event>* ResolveEvents(Level& level, const EventOwnerKind kind, const ID ownerID) {
        switch (kind) {
            case EventOwnerKind::LINE: {
                Wall* wall = MapQueries::GetWallByID(level, ownerID);
                return wall ? &wall->events : nullptr;
            }
            case EventOwnerKind::SECTOR: {
                Sector* sector = level.GetSector(ownerID);
                return sector ? &sector->events : nullptr;
            }
            case EventOwnerKind::ENTITY: {
                Entity* entity = level.GetEntity(ownerID);
                return entity ? &entity->events : nullptr;
            }
            case EventOwnerKind::LINEDEF: {
                Linedef* linedef = level.GetLinedef(ownerID);
                return linedef ? &linedef->events : nullptr;
            }
        }
        return nullptr;
    }

    template <typename Payload>
    const TagFilter* GetTagFilter(const Payload& payload) {
        if constexpr (requires { payload.filter; }) return &payload.filter;
        else return nullptr;
    }

    bool PassesTagFilter(const TagFilter& filter, const std::vector<TagId>& tags) {
        if (filter.tagType == TagType::ALL) return true;

        const bool hasAnyTag = std::ranges::any_of(filter.tags, [&](const TagId tag) {
            return std::ranges::find(tags, tag) != tags.end();
        });
        return filter.tagType == TagType::INCLUDE ? hasAnyTag : !hasAnyTag;
    }

    bool IsEnabledEntity(const Level& level, const ID entityID) {
        const Entity* entity = level.GetEntity(entityID);
        return entity && entity->enabled;
    }

    std::vector<TagId> EntityTags(const Level& level, const ID entityID) {
        const Entity* entity = level.GetEntity(entityID);
        return entity ? entity->tagIds : std::vector<TagId>{};
    }

    ID SectorIDAt(const Level& level, const int sectorIndex) {
        if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) return INVALID_ID;
        return level.sectors[sectorIndex].id;
    }

    // `filterTags` is what the event's TagFilter is checked against.
    void Trigger(Level& level, Event& event, const ID instigatorID, const std::vector<TagId>& filterTags) {
        if (event.maxTriggers != 0 && event.triggerCount >= event.maxTriggers) return;

        const TagFilter* filter = std::visit([](const auto& p) { return GetTagFilter(p); }, event.payload);
        if (filter && !PassesTagFilter(*filter, filterTags)) return;

        // Like a failed chance roll, a failed condition doesn't use up a trigger.
        if (!level.CheckCondition(event.condition)) return;

        if (event.chance < 100.0f && MathFunctions::GetRandomFloat(0.0f, 100.0f) >= event.chance) return;

        ++event.triggerCount;

        // Dispatches can add/destroy things, which may move `event` or the
        // dispatch being run - copy what's needed instead of holding references.
        const auto [begin, end] = dispatchRegistry.equal_range(event.tagId);
        std::vector<DispatchHandle> handles;
        for (auto it = begin; it != end; ++it) handles.push_back(it->second);

        for (const DispatchHandle& handle : handles) {
            const Dispatch* dispatch = ResolveDispatch(level, handle);
            if (!dispatch) continue;

            const Dispatch copy = *dispatch;
            EventSystem::ExecuteDispatch(level, copy, handle.ownerKind, handle.ownerID, instigatorID);
        }
    }

    // Fires every `Payload` event of the owner that `accept` lets through. By
    // index, re-resolving the owner each time: a dispatch can add or remove
    // things and move the events vector.
    template <typename Payload, typename Accept>
    void FireEvents(Level& level, const EventOwnerKind kind, const ID ownerID, const ID instigatorID,
                    const std::vector<TagId>& filterTags, Accept&& accept) {
        for (size_t i = 0;; ++i) {
            std::vector<Event>* events = ResolveEvents(level, kind, ownerID);
            if (!events || i >= events->size()) return;

            Event& event = (*events)[i];
            const Payload* payload = std::get_if<Payload>(&event.payload);
            if (!payload || !accept(*payload)) continue;

            Trigger(level, event, instigatorID, filterTags);
        }
    }

    template <typename Payload>
    void FireEvents(Level& level, const EventOwnerKind kind, const ID ownerID, const ID instigatorID,
                    const std::vector<TagId>& filterTags) {
        FireEvents<Payload>(level, kind, ownerID, instigatorID, filterTags, [](const Payload&) { return true; });
    }

    struct Crossing {
        EventOwnerKind ownerKind; // LINE or LINEDEF
        ID lineID;
        ID entityID;
        bool forward; // front side -> back side
    };

    struct SectorChange {
        ID entityID;
        ID from;
        ID to;
    };

    void FireEnableEvents(Level& level) {
        std::vector<std::pair<ID, bool>> flipped;

        for (const Entity& entity : level.entities) {
            const auto [it, inserted] = lastEnabled.try_emplace(entity.id, entity.enabled);

            // Spawned since last frame: how it starts is the baseline.
            if (inserted || it->second == entity.enabled) continue;

            it->second = entity.enabled;
            flipped.emplace_back(entity.id, entity.enabled);
        }

        PruneDestroyed(lastEnabled, level);

        for (const auto& [entityID, enabled] : flipped) {
            const std::vector<TagId> tags = EntityTags(level, entityID);

            if (enabled) FireEvents<PayloadEntityEnable>(level, EventOwnerKind::ENTITY, entityID, entityID, tags);
            else FireEvents<PayloadEntityDisable>(level, EventOwnerKind::ENTITY, entityID, entityID, tags);
        }
    }

    template <typename Payload>
    bool HasEvent(const Linedef& linedef) {
        return std::ranges::any_of(linedef.events, [](const Event& event) {
            return std::holds_alternative<Payload>(event.payload);
        });
    }

    // Whether the feet moved from `from` to `to` through the segment
    // start -> end (`vector` = end - start).
    bool CrossesSegment(const Vector2& start, const Vector2& end, const Vector2& vector,
                        const Vector2& from, const Vector2& to, bool& endedOnNormalSide) {
        const float sideFrom = Vector2Math::Cross(vector, from - start);
        const float sideTo = Vector2Math::Cross(vector, to - start);

        if ((sideFrom < 0.0f) == (sideTo < 0.0f)) return false;

        // And the segment's ends are on either side of the path, so the path
        // goes through the segment rather than past its end.
        const Vector2 path = to - from;
        const float startSide = Vector2Math::Cross(path, start - from);
        const float endSide = Vector2Math::Cross(path, end - from);

        if (startSide * endSide > 0.0f) return false;

        endedOnNormalSide = sideTo >= 0.0f;
        return true;
    }

    bool CrossesWall(const Wall& wall, const Vector2& from, const Vector2& to, bool& endedOnNormalSide) {
        return CrossesSegment(wall.start, wall.end, wall.vector, from, to, endedOnNormalSide);
    }

    void FireMovementEvents(Level& level) {
        std::vector<Crossing> crossings;
        std::vector<SectorChange> sectorChanges;

        for (const ComponentTransform& transform : level.transforms.components) {
            // Only an entity's first transform places it.
            if (level.transforms.Get(transform.ownerID) != &transform) continue;

            const ID entityID = transform.ownerID;
            const Vector2 position = {transform.position.x, transform.position.z};
            const ID sectorID = SectorIDAt(level, transform.sectorIndex);

            const auto [positionIt, newPosition] = lastPositions.try_emplace(entityID, position);
            const auto [sectorIt, newSector] = lastSectors.try_emplace(entityID, sectorID);

            const Vector2 previousPosition = positionIt->second;
            const ID previousSector = sectorIt->second;

            positionIt->second = position;
            sectorIt->second = sectorID;

            // Still tracked while disabled, so enabling it again doesn't
            // count everything it passed through meanwhile.
            if (!IsEnabledEntity(level, entityID)) continue;

            if (!newSector && previousSector != sectorID)
                sectorChanges.push_back({entityID, previousSector, sectorID});

            // Compared rather than read from isDirty, so a move made after
            // the transform update (a TELEPORT dispatch) still counts.
            if (newPosition || (previousPosition.x == position.x && previousPosition.y == position.y)) continue;

            for (const CrossWall& crossWall : crossWalls) {
                const Wall* wall = MapQueries::GetWallByID(level, crossWall.wallID);
                if (!wall) continue;

                bool endedOnNormalSide = false;
                if (!CrossesWall(*wall, previousPosition, position, endedOnNormalSide)) continue;

                crossings.push_back({EventOwnerKind::LINE, crossWall.wallID, entityID, endedOnNormalSide != crossWall.frontIsNormalSide});
            }

            // Linedefs of the sector the entity is in, and on the frame it
            // changes sector also of the one it left, so a line crossed on
            // the way out isn't missed. Outside every sector: none.
            std::vector<const Linedef*> nearbyLinedefs;

            const auto addSectorLinedefs = [&](const ID id) {
                const Sector* sector = id != INVALID_ID ? level.GetSector(id) : nullptr;
                if (!sector) return;

                for (const Linedef* linedef : sector->linedefs)
                    if (HasEvent<PayloadLineEntityCross>(*linedef) && std::ranges::find(nearbyLinedefs, linedef) == nearbyLinedefs.end())
                        nearbyLinedefs.push_back(linedef);
            };

            addSectorLinedefs(sectorID);
            if (!newSector && previousSector != sectorID) addSectorLinedefs(previousSector);

            // A linedef's front is its normal side (where the editor draws
            // its tick: right of start -> end on screen), so ending on the
            // other side is forward.
            for (const Linedef* linedef : nearbyLinedefs) {
                bool endedOnNormalSide = false;
                if (!CrossesSegment(linedef->start, linedef->end, linedef->vector, previousPosition, position, endedOnNormalSide)) continue;

                crossings.push_back({EventOwnerKind::LINEDEF, linedef->id, entityID, !endedOnNormalSide});
            }
        }

        PruneDestroyed(lastPositions, level);
        PruneDestroyed(lastSectors, level);

        for (const Crossing& crossing : crossings) {
            FireEvents<PayloadLineEntityCross>(
                level, crossing.ownerKind, crossing.lineID, crossing.entityID, EntityTags(level, crossing.entityID),
                [&](const PayloadLineEntityCross& cross) {
                    switch (cross.direction) {
                        case PayloadLineEntityCross::FORWARD: return crossing.forward;
                        case PayloadLineEntityCross::BACKWARD: return !crossing.forward;
                        case PayloadLineEntityCross::EITHER: return true;
                    }
                    return true;
                });
        }

        for (const SectorChange& change : sectorChanges) {
            const std::vector<TagId> tags = EntityTags(level, change.entityID);

            // Exit first, so nothing sees the entity in two sectors.
            if (change.from != INVALID_ID)
                FireEvents<PayloadSectorEntityExit>(level, EventOwnerKind::SECTOR, change.from, change.entityID, tags);
            if (change.to != INVALID_ID)
                FireEvents<PayloadSectorEntityEnter>(level, EventOwnerKind::SECTOR, change.to, change.entityID, tags);

            // Filtered by the tags of the sector entered.
            const Sector* newSector = change.to != INVALID_ID ? level.GetSector(change.to) : nullptr;
            const std::vector<TagId> sectorTags = newSector ? newSector->tagIds : std::vector<TagId>{};

            if (IsEnabledEntity(level, change.entityID))
                FireEvents<PayloadEntityChangeSector>(level, EventOwnerKind::ENTITY, change.entityID, change.entityID, sectorTags);
        }
    }

    // Moves `value` toward `target` by at most `step`. True once it's there.
    bool StepToward(float& value, const float target, const float step) {
        value += std::clamp(target - value, -step, step);
        return value == target;
    }

    void UpdateColorFades(Level& level, const float deltaTime) {
        std::erase_if(colorFades, [&](const ColorFade& fade) {
            Wall* wall = MapQueries::GetWallByID(level, fade.wallID);
            if (!wall) return true;

            const float step = fade.speed * deltaTime;
            const bool x = StepToward(wall->color.x, fade.target.x, step);
            const bool y = StepToward(wall->color.y, fade.target.y, step);
            const bool z = StepToward(wall->color.z, fade.target.z, step);
            return x && y && z;
        });
    }

    void FireContactEvents(Level& level, const PhysicsSystem::Contacts& contacts) {
        // Trigger overlaps count as entity collisions too.
        std::vector<PhysicsSystem::CollisionPair> entityContacts = contacts.collisions;
        entityContacts.insert(entityContacts.end(), contacts.triggers.begin(), contacts.triggers.end());
        SortUnique(entityContacts);

        std::vector<PhysicsSystem::WallContact> wallContacts = contacts.walls;
        SortUnique(wallContacts);

        // Only contacts that started this frame fire.
        const std::vector<PhysicsSystem::CollisionPair> startedEntity = Started(entityContacts, lastEntityContacts);
        const std::vector<PhysicsSystem::WallContact> startedWall = Started(wallContacts, lastWallContacts);

        lastEntityContacts = std::move(entityContacts);
        lastWallContacts = std::move(wallContacts);

        for (const auto& [a, b] : startedEntity) {
            if (!IsEnabledEntity(level, a) || !IsEnabledEntity(level, b)) continue;

            FireEvents<PayloadEntityCollide>(level, EventOwnerKind::ENTITY, a, b, EntityTags(level, b));

            // `a`'s dispatches may have disabled either of them.
            if (IsEnabledEntity(level, a) && IsEnabledEntity(level, b))
                FireEvents<PayloadEntityCollide>(level, EventOwnerKind::ENTITY, b, a, EntityTags(level, a));
        }

        for (const auto& [entityID, wallID] : startedWall) {
            if (!IsEnabledEntity(level, entityID)) continue;

            FireEvents<PayloadLineEntityCollide>(level, EventOwnerKind::LINE, wallID, entityID, EntityTags(level, entityID));
        }
    }

    // Liang-Barsky: does any part of segment a-b lie in the box?
    bool SegmentTouchesBox(const Vector2& a, const Vector2& b, const Vector2& boxMin, const Vector2& boxMax) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;

        const float p[4] = {-dx, dx, -dy, dy};
        const float q[4] = {a.x - boxMin.x, boxMax.x - a.x, a.y - boxMin.y, boxMax.y - a.y};

        float t0 = 0.0f;
        float t1 = 1.0f;

        for (int i = 0; i < 4; ++i) {
            if (p[i] == 0.0f) {
                if (q[i] < 0.0f) return false;
                continue;
            }

            const float t = q[i] / p[i];

            if (p[i] < 0.0f) {
                if (t > t1) return false;
                t0 = std::max(t0, t);
            }
            else {
                if (t < t0) return false;
                t1 = std::min(t1, t);
            }
        }

        return true;
    }

    // Whether the collider, seen from above at any height, overlaps the line.
    bool ColliderTouchesLine(const ComponentCollider& collider, const ComponentTransform& transform,
                             const Vector2& start, const Vector2& end) {
        const Vector3 base = collider.Base(transform);
        const Vector2 centre = {base.x, base.z};

        if (collider.IsRound()) {
            const float radius = collider.Radius(transform);
            const Vector2 line = end - start;
            const float lengthSq = Vector2Math::Dot(line, line);
            const float t = lengthSq > 0.0f ? std::clamp(Vector2Math::Dot(centre - start, line) / lengthSq, 0.0f, 1.0f) : 0.0f;
            const Vector2 closest = start + line * t;

            return Vector2Math::DistanceSquared(centre, closest) <= radius * radius;
        }

        const Vector3 size = collider.WorldSize(transform);
        const Vector2 half = {size.x * 0.5f, size.z * 0.5f};
        return SegmentTouchesBox(start, end, centre - half, centre + half);
    }

    // Linedefs don't collide, so their COLLIDE events come from overlaps
    // found here rather than from physics. Like a wall: first touch only,
    // re-armed once the entity is clear of the line again.
    void FireLinedefContactEvents(Level& level) {
        std::vector<LinedefContact> contacts;

        // Only the linedefs of the sector the entity is in: one just over
        // the edge in the next sector is never touched.
        for (const ComponentCollider& collider : level.colliders.ActiveColliders()) {
            const ComponentTransform* transform = level.transforms.Get(collider.ownerID);
            if (!transform) continue;

            if (transform->sectorIndex < 0 || transform->sectorIndex >= static_cast<int>(level.sectors.size())) continue;

            for (const Linedef* linedef : level.sectors[transform->sectorIndex].linedefs) {
                if (!HasEvent<PayloadLineEntityCollide>(*linedef)) continue;
                if (!ColliderTouchesLine(collider, *transform, linedef->start, linedef->end)) continue;

                contacts.push_back({collider.ownerID, linedef->id});
            }
        }

        SortUnique(contacts);

        const std::vector<LinedefContact> started = Started(contacts, lastLinedefContacts);
        lastLinedefContacts = std::move(contacts);

        for (const auto& [entityID, linedefID] : started) {
            if (!IsEnabledEntity(level, entityID)) continue;

            FireEvents<PayloadLineEntityCollide>(level, EventOwnerKind::LINEDEF, linedefID, entityID, EntityTags(level, entityID));
        }
    }
}

namespace EventSystem {
    void CreateRegistry(const Level& level) {
        dispatchRegistry.clear();
        crossWalls.clear();

        const auto add = [](const EventOwnerKind kind, const ID ownerID, const std::vector<Dispatch>& dispatches) {
            for (size_t i = 0; i < dispatches.size(); ++i)
                dispatchRegistry.emplace(dispatches[i].tagId, DispatchHandle{kind, ownerID, i});
        };

        for (const Wall& wall : level.walls) add(EventOwnerKind::LINE, wall.id, wall.dispatches);
        for (const Linedef& linedef : level.linedefs) add(EventOwnerKind::LINEDEF, linedef.id, linedef.dispatches);
        for (const Sector& sector : level.sectors) add(EventOwnerKind::SECTOR, sector.id, sector.dispatches);
        for (const Entity& entity : level.entities) add(EventOwnerKind::ENTITY, entity.id, entity.dispatches);

        for (const Wall& wall : level.walls) {
            const bool hasCross = std::ranges::any_of(wall.events, [](const Event& event) {
                return std::holds_alternative<PayloadLineEntityCross>(event.payload);
            });
            if (!hasCross) continue;

            // Which side is front: whichever sector is just off the middle of
            // the wall on its normal side. Normal side if neither matches.
            const Vector2 middle = (wall.start + wall.end) * 0.5f;
            const Vector2 probe = middle + wall.normal * 0.01f;
            const ID probeSector = SectorIDAt(level, MapQueries::FindSectorContainingPoint(level.sectors, probe));

            crossWalls.push_back({wall.id, probeSector != wall.backSector || wall.backSector == INVALID_ID});
        }
    }

    void Start(Level& level, Hooks newHooks) {
        Stop();
        hooks = std::move(newHooks);
        MapQueries::AssignLinedefsToSectors(level);
        CreateRegistry(level);

        // Counts are runtime only: every play session starts from 0.
        const auto resetCounts = [](std::vector<Event>& events) {
            for (Event& event : events) event.triggerCount = 0;
        };
        for (Wall& wall : level.walls) resetCounts(wall.events);
        for (Linedef& linedef : level.linedefs) resetCounts(linedef.events);
        for (Sector& sector : level.sectors) resetCounts(sector.events);
        for (Entity& entity : level.entities) resetCounts(entity.events);

        for (const Entity& entity : level.entities) lastEnabled[entity.id] = entity.enabled;

        for (const ComponentTransform& transform : level.transforms.components) {
            if (level.transforms.Get(transform.ownerID) != &transform) continue;

            lastPositions[transform.ownerID] = {transform.position.x, transform.position.z};
            lastSectors[transform.ownerID] = SectorIDAt(level, transform.sectorIndex);
        }
    }

    void Update(Level& level, const PhysicsSystem::Contacts& contacts) {
        UpdateColorFades(level, GameTime::deltaTime);

        FireEnableEvents(level);
        FireMovementEvents(level);
        FireContactEvents(level, contacts);
        FireLinedefContactEvents(level);
    }

    void Stop() {
        hooks = {};
        dispatchRegistry.clear();
        crossWalls.clear();
        colorFades.clear();
        lastPositions.clear();
        lastSectors.clear();
        lastEnabled.clear();
        lastEntityContacts.clear();
        lastWallContacts.clear();
        lastLinedefContacts.clear();
    }

    void ExecuteDispatch(Level& level, const Dispatch& dispatch, const EventOwnerKind ownerKind, const ID ownerID,
                         const ID instigatorID) {
        // A dispatch for another kind of owner (a sector move on an entity)
        // would use the owner's ID as the wrong kind of thing.
        if (!CanAttachTo(dispatch.payload, ownerKind)) {
            spdlog::warn("Dispatch '{}' is attached to the wrong kind of object (owner {}), skipped",
                         GetTranslationKey(dispatch.payload), ownerID);
            return;
        }

        const auto moveSector = [&](const int floorIndex, const float speed, const auto& start) {
            Sector* sector = level.GetSector(ownerID);
            if (!sector || floorIndex < 0 || floorIndex >= static_cast<int>(sector->floors.size())) return;

            if (speed <= 0.0f) {
                spdlog::warn("Sector move dispatch on sector {} has a speed of {}; it must be above 0", ownerID, speed);
                return;
            }

            start(sector->floors[floorIndex]);
        };

        std::visit(Overloaded{
            [&](const PayloadDispatchLineSetColor& setColor) {
                Wall* wall = MapQueries::GetWallByID(level, ownerID);
                if (!wall) return;

                // A new color replaces a fade still running on the wall.
                std::erase_if(colorFades, [&](const ColorFade& fade) { return fade.wallID == ownerID; });

                if (setColor.speed <= 0.0f) {
                    wall->color.x = setColor.color.x;
                    wall->color.y = setColor.color.y;
                    wall->color.z = setColor.color.z;
                }
                else colorFades.push_back({ownerID, setColor.color, setColor.speed});
            },
            [&](const PayloadDispatchSectorMoveFloorToCeiling& move) {
                moveSector(move.floorIndex, move.speed, [&](SectorFloor& floor) {
                    floor.MoveFloorToCeiling(move.speed, move.gap, move.collisionType);
                });
            },
            [&](const PayloadDispatchSectorMoveCeilingToFloor& move) {
                moveSector(move.floorIndex, move.speed, [&](SectorFloor& floor) {
                    floor.MoveCeilingToFloor(move.speed, move.gap, move.collisionType);
                });
            },
            [&](const PayloadDispatchEntityDestroy&) {
                if (hooks.destroyEntity) hooks.destroyEntity(ownerID);
            },
            [&](const PayloadDispatchEntityTeleport& teleport) {
                ComponentTransform* transform = level.transforms.Get(ownerID);
                if (!transform) return;

                // Velocity is kept, as with a script's SetPosition.
                transform->SetPosition(teleport.position);
                transform->rotation = teleport.rotation.Normalized();
                transform->isDirty = true;

                // The frame's sector update has already run.
                transform->UpdateObjectSectorAndFloor(level.sectors);
            },
            [&](const PayloadDispatchEntityDisable&) {
                if (Entity* entity = level.GetEntity(ownerID)) entity->enabled = false;
            },
            [&](const PayloadDispatchEntityEnable&) {
                if (Entity* entity = level.GetEntity(ownerID)) entity->enabled = true;
            },
            [&](const PayloadDispatchEntityCallFunction& call) {
                if (hooks.callFunction && !call.functionName.empty())
                    hooks.callFunction(level, ownerID, call.functionName, instigatorID);
            },
            [&](const PayloadDispatchChangeVariable& change) {
                if (change.variable == 0) return;

                float* value = level.GetEventVariableValue(change.variable);
                if (!value) {
                    spdlog::warn("Change Variable dispatch on owner {} uses a variable that no longer exists, skipped", ownerID);
                    return;
                }

                switch (change.operation) {
                    case PayloadDispatchChangeVariable::SET: *value = change.value; break;
                    case PayloadDispatchChangeVariable::ADD: *value += change.value; break;
                    case PayloadDispatchChangeVariable::SUBTRACT: *value -= change.value; break;
                }
            },
        }, dispatch.payload);
    }

    void TriggerEvent(Level& level, Event& event, const ID instigatorID) {
        Trigger(level, event, instigatorID, EntityTags(level, instigatorID));
    }
}
