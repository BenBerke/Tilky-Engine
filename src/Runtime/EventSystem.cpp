//
// Created by berke on 10/26/2026.
//

#include "Headers/Runtime/EventSystem.hpp"
#include "Headers/Objects/Event.hpp"
#include "Headers/Objects/EventFields.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/MathFunctions.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Engine/InputManager.hpp"
#include "Headers/Runtime/Sound/SoundManager.hpp"
#include "Headers/Runtime/Sound/MusicPlayer.hpp"
#include "Headers/Runtime/Gameplay/PathfinderSystem.hpp"
#include "Headers/Runtime/Gameplay/MonsterAISystem.hpp"
#include "Headers/TagRegistry.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

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

    // Where an event lives (Timer and Condition Met are checked every frame).
    struct EventHandle {
        EventOwnerKind ownerKind;
        ID ownerID;
        size_t index;

        bool operator==(const EventHandle&) const = default;
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

    // A dispatch waiting out its delay.
    struct PendingDispatch {
        DispatchHandle handle;
        ID instigatorID;
        float remaining; // seconds
        bool realTime; // counted in real seconds (a UI Button event's chain)
    };

    // A flow step of a dispatch that ran, waiting out its wait.
    struct PendingFlow {
        DispatchFlow flow;
        EventOwnerKind callerKind; // the dispatch's object (the call's Self)
        ID callerID;
        ID instigatorID;
        float remaining; // seconds
        bool realTime;
        uint64_t firstUpdate; // runs no earlier than this Update (the next frame's)
    };

    // A dispatch's cooldown: calls since the last one started, and how long
    // the running one has left.
    struct CooldownState {
        int calls = 0;
        float remaining = 0.0f; // seconds
        bool realTime = false; // the running one counts real seconds
    };

    // DispatchHandle as a map key.
    using DispatchKey = std::tuple<uint8_t, ID, size_t>;

    struct TimerState {
        EventHandle event;
        float elapsed = 0.0f;
        bool firedOnce = false; // the first wait is firstDelay, the rest interval
    };

    struct ConditionWatch {
        EventHandle event;
        bool wasMet = false;
    };

    // How long an entity has been in the sector it is in (Stay in Sector).
    struct StayClock {
        ID sectorID;
        float elapsed;
    };

    // What a typed ray hit, waiting for the next Update.
    struct RayReport {
        RayType type;
        ID casterID;
        EventOwnerKind ownerKind;
        ID ownerID;
        bool fromFront; // lines only: the ray came from the front side
    };

    // One caster's ray of one type on one owner, for First Hit Only.
    using RayKey = std::tuple<ID, uint8_t, uint8_t, ID>;

    // A Door dispatch on one of a sector's floors, from its first trigger
    // until its cycle ends. Open/Close and Stay and Toggle doors stay STILL
    // afterwards: they remember where they are.
    struct DoorState {
        enum Phase { OPENING, WAITING, CLOSING, STILL };

        ID sectorID;
        int floorIndex;
        PayloadDispatchSectorDoor settings;
        float closedCeiling, openCeiling;
        float closedFloor, openFloor; // split doors only
        Phase phase = OPENING;
        bool open = false; // WAITING / STILL: where it waits
        float waitLeft = 0.0f;
    };

    // A Move To dispatch gliding an entity.
    struct EntityMove {
        ID entityID;
        Vector3 from, to;
        bool rotate;
        Quaternion rotationFrom, rotationTo;
        float elapsed;
        float duration;
    };

    struct ShownMessage {
        EventSystem::Message message;
        float age; // seconds on screen so far
        float duration;
    };

    constexpr int MAX_RELAY_DEPTH = 32;

    EventSystem::Hooks hooks;
    bool running = false;

    std::unordered_multimap<TagId, DispatchHandle> dispatchRegistry;

    // Dispatch names as numbers (from 1). Only grows during a play session,
    // so a call resolved before a registry rebuild keeps its number.
    std::unordered_map<std::string, uint32_t> dispatchNameIDs;

    // Owner + name number -> that owner's dispatches with the name.
    std::unordered_map<uint64_t, std::vector<DispatchHandle>> namedDispatches;

    uint64_t NamedDispatchKey(const EventOwnerKind kind, const ID ownerID, const uint32_t nameID) {
        return (static_cast<uint64_t>(kind) << 56) | (static_cast<uint64_t>(ownerID) << 24) | (nameID & 0xFFFFFFu);
    }
    std::vector<CrossWall> crossWalls;
    std::vector<ColorFade> colorFades;
    std::vector<PendingDispatch> pendingDispatches;
    std::vector<PendingFlow> pendingFlows;
    uint64_t updateCount = 0; // Updates started this session
    bool inUpdate = false;    // EventSystem::Update is running
    std::map<DispatchKey, CooldownState> cooldowns;
    std::vector<TimerState> timers;
    std::vector<ConditionWatch> conditionWatches;
    std::unordered_set<ID> staySectors; // sectors with a Stay in Sector event
    std::unordered_map<ID, StayClock> stayClocks; // entity -> clock
    std::vector<RayReport> rayReports;
    std::vector<RayKey> lastRayKeys; // sorted, unique
    std::vector<DoorState> doors;
    std::vector<EntityMove> entityMoves;
    std::vector<std::string> soundSources; // temporary sources of Play Sound
    uint64_t nextSoundSource = 0;
    std::vector<ShownMessage> shownMessages; // oldest first
    MessageStyle savedMessageStyle; // the level's, as it started (Game.MessageBox.Reset)
    uint64_t nextMessageID = 1;
    int relayDepth = 0;

    // True while a UI Button event's dispatches run: their delays and
    // cooldowns count real time, so buttons work while the game is paused.
    bool realTimeChain = false;

    // Set by a dispatch that was refused (a locked door or lift) while it
    // runs, so its flow doesn't go on.
    bool dispatchRefused = false;

    // Sets realTimeChain for a scope, then puts it back.
    struct RealTimeChainScope {
        bool previous;
        explicit RealTimeChainScope(const bool realTime) : previous(realTimeChain) { realTimeChain = realTime; }
        ~RealTimeChainScope() { realTimeChain = previous; }
        RealTimeChainScope(const RealTimeChainScope&) = delete;
        RealTimeChainScope& operator=(const RealTimeChainScope&) = delete;
    };

    // Last frame's state, diffed against this frame's to find what changed.
    std::unordered_map<ID, Vector2> lastPositions; // entity -> transform (feet) x/z
    std::unordered_map<ID, ID> lastSectors;        // entity -> sector ID, INVALID_ID outside the map
    std::unordered_map<ID, bool> lastEnabled;

    // Land-lock: entity -> the sector a teleport put it in. Teleport Entities
    // Inside leaves it alone until it has left that sector, so two
    // teleporters pointing at each other don't bounce it back every frame.
    std::unordered_map<ID, ID> teleportLocks;
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

    const std::vector<Dispatch>* ResolveDispatches(const Level& level, const EventOwnerKind kind, const ID ownerID) {
        switch (kind) {
            case EventOwnerKind::LINE: {
                const Wall* wall = MapQueries::GetWallByID(level, ownerID);
                return wall ? &wall->dispatches : nullptr;
            }
            case EventOwnerKind::SECTOR: {
                const Sector* sector = level.GetSector(ownerID);
                return sector ? &sector->dispatches : nullptr;
            }
            case EventOwnerKind::ENTITY: {
                const Entity* entity = level.GetEntity(ownerID);
                return entity ? &entity->dispatches : nullptr;
            }
            case EventOwnerKind::LINEDEF: {
                const Linedef* linedef = level.GetLinedef(ownerID);
                return linedef ? &linedef->dispatches : nullptr;
            }
            case EventOwnerKind::LEVEL:
                return &level.worldSettings.dispatches;
        }
        return nullptr;
    }

    const Dispatch* ResolveDispatch(const Level& level, const DispatchHandle& handle) {
        const std::vector<Dispatch>* dispatches = ResolveDispatches(level, handle.ownerKind, handle.ownerID);
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
            case EventOwnerKind::LEVEL:
                return &level.worldSettings.events;
        }
        return nullptr;
    }

    Event* ResolveEvent(Level& level, const EventHandle& handle) {
        std::vector<Event>* events = ResolveEvents(level, handle.ownerKind, handle.ownerID);
        if (!events || handle.index >= events->size()) return nullptr;
        return &(*events)[handle.index];
    }

    // Calls f(kind, ownerID, events) for every owner in the level.
    template <typename F>
    void ForEachEventOwner(const Level& level, F&& f) {
        for (const Wall& wall : level.walls) f(EventOwnerKind::LINE, wall.id, wall.events);
        for (const Linedef& linedef : level.linedefs) f(EventOwnerKind::LINEDEF, linedef.id, linedef.events);
        for (const Sector& sector : level.sectors) f(EventOwnerKind::SECTOR, sector.id, sector.events);
        for (const Entity& entity : level.entities) f(EventOwnerKind::ENTITY, entity.id, entity.events);
        f(EventOwnerKind::LEVEL, ID{0}, level.worldSettings.events);
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

    // Disabled entities and linedefs are inert: their events don't fire.
    bool IsOwnerActive(const Level& level, const EventOwnerKind kind, const ID ownerID) {
        if (kind == EventOwnerKind::ENTITY) return IsEnabledEntity(level, ownerID);

        if (kind == EventOwnerKind::LINEDEF) {
            const Linedef* linedef = level.GetLinedef(ownerID);
            return linedef && linedef->enabled;
        }

        return true;
    }

    std::vector<TagId> EntityTags(const Level& level, const ID entityID) {
        const Entity* entity = level.GetEntity(entityID);
        return entity ? entity->tagIds : std::vector<TagId>{};
    }

    ID SectorIDAt(const Level& level, const int sectorIndex) {
        if (sectorIndex < 0 || sectorIndex >= static_cast<int>(level.sectors.size())) return INVALID_ID;
        return level.sectors[sectorIndex].id;
    }

    bool SideMatches(const LineSide side, const bool front) {
        switch (side) {
            case LineSide::FRONT: return front;
            case LineSide::BACK: return !front;
            case LineSide::EITHER: return true;
        }
        return true;
    }

    // Which side of the wall its front sector is on: whichever sector is
    // just off the middle of the wall on its normal side. Normal side if
    // neither matches.
    bool WallFrontIsNormalSide(const Level& level, const Wall& wall) {
        const Vector2 middle = (wall.start + wall.end) * 0.5f;
        const Vector2 probe = middle + wall.normal * 0.01f;
        const ID probeSector = SectorIDAt(level, MapQueries::FindSectorContainingPoint(level.sectors, probe));

        return probeSector != wall.backSector || wall.backSector == INVALID_ID;
    }

    bool OnNormalSide(const Vector2& start, const Vector2& vector, const Vector2& point) {
        return Vector2Math::Cross(vector, point - start) >= 0.0f;
    }

    // Whether `point` is on the wall's front (its front sector's side).
    bool OnWallFront(const Level& level, const Wall& wall, const Vector2& point) {
        return OnNormalSide(wall.start, wall.vector, point) == WallFrontIsNormalSide(level, wall);
    }

    // A linedef's front is its normal side (the editor's tick).
    bool OnLinedefFront(const Linedef& linedef, const Vector2& point) {
        return OnNormalSide(linedef.start, linedef.vector, point);
    }

    std::optional<Vector2> EntityPosition2D(const Level& level, const ID entityID) {
        const ComponentTransform* transform = level.transforms.Get(entityID);
        if (!transform) return std::nullopt;
        return Vector2{transform->position.x, transform->position.z};
    }

    // Whether a call to the dispatch goes through: not while its cooldown runs.
    // Counts the call and starts the cooldown on every cooldownTriggers-th one.
    bool PassesCooldown(const Dispatch& dispatch, const DispatchHandle& handle) {
        if (dispatch.cooldown <= 0.0f) return true;

        CooldownState& state = cooldowns[{static_cast<uint8_t>(handle.ownerKind), handle.ownerID, handle.index}];
        if (state.remaining > 0.0f) return false;

        if (++state.calls >= std::max(1, dispatch.cooldownTriggers)) {
            state.calls = 0;
            state.remaining = dispatch.cooldown;
            state.realTime = realTimeChain;
        }
        return true;
    }

    void TickCooldowns(const float deltaTime, const float realDeltaTime) {
        std::erase_if(cooldowns, [&](auto& entry) {
            CooldownState& state = entry.second;
            state.remaining = std::max(0.0f, state.remaining - (state.realTime ? realDeltaTime : deltaTime));
            return state.remaining <= 0.0f && state.calls == 0;
        });
    }

    // Runs the dispatches that aren't cooling down, now or after their delay.
    void RunHandles(Level& level, const std::vector<DispatchHandle>& handles, const ID instigatorID) {
        if (relayDepth >= MAX_RELAY_DEPTH) {
            spdlog::warn("Dispatches skipped: relays and calls went {} deep, they probably loop", MAX_RELAY_DEPTH);
            return;
        }

        ++relayDepth;

        for (const DispatchHandle& handle : handles) {
            const Dispatch* dispatch = ResolveDispatch(level, handle);
            if (!dispatch || !PassesCooldown(*dispatch, handle)) continue;

            if (dispatch->delay > 0.0f) {
                pendingDispatches.push_back({handle, instigatorID, dispatch->delay, realTimeChain});
                continue;
            }

            const Dispatch copy = *dispatch;
            EventSystem::ExecuteDispatch(level, copy, handle.ownerKind, handle.ownerID, instigatorID);
        }

        --relayDepth;
    }

    // Runs every dispatch listening on `tag`.
    void RunDispatches(Level& level, const TagId tag, const ID instigatorID) {
        // Dispatches can add/destroy things, which may move the dispatch
        // being run - copy the handles instead of holding references.
        const auto [begin, end] = dispatchRegistry.equal_range(tag);
        std::vector<DispatchHandle> handles;
        for (auto it = begin; it != end; ++it) handles.push_back(it->second);

        RunHandles(level, handles, instigatorID);
    }

    // Runs the dispatches named by `call` on its target(s). `callerKind` /
    // `callerID` is the object the event or relay is on (Self).
    void RunCall(Level& level, const DirectCall& call, const EventOwnerKind callerKind, const ID callerID,
                 const ID instigatorID) {
        // Normally resolved with the registry; one built before the call
        // existed is looked up once here and kept.
        if (call.nameID == 0 && !call.dispatch.empty()) {
            const auto it = dispatchNameIDs.find(call.dispatch);
            if (it != dispatchNameIDs.end()) call.nameID = it->second;
        }
        if (call.nameID == 0) return; // nothing in the level has that name

        std::vector<std::pair<EventOwnerKind, ID>> targets;

        switch (call.target) {
            case CallTarget::SELF: targets.emplace_back(callerKind, callerID); break;
            case CallTarget::INSTIGATOR:
                if (instigatorID != INVALID_ID) targets.emplace_back(EventOwnerKind::ENTITY, instigatorID);
                break;
            case CallTarget::ENTITY: targets.emplace_back(EventOwnerKind::ENTITY, call.targetID); break;
            case CallTarget::SECTOR: targets.emplace_back(EventOwnerKind::SECTOR, call.targetID); break;
            case CallTarget::WALL: targets.emplace_back(EventOwnerKind::LINE, call.targetID); break;
            case CallTarget::LINEDEF: targets.emplace_back(EventOwnerKind::LINEDEF, call.targetID); break;
            case CallTarget::TAGGED: {
                const auto has = [&](const std::vector<TagId>& tags) { return std::ranges::find(tags, call.targetTag) != tags.end(); };
                for (const Entity& entity : level.entities) if (has(entity.tagIds)) targets.emplace_back(EventOwnerKind::ENTITY, entity.id);
                for (const Sector& sector : level.sectors) if (has(sector.tagIds)) targets.emplace_back(EventOwnerKind::SECTOR, sector.id);
                for (const Wall& wall : level.walls) if (has(wall.tagIds)) targets.emplace_back(EventOwnerKind::LINE, wall.id);
                break;
            }
        }

        std::vector<DispatchHandle> handles;
        for (const auto& [kind, id] : targets) {
            const auto it = namedDispatches.find(NamedDispatchKey(kind, id, call.nameID));
            if (it != namedDispatches.end()) handles.insert(handles.end(), it->second.begin(), it->second.end());
        }

        if (!handles.empty()) RunHandles(level, handles, instigatorID);
    }

    // `filterTags` is what the event's TagFilter is checked against.
    void Trigger(Level& level, Event& event, const EventOwnerKind ownerKind, const ID ownerID, const ID instigatorID,
                 const std::vector<TagId>& filterTags) {
        if (event.maxTriggers != 0 && event.triggerCount >= event.maxTriggers) return;

        const TagFilter* filter = std::visit([](const auto& p) { return GetTagFilter(p); }, event.payload);
        if (filter && !PassesTagFilter(*filter, filterTags)) return;

        // Like a failed chance roll, a failed condition doesn't use up a trigger.
        if (!level.CheckCondition(event.condition)) return;

        if (event.chance < 100.0f && MathFunctions::GetRandomFloat(0.0f, 100.0f) >= event.chance) return;

        ++event.triggerCount;

        // `event` may move once dispatches run.
        const bool useTag = event.useTag;
        const TagId tag = event.tagId;
        const std::vector<DirectCall> calls = event.calls;

        if (useTag) RunDispatches(level, tag, instigatorID);
        for (const DirectCall& call : calls) RunCall(level, call, ownerKind, ownerID, instigatorID);
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

            Trigger(level, event, kind, ownerID, instigatorID, filterTags);
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
        std::vector<ID> spawned;

        for (const Entity& entity : level.entities) {
            const auto [it, inserted] = lastEnabled.try_emplace(entity.id, entity.enabled);

            // Spawned since last frame: how it starts is the baseline, and
            // it gets its On Start (scripts' Start has run by now).
            if (inserted && entity.enabled) spawned.push_back(entity.id);
            if (inserted || it->second == entity.enabled) continue;

            it->second = entity.enabled;
            flipped.emplace_back(entity.id, entity.enabled);
        }

        PruneDestroyed(lastEnabled, level);

        for (const ID entityID : spawned)
            if (IsEnabledEntity(level, entityID)) FireEvents<PayloadOnStart>(level, EventOwnerKind::ENTITY, entityID, INVALID_ID, {});

        for (const auto& [entityID, enabled] : flipped) {
            const std::vector<TagId> tags = EntityTags(level, entityID);

            if (enabled) FireEvents<PayloadEntityEnable>(level, EventOwnerKind::ENTITY, entityID, entityID, tags);
            else FireEvents<PayloadEntityDisable>(level, EventOwnerKind::ENTITY, entityID, entityID, tags);
        }
    }

    // The SDL mouse button behind a KeyEventInput (KEY has none).
    Uint32 MouseButtonOf(const KeyEventInput input) {
        switch (input) {
            case KeyEventInput::MOUSE_LEFT: return SDL_BUTTON_LEFT;
            case KeyEventInput::MOUSE_RIGHT: return SDL_BUTTON_RIGHT;
            case KeyEventInput::MOUSE_MIDDLE: return SDL_BUTTON_MIDDLE;
            case KeyEventInput::MOUSE_BACK: return SDL_BUTTON_X1;
            case KeyEventInput::MOUSE_FORWARD: return SDL_BUTTON_X2;
            case KeyEventInput::KEY: break;
        }
        return 0;
    }

    // Whether the event's key or button went down (`press`) or up this frame.
    template <typename Payload>
    bool KeyInputHappened(const Payload& payload, const bool press) {
        if (payload.input == KeyEventInput::KEY) {
            if (payload.key == SDL_SCANCODE_UNKNOWN) return false;
            return press ? InputManager::GetKeyDown(payload.key) : InputManager::GetKeyUp(payload.key);
        }

        const Uint32 button = MouseButtonOf(payload.input);
        return press ? InputManager::GetMouseButtonDown(button) : InputManager::GetMouseButtonUp(button);
    }

    bool HasKeyEvent(const std::vector<Event>& events) {
        return std::ranges::any_of(events, [](const Event& event) {
            return std::holds_alternative<PayloadKeyPress>(event.payload) ||
                   std::holds_alternative<PayloadKeyRelease>(event.payload);
        });
    }

    // Key Press / Key Release. Runs while paused too (deltaTime 0), so a
    // key can resume the game.
    void FireKeyEvents(Level& level) {
        // Nothing went down or up this frame: no need to look at any event.
        bool anyChange = InputManager::GetAnyKeyDown() != SDL_SCANCODE_UNKNOWN ||
                         InputManager::GetAnyKeyUp() != SDL_SCANCODE_UNKNOWN;

        for (const Uint32 button : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1, SDL_BUTTON_X2})
            anyChange = anyChange || InputManager::GetMouseButtonDown(button) || InputManager::GetMouseButtonUp(button);

        if (!anyChange) return;

        // IDs first: a dispatch can add or remove things while these run.
        std::vector<std::pair<EventOwnerKind, ID>> owners;
        for (const Wall& wall : level.walls) if (HasKeyEvent(wall.events)) owners.emplace_back(EventOwnerKind::LINE, wall.id);
        for (const Linedef& linedef : level.linedefs)
            if (linedef.enabled && HasKeyEvent(linedef.events)) owners.emplace_back(EventOwnerKind::LINEDEF, linedef.id);
        for (const Sector& sector : level.sectors) if (HasKeyEvent(sector.events)) owners.emplace_back(EventOwnerKind::SECTOR, sector.id);
        for (const Entity& entity : level.entities)
            if (entity.enabled && HasKeyEvent(entity.events)) owners.emplace_back(EventOwnerKind::ENTITY, entity.id);

        for (const auto& [kind, ownerID] : owners) {
            // An earlier one's dispatch may have disabled it.
            if (kind == EventOwnerKind::ENTITY && !IsEnabledEntity(level, ownerID)) continue;
            if (kind == EventOwnerKind::LINEDEF) {
                const Linedef* linedef = level.GetLinedef(ownerID);
                if (linedef == nullptr || !linedef->enabled) continue;
            }

            FireEvents<PayloadKeyPress>(level, kind, ownerID, INVALID_ID, {},
                                        [](const PayloadKeyPress& payload) { return KeyInputHappened(payload, true); });
            FireEvents<PayloadKeyRelease>(level, kind, ownerID, INVALID_ID, {},
                                          [](const PayloadKeyRelease& payload) { return KeyInputHappened(payload, false); });
        }
    }

    template <typename Payload>
    bool HasEvent(const Linedef& linedef) {
        return linedef.enabled && std::ranges::any_of(linedef.events, [](const Event& event) {
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

    // Each entity in a sector with Stay in Sector events has a clock started
    // when it entered; an event of interval I fires each time it passes a
    // multiple of I.
    void FireStayEvents(Level& level, const float deltaTime) {
        if (staySectors.empty()) {
            stayClocks.clear();
            return;
        }

        struct Tick {
            ID entityID;
            ID sectorID;
            float before;
            float after;
        };
        std::vector<Tick> ticks;

        for (const auto& [entityID, sectorID] : lastSectors) {
            if (sectorID == INVALID_ID || !staySectors.contains(sectorID) || !IsEnabledEntity(level, entityID)) {
                stayClocks.erase(entityID);
                continue;
            }

            const auto [it, entered] = stayClocks.try_emplace(entityID, StayClock{sectorID, 0.0f});
            StayClock& clock = it->second;

            if (!entered && clock.sectorID != sectorID) clock = {sectorID, 0.0f};
            else if (!entered) {
                const float before = clock.elapsed;
                clock.elapsed += deltaTime;
                ticks.push_back({entityID, sectorID, before, clock.elapsed});
            }
        }

        for (const Tick& tick : ticks) {
            FireEvents<PayloadSectorEntityStay>(
                level, EventOwnerKind::SECTOR, tick.sectorID, tick.entityID, EntityTags(level, tick.entityID),
                [&](const PayloadSectorEntityStay& stay) {
                    if (stay.interval <= 0.0f) return false;
                    return std::floor(tick.after / stay.interval) > std::floor(tick.before / stay.interval);
                });
        }
    }

    // Every frame, for each enabled entity with a grounded Rigidbody that is in
    // a sector: the sector's Entity Grounded and the entity's Grounded In Sector.
    void FireGroundedEvents(Level& level) {
        struct Grounded {
            ID entityID;
            ID sectorID;
        };
        std::vector<Grounded> grounded;

        for (const ComponentRigidbody& rigidbody : level.rigidbodies.components) {
            if (!rigidbody.isGrounded || !IsEnabledEntity(level, rigidbody.ownerID)) continue;

            const auto sector = lastSectors.find(rigidbody.ownerID);
            if (sector == lastSectors.end() || sector->second == INVALID_ID) continue;

            // Once per entity, however many grounded Rigidbodies it has.
            if (std::ranges::any_of(grounded, [&](const Grounded& g) { return g.entityID == rigidbody.ownerID; }))
                continue;
            grounded.push_back({rigidbody.ownerID, sector->second});
        }

        for (const Grounded& g : grounded) {
            FireEvents<PayloadSectorEntityGrounded>(level, EventOwnerKind::SECTOR, g.sectorID, g.entityID,
                                                    EntityTags(level, g.entityID));

            const Sector* sector = level.GetSector(g.sectorID);
            const std::vector<TagId> sectorTags = sector ? sector->tagIds : std::vector<TagId>{};
            if (IsEnabledEntity(level, g.entityID))
                FireEvents<PayloadEntityGroundedInSector>(level, EventOwnerKind::ENTITY, g.entityID, g.entityID, sectorTags);
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

            // Which side the entity's centre is on as it touches.
            const Wall* wall = MapQueries::GetWallByID(level, wallID);
            const std::optional<Vector2> position = EntityPosition2D(level, entityID);
            const bool front = wall && position ? OnWallFront(level, *wall, *position) : true;

            FireEvents<PayloadLineEntityCollide>(level, EventOwnerKind::LINE, wallID, entityID, EntityTags(level, entityID),
                [&](const PayloadLineEntityCollide& collide) { return SideMatches(collide.side, front); });
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

            const Linedef* linedef = level.GetLinedef(linedefID);
            const std::optional<Vector2> position = EntityPosition2D(level, entityID);
            const bool front = linedef && position ? OnLinedefFront(*linedef, *position) : true;

            FireEvents<PayloadLineEntityCollide>(level, EventOwnerKind::LINEDEF, linedefID, entityID, EntityTags(level, entityID),
                [&](const PayloadLineEntityCollide& collide) { return SideMatches(collide.side, front); });
        }
    }

    void FireRayEvents(Level& level) {
        std::vector<RayReport> reports = std::move(rayReports);
        rayReports.clear();

        std::vector<RayKey> keys;

        for (const RayReport& report : reports) {
            const RayKey key = {report.casterID, static_cast<uint8_t>(report.type), static_cast<uint8_t>(report.ownerKind), report.ownerID};

            // A disabled caster can't instigate.
            if (report.casterID != INVALID_ID && level.GetEntity(report.casterID) && !IsEnabledEntity(level, report.casterID)) continue;
            if (!IsOwnerActive(level, report.ownerKind, report.ownerID)) continue;

            // First of a run: not hit last frame, nor already this frame.
            const bool firstHit = !std::ranges::binary_search(lastRayKeys, key) && std::ranges::find(keys, key) == keys.end();
            keys.push_back(key);

            const bool isLine = report.ownerKind == EventOwnerKind::LINE || report.ownerKind == EventOwnerKind::LINEDEF;

            FireEvents<PayloadReceiveRay>(
                level, report.ownerKind, report.ownerID, report.casterID, EntityTags(level, report.casterID),
                [&](const PayloadReceiveRay& receive) {
                    if (receive.rayType != report.type) return false;
                    if (receive.firstHitOnly && !firstHit) return false;
                    return !isLine || SideMatches(receive.side, report.fromFront);
                });
        }

        SortUnique(keys);
        lastRayKeys = std::move(keys);
    }

    void FireMoveFinishedEvents(Level& level) {
        const std::vector<SectorMoveEnd> ends = std::move(level.sectorMoveEnds);
        level.sectorMoveEnds.clear();

        for (const SectorMoveEnd& end : ends) {
            FireEvents<PayloadSectorMoveFinished>(
                level, EventOwnerKind::SECTOR, end.sectorID, INVALID_ID, {},
                [&](const PayloadSectorMoveFinished& finished) {
                    if (finished.surface == SectorSurfacePick::FLOOR && end.ceiling) return false;
                    if (finished.surface == SectorSurfacePick::CEILING && !end.ceiling) return false;
                    if (finished.floorIndex >= 0 && finished.floorIndex != end.floorIndex) return false;

                    switch (finished.reason) {
                        case PayloadSectorMoveFinished::ANY: return true;
                        case PayloadSectorMoveFinished::REACHED: return end.reason == SectorMoveEndReason::REACHED;
                        case PayloadSectorMoveFinished::STOPPED: return end.reason == SectorMoveEndReason::STOPPED;
                        case PayloadSectorMoveFinished::RETRACTED: return end.reason == SectorMoveEndReason::RETRACTED;
                    }
                    return true;
                });
        }
    }

    void UpdateTimers(Level& level, const float deltaTime) {
        for (size_t i = 0; i < timers.size(); ++i) {
            // Copied: a dispatch can rebuild the registry (Spawn Prefab).
            const EventHandle handle = timers[i].event;
            if (!IsOwnerActive(level, handle.ownerKind, handle.ownerID)) continue; // paused

            Event* event = ResolveEvent(level, handle);
            const PayloadTimer* timer = event ? std::get_if<PayloadTimer>(&event->payload) : nullptr;
            if (!timer) continue;

            TimerState& state = timers[i];
            state.elapsed += deltaTime;

            const float wait = state.firedOnce ? timer->interval : timer->firstDelay;
            if (state.elapsed < wait) continue;

            // At most once a frame, however short the interval.
            state.elapsed = std::min(state.elapsed - wait, std::max(timer->interval, 0.0f));
            state.firedOnce = true;

            Trigger(level, *event, handle.ownerKind, handle.ownerID, INVALID_ID, {});
        }
    }

    void UpdateConditionWatches(Level& level) {
        for (size_t i = 0; i < conditionWatches.size(); ++i) {
            const EventHandle handle = conditionWatches[i].event;

            Event* event = ResolveEvent(level, handle);
            if (!event || !std::holds_alternative<PayloadConditionMet>(event->payload)) continue;

            const bool met = level.CheckCondition(event->condition);
            const bool wasMet = std::exchange(conditionWatches[i].wasMet, met);

            if (met && !wasMet && IsOwnerActive(level, handle.ownerKind, handle.ownerID))
                Trigger(level, *event, handle.ownerKind, handle.ownerID, INVALID_ID, {});
        }
    }

    void RunPendingDispatches(Level& level, const float deltaTime, const float realDeltaTime) {
        std::vector<PendingDispatch> due;

        std::erase_if(pendingDispatches, [&](PendingDispatch& pending) {
            pending.remaining -= pending.realTime ? realDeltaTime : deltaTime;
            if (pending.remaining > 0.0f) return false;

            due.push_back(pending);
            return true;
        });

        for (const PendingDispatch& pending : due) {
            const Dispatch* dispatch = ResolveDispatch(level, pending.handle);
            if (!dispatch) continue;

            // A relay it runs stays in the same kind of time.
            const RealTimeChainScope scope(pending.realTime);
            const Dispatch copy = *dispatch;
            EventSystem::ExecuteDispatch(level, copy, pending.handle.ownerKind, pending.handle.ownerID, pending.instigatorID);
        }
    }

    bool FlowConditionHolds(const Level& level, const FlowCondition& condition) {
        return std::visit([&]<typename Kind>(const Kind& kind) {
            if constexpr (std::is_same_v<Kind, FlowVariable>) return level.CheckCondition(kind.condition);
            else if constexpr (std::is_same_v<Kind, FlowChance>)
                return kind.chance >= 100.0f || MathFunctions::GetRandomFloat(0.0f, 100.0f) < kind.chance;
            else return true;
        }, condition);
    }

    // A step never runs in the frame it was queued (a 0 wait = the next
    // frame), so a loop of flows runs at most once a frame.
    void RunPendingFlows(Level& level, const float deltaTime, const float realDeltaTime) {
        std::vector<PendingFlow> waiting = std::move(pendingFlows);
        pendingFlows.clear();

        std::vector<PendingFlow> due;
        for (PendingFlow& pending : waiting) {
            pending.remaining -= pending.realTime ? realDeltaTime : deltaTime;
            if (pending.remaining > 0.0f || updateCount < pending.firstUpdate) pendingFlows.push_back(std::move(pending));
            else due.push_back(std::move(pending));
        }

        for (const PendingFlow& pending : due) {
            if (!FlowConditionHolds(level, pending.flow.condition)) continue;

            const RealTimeChainScope scope(pending.realTime);
            RunCall(level, pending.flow.call, pending.callerKind, pending.callerID, pending.instigatorID);
        }
    }

    // ── Sectors ─────────────────────────────────────────────────────────────

    // The sectors across this one's two-sided walls.
    std::vector<const Sector*> NeighborSectors(const Level& level, const ID sectorID) {
        std::vector<const Sector*> neighbors;

        for (const Wall& wall : level.walls) {
            ID other = INVALID_ID;
            if (wall.frontSector == sectorID) other = wall.backSector;
            else if (wall.backSector == sectorID) other = wall.frontSector;

            if (other == INVALID_ID || other == sectorID) continue;

            const Sector* sector = level.GetSector(other);
            if (sector && !sector->floors.empty() && std::ranges::find(neighbors, sector) == neighbors.end())
                neighbors.push_back(sector);
        }

        return neighbors;
    }

    // A neighbour's floor is its lowest floor, its ceiling its highest ceiling.
    std::optional<float> NeighborHeight(const Level& level, const ID sectorID, const bool ceiling, const bool highest) {
        std::optional<float> result;

        for (const Sector* neighbor : NeighborSectors(level, sectorID)) {
            const float height = ceiling ? neighbor->floors.back().ceiling.height : neighbor->floors.front().floor.height;
            if (!result || (highest ? height > *result : height < *result)) result = height;
        }

        return result;
    }

    // The neighbour height closest to `current` that is above it (`higher`)
    // or below it, nullopt if there is none.
    std::optional<float> NextNeighborHeight(const Level& level, const ID sectorID, const bool ceiling, const bool higher,
                                            const float current) {
        std::optional<float> result;

        for (const Sector* neighbor : NeighborSectors(level, sectorID)) {
            const float height = ceiling ? neighbor->floors.back().ceiling.height : neighbor->floors.front().floor.height;
            if (higher ? height <= current : height >= current) continue;
            if (!result || (higher ? height < *result : height > *result)) result = height;
        }

        return result;
    }

    // Where a Move to Height heads, nullopt (with a warning) if it can't tell.
    std::optional<float> MoveTargetHeight(const Level& level, const ID sectorID, const float current,
                                          const SectorHeightMode mode, const float height) {
        std::optional<float> base;

        switch (mode) {
            case SectorHeightMode::ABSOLUTE_HEIGHT: return height;
            case SectorHeightMode::RELATIVE_OFFSET: return current + height;
            case SectorHeightMode::LOWEST_NEIGHBOR_FLOOR: base = NeighborHeight(level, sectorID, false, false); break;
            case SectorHeightMode::HIGHEST_NEIGHBOR_FLOOR: base = NeighborHeight(level, sectorID, false, true); break;
            case SectorHeightMode::LOWEST_NEIGHBOR_CEILING: base = NeighborHeight(level, sectorID, true, false); break;
            case SectorHeightMode::HIGHEST_NEIGHBOR_CEILING: base = NeighborHeight(level, sectorID, true, true); break;
            case SectorHeightMode::NEXT_HIGHER_NEIGHBOR_FLOOR: base = NextNeighborHeight(level, sectorID, false, true, current); break;
            case SectorHeightMode::NEXT_LOWER_NEIGHBOR_FLOOR: base = NextNeighborHeight(level, sectorID, false, false, current); break;
            case SectorHeightMode::NEXT_HIGHER_NEIGHBOR_CEILING: base = NextNeighborHeight(level, sectorID, true, true, current); break;
            case SectorHeightMode::NEXT_LOWER_NEIGHBOR_CEILING: base = NextNeighborHeight(level, sectorID, true, false, current); break;
        }

        if (!base) {
            spdlog::warn("Sector {} has no neighbouring sector at a height to move to", sectorID);
            return std::nullopt;
        }

        return *base + height;
    }

    SectorFloor* GetSectorFloor(Level& level, const ID sectorID, const int floorIndex) {
        Sector* sector = level.GetSector(sectorID);
        if (!sector || floorIndex < 0 || floorIndex >= static_cast<int>(sector->floors.size())) return nullptr;
        return &sector->floors[floorIndex];
    }

    std::optional<float> DoorOpenHeight(const Level& level, const ID sectorID, const PayloadDispatchSectorDoor& door) {
        if (door.openMode == PayloadDispatchSectorDoor::FIXED_HEIGHT) return door.openHeight;

        const std::optional<float> lowest = NeighborHeight(level, sectorID, true, false);
        if (!lowest) {
            spdlog::warn("Door on sector {} has no neighbouring sectors to open to", sectorID);
            return std::nullopt;
        }

        return *lowest - door.lip;
    }

    std::optional<float> DoorFloorOpenHeight(const Level& level, const ID sectorID, const PayloadDispatchSectorDoor& door) {
        if (door.floorOpenMode == PayloadDispatchSectorDoor::FIXED_FLOOR_HEIGHT) return door.floorOpenHeight;

        const std::optional<float> lowest = NeighborHeight(level, sectorID, false, false);
        if (!lowest) {
            spdlog::warn("Split door on sector {} has no neighbouring sectors to open its floor to", sectorID);
            return std::nullopt;
        }

        return *lowest + door.floorLip;
    }

    // A move never gets closer than MIN_MOVE_GAP to the opposite surface.
    bool IsDoorShut(const SectorFloor& floor) {
        return floor.ceiling.height - floor.floor.height <= SectorFloor::MIN_MOVE_GAP + 0.001f;
    }

    // Defined with the other sounds below.
    void PlayDoorSound(const Level& level, const DoorState& door, const std::string& sound);

    void MoveDoor(const Level& level, SectorFloor& floor, DoorState& door, const bool open) {
        const float speed = open ? door.settings.openSpeed : door.settings.closeSpeed;
        const SectorCollision collision = open ? SECTOR_FORCE_MOVE : door.settings.closeCollision;

        floor.MoveCeilingTo(open ? door.openCeiling : door.closedCeiling, speed, collision);
        if (door.settings.split) floor.MoveFloorTo(open ? door.openFloor : door.closedFloor, speed, collision);

        door.phase = open ? DoorState::OPENING : DoorState::CLOSING;
        PlayDoorSound(level, door, open ? door.settings.openSound : door.settings.closeSound);
    }

    // A new door's heights, nullopt (with a warning) if it can't tell where
    // it opens to. Open types and a shut Toggle door open from where they
    // are; Close types and an open Toggle door close down to the floor
    // (split: both meet halfway) and reopen to where they are.
    std::optional<DoorState> NewDoor(const Level& level, const ID sectorID, const SectorFloor& floor,
                                     const PayloadDispatchSectorDoor& settings) {
        using Door = PayloadDispatchSectorDoor;

        DoorState door{sectorID, settings.floorIndex, settings,
                       floor.ceiling.height, floor.ceiling.height, floor.floor.height, floor.floor.height};

        const bool opensFirst = settings.type == Door::OPEN_WAIT_CLOSE || settings.type == Door::OPEN_STAY ||
                                (settings.type == Door::OPEN_CLOSE_TOGGLE && IsDoorShut(floor));

        if (opensFirst) {
            const std::optional<float> openCeiling = DoorOpenHeight(level, sectorID, settings);
            if (!openCeiling) return std::nullopt;
            door.openCeiling = *openCeiling;

            if (settings.split) {
                const std::optional<float> openFloor = DoorFloorOpenHeight(level, sectorID, settings);
                if (!openFloor) return std::nullopt;
                door.openFloor = *openFloor;
            }
        }
        else {
            const float shut = settings.split ? (floor.floor.height + floor.ceiling.height) * 0.5f : floor.floor.height;
            door.closedCeiling = shut;
            door.closedFloor = shut;
        }

        return door;
    }

    // Plays the locked sound / message and returns true when `instigatorID`
    // may not move the door.
    bool DoorLocked(const Level& level, const ID sectorID, const PayloadDispatchSectorDoor& settings, const ID instigatorID);

    void TriggerDoor(Level& level, const ID sectorID, const PayloadDispatchSectorDoor& settings, const ID instigatorID) {
        using Door = PayloadDispatchSectorDoor;

        SectorFloor* floor = GetSectorFloor(level, sectorID, settings.floorIndex);
        if (!floor) return;

        if (settings.openSpeed <= 0.0f || settings.closeSpeed <= 0.0f) {
            spdlog::warn("Door dispatch on sector {} needs open and close speeds above 0", sectorID);
            return;
        }

        if (DoorLocked(level, sectorID, settings, instigatorID)) {
            dispatchRefused = true;
            return;
        }

        const auto existing = std::ranges::find_if(doors, [&](const DoorState& door) {
            return door.sectorID == sectorID && door.floorIndex == settings.floorIndex;
        });

        if (existing == doors.end()) {
            const bool closeType = settings.type == Door::CLOSE_WAIT_OPEN || settings.type == Door::CLOSE_STAY;
            if (closeType && IsDoorShut(*floor)) return; // nothing to close

            std::optional<DoorState> door = NewDoor(level, sectorID, *floor, settings);
            if (!door) return;

            const bool open = settings.type == Door::OPEN_WAIT_CLOSE || settings.type == Door::OPEN_STAY ||
                              (settings.type == Door::OPEN_CLOSE_TOGGLE && IsDoorShut(*floor));
            MoveDoor(level, *floor, *door, open);
            doors.push_back(*door);
            return;
        }

        DoorState& door = *existing;

        switch (door.settings.type) {
            case Door::OPEN_STAY:
            case Door::CLOSE_STAY:
                return;

            case Door::OPEN_CLOSE_TOGGLE: {
                // Reverses whatever it did last.
                const bool opening = door.phase == DoorState::OPENING || (door.phase == DoorState::STILL && door.open);
                MoveDoor(level, *floor, door, !opening);
                return;
            }

            case Door::OPEN_WAIT_CLOSE:
            case Door::CLOSE_WAIT_OPEN:
                break;
        }

        // The cycle goes `away` first (open, or closed for Close, Wait,
        // Open), waits, then heads back.
        const bool away = door.settings.type == Door::OPEN_WAIT_CLOSE;
        const bool headingBack = door.phase == (away ? DoorState::CLOSING : DoorState::OPENING);

        switch (settings.retrigger) {
            case Door::RESTART:
                if (headingBack) MoveDoor(level, *floor, door, away);
                else if (door.phase == DoorState::WAITING) door.waitLeft = door.settings.wait;
                break;
            case Door::TOGGLE:
                MoveDoor(level, *floor, door, headingBack ? away : !away);
                break;
            case Door::IGNORE_WHILE_OPEN:
                break;
        }
    }

    void UpdateDoors(Level& level, const float deltaTime) {
        using Door = PayloadDispatchSectorDoor;

        const auto wait = [](DoorState& door, const bool open) {
            door.phase = DoorState::WAITING;
            door.open = open;
            door.waitLeft = door.settings.wait;
        };

        const auto still = [](DoorState& door, const bool open) {
            door.phase = DoorState::STILL;
            door.open = open;
        };

        std::erase_if(doors, [&](DoorState& door) {
            SectorFloor* floor = GetSectorFloor(level, door.sectorID, door.floorIndex);
            if (!floor) return true;

            const bool split = door.settings.split;
            const bool moving = floor->ceilingMove.active || (split && floor->floorMove.active);

            switch (door.phase) {
                case DoorState::OPENING:
                    if (moving) return false;

                    switch (door.settings.type) {
                        case Door::OPEN_WAIT_CLOSE: wait(door, true); return false;
                        case Door::CLOSE_WAIT_OPEN: return true;
                        default: still(door, true); return false;
                    }

                case DoorState::WAITING:
                    door.waitLeft -= deltaTime;
                    if (door.waitLeft <= 0.0f) MoveDoor(level, *floor, door, !door.open);
                    return false;

                case DoorState::CLOSING: {
                    // A split door blocked on one side takes the other back too.
                    if (split && floor->ceilingMove.retracting != floor->floorMove.retracting) {
                        if (!floor->ceilingMove.retracting)
                            floor->MoveCeilingTo(door.openCeiling, door.settings.closeSpeed, SECTOR_FORCE_MOVE);
                        if (!floor->floorMove.retracting)
                            floor->MoveFloorTo(door.openFloor, door.settings.closeSpeed, SECTOR_FORCE_MOVE);
                        floor->ceilingMove.retracting = floor->floorMove.retracting = true;
                    }

                    if (moving) return false;

                    // A move never gets closer than MIN_MOVE_GAP to the
                    // opposite surface, so a door that started shut flat on
                    // its floor ends that far above it - comparing with where
                    // it started took that for blocked, and the door waited
                    // and "closed" forever, never opening again.
                    const float gap = SectorFloor::MIN_MOVE_GAP;
                    const bool retracted = floor->ceilingMove.retracting || (split && floor->floorMove.retracting);
                    const bool ceilingShut = floor->ceiling.height <= std::max(door.closedCeiling, floor->floor.height + gap) + 0.001f;
                    const bool floorShut = !split || floor->floor.height >= std::min(door.closedFloor, floor->ceiling.height - gap) - 0.001f;

                    if (!retracted && ceilingShut && floorShut) {
                        switch (door.settings.type) {
                            case Door::OPEN_WAIT_CLOSE: return true;
                            case Door::CLOSE_WAIT_OPEN: wait(door, false); return false;
                            default: still(door, false); return false;
                        }
                    }

                    // Blocked short of shut (retracted or stopped): counts as
                    // open. Cycles wait and try again; a Close and Stay door
                    // can be triggered again.
                    switch (door.settings.type) {
                        case Door::OPEN_WAIT_CLOSE:
                        case Door::CLOSE_WAIT_OPEN: wait(door, true); return false;
                        case Door::CLOSE_STAY: return true;
                        default: still(door, true); return false;
                    }
                }

                case DoorState::STILL:
                    return false;
            }

            return false;
        });
    }

    // Runs `edit` on the sector, then passes the change on to its children,
    // same as a value written from Lua.
    template <typename Edit>
    void EditSector(Level& level, Sector& sector, Edit&& edit) {
        const std::vector<SectorFloor> floorsBefore = sector.children.empty() ? std::vector<SectorFloor>{} : sector.floors;
        const Vector3 lightBefore = sector.light;

        edit();

        if (!sector.children.empty()) level.PropagateSectorChanges(sector.id, floorsBefore, lightBefore);
    }

    // ── Entities ────────────────────────────────────────────────────────────

    Quaternion Nlerp(const Quaternion& a, Quaternion b, const float t) {
        // Take the short way round.
        if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = {-b.x, -b.y, -b.z, -b.w};

        return Quaternion{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        }.Normalized();
    }

    void PlaceTransform(Level& level, ComponentTransform& transform, const Vector3& position) {
        transform.SetPosition(position);
        transform.isDirty = true;

        // The frame's sector update has already run.
        transform.UpdateObjectSectorAndFloor(level.sectors);
    }

    // The area centroid of the sector's outline (map X and Z); the corners'
    // average if it has no area. Not always inside the sector (an L or a
    // ring has it outside).
    Vector2 SectorCentre(const Sector& sector) {
        const std::vector<Vector2>& corners = sector.vertices;
        if (corners.empty()) return {0.0f, 0.0f};

        double area = 0.0, x = 0.0, z = 0.0;
        for (size_t i = 0; i < corners.size(); ++i) {
            const Vector2& a = corners[i];
            const Vector2& b = corners[(i + 1) % corners.size()];
            const double cross = static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
            area += cross;
            x += (a.x + b.x) * cross;
            z += (a.y + b.y) * cross;
        }

        if (std::abs(area) < 1e-6) {
            Vector2 sum = {0.0f, 0.0f};
            for (const Vector2& corner : corners) sum = {sum.x + corner.x, sum.y + corner.y};
            return {sum.x / static_cast<float>(corners.size()), sum.y / static_cast<float>(corners.size())};
        }

        area *= 0.5;
        return {static_cast<float>(x / (6.0 * area)), static_cast<float>(z / (6.0 * area))};
    }

    // How far the entity's feet are above its base collider's bottom (0
    // without a collider).
    float FeetAboveBase(const Level& level, const ComponentTransform& transform) {
        if (const ComponentCollider* collider = PhysicsSystem::FindBaseCollider(level, transform.ownerID))
            return transform.position.y - collider->Base(transform).y;
        return 0.0f;
    }

    // Stands the entity on `floorHeight` at `point`: its base collider's
    // bottom there (its feet without a collider), grounded at once.
    void StandOn(Level& level, ComponentTransform& transform, const Vector2 point, const float floorHeight) {
        const float feetToBase = FeetAboveBase(level, transform);

        PlaceTransform(level, transform, {point.x, floorHeight + feetToBase, point.y});

        if (ComponentRigidbody* rigidbody = level.rigidbodies.Get(transform.ownerID)) {
            rigidbody->isGrounded = true;
            if (rigidbody->velocity.y < 0.0f) rigidbody->velocity.y = 0.0f;
        }
    }

    // The highest of the sector's floors at or below `height` at `point`
    // (the floor something at that height stands over); the lowest if none.
    float FloorBelow(const Sector& sector, const Vector2 point, const float height) {
        std::optional<float> best;
        float lowest = std::numeric_limits<float>::max();

        for (const SectorFloor& floor : sector.floors) {
            const float h = MapQueries::SurfaceHeightAt(sector, floor.floor, point);
            lowest = std::min(lowest, h);
            if (h <= height + 0.01f && (!best || h > *best)) best = h;
        }

        return best.value_or(sector.floors.empty() ? height : lowest);
    }

    // Which of the sector's floors (slabs) something whose bottom is at
    // `height` stands in: the highest floor at or below it, else the lowest.
    int FloorIndexAt(const Sector& sector, const Vector2 point, const float height) {
        int best = -1, lowest = 0;
        float bestHeight = 0.0f, lowestHeight = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(sector.floors.size()); ++i) {
            const float h = MapQueries::SurfaceHeightAt(sector, sector.floors[i].floor, point);
            if (h < lowestHeight) {
                lowestHeight = h;
                lowest = i;
            }
            if (h <= height + 0.01f && (best < 0 || h > bestHeight)) {
                best = i;
                bestHeight = h;
            }
        }

        return best >= 0 ? best : lowest;
    }

    // Teleports `entityID` as `teleport` says. `offset` (map X and Z) moves
    // its landing point from the target point, which keeps a group's spacing.
    void Teleport(Level& level, const ID entityID, const TeleportSettings& teleport, const Vector2 offset = {0.0f, 0.0f}) {
        const ID ownerID = entityID;
        ComponentTransform* transform = level.transforms.Get(ownerID);
        if (!transform) return;

        std::optional<Quaternion> targetRotation;

        switch (teleport.target) {
            case TeleportTarget::COORDINATES:
                PlaceTransform(level, *transform, {teleport.position.x + offset.x, teleport.position.y, teleport.position.z + offset.y});
                break;

            case TeleportTarget::ENTITY: {
                const ComponentTransform* target = level.transforms.Get(teleport.targetID);
                if (!target) {
                    spdlog::warn("Teleport dispatch on entity {} skipped: its target entity {} doesn't exist", ownerID, teleport.targetID);
                    return;
                }

                const Vector3 destination = {target->position.x + offset.x, target->position.y, target->position.z + offset.y};
                targetRotation = target->rotation;
                const Vector2 point = {destination.x, destination.z};

                if (target->sectorIndex >= 0 && target->sectorIndex < static_cast<int>(level.sectors.size()))
                    StandOn(level, *transform, point, FloorBelow(level.sectors[target->sectorIndex], point, destination.y));
                else PlaceTransform(level, *transform, destination); // outside the map: exactly there
                break;
            }

            case TeleportTarget::SECTOR: {
                const Sector* sector = level.GetSector(teleport.targetID);
                if (!sector || sector->floors.empty()) {
                    spdlog::warn("Teleport dispatch on entity {} skipped: its target sector {} doesn't exist", ownerID, teleport.targetID);
                    return;
                }

                const Vector2 centre = SectorCentre(*sector);
                const Vector2 point = {centre.x + offset.x, centre.y + offset.y};
                const int floorIndex = std::clamp(teleport.floorIndex, 0, static_cast<int>(sector->floors.size()) - 1);
                const float height = MapQueries::SurfaceHeightAt(*sector, sector->floors[floorIndex].floor, point);
                StandOn(level, *transform, point, height);
                break;
            }
        }

        switch (teleport.facing) {
            case TeleportFacing::SET: transform->rotation = teleport.rotation.Normalized(); break;
            case TeleportFacing::TARGET: if (targetRotation) transform->rotation = targetRotation->Normalized(); break;
            case TeleportFacing::KEEP: break;
        }

        // Kept otherwise, as with a script's SetPosition.
        if (teleport.stopVelocity)
            if (ComponentRigidbody* rigidbody = level.rigidbodies.Get(ownerID)) rigidbody->velocity = {0.0f, 0.0f, 0.0f};

        teleportLocks[ownerID] = SectorIDAt(level, transform->sectorIndex);
    }

    // Drops the land-locks of entities that have left the sector they were
    // teleported into (or are gone).
    void UpdateTeleportLocks(const Level& level) {
        std::erase_if(teleportLocks, [&](const auto& lock) {
            const ComponentTransform* transform = level.transforms.Get(lock.first);
            return transform == nullptr || SectorIDAt(level, transform->sectorIndex) != lock.second;
        });
    }

    void UpdateEntityMoves(Level& level, const float deltaTime) {
        std::erase_if(entityMoves, [&](EntityMove& move) {
            ComponentTransform* transform = level.transforms.Get(move.entityID);
            if (!transform) return true;

            // Paused while the entity is disabled.
            if (!IsEnabledEntity(level, move.entityID)) return false;

            move.elapsed += deltaTime;
            const float t = move.duration > 0.0f ? std::min(move.elapsed / move.duration, 1.0f) : 1.0f;

            PlaceTransform(level, *transform, move.from + (move.to - move.from) * t);
            if (move.rotate) transform->rotation = Nlerp(move.rotationFrom, move.rotationTo, t);

            return t >= 1.0f;
        });
    }

    void SetComponents(Level& level, const ID entityID, const ToggleableComponent component, const ToggleMode mode) {
        const auto setFlags = [&](auto pointers, auto&& flag) {
            for (auto* c : pointers) flag(*c) = ApplyToggle(mode, flag(*c));
        };

        switch (component) {
            case ToggleableComponent::SPRITE:
                setFlags(level.sprites.GetAll(entityID), [](ComponentSprite& c) -> bool& { return c.isActive; });
                break;
            case ToggleableComponent::UI_SPRITE:
                setFlags(level.ui_sprites.GetAll(entityID), [](ComponentUISprite& c) -> bool& { return c.isActive; });
                break;
            case ToggleableComponent::PATHFINDER:
                setFlags(level.pathfinders.GetAll(entityID), [](ComponentPathfinder& c) -> bool& { return c.isActive; });
                break;
            case ToggleableComponent::MONSTER_AI:
                setFlags(level.monsterAIs.GetAll(entityID), [](ComponentMonsterAI& c) -> bool& { return c.isActive; });
                break;
            case ToggleableComponent::SCRIPT:
                setFlags(level.scripts.GetAll(entityID), [](ComponentScript& c) -> bool& { return c.enabled; });
                break;
            case ToggleableComponent::COLLIDER: {
                // SetActive moves colliders around, so go by instance.
                std::vector<std::pair<ComponentInstanceID, bool>> colliders;
                for (const ComponentCollider* c : level.colliders.GetAll(entityID)) colliders.emplace_back(c->instanceID, c->isActive);

                for (const auto& [instanceID, active] : colliders) level.colliders.SetActive(instanceID, ApplyToggle(mode, active));
                break;
            }
            // Only one camera and one player controller are active in the
            // level: turning on activates the entity's first, which turns
            // every other off.
            case ToggleableComponent::CAMERA: {
                const std::vector<ComponentCamera*> cameras = level.cameras.GetAll(entityID);
                if (cameras.empty()) break;

                const bool anyActive = std::ranges::any_of(cameras, [](const ComponentCamera* c) { return c->isActive; });
                if (ApplyToggle(mode, anyActive)) level.ActivateCamera(*cameras.front());
                else for (ComponentCamera* c : cameras) c->isActive = false;
                break;
            }
            case ToggleableComponent::PLAYER_CONTROLLER: {
                const std::vector<ComponentPlayerController*> controllers = level.playerControllers.GetAll(entityID);
                if (controllers.empty()) break;

                const bool anyActive = std::ranges::any_of(controllers, [](const ComponentPlayerController* c) { return c->isActive; });
                if (ApplyToggle(mode, anyActive)) level.ActivatePlayerController(*controllers.front());
                else for (ComponentPlayerController* c : controllers) c->isActive = false;
                break;
            }
        }
    }

    // ── Owners ──────────────────────────────────────────────────────────────

    // Height of the floor under `point`, or 0 outside every sector.
    float FloorHeightAt(const Level& level, const Vector2& point) {
        const int index = MapQueries::FindSectorContainingPoint(level.sectors, point);
        if (index < 0 || level.sectors[index].floors.empty()) return 0.0f;

        const Sector& sector = level.sectors[index];
        return MapQueries::SurfaceHeightAt(sector, sector.floors.front().floor, point);
    }

    // Where a Spawn Prefab or Play Sound on this owner happens: an entity's
    // transform, a line's middle on the floor, a sector's bounds centre on its
    // floor. nullopt for the level (and anything gone): the origin, or
    // unpositioned for a sound.
    std::optional<Vector3> OwnerPosition(const Level& level, const EventOwnerKind kind, const ID ownerID) {
        switch (kind) {
            case EventOwnerKind::ENTITY: {
                const ComponentTransform* transform = level.transforms.Get(ownerID);
                if (!transform) return std::nullopt;
                return transform->position;
            }
            case EventOwnerKind::LINE: {
                const Wall* wall = MapQueries::GetWallByID(level, ownerID);
                if (!wall) return std::nullopt;

                const Vector2 middle = (wall->start + wall->end) * 0.5f;
                // Nudged into the front sector, so it isn't on the edge.
                const Vector2 inside = middle + wall->normal * (WallFrontIsNormalSide(level, *wall) ? 0.01f : -0.01f);
                return Vector3{middle.x, FloorHeightAt(level, inside), middle.y};
            }
            case EventOwnerKind::LINEDEF: {
                const Linedef* linedef = level.GetLinedef(ownerID);
                if (!linedef) return std::nullopt;

                const Vector2 middle = (linedef->start + linedef->end) * 0.5f;
                return Vector3{middle.x, FloorHeightAt(level, middle), middle.y};
            }
            case EventOwnerKind::SECTOR: {
                const Sector* sector = level.GetSector(ownerID);
                if (!sector || sector->vertices.empty() || sector->floors.empty()) return std::nullopt;

                Vector2 min = sector->vertices.front();
                Vector2 max = min;
                for (const Vector2& vertex : sector->vertices) {
                    min = {std::min(min.x, vertex.x), std::min(min.y, vertex.y)};
                    max = {std::max(max.x, vertex.x), std::max(max.y, vertex.y)};
                }

                const Vector2 centre = (min + max) * 0.5f;
                return Vector3{centre.x, MapQueries::SurfaceHeightAt(*sector, sector->floors.front().floor, centre), centre.y};
            }
            case EventOwnerKind::LEVEL:
                return std::nullopt;
        }
        return std::nullopt;
    }

    std::vector<TagId>* OwnerTags(Level& level, const EventOwnerKind kind, const ID ownerID) {
        switch (kind) {
            case EventOwnerKind::LINE: {
                Wall* wall = MapQueries::GetWallByID(level, ownerID);
                return wall ? &wall->tagIds : nullptr;
            }
            case EventOwnerKind::SECTOR: {
                Sector* sector = level.GetSector(ownerID);
                return sector ? &sector->tagIds : nullptr;
            }
            case EventOwnerKind::ENTITY: {
                Entity* entity = level.GetEntity(ownerID);
                return entity ? &entity->tagIds : nullptr;
            }
            default:
                return nullptr;
        }
    }

    // ── Sounds and messages ─────────────────────────────────────────────────

    void PlaySound(const PayloadDispatchPlaySound& play, const std::optional<Vector3>& position) {
        if (play.sound.empty()) return;

        const std::string name = "event_sound_" + std::to_string(nextSoundSource++);
        if (!SoundManager::CreateSource(name)) return;

        if (position) SoundManager::SetSourcePosition(name, *position);
        else SoundManager::SetSourceRelative(name, true);

        SoundManager::SetSourceGain(name, play.gain);
        SoundManager::SetSourcePitch(name, play.pitch);
        SoundManager::PlaySoundOnSource(name, play.sound);

        soundSources.push_back(name);
    }

    void PlayDoorSound(const Level& level, const DoorState& door, const std::string& sound) {
        if (sound.empty()) return;

        PayloadDispatchPlaySound play;
        play.sound = sound;
        play.gain = door.settings.soundGain;
        play.pitch = door.settings.soundPitch;
        PlaySound(play, OwnerPosition(level, EventOwnerKind::SECTOR, door.sectorID));
    }

    // Deletes the sources of sounds that have finished.
    void UpdateSounds() {
        std::erase_if(soundSources, [](const std::string& name) {
            if (SoundManager::IsSourcePlaying(name)) return false;
            SoundManager::DestroySource(name);
            return true;
        });
    }

    // 0..1: rising over fadeIn at the start, falling over fadeOut at the end.
    // Fades longer than the message together are shortened to fit it.
    float MessageAlpha(const MessageStyle& style, const ShownMessage& shown) {
        float fadeIn = std::max(0.0f, style.fadeIn);
        float fadeOut = std::max(0.0f, style.fadeOut);
        if (fadeIn + fadeOut > shown.duration) {
            const float shrink = shown.duration / (fadeIn + fadeOut);
            fadeIn *= shrink;
            fadeOut *= shrink;
        }

        float alpha = 1.0f;
        if (fadeIn > 0.0f) alpha = std::min(alpha, shown.age / fadeIn);
        if (fadeOut > 0.0f) alpha = std::min(alpha, (shown.duration - shown.age) / fadeOut);
        return std::clamp(alpha, 0.0f, 1.0f);
    }

    // `color` is 0-255.
    void ShowMessage(const MessageStyle& style, const std::string& text, const Vector3& color, const float duration) {
        if (duration <= 0.0f) return;

        if (!style.stack) shownMessages.clear();

        ShownMessage shown{{nextMessageID++, text, color, 0.0f}, 0.0f, duration};
        shown.message.alpha = MessageAlpha(style, shown);
        shownMessages.push_back(std::move(shown));

        const size_t limit = style.stack ? static_cast<size_t>(std::max(1, style.maxMessages)) : 1;
        if (shownMessages.size() > limit)
            shownMessages.erase(shownMessages.begin(), shownMessages.end() - static_cast<std::ptrdiff_t>(limit));
    }

    void UpdateMessages(const MessageStyle& style, const float deltaTime) {
        std::erase_if(shownMessages, [&](ShownMessage& shown) {
            shown.age += deltaTime;
            if (shown.age >= shown.duration) return true;

            shown.message.alpha = MessageAlpha(style, shown);
            return false;
        });
    }

    // Door and Lift: plays the locked sound / message and returns true when
    // `instigatorID` lacks the required tag. Runs without an instigator
    // entity are never locked out.
    template <typename Settings>
    bool SectorDispatchLocked(const Level& level, const ID sectorID, const Settings& settings, const ID instigatorID) {
        if (!settings.locked) return false;

        const Entity* instigator = level.GetEntity(instigatorID);
        if (!instigator || std::ranges::find(instigator->tagIds, settings.requiredTag) != instigator->tagIds.end()) return false;

        if (!settings.lockedSound.empty()) {
            PayloadDispatchPlaySound play;
            play.sound = settings.lockedSound;
            play.gain = settings.soundGain;
            play.pitch = settings.soundPitch;
            PlaySound(play, OwnerPosition(level, EventOwnerKind::SECTOR, sectorID));
        }

        if (!settings.lockedMessage.empty())
            ShowMessage(level.worldSettings.messageStyle, settings.lockedMessage,
                        settings.lockedMessageColor * 255.0f, settings.lockedMessageDuration);

        return true;
    }

    bool DoorLocked(const Level& level, const ID sectorID, const PayloadDispatchSectorDoor& settings, const ID instigatorID) {
        return SectorDispatchLocked(level, sectorID, settings, instigatorID);
    }

    void PlaySectorSound(const Level& level, const ID sectorID, const std::string& sound, const float gain, const float pitch) {
        if (sound.empty()) return;

        PayloadDispatchPlaySound play;
        play.sound = sound;
        play.gain = gain;
        play.pitch = pitch;
        PlaySound(play, OwnerPosition(level, EventOwnerKind::SECTOR, sectorID));
    }

    // Puts the sector's light at `light` and passes the change on to its children.
    void WriteSectorLight(Level& level, Sector& sector, const Vector3& light) {
        if (sector.light == light) return;

        const Vector3 before = sector.light;
        sector.light = light;
        if (!sector.children.empty()) level.PropagateSectorChanges(sector.id, sector.floors, before);
    }

    // ── Lifts ───────────────────────────────────────────────────────────────

    // A Lift dispatch on one of a sector's floors, from its first trigger
    // until its cycle ends (Perpetual: until a Stop Movement).
    struct LiftState {
        enum Phase { MOVING, WAITING };

        ID sectorID;
        int floorIndex;
        PayloadDispatchSectorLift settings;
        float home;       // where it started: Down/Up, Wait, back return here
        float low, high;  // what the type uses
        Phase phase = MOVING;
        bool away = true; // wait types: heading (or waiting) away from home. Perpetual: toward `high`
        float waitLeft = 0.0f;
    };

    std::vector<LiftState> lifts;

    std::optional<float> LiftLow(const Level& level, const ID sectorID, const PayloadDispatchSectorLift& lift) {
        if (lift.lowMode == PayloadDispatchSectorLift::FIXED_LOW) return lift.lowHeight;

        const std::optional<float> lowest = NeighborHeight(level, sectorID, false, false);
        if (!lowest) spdlog::warn("Lift on sector {} has no neighbouring sectors to go down to", sectorID);
        return lowest ? std::optional(*lowest + lift.lowLip) : std::nullopt;
    }

    std::optional<float> LiftHigh(const Level& level, const ID sectorID, const PayloadDispatchSectorLift& lift) {
        if (lift.highMode == PayloadDispatchSectorLift::FIXED_HIGH) return lift.highHeight;

        const std::optional<float> highest = NeighborHeight(level, sectorID, false, true);
        if (!highest) spdlog::warn("Lift on sector {} has no neighbouring sectors to go up to", sectorID);
        return highest ? std::optional(*highest + lift.highLip) : std::nullopt;
    }

    // Where the leg the lift is on (or about to start) heads.
    float LiftTarget(const LiftState& lift) {
        using Lift = PayloadDispatchSectorLift;

        switch (lift.settings.type) {
            case Lift::DOWN_WAIT_UP: return lift.away ? lift.low : lift.home;
            case Lift::UP_WAIT_DOWN: return lift.away ? lift.high : lift.home;
            case Lift::PERPETUAL: return lift.away ? lift.high : lift.low;
        }
        return lift.home;
    }

    void MoveLift(const Level& level, SectorFloor& floor, LiftState& lift) {
        const float target = LiftTarget(lift);
        const SectorCollision collision = target > floor.floor.height ? lift.settings.collision : SECTOR_FORCE_MOVE;

        floor.MoveFloorTo(target, lift.settings.speed, collision);
        lift.phase = LiftState::MOVING;
        PlaySectorSound(level, lift.sectorID, lift.settings.startSound, lift.settings.soundGain, lift.settings.soundPitch);
    }

    void TriggerLift(Level& level, const ID sectorID, const PayloadDispatchSectorLift& settings, const ID instigatorID) {
        using Lift = PayloadDispatchSectorLift;
        using Door = PayloadDispatchSectorDoor;

        SectorFloor* floor = GetSectorFloor(level, sectorID, settings.floorIndex);
        if (!floor) return;

        if (settings.speed <= 0.0f) {
            spdlog::warn("Lift dispatch on sector {} needs a speed above 0", sectorID);
            return;
        }

        if (SectorDispatchLocked(level, sectorID, settings, instigatorID)) {
            dispatchRefused = true;
            return;
        }

        const auto existing = std::ranges::find_if(lifts, [&](const LiftState& lift) {
            return lift.sectorID == sectorID && lift.floorIndex == settings.floorIndex;
        });

        if (existing == lifts.end()) {
            LiftState lift{sectorID, settings.floorIndex, settings, floor->floor.height, floor->floor.height, floor->floor.height};

            if (settings.type != Lift::UP_WAIT_DOWN) {
                const std::optional<float> low = LiftLow(level, sectorID, settings);
                if (!low) return;
                lift.low = *low;
            }

            if (settings.type != Lift::DOWN_WAIT_UP) {
                const std::optional<float> high = LiftHigh(level, sectorID, settings);
                if (!high) return;
                lift.high = *high;
            }

            // Perpetual heads first for the end further away.
            if (settings.type == Lift::PERPETUAL)
                lift.away = std::abs(lift.high - lift.home) >= std::abs(lift.home - lift.low);

            MoveLift(level, *floor, lift);
            lifts.push_back(lift);
            return;
        }

        LiftState& lift = *existing;
        if (lift.settings.type == Lift::PERPETUAL) return;

        switch (lift.settings.retrigger) {
            case Door::RESTART:
                if (lift.phase == LiftState::MOVING && !lift.away) {
                    lift.away = true;
                    MoveLift(level, *floor, lift);
                }
                else if (lift.phase == LiftState::WAITING) lift.waitLeft = lift.settings.wait;
                break;
            case Door::TOGGLE:
                lift.away = !lift.away;
                MoveLift(level, *floor, lift);
                break;
            case Door::IGNORE_WHILE_OPEN:
                break;
        }
    }

    void UpdateLifts(Level& level, const float deltaTime) {
        using Lift = PayloadDispatchSectorLift;

        std::erase_if(lifts, [&](LiftState& lift) {
            SectorFloor* floor = GetSectorFloor(level, lift.sectorID, lift.floorIndex);
            if (!floor) return true;

            if (lift.phase == LiftState::WAITING) {
                lift.waitLeft -= deltaTime;
                if (lift.waitLeft <= 0.0f) MoveLift(level, *floor, lift);
                return false;
            }

            if (floor->floorMove.active) return false;

            PlaySectorSound(level, lift.sectorID, lift.settings.stopSound, lift.settings.soundGain, lift.settings.soundPitch);

            const bool perpetual = lift.settings.type == Lift::PERPETUAL;

            // Blocked and gone back where the leg started: wait there and
            // try the same leg again - unless that was home, leaving.
            if (floor->floorMove.retracting) {
                if (!perpetual && lift.away) return true;
                lift.phase = LiftState::WAITING;
                lift.waitLeft = lift.settings.wait;
                return false;
            }

            // Arrived (or stopped short): home ends a cycle, anything else
            // waits and heads for the other end.
            if (!perpetual && !lift.away) return true;

            lift.away = !lift.away;
            lift.phase = LiftState::WAITING;
            lift.waitLeft = lift.settings.wait;
            return false;
        });
    }

    // ── Stairs ──────────────────────────────────────────────────────────────

    void RaiseStairs(Level& level, const ID sectorID, const PayloadDispatchSectorRaiseStairs& stairs) {
        if (stairs.speed <= 0.0f) {
            spdlog::warn("Raise Stairs dispatch on sector {} needs a speed above 0", sectorID);
            return;
        }

        const auto hasFloor = [&](const Sector* sector) {
            return sector && stairs.floorIndex >= 0 && stairs.floorIndex < static_cast<int>(sector->floors.size());
        };

        Sector* step = level.GetSector(sectorID);
        if (!hasFloor(step)) return;

        const std::string texture = step->floors[stairs.floorIndex].floor.texture.First();
        std::vector<ID> visited = {sectorID};
        float height = step->floors[stairs.floorIndex].floor.height;

        while (true) {
            height += stairs.stepHeight;
            step->floors[stairs.floorIndex].MoveFloorTo(height, stairs.speed, stairs.collision);

            // The next step: the one unvisited neighbour that fits. None, or
            // a choice of several, ends the stairs.
            std::vector<ID> next;
            for (const Sector* neighbor : NeighborSectors(level, step->id)) {
                if (std::ranges::find(visited, neighbor->id) != visited.end() || !hasFloor(neighbor)) continue;
                if (!stairs.ignoreTexture && neighbor->floors[stairs.floorIndex].floor.texture.First() != texture) continue;
                next.push_back(neighbor->id);
            }

            if (next.size() != 1) return;

            step = level.GetSector(next.front());
            visited.push_back(next.front());
        }
    }

    // ── Light effects ───────────────────────────────────────────────────────

    // How far a light effect is from bright (0) to dim (1), and what drives it.
    struct LightEffectClock {
        float elapsed = 0.0f; // Glow
        float stateLeft = 0.0f; // seconds until the next change
        bool dim = false;
        float level = 0.0f;
    };

    // Effects that sync share one clock per type and timing.
    using LightSyncKey = std::tuple<uint8_t, float, float, float, float>;

    LightSyncKey SyncKeyOf(const PayloadDispatchSectorLightEffect& effect) {
        return {effect.type, effect.brightTime, effect.dimTime, effect.speed, effect.interval};
    }

    struct LightEffectState {
        ID sectorID;
        PayloadDispatchSectorLightEffect settings;
        Vector3 bright; // the sector's own light, from before the first effect
        Vector3 dim;
        LightEffectClock clock; // unless synced
    };

    std::vector<LightEffectState> lightEffects;
    std::map<LightSyncKey, LightEffectClock> lightSyncClocks;

    constexpr float MIN_LIGHT_STATE_TIME = 0.01f;

    // Seconds for Glow to go from bright to dim at `speed`.
    float GlowHalfPeriod(const LightEffectState& effect) {
        const Vector3 difference = effect.dim - effect.bright;
        const float furthest = std::max({std::abs(difference.x), std::abs(difference.y), std::abs(difference.z)});
        return effect.settings.speed > 0.0f ? furthest / effect.settings.speed : 0.0f;
    }

    // The next bright / dim stretch of a Strobe or Flicker.
    float LightStateTime(const PayloadDispatchSectorLightEffect& effect, const bool dim) {
        const float time = dim ? effect.dimTime : effect.brightTime;
        if (effect.type == PayloadDispatchSectorLightEffect::FLICKER)
            return std::max(MIN_LIGHT_STATE_TIME, MathFunctions::GetRandomFloat(0.0f, time));
        return std::max(MIN_LIGHT_STATE_TIME, time);
    }

    void AdvanceLightClock(LightEffectClock& clock, const PayloadDispatchSectorLightEffect& effect, const float deltaTime) {
        using Effect = PayloadDispatchSectorLightEffect;

        clock.elapsed += deltaTime;

        switch (effect.type) {
            case Effect::STROBE:
            case Effect::FLICKER:
                clock.stateLeft -= deltaTime;
                while (clock.stateLeft <= 0.0f) {
                    clock.dim = !clock.dim;
                    clock.stateLeft += LightStateTime(effect, clock.dim);
                }
                clock.level = clock.dim ? 1.0f : 0.0f;
                break;

            case Effect::FIRE_FLICKER: {
                const float interval = std::max(MIN_LIGHT_STATE_TIME, effect.interval);
                clock.stateLeft -= deltaTime;
                if (clock.stateLeft <= 0.0f) {
                    // One of four steps from bright to dim, like Doom's fire.
                    clock.level = static_cast<float>(std::min(3, static_cast<int>(MathFunctions::GetRandomFloat(0.0f, 4.0f)))) / 3.0f;
                    clock.stateLeft = std::max(clock.stateLeft + interval, MIN_LIGHT_STATE_TIME);
                }
                break;
            }

            case Effect::GLOW:
            case Effect::NONE:
                break;
        }
    }

    // A clock starting at a random point of the effect, so unsynced sectors
    // don't all change together.
    LightEffectClock RandomLightClock(const LightEffectState& effect) {
        using Effect = PayloadDispatchSectorLightEffect;
        LightEffectClock clock;

        switch (effect.settings.type) {
            case Effect::STROBE:
            case Effect::FLICKER:
                clock.dim = MathFunctions::GetRandomFloat(0.0f, 1.0f) < 0.5f;
                clock.stateLeft = MathFunctions::GetRandomFloat(0.0f, LightStateTime(effect.settings, clock.dim));
                clock.level = clock.dim ? 1.0f : 0.0f;
                break;
            case Effect::FIRE_FLICKER:
                clock.stateLeft = MathFunctions::GetRandomFloat(0.0f, std::max(MIN_LIGHT_STATE_TIME, effect.settings.interval));
                break;
            case Effect::GLOW:
                clock.elapsed = MathFunctions::GetRandomFloat(0.0f, 2.0f * GlowHalfPeriod(effect));
                break;
            case Effect::NONE:
                break;
        }

        return clock;
    }

    // A clock at the start of the effect: bright, as the sector is.
    LightEffectClock FreshLightClock(const PayloadDispatchSectorLightEffect& effect) {
        LightEffectClock clock;
        clock.stateLeft = effect.type == PayloadDispatchSectorLightEffect::FIRE_FLICKER
            ? std::max(MIN_LIGHT_STATE_TIME, effect.interval)
            : LightStateTime(effect, false);
        return clock;
    }

    // The darkest neighbouring sector's light, nullopt without neighbours.
    std::optional<Vector3> DarkestNeighborLight(const Level& level, const ID sectorID) {
        std::optional<Vector3> darkest;
        for (const Sector* neighbor : NeighborSectors(level, sectorID)) {
            const Vector3& light = neighbor->light;
            if (!darkest || light.x + light.y + light.z < darkest->x + darkest->y + darkest->z) darkest = light;
        }
        return darkest;
    }

    void StopLightEffect(Level& level, const ID sectorID, const bool restore) {
        const auto it = std::ranges::find(lightEffects, sectorID, &LightEffectState::sectorID);
        if (it == lightEffects.end()) return;

        if (restore)
            if (Sector* sector = level.GetSector(sectorID)) WriteSectorLight(level, *sector, it->bright);

        lightEffects.erase(it);
    }

    void StartLightEffect(Level& level, const ID sectorID, const PayloadDispatchSectorLightEffect& settings) {
        using Effect = PayloadDispatchSectorLightEffect;

        Sector* sector = level.GetSector(sectorID);
        if (!sector) return;

        if (settings.type == Effect::NONE) {
            StopLightEffect(level, sectorID, true);
            return;
        }

        // A new effect replaces a running one but keeps its bright light,
        // which is the sector's own, not wherever the last one left it.
        const auto old = std::ranges::find(lightEffects, sectorID, &LightEffectState::sectorID);
        const Vector3 bright = old != lightEffects.end() ? old->bright : sector->light;

        Vector3 dim = settings.dimLight;
        if (settings.dimMode == Effect::LOWEST_NEIGHBOR) {
            const std::optional<Vector3> darkest = DarkestNeighborLight(level, sectorID);
            if (!darkest) {
                spdlog::warn("Light Effect on sector {} has no neighbouring sectors to take its dim light from", sectorID);
                return;
            }
            dim = *darkest;
        }

        if (old != lightEffects.end()) lightEffects.erase(old);

        // An effect and a fade would fight over the light.
        sector->lightFade.active = false;

        LightEffectState effect{sectorID, settings, bright, dim};
        if (settings.sync) lightSyncClocks.try_emplace(SyncKeyOf(settings), FreshLightClock(settings));
        else effect.clock = RandomLightClock(effect);

        lightEffects.push_back(effect);
    }

    void UpdateLightEffects(Level& level, const float deltaTime) {
        using Effect = PayloadDispatchSectorLightEffect;

        // Clocks nobody uses any more go.
        std::erase_if(lightSyncClocks, [](const auto& entry) {
            return std::ranges::none_of(lightEffects, [&](const LightEffectState& effect) {
                return effect.settings.sync && SyncKeyOf(effect.settings) == entry.first;
            });
        });

        // Each shared clock moves once a frame, by its first user's settings
        // (all of them have the same timing).
        for (auto& [key, clock] : lightSyncClocks) {
            const auto user = std::ranges::find_if(lightEffects, [&](const LightEffectState& effect) {
                return effect.settings.sync && SyncKeyOf(effect.settings) == key;
            });
            AdvanceLightClock(clock, user->settings, deltaTime);
        }

        std::erase_if(lightEffects, [&](LightEffectState& effect) {
            Sector* sector = level.GetSector(effect.sectorID);
            if (!sector) return true;

            LightEffectClock* clock = &effect.clock;
            if (effect.settings.sync) clock = &lightSyncClocks.at(SyncKeyOf(effect.settings));
            else AdvanceLightClock(*clock, effect.settings, deltaTime);

            float t = clock->level;
            if (effect.settings.type == Effect::GLOW) {
                // Down to dim and back up, over and over.
                const float half = GlowHalfPeriod(effect);
                const float phase = half > 0.0f ? std::fmod(clock->elapsed, 2.0f * half) / half : 0.0f;
                t = phase <= 1.0f ? phase : 2.0f - phase;
            }

            WriteSectorLight(level, *sector, effect.bright + (effect.dim - effect.bright) * t);
            return false;
        });
    }

    // ── Set Field ───────────────────────────────────────────────────────────

    // One field of one component of an entity.
    struct FieldKey {
        ID entityID;
        FieldComponent component;
        int componentIndex;
        std::string field;

        bool operator==(const FieldKey&) const = default;
    };

    // A field sliding toward `target` (Set Field over time).
    struct FieldTween {
        FieldKey key;
        FieldValueKind kind;
        FieldValue current; // kept here, so an int field doesn't stall on rounding
        FieldValue target;
        float speed; // per channel, per second
        bool realTime;
    };

    // A Set Field's revert: waits for the change to arrive, then
    // `waitLeft` seconds, then puts `original` back.
    struct FieldRevert {
        enum Phase { ARRIVING, WAITING, RETURNING };

        FieldKey key;
        FieldValueKind kind;
        FieldValue original;
        float revertAfter;
        float speed; // 0: back at once
        bool realTime;
        Phase phase = ARRIVING;
        float waitLeft = 0.0f;
    };

    std::vector<FieldTween> fieldTweens;
    std::vector<FieldRevert> fieldReverts;

    template <typename T>
    T* NthComponent(ComponentStorage<T>& storage, const ID ownerID, const int index) {
        const std::vector<ComponentInstanceID>& instances = storage.InstancesOf(ownerID);
        if (index < 0 || index >= static_cast<int>(instances.size())) return nullptr;
        return storage.GetInstance(instances[index]);
    }

    // Copies a field into `read` and/or from `write`.
    void Access(float& field, FieldValue* read, const FieldValue* write) {
        if (read) read->number = {field, 0.0f, 0.0f, 0.0f};
        if (write) field = write->number.x;
    }

    void Access(bool& field, FieldValue* read, const FieldValue* write) {
        if (read) read->flag = field;
        if (write) field = write->flag;
    }

    void Access(std::string& field, FieldValue* read, const FieldValue* write) {
        if (read) read->text = field;
        if (write) field = write->text;
    }

    void Access(Vector2& field, FieldValue* read, const FieldValue* write) {
        if (read) read->number = {field.x, field.y, 0.0f, 0.0f};
        if (write) field = {write->number.x, write->number.y};
    }

    void Access(Vector3& field, FieldValue* read, const FieldValue* write) {
        if (read) read->number = {field.x, field.y, field.z, 0.0f};
        if (write) field = {write->number.x, write->number.y, write->number.z};
    }

    void Access(Vector4& field, FieldValue* read, const FieldValue* write) {
        if (read) read->number = field;
        if (write) field = write->number;
    }

    // Reads (`read`) and/or writes (`write`) one engine field. False if the
    // entity has no such component or the field isn't in EVENT_FIELDS.
    bool AccessEngineField(Level& level, const FieldKey& key, FieldValue* read, const FieldValue* write) {
        const ID owner = key.entityID;
        const int index = key.componentIndex;
        const std::string_view field = key.field;

        switch (key.component) {
            case FieldComponent::TRANSFORM: {
                ComponentTransform* c = index == 0 ? level.transforms.Get(owner) : nullptr;
                if (!c) return false;

                if (field == "position") {
                    Vector3 position = c->position;
                    Access(position, read, write);
                    if (write) PlaceTransform(level, *c, position);
                    return true;
                }
                if (field == "rotation") {
                    Vector3 euler = c->rotation.Normalized().ToEulerDegrees();
                    Access(euler, read, write);
                    if (write) c->rotation = Quaternion::FromEulerDegrees(euler.x, euler.y, euler.z);
                    return true;
                }
                if (field == "scale") { Access(c->scale, read, write); return true; }
                return false;
            }
            case FieldComponent::SPRITE: {
                ComponentSprite* c = NthComponent(level.sprites, owner, index);
                if (!c) return false;
                if (field == "color") { Access(c->color, read, write); return true; }
                if (field == "isActive") { Access(c->isActive, read, write); return true; }
                if (field == "offset") { Access(c->offset, read, write); return true; }
                return false;
            }
            case FieldComponent::AUDIO_SOURCE: {
                ComponentAudioSource* c = NthComponent(level.audioSources, owner, index);
                if (!c) return false;
                if (field == "sound") { Access(c->soundFileName, read, write); return true; }
                if (field == "gain") { Access(c->gain, read, write); return true; }
                if (field == "pitch") { Access(c->pitch, read, write); return true; }
                if (field == "looping") { Access(c->looping, read, write); return true; }
                if (field == "referenceDistance") { Access(c->referenceDistance, read, write); return true; }
                if (field == "maxDistance") { Access(c->maxDistance, read, write); return true; }
                if (field == "rollOffFactor") { Access(c->rollOffFactor, read, write); return true; }
                if (field == "offset") { Access(c->offset, read, write); return true; }
                return false;
            }
            case FieldComponent::PLAYER_CONTROLLER: {
                ComponentPlayerController* c = NthComponent(level.playerControllers, owner, index);
                if (!c) return false;
                if (field == "speed") { Access(c->speed, read, write); return true; }
                if (field == "runningSpeed") { Access(c->runningSpeed, read, write); return true; }
                if (field == "jumpPower") { Access(c->jumpPower, read, write); return true; }
                if (field == "eyeHeight") { Access(c->eyeHeight, read, write); return true; }
                if (field == "acceleration") { Access(c->acceleration, read, write); return true; }
                if (field == "deceleration") { Access(c->deceleration, read, write); return true; }
                if (field == "airControl") { Access(c->airControl, read, write); return true; }
                if (field == "sensitivityX") { Access(c->sensitivityX, read, write); return true; }
                if (field == "sensitivityY") { Access(c->sensitivityY, read, write); return true; }
                if (field == "useReach") { Access(c->useReach, read, write); return true; }
                return false;
            }
            case FieldComponent::CAMERA: {
                ComponentCamera* c = NthComponent(level.cameras, owner, index);
                if (!c) return false;
                if (field == "fov") { Access(c->fov, read, write); return true; }
                if (field == "smoothStep") { Access(c->smoothStep, read, write); return true; }
                return false;
            }
            case FieldComponent::COLLIDER: {
                ComponentCollider* c = NthComponent(level.colliders, owner, index);
                if (!c) return false;
                if (field == "isActive") {
                    // SetActive moves colliders between partitions.
                    bool active = c->isActive;
                    Access(active, read, write);
                    if (write && active != c->isActive) level.colliders.SetActive(c->instanceID, active);
                    return true;
                }
                if (field == "isTrigger") { Access(c->isTrigger, read, write); return true; }
                if (field == "scale") { Access(c->scale, read, write); return true; }
                if (field == "offset") { Access(c->offset, read, write); return true; }
                if (field == "stepSize") { Access(c->stepSize, read, write); return true; }
                if (field == "maxSlope") { Access(c->maxSlope, read, write); return true; }
                return false;
            }
            case FieldComponent::RIGIDBODY: {
                ComponentRigidbody* c = NthComponent(level.rigidbodies, owner, index);
                if (!c) return false;
                if (field == "isStatic") { Access(c->isStatic, read, write); return true; }
                if (field == "mass") { Access(c->mass, read, write); return true; }
                if (field == "gravityScale") { Access(c->gravityScale, read, write); return true; }
                if (field == "friction") { Access(c->friction, read, write); return true; }
                return false;
            }
            case FieldComponent::MODEL: {
                ComponentModel* c = NthComponent(level.models, owner, index);
                if (!c) return false;
                if (field == "offset") { Access(c->offset, read, write); return true; }
                return false;
            }
            case FieldComponent::FLIPBOOK: {
                ComponentFlipbook* c = NthComponent(level.flipbooks, owner, index);
                if (!c) return false;
                if (field == "speed") { Access(c->speed, read, write); return true; }
                return false;
            }
            case FieldComponent::PATHFINDER: {
                ComponentPathfinder* c = NthComponent(level.pathfinders, owner, index);
                if (!c) return false;
                if (field == "isActive") { Access(c->isActive, read, write); return true; }
                if (field == "speed") {
                    Access(c->speed, read, write);
                    c->speed = std::max(c->speed, 0.0f);
                    return true;
                }
                if (field == "range") {
                    Access(c->range, read, write);
                    c->range = std::max(c->range, 0.0f);
                    return true;
                }
                if (field == "offset") { Access(c->offset, read, write); return true; }
                if (field == "chaseStep") { Access(c->chaseStep, read, write); c->chaseStep = std::max(c->chaseStep, 0.0f); return true; }
                if (field == "holdMinSteps") { Access(c->holdMinSteps, read, write); c->holdMinSteps = std::max(c->holdMinSteps, 0.0f); return true; }
                if (field == "holdMaxSteps") { Access(c->holdMaxSteps, read, write); c->holdMaxSteps = std::max(c->holdMaxSteps, 0.0f); return true; }
                if (field == "deadzone") { Access(c->deadzone, read, write); c->deadzone = std::max(c->deadzone, 0.0f); return true; }
                if (field == "swapChance") { Access(c->swapChance, read, write); return true; }
                if (field == "floatSpeed") { Access(c->floatSpeed, read, write); c->floatSpeed = std::max(c->floatSpeed, 0.0f); return true; }
                if (field == "floatRatio") { Access(c->floatRatio, read, write); c->floatRatio = std::max(c->floatRatio, 0.0f); return true; }
                if (field == "pushFriction") { Access(c->pushFriction, read, write); c->pushFriction = std::clamp(c->pushFriction, 0.0f, 1.0f); return true; }
                if (field == "stuckTime") { Access(c->stuckTime, read, write); c->stuckTime = std::max(c->stuckTime, 0.0f); return true; }

                constexpr std::pair<const char*, ComponentPathfinder::MetadataMask> kFlags[] = {
                    {"isAirEntity", ComponentPathfinder::IS_AIR_ENTITY},
                    {"isRangedEntity", ComponentPathfinder::IS_RANGED_ENTITY},
                    {"canDropOff", ComponentPathfinder::CAN_DROP_OFF},
                    {"opensDoors", ComponentPathfinder::OPENS_DOORS},
                    {"keepMovingInRange", ComponentPathfinder::KEEP_MOVING_IN_RANGE}
                };
                for (const auto& [name, flag] : kFlags) {
                    if (field != name) continue;
                    bool on = (c->metadata & flag) != 0;
                    Access(on, read, write);
                    if (write) c->SetFlag(flag, on);
                    return true;
                }
                return false;
            }
            case FieldComponent::MONSTER_AI: {
                ComponentMonsterAI* c = NthComponent(level.monsterAIs, owner, index);
                if (!c) return false;

                const std::pair<const char*, float*> floats[] = {
                    {"sightFov", &c->sightFov}, {"sightRange", &c->sightRange}, {"nearRange", &c->nearRange},
                    {"lookInterval", &c->lookInterval}, {"chaseStep", &c->chaseStep}, {"reactionTime", &c->reactionTime},
                    {"meleeRange", &c->meleeRange}, {"missileMinRange", &c->missileMinRange}, {"missileMaxRange", &c->missileMaxRange},
                    {"missileDistanceOffset", &c->missileDistanceOffset}, {"missileDistanceScale", &c->missileDistanceScale},
                    {"missileDistanceCap", &c->missileDistanceCap}, {"attackPause", &c->attackPause},
                    {"painChance", &c->painChance}, {"painPause", &c->painPause}, {"targetLockTime", &c->targetLockTime},
                    {"activeChance", &c->activeChance}, {"health", &c->health}
                };
                for (const auto& [name, value] : floats) {
                    if (field != name) continue;
                    Access(*value, read, write);
                    *value = std::max(*value, 0.0f);
                    return true;
                }

                const std::pair<const char*, bool*> flags[] = {
                    {"isActive", &c->isActive}, {"hasMelee", &c->hasMelee}, {"hasMissile", &c->hasMissile}, {"hearing", &c->hearing},
                    {"ambush", &c->ambush}, {"aggressive", &c->aggressive}, {"infights", &c->infights},
                    {"drivePathfinder", &c->drivePathfinder}, {"faceTargetOnAttack", &c->faceTargetOnAttack}
                };
                for (const auto& [name, value] : flags) {
                    if (field != name) continue;
                    Access(*value, read, write);
                    return true;
                }
                return false;
            }
            case FieldComponent::UI_TRANSFORM: {
                ComponentUITransform* c = index == 0 ? level.ui_transforms.Get(owner) : nullptr;
                if (!c) return false;
                if (field == "position") { Access(c->position, read, write); return true; }
                if (field == "scale") { Access(c->scale, read, write); return true; }
                if (field == "rotation") { Access(c->rotation, read, write); return true; }
                return false;
            }
            case FieldComponent::UI_SPRITE: {
                ComponentUISprite* c = NthComponent(level.ui_sprites, owner, index);
                if (!c) return false;
                if (field == "texture") { Access(c->texture, read, write); return true; }
                if (field == "color") { Access(c->color, read, write); return true; }
                if (field == "isActive") { Access(c->isActive, read, write); return true; }
                return false;
            }
            case FieldComponent::UI_TEXT: {
                ComponentUIText* c = NthComponent(level.ui_texts, owner, index);
                if (!c) return false;
                if (field == "text") { Access(c->text, read, write); return true; }
                if (field == "prefix") { Access(c->prefix, read, write); return true; }
                if (field == "suffix") { Access(c->suffix, read, write); return true; }
                if (field == "fontSize") { Access(c->fontSize, read, write); return true; }
                if (field == "color") { Access(c->color, read, write); return true; }
                return false;
            }
            case FieldComponent::UI_BUTTON: {
                ComponentUIButton* c = NthComponent(level.ui_buttons, owner, index);
                if (!c) return false;
                if (field == "interactable") { Access(c->interactable, read, write); return true; }
                if (field == "normalColor") { Access(c->normalColor, read, write); return true; }
                if (field == "hoverColor") { Access(c->hoverColor, read, write); return true; }
                if (field == "pressedColor") { Access(c->pressedColor, read, write); return true; }
                if (field == "disabledColor") { Access(c->disabledColor, read, write); return true; }
                return false;
            }
            case FieldComponent::SCRIPT:
                return false;
        }

        return false;
    }

    // The field's kind and value now, nullopt if it can't be found.
    std::optional<std::pair<FieldValueKind, FieldValue>> ReadField(Level& level, const FieldKey& key) {
        if (key.component == FieldComponent::SCRIPT) {
            if (!hooks.getScriptField) return std::nullopt;
            return hooks.getScriptField(level, key.entityID, key.componentIndex, key.field);
        }

        const EventFieldInfo* info = FindEventField(key.component, key.field);
        if (!info) return std::nullopt;

        FieldValue value;
        if (!AccessEngineField(level, key, &value, nullptr)) return std::nullopt;
        return std::pair{info->kind, value};
    }

    void WriteField(Level& level, const FieldKey& key, const FieldValueKind kind, FieldValue value) {
        // Ints and enums are whole numbers, however they got here.
        if (kind == FieldValueKind::INT || kind == FieldValueKind::ENUM) value.number.x = std::round(value.number.x);

        if (key.component == FieldComponent::SCRIPT) {
            if (hooks.setScriptField) hooks.setScriptField(level, key.entityID, key.componentIndex, key.field, kind, value);
            return;
        }

        AccessEngineField(level, key, nullptr, &value);
    }

    // What a Set Field's value is, fixed or from its variable.
    FieldValue SetFieldInput(Level& level, const PayloadDispatchEntitySetField& set) {
        if (!set.fromVariable) return set.value;

        const float* variable = set.variable != 0 ? level.GetEventVariableValue(set.variable) : nullptr;
        const float number = variable ? *variable : 0.0f;

        FieldValue value;
        value.number = {number, number, number, number};
        value.flag = number != 0.0f;
        value.text = FormatEventNumber(number);
        return value;
    }

    // Moves `current` toward `target` by at most `step` per channel. True once there.
    bool StepFieldToward(FieldValue& current, const FieldValue& target, const int channels, const float step) {
        float* values[] = {&current.number.x, &current.number.y, &current.number.z, &current.number.w};
        const float targets[] = {target.number.x, target.number.y, target.number.z, target.number.w};

        bool arrived = true;
        for (int i = 0; i < channels; ++i) arrived &= StepToward(*values[i], targets[i], step);
        return arrived;
    }

    // Starts sliding the field to `target`, replacing a slide already running on it.
    void StartFieldTween(const FieldKey& key, const FieldValueKind kind, const FieldValue& current, const FieldValue& target,
                         const float speed, const bool realTime) {
        std::erase_if(fieldTweens, [&](const FieldTween& tween) { return tween.key == key; });
        fieldTweens.push_back({key, kind, current, target, speed, realTime});
    }

    void SetField(Level& level, const ID entityID, const PayloadDispatchEntitySetField& set) {
        using Set = PayloadDispatchEntitySetField;

        const FieldKey key{entityID, set.component, set.componentIndex, set.field};

        const std::optional<std::pair<FieldValueKind, FieldValue>> read = ReadField(level, key);
        if (!read) {
            spdlog::warn("Set Field dispatch on entity {}: it has no field '{}' on that component, skipped", entityID, set.field);
            return;
        }

        const auto& [kind, current] = *read;
        const FieldValue input = SetFieldInput(level, set);
        const bool numeric = IsNumericFieldKind(kind);
        const int channels = FieldChannelCount(kind);

        FieldValue target = current;
        switch (set.operation) {
            case Set::SET:
                target = input;
                break;
            case Set::ADD:
            case Set::MULTIPLY: {
                if (!numeric) {
                    spdlog::warn("Set Field dispatch on entity {}: '{}' isn't a number, so it can't be added to or multiplied", entityID, set.field);
                    return;
                }
                const float* inputs[] = {&input.number.x, &input.number.y, &input.number.z, &input.number.w};
                float* values[] = {&target.number.x, &target.number.y, &target.number.z, &target.number.w};
                for (int i = 0; i < channels; ++i) {
                    if (set.operation == Set::ADD) *values[i] += *inputs[i];
                    else *values[i] *= *inputs[i];
                }
                break;
            }
            case Set::TOGGLE:
                if (kind != FieldValueKind::BOOL) {
                    spdlog::warn("Set Field dispatch on entity {}: '{}' isn't on/off, so it can't be toggled", entityID, set.field);
                    return;
                }
                target.flag = !current.flag;
                break;
        }

        const bool slides = set.overTime && numeric && set.speed > 0.0f;

        if (set.revert) {
            // A revert already waiting keeps the value from before its first run.
            const auto existing = std::ranges::find(fieldReverts, key, &FieldRevert::key);
            if (existing != fieldReverts.end()) {
                existing->phase = FieldRevert::ARRIVING;
                existing->revertAfter = std::max(0.0f, set.revertAfter);
                existing->speed = slides ? set.speed : 0.0f;
                existing->realTime = realTimeChain;
            }
            else fieldReverts.push_back({key, kind, current, std::max(0.0f, set.revertAfter), slides ? set.speed : 0.0f, realTimeChain});
        }

        if (slides) StartFieldTween(key, kind, current, target, set.speed, realTimeChain);
        else {
            std::erase_if(fieldTweens, [&](const FieldTween& tween) { return tween.key == key; });
            WriteField(level, key, kind, target);
        }
    }

    void UpdateFieldChanges(Level& level, const float deltaTime, const float realDeltaTime) {
        // Paused while the entity is disabled, like Move To.
        const auto paused = [&](const FieldKey& key) { return !IsEnabledEntity(level, key.entityID); };

        std::erase_if(fieldTweens, [&](FieldTween& tween) {
            if (!level.GetEntity(tween.key.entityID)) return true;
            if (paused(tween.key)) return false;

            const float step = tween.speed * (tween.realTime ? realDeltaTime : deltaTime);
            const bool arrived = StepFieldToward(tween.current, tween.target, FieldChannelCount(tween.kind), step);
            WriteField(level, tween.key, tween.kind, tween.current);
            return arrived;
        });

        std::erase_if(fieldReverts, [&](FieldRevert& revert) {
            if (!level.GetEntity(revert.key.entityID)) return true;
            if (paused(revert.key)) return false;

            const bool sliding = std::ranges::any_of(fieldTweens, [&](const FieldTween& tween) { return tween.key == revert.key; });

            switch (revert.phase) {
                case FieldRevert::ARRIVING:
                    if (sliding) return false;
                    revert.phase = FieldRevert::WAITING;
                    revert.waitLeft = revert.revertAfter;
                    [[fallthrough]];

                case FieldRevert::WAITING: {
                    revert.waitLeft -= revert.realTime ? realDeltaTime : deltaTime;
                    if (revert.waitLeft > 0.0f) return false;

                    const std::optional<std::pair<FieldValueKind, FieldValue>> now = ReadField(level, revert.key);
                    if (!now) return true;

                    if (revert.speed > 0.0f) {
                        StartFieldTween(revert.key, revert.kind, now->second, revert.original, revert.speed, revert.realTime);
                        revert.phase = FieldRevert::RETURNING;
                        return false;
                    }

                    WriteField(level, revert.key, revert.kind, revert.original);
                    return true;
                }

                case FieldRevert::RETURNING:
                    return !sliding;
            }

            return true;
        });
    }
}

namespace EventSystem {
    void PlayOneShot(const std::string& sound, const float gain, const float pitch, const std::optional<Vector3>& position) {
        PayloadDispatchPlaySound play;
        play.sound = sound;
        play.gain = gain;
        play.pitch = pitch;
        PlaySound(play, position);
    }

    void CreateRegistry(const Level& level) {
        dispatchRegistry.clear();
        crossWalls.clear();
        staySectors.clear();

        namedDispatches.clear();

        const auto add = [](const EventOwnerKind kind, const ID ownerID, const std::vector<Dispatch>& dispatches) {
            for (size_t i = 0; i < dispatches.size(); ++i) {
                const Dispatch& dispatch = dispatches[i];
                if (dispatch.useTag) dispatchRegistry.emplace(dispatch.tagId, DispatchHandle{kind, ownerID, i});

                if (dispatch.name.empty()) continue;
                const auto [it, added] = dispatchNameIDs.try_emplace(dispatch.name, static_cast<uint32_t>(dispatchNameIDs.size() + 1));
                namedDispatches[NamedDispatchKey(kind, ownerID, it->second)].push_back({kind, ownerID, i});
            }
        };

        for (const Wall& wall : level.walls) add(EventOwnerKind::LINE, wall.id, wall.dispatches);
        for (const Linedef& linedef : level.linedefs) add(EventOwnerKind::LINEDEF, linedef.id, linedef.dispatches);
        for (const Sector& sector : level.sectors) add(EventOwnerKind::SECTOR, sector.id, sector.dispatches);
        for (const Entity& entity : level.entities) add(EventOwnerKind::ENTITY, entity.id, entity.dispatches);
        add(EventOwnerKind::LEVEL, ID{0}, level.worldSettings.dispatches);

        // Calls by name become calls by number. A name no dispatch has gets 0.
        const auto resolve = [](const DirectCall& call) {
            const auto it = dispatchNameIDs.find(call.dispatch);
            call.nameID = it != dispatchNameIDs.end() ? it->second : 0;
        };

        ForEachEventOwner(level, [&](const EventOwnerKind, const ID, const std::vector<Event>& events) {
            for (const Event& event : events)
                for (const DirectCall& call : event.calls) resolve(call);
        });

        const auto resolveRelays = [&](const std::vector<Dispatch>& dispatches) {
            for (const Dispatch& dispatch : dispatches)
            {
                if (const auto* relay = std::get_if<PayloadDispatchRelay>(&dispatch.payload)) resolve(relay->call);
                for (const DispatchFlow& flow : dispatch.flows) resolve(flow.call);
            }
        };
        for (const Wall& wall : level.walls) resolveRelays(wall.dispatches);
        for (const Linedef& linedef : level.linedefs) resolveRelays(linedef.dispatches);
        for (const Sector& sector : level.sectors) resolveRelays(sector.dispatches);
        for (const Entity& entity : level.entities) resolveRelays(entity.dispatches);
        resolveRelays(level.worldSettings.dispatches);

        for (const Wall& wall : level.walls) {
            const bool hasCross = std::ranges::any_of(wall.events, [](const Event& event) {
                return std::holds_alternative<PayloadLineEntityCross>(event.payload);
            });
            if (!hasCross) continue;

            crossWalls.push_back({wall.id, WallFrontIsNormalSide(level, wall)});
        }

        for (const Sector& sector : level.sectors) {
            const bool hasStay = std::ranges::any_of(sector.events, [](const Event& event) {
                return std::holds_alternative<PayloadSectorEntityStay>(event.payload);
            });
            if (hasStay) staySectors.insert(sector.id);
        }

        // Timers and condition watches keep their state across a rebuild; a
        // new condition watch starts from the condition's current value.
        std::vector<TimerState> newTimers;
        std::vector<ConditionWatch> newWatches;

        ForEachEventOwner(level, [&](const EventOwnerKind kind, const ID ownerID, const std::vector<Event>& events) {
            for (size_t i = 0; i < events.size(); ++i) {
                const EventHandle handle{kind, ownerID, i};

                if (std::holds_alternative<PayloadTimer>(events[i].payload)) {
                    const auto old = std::ranges::find(timers, handle, &TimerState::event);
                    newTimers.push_back(old != timers.end() ? *old : TimerState{handle});
                }
                else if (std::holds_alternative<PayloadConditionMet>(events[i].payload)) {
                    const auto old = std::ranges::find(conditionWatches, handle, &ConditionWatch::event);
                    newWatches.push_back(old != conditionWatches.end()
                        ? *old
                        : ConditionWatch{handle, level.CheckCondition(events[i].condition)});
                }
            }
        });

        timers = std::move(newTimers);
        conditionWatches = std::move(newWatches);
    }

    void Start(Level& level, Hooks newHooks) {
        // Messages the level's scripts showed while starting stay.
        std::vector<ShownMessage> messages = std::move(shownMessages);
        Stop();
        shownMessages = std::move(messages);
        hooks = std::move(newHooks);
        running = true;
        MapQueries::AssignLinedefsToSectors(level);
        CreateRegistry(level);

        // Counts are runtime only: every play session starts from 0.
        ForEachEventOwner(level, [&](const EventOwnerKind kind, const ID ownerID, const std::vector<Event>&) {
            if (std::vector<Event>* events = ResolveEvents(level, kind, ownerID))
                for (Event& event : *events) event.triggerCount = 0;
        });

        for (const Entity& entity : level.entities) lastEnabled[entity.id] = entity.enabled;

        for (const ComponentTransform& transform : level.transforms.components) {
            if (level.transforms.Get(transform.ownerID) != &transform) continue;

            lastPositions[transform.ownerID] = {transform.position.x, transform.position.z};
            lastSectors[transform.ownerID] = SectorIDAt(level, transform.sectorIndex);
        }

        // Moves that ended before play don't count.
        level.sectorMoveEnds.clear();
    }

    void FireOnStart(Level& level, const std::unordered_set<ID>* skipEntities) {
        if (!running) return;

        // IDs first: a dispatch can add or remove things while these run.
        std::vector<std::pair<EventOwnerKind, ID>> owners{{EventOwnerKind::LEVEL, ID{0}}};
        for (const Wall& wall : level.walls) owners.emplace_back(EventOwnerKind::LINE, wall.id);
        for (const Linedef& linedef : level.linedefs)
            if (linedef.enabled) owners.emplace_back(EventOwnerKind::LINEDEF, linedef.id);
        for (const Sector& sector : level.sectors) owners.emplace_back(EventOwnerKind::SECTOR, sector.id);
        for (const Entity& entity : level.entities)
            if (entity.enabled && (skipEntities == nullptr || !skipEntities->contains(entity.id)))
                owners.emplace_back(EventOwnerKind::ENTITY, entity.id);

        for (const auto& [kind, ownerID] : owners) {
            if (kind == EventOwnerKind::ENTITY && !IsEnabledEntity(level, ownerID)) continue; // disabled by an earlier one
            FireEvents<PayloadOnStart>(level, kind, ownerID, INVALID_ID, {});
        }
    }

    void Update(Level& level, const PhysicsSystem::Contacts& contacts) {
        const float deltaTime = GameTime::deltaTime;
        ++updateCount;
        inUpdate = true;

        TickCooldowns(deltaTime, GameTime::unscaledDeltaTime);
        UpdateTeleportLocks(level);
        RunPendingDispatches(level, deltaTime, GameTime::unscaledDeltaTime);
        RunPendingFlows(level, deltaTime, GameTime::unscaledDeltaTime);

        UpdateColorFades(level, deltaTime);
        UpdateDoors(level, deltaTime);
        UpdateLifts(level, deltaTime);
        UpdateLightEffects(level, deltaTime);
        UpdateFieldChanges(level, deltaTime, GameTime::unscaledDeltaTime);
        UpdateEntityMoves(level, deltaTime);
        UpdateSounds();
        UpdateMessages(level.worldSettings.messageStyle, deltaTime);

        FireEnableEvents(level);
        FireKeyEvents(level);
        FireMovementEvents(level);
        FireStayEvents(level, deltaTime);
        FireGroundedEvents(level);
        FireContactEvents(level, contacts);
        FireLinedefContactEvents(level);
        FireRayEvents(level);
        FireMoveFinishedEvents(level);
        UpdateTimers(level, deltaTime);

        // Last, so a variable changed by anything above counts this frame.
        UpdateConditionWatches(level);
        inUpdate = false;
    }

    void Stop() {
        for (const std::string& name : soundSources) SoundManager::DestroySource(name);

        hooks = {};
        running = false;
        dispatchRegistry.clear();
        dispatchNameIDs.clear();
        namedDispatches.clear();
        crossWalls.clear();
        colorFades.clear();
        pendingDispatches.clear();
        pendingFlows.clear();
        updateCount = 0;
        inUpdate = false;
        cooldowns.clear();
        timers.clear();
        conditionWatches.clear();
        staySectors.clear();
        stayClocks.clear();
        rayReports.clear();
        lastRayKeys.clear();
        doors.clear();
        lifts.clear();
        lightEffects.clear();
        lightSyncClocks.clear();
        fieldTweens.clear();
        fieldReverts.clear();
        entityMoves.clear();
        soundSources.clear();
        shownMessages.clear();
        relayDepth = 0;
        realTimeChain = false;
        dispatchRefused = false;
        lastPositions.clear();
        lastSectors.clear();
        lastEnabled.clear();
        teleportLocks.clear();
        lastEntityContacts.clear();
        lastWallContacts.clear();
        lastLinedefContacts.clear();
    }

    // Does what the dispatch's payload says. False when the owner skipped it
    // (wrong kind of object, a disabled entity).
    static bool RunPayload(Level& level, const Dispatch& dispatch, const EventOwnerKind ownerKind, const ID ownerID,
                           const ID instigatorID) {
        // A dispatch for another kind of owner (a sector move on an entity)
        // would use the owner's ID as the wrong kind of thing.
        if (!CanAttachTo(dispatch.payload, ownerKind)) {
            spdlog::warn("Dispatch '{}' is attached to the wrong kind of object (owner {}), skipped",
                         GetTranslationKey(dispatch.payload), ownerID);
            return false;
        }

        // A disabled entity ignores every dispatch but Enable and Destroy.
        if (ownerKind == EventOwnerKind::ENTITY && !IsEnabledEntity(level, ownerID) &&
            !std::holds_alternative<PayloadDispatchEntityEnable>(dispatch.payload) &&
            !std::holds_alternative<PayloadDispatchEntityDestroy>(dispatch.payload))
            return false;

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
            [&](const PayloadDispatchLineSetTexture& setTexture) {
                Wall* wall = MapQueries::GetWallByID(level, ownerID);
                if (!wall) return;

                switch (setTexture.piece) {
                    case WallPiece::TOP: wall->top.texture.Set(setTexture.texture); break;
                    case WallPiece::MIDDLE: wall->middle.texture.Set(setTexture.texture); break;
                    case WallPiece::BOTTOM: wall->bottom.texture.Set(setTexture.texture); break;
                }
            },
            [&](const PayloadDispatchLineSetCollision& setCollision) {
                if (Wall* wall = MapQueries::GetWallByID(level, ownerID))
                    wall->middleHasCollision = ApplyToggle(setCollision.mode, wall->middleHasCollision);
            },
            [&](const PayloadDispatchLinedefSetEnabled& setEnabled) {
                if (Linedef* linedef = level.GetLinedef(ownerID))
                    linedef->enabled = ApplyToggle(setEnabled.mode, linedef->enabled);
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
            [&](const PayloadDispatchSectorMoveToHeight& move) {
                moveSector(move.floorIndex, move.speed, [&](SectorFloor& floor) {
                    const float current = move.ceiling ? floor.ceiling.height : floor.floor.height;
                    const std::optional<float> target = MoveTargetHeight(level, ownerID, current, move.mode, move.height);
                    if (!target) return;

                    if (move.ceiling) floor.MoveCeilingTo(*target, move.speed, move.collisionType);
                    else floor.MoveFloorTo(*target, move.speed, move.collisionType);
                });
            },
            [&](const PayloadDispatchSectorDoor& door) {
                TriggerDoor(level, ownerID, door, instigatorID);
            },
            [&](const PayloadDispatchSectorStopMovement& stop) {
                const bool floor = stop.surface != SectorSurfacePick::CEILING;
                const bool ceiling = stop.surface != SectorSurfacePick::FLOOR;

                level.StopSectorMoves(ownerID, stop.floorIndex, floor, ceiling);

                // A door's cycle is its ceiling (split: and floor) moving.
                std::erase_if(doors, [&](const DoorState& door) {
                    return door.sectorID == ownerID && door.floorIndex == stop.floorIndex &&
                           (ceiling || (floor && door.settings.split));
                });

                // A lift's is its floor.
                if (floor)
                    std::erase_if(lifts, [&](const LiftState& lift) {
                        return lift.sectorID == ownerID && lift.floorIndex == stop.floorIndex;
                    });
            },
            [&](const PayloadDispatchSectorLift& lift) {
                TriggerLift(level, ownerID, lift, instigatorID);
            },
            [&](const PayloadDispatchSectorRaiseStairs& stairs) {
                RaiseStairs(level, ownerID, stairs);
            },
            [&](const PayloadDispatchSectorLightEffect& effect) {
                StartLightEffect(level, ownerID, effect);
            },
            [&](const PayloadDispatchEntitySetField& set) {
                SetField(level, ownerID, set);
            },
            [&](const PayloadDispatchSectorSetLight& setLight) {
                Sector* sector = level.GetSector(ownerID);
                if (!sector) return;

                // Set Light ends a light effect; the fade starts from where
                // the effect left the light.
                StopLightEffect(level, ownerID, false);

                // A fade long enough for the channel that has furthest to go
                // to get there at `speed`; advanced (and passed on to child
                // sectors) by Level::UpdateSectorMovement.
                const Vector3 difference = setLight.light - sector->light;
                const float furthest = std::max({std::abs(difference.x), std::abs(difference.y), std::abs(difference.z)});
                const float duration = setLight.speed > 0.0f ? furthest / setLight.speed : 0.0f;

                sector->lightFade = {true, sector->light, setLight.light, 0.0f, duration};
            },
            [&](const PayloadDispatchSectorSetTexture& setTexture) {
                Sector* sector = level.GetSector(ownerID);
                if (!sector || setTexture.floorIndex < 0 || setTexture.floorIndex >= static_cast<int>(sector->floors.size())) return;

                EditSector(level, *sector, [&] {
                    SectorFloor& floor = sector->floors[setTexture.floorIndex];
                    if (setTexture.surface != SectorSurfacePick::CEILING) floor.floor.texture.Set(setTexture.texture);
                    if (setTexture.surface != SectorSurfacePick::FLOOR) floor.ceiling.texture.Set(setTexture.texture);
                });
            },
            [&](const PayloadDispatchChangeTag& change) {
                std::vector<TagId>* tags = OwnerTags(level, ownerKind, ownerID);
                if (!tags) return;

                const auto it = std::ranges::find(*tags, change.tag);
                if (change.remove && it != tags->end()) tags->erase(it);
                else if (!change.remove && it == tags->end()) tags->push_back(change.tag);
            },
            [&](const PayloadDispatchEntityDestroy&) {
                if (hooks.destroyEntity) hooks.destroyEntity(ownerID);
            },
            [&](const PayloadDispatchEntityTeleport& teleport) { Teleport(level, ownerID, teleport); },
            [&](const PayloadDispatchSectorTeleportInside& teleport) {
                const Sector* sector = level.GetSector(ownerID);
                if (!sector) return;

                const Vector2 centre = SectorCentre(*sector);

                // Who's inside first: teleporting moves them out (or in).
                std::vector<std::pair<ID, Vector2>> inside; // entity, offset from this sector's centre
                for (const ComponentTransform& transform : level.transforms.components) {
                    if (level.transforms.Get(transform.ownerID) != &transform) continue; // first transform only
                    if (SectorIDAt(level, transform.sectorIndex) != ownerID || !IsEnabledEntity(level, transform.ownerID)) continue;

                    // Just teleported in and hasn't left yet.
                    if (const auto lock = teleportLocks.find(transform.ownerID); lock != teleportLocks.end() && lock->second == ownerID)
                        continue;

                    const Vector2 point = {transform.position.x, transform.position.z};
                    if (teleport.insideFloor >= 0 &&
                        FloorIndexAt(*sector, point, transform.position.y - FeetAboveBase(level, transform)) != teleport.insideFloor)
                        continue;

                    if (!PassesTagFilter(teleport.filter, EntityTags(level, transform.ownerID))) continue;

                    inside.emplace_back(transform.ownerID, Vector2{point.x - centre.x, point.y - centre.y});
                }

                for (const auto& [entityID, offset] : inside) Teleport(level, entityID, teleport, offset);
            },
            [&](const PayloadDispatchEntityDisable&) {
                if (Entity* entity = level.GetEntity(ownerID)) level.SetEntityEnabled(*entity, false);
            },
            [&](const PayloadDispatchEntityEnable&) {
                if (Entity* entity = level.GetEntity(ownerID)) level.SetEntityEnabled(*entity, true);
            },
            [&](const PayloadDispatchEntityCallFunction& call) {
                if (hooks.callFunction && !call.functionName.empty())
                    hooks.callFunction(level, ownerID, call.functionName, instigatorID);
            },
            [&](const PayloadDispatchEntityChangeVelocity& change) {
                ComponentRigidbody* rigidbody = level.rigidbodies.Get(ownerID);
                if (!rigidbody) {
                    spdlog::warn("Change Velocity dispatch on entity {} skipped: it has no Rigidbody", ownerID);
                    return;
                }

                rigidbody->velocity = change.set ? change.velocity : rigidbody->velocity + change.velocity;

                // Pushed upward: off the ground, as a jump does.
                if (rigidbody->velocity.y > 0.0f) {
                    rigidbody->isGrounded = false;
                    rigidbody->groundNormal = {};
                }
            },
            [&](const PayloadDispatchEntityMoveTo& moveTo) {
                const ComponentTransform* transform = level.transforms.Get(ownerID);
                if (!transform) return;

                const Vector3 from = transform->position;
                const Vector3 to = moveTo.relative ? from + moveTo.position : moveTo.position;
                const Vector3 path = to - from;
                const float distance = std::sqrt(path.x * path.x + path.y * path.y + path.z * path.z);

                float duration = 0.0f;
                if (moveTo.useDuration) duration = std::max(moveTo.duration, 0.0f);
                else if (moveTo.speed > 0.0f) duration = distance / moveTo.speed;

                // A new move replaces one still running.
                std::erase_if(entityMoves, [&](const EntityMove& move) { return move.entityID == ownerID; });

                entityMoves.push_back({
                    ownerID, from, to, moveTo.rotate,
                    transform->rotation.Normalized(),
                    Quaternion::FromEulerDegrees(moveTo.rotation.x, moveTo.rotation.y, moveTo.rotation.z),
                    0.0f, duration
                });
            },
            [&](const PayloadDispatchEntitySetComponent& set) {
                SetComponents(level, ownerID, set.component, set.mode);
            },
            [&](const PayloadDispatchEntityPathfinder& pathfinderDispatch) {
                ComponentPathfinder* pathfinder = level.pathfinders.Get(ownerID);
                if (!pathfinder) {
                    spdlog::warn("Pathfinder dispatch on entity {} skipped: it has no Pathfinder", ownerID);
                    return;
                }

                const auto follow = [&](const ID targetID, const char* what) {
                    if (targetID == ownerID || !level.GetEntity(targetID)) {
                        if (targetID != ownerID)
                            spdlog::warn("Pathfinder dispatch on entity {} skipped: {} doesn't exist", ownerID, what);
                        return;
                    }
                    PathfinderSystem::SetTarget(level, *pathfinder, targetID, pathfinderDispatch.offset);
                };

                switch (pathfinderDispatch.action) {
                    case PathfinderAction::GO_TO_COORDINATES:
                        PathfinderSystem::SetDestination(*pathfinder, pathfinderDispatch.position);
                        break;
                    case PathfinderAction::GO_TO_SECTOR: {
                        const Sector* sector = level.GetSector(pathfinderDispatch.targetID);
                        if (!sector || sector->floors.empty()) {
                            spdlog::warn("Pathfinder dispatch on entity {} skipped: its sector {} doesn't exist",
                                         ownerID, pathfinderDispatch.targetID);
                            return;
                        }

                        const Vector2 centre = SectorCentre(*sector);
                        const int floorIndex = std::clamp(pathfinderDispatch.floorIndex, 0, static_cast<int>(sector->floors.size()) - 1);
                        const float height = MapQueries::SurfaceHeightAt(*sector, sector->floors[floorIndex].floor, centre);
                        PathfinderSystem::SetDestination(*pathfinder, {centre.x, height, centre.y});
                        break;
                    }
                    case PathfinderAction::FOLLOW_ENTITY: follow(pathfinderDispatch.targetID, "its target entity"); break;
                    case PathfinderAction::FOLLOW_INSTIGATOR:
                        if (instigatorID == INVALID_ID) {
                            spdlog::warn("Pathfinder dispatch on entity {} skipped: Follow Instigator, but the event has no instigator", ownerID);
                            return;
                        }
                        follow(instigatorID, "the instigator");
                        break;
                    case PathfinderAction::STOP: pathfinder->isActive = false; break;
                    case PathfinderAction::RESUME: pathfinder->isActive = true; break;
                    case PathfinderAction::PAUSE: PathfinderSystem::Pause(*pathfinder, pathfinderDispatch.seconds); break;
                    case PathfinderAction::PICK_NEW_DIRECTION: PathfinderSystem::PickNewDirection(*pathfinder); break;
                }
            },
            [&](const PayloadDispatchEntityMonsterAI& monsterDispatch) {
                ComponentMonsterAI* ai = level.monsterAIs.Get(ownerID);
                if (!ai) {
                    spdlog::warn("Monster AI dispatch on entity {} skipped: it has no Monster AI", ownerID);
                    return;
                }

                switch (monsterDispatch.action) {
                    case MonsterAIAction::WAKE_ON_PLAYER: MonsterAISystem::Wake(level, *ai, MonsterAISystem::FindPlayer(level)); break;
                    case MonsterAIAction::WAKE_ON_INSTIGATOR: MonsterAISystem::Wake(level, *ai, instigatorID); break;
                    case MonsterAIAction::WAKE_ON_ENTITY: MonsterAISystem::Wake(level, *ai, monsterDispatch.targetID); break;
                    case MonsterAIAction::SLEEP: MonsterAISystem::Sleep(level, *ai); break;
                    case MonsterAIAction::HURT: MonsterAISystem::Hurt(level, *ai, instigatorID, monsterDispatch.damage); break;
                }
            },
            [&](const PayloadDispatchNoiseAlert& noise) {
                const ID noiseTarget = noise.target == NoiseTarget::SELF ? (ownerKind == EventOwnerKind::ENTITY ? ownerID : INVALID_ID)
                                     : noise.target == NoiseTarget::INSTIGATOR ? instigatorID
                                     : MonsterAISystem::FindPlayer(level);

                if (noiseTarget == INVALID_ID || !level.GetEntity(noiseTarget)) {
                    spdlog::warn("Noise Alert dispatch skipped: its target doesn't exist");
                    return;
                }

                // From the owner; the level has no place of its own, so from the target.
                std::optional<Vector3> from = OwnerPosition(level, ownerKind, ownerID);
                const ComponentTransform* targetTransform = level.transforms.Get(noiseTarget);
                if (ownerKind == EventOwnerKind::LEVEL && targetTransform) from = targetTransform->position;
                if (!from) return;

                MonsterAISystem::NoiseAlert(level, noiseTarget, MapQueries::FindSectorContainingPoint(level.sectors, {from->x, from->z}));
            },
            [&](const PayloadDispatchEntitySetSprite& setSprite) {
                if (ComponentSprite* sprite = level.sprites.Get(ownerID)) sprite->textureFileNames = setSprite.textures;
            },
            [&](const PayloadDispatchSpawnPrefab& spawn) {
                if (spawn.prefab.empty() || !hooks.spawnPrefab) return;

                const Vector3 base = spawn.worldPosition ? Vector3{0.0f, 0.0f, 0.0f}
                                                         : OwnerPosition(level, ownerKind, ownerID).value_or(Vector3{0.0f, 0.0f, 0.0f});

                const ID spawned = hooks.spawnPrefab(level, spawn.prefab);
                if (spawned == INVALID_ID) {
                    spdlog::warn("Spawn Prefab dispatch: couldn't load prefab '{}'", spawn.prefab);
                    return;
                }

                if (ComponentTransform* transform = level.transforms.Get(spawned)) {
                    PlaceTransform(level, *transform, base + spawn.offset);
                    transform->rotation = Quaternion::FromEulerDegrees(spawn.rotation.x, spawn.rotation.y, spawn.rotation.z);
                }

                // So its own dispatches can be reached.
                CreateRegistry(level);
            },
            [&](const PayloadDispatchPlaySound& play) {
                PlaySound(play, OwnerPosition(level, ownerKind, ownerID));
            },
            [&](const PayloadDispatchShowMessage& show) {
                ShowMessage(level.worldSettings.messageStyle, show.text, show.color * 255.0f, show.duration);
            },
            [&](const PayloadDispatchRelay& relay) {
                if (relay.direct) RunCall(level, relay.call, ownerKind, ownerID, instigatorID);
                else RunDispatches(level, relay.target, instigatorID);
            },
            [&](const PayloadDispatchResetEvent& reset) {
                ForEachEventOwner(level, [&](const EventOwnerKind kind, const ID id, const std::vector<Event>&) {
                    if (std::vector<Event>* events = ResolveEvents(level, kind, id))
                        for (Event& event : *events)
                            if (event.tagId == reset.target) event.triggerCount = 0;
                });
            },
            [&](const PayloadDispatchLoadLevel& load) {
                if (!hooks.loadLevel) return;

                const std::string levelName = load.nextLevel ? LevelManager::NextLevelName() : load.levelName;
                if (!levelName.empty()) hooks.loadLevel(levelName);
            },
            [&](const PayloadDispatchPause&) {
                if (hooks.pause) hooks.pause();
            },
            [&](const PayloadDispatchResume&) {
                if (hooks.resume) hooks.resume(level);
            },
            [&](const PayloadDispatchQuit&) {
                if (hooks.quit) hooks.quit();
            },
            [&](const PayloadDispatchMusic& music) {
                switch (music.action) {
                    case PayloadDispatchMusic::PLAY: MusicPlayer::Start(); break;
                    case PayloadDispatchMusic::STOP: MusicPlayer::Stop(); break;
                    case PayloadDispatchMusic::PAUSE: MusicPlayer::Pause(); break;
                    case PayloadDispatchMusic::SWITCH_TO_TRACK:
                        if (!MusicPlayer::SwitchToIndex(music.track))
                            spdlog::warn("Music dispatch: the level has no music track {}", music.track + 1);
                        break;
                    case PayloadDispatchMusic::SET_VOLUME: MusicPlayer::SetVolume(music.volume); break;
                }
            },
            [&](const PayloadDispatchSetUIText& setText) {
                ComponentUIText* text = level.ui_texts.Get(ownerID);
                if (!text) return;

                switch (setText.part) {
                    case PayloadDispatchSetUIText::TEXT: text->text = setText.text; break;
                    case PayloadDispatchSetUIText::PREFIX: text->prefix = setText.text; break;
                    case PayloadDispatchSetUIText::SUFFIX: text->suffix = setText.text; break;
                }
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
                    case PayloadDispatchChangeVariable::MULTIPLY: *value *= change.value; break;
                    case PayloadDispatchChangeVariable::DIVIDE:
                        if (change.value == 0.0f) spdlog::warn("Change Variable dispatch on owner {} divides by 0, skipped", ownerID);
                        else *value /= change.value;
                        break;
                }
            },
        }, dispatch.payload);

        return true;
    }

    void ExecuteDispatch(Level& level, const Dispatch& dispatch, const EventOwnerKind ownerKind, const ID ownerID,
                         const ID instigatorID) {
        // A relay inside runs dispatches of its own; each answers for itself.
        const bool outerRefused = dispatchRefused;
        dispatchRefused = false;

        const bool ran = RunPayload(level, dispatch, ownerKind, ownerID, instigatorID) && !dispatchRefused;
        dispatchRefused = outerRefused;
        if (!ran) return;

        // `dispatch` is the caller's copy, so the flows stay valid here.
        for (const DispatchFlow& flow : dispatch.flows)
            // Queued before this frame's Update (scripts, UI buttons), the
            // next frame's is the one after it.
            pendingFlows.push_back({flow, ownerKind, ownerID, instigatorID, flow.wait, realTimeChain,
                                    updateCount + (inUpdate ? 1 : 2)});
    }

    void TriggerEvent(Level& level, Event& event, const EventOwnerKind ownerKind, const ID ownerID, const ID instigatorID) {
        Trigger(level, event, ownerKind, ownerID, instigatorID, EntityTags(level, instigatorID));
    }

    void ReportRay(Level& level, const RayType type, const ID casterID, const Vector3& origin, const Vector3& direction,
                   const float length, const std::optional<RayHit>& hit) {
        if (!running || type == RayType::GENERAL) return;

        const Vector2 origin2D = {origin.x, origin.z};

        if (hit) {
            if (hit->HitEntity()) rayReports.push_back({type, casterID, EventOwnerKind::ENTITY, hit->entity->id, true});
            else if (hit->HitWall())
                rayReports.push_back({type, casterID, EventOwnerKind::LINE, hit->wall->id, OnWallFront(level, *hit->wall, origin2D)});
            else if (hit->HitSectorFloor() || hit->HitSectorCeiling())
                rayReports.push_back({type, casterID, EventOwnerKind::SECTOR, hit->sector->id, true});
        }

        // Linedefs are see-through walls from the floor to the ceiling of
        // the sector the ray meets them in: every one passed before the ray
        // stops is hit.
        const float directionLength = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (directionLength <= 0.0f) return;

        const Vector3 unit = direction / directionLength;
        const float reach = hit ? hit->distance : length;
        const Vector2 path = Vector2{unit.x, unit.z} * reach;

        for (const Linedef& linedef : level.linedefs) {
            if (!HasEvent<PayloadReceiveRay>(linedef)) continue;

            // Ray: origin + path * t, linedef: start + vector * u.
            const float denominator = Vector2Math::Cross(path, linedef.vector);
            if (std::abs(denominator) <= std::numeric_limits<float>::epsilon()) continue;

            const Vector2 toStart = linedef.start - origin2D;
            const float t = Vector2Math::Cross(toStart, linedef.vector) / denominator;
            const float u = Vector2Math::Cross(toStart, path) / denominator;
            if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) continue;

            const Vector2 point = origin2D + path * t;
            const float height = origin.y + unit.y * reach * t;

            const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, point);
            if (sectorIndex < 0) continue;

            const Sector& sector = level.sectors[sectorIndex];
            const int floorIndex = MapQueries::FindFloorAtHeight(sector, point, height);
            if (floorIndex < 0) continue;

            const SectorFloor& floor = sector.floors[floorIndex];
            if (height < MapQueries::SurfaceHeightAt(sector, floor.floor, point) ||
                height > MapQueries::SurfaceHeightAt(sector, floor.ceiling, point)) continue;

            rayReports.push_back({type, casterID, EventOwnerKind::LINEDEF, linedef.id, OnLinedefFront(linedef, origin2D)});
        }
    }

    void SetMessage(const Level& level, const std::string& text, const Vector3& color, const float duration) {
        ShowMessage(level.worldSettings.messageStyle, text, color, duration);
    }

    void BeginLevelMessages(const Level& level) {
        shownMessages.clear();
        savedMessageStyle = level.worldSettings.messageStyle;
    }

    void ClearMessages() {
        shownMessages.clear();
    }

    void ResetMessageStyle(Level& level) {
        level.worldSettings.messageStyle = savedMessageStyle;
    }

    void Signal(Level& level, const std::string& name, const ID instigatorID) {
        if (!running) return;

        // Owners first: a dispatch can add entities while this runs.
        std::vector<std::pair<EventOwnerKind, ID>> owners;
        ForEachEventOwner(level, [&](const EventOwnerKind kind, const ID ownerID, const std::vector<Event>& events) {
            const bool listens = std::ranges::any_of(events, [&](const Event& event) {
                const PayloadLuaSignal* signal = std::get_if<PayloadLuaSignal>(&event.payload);
                return signal && signal->name == name;
            });
            if (listens) owners.emplace_back(kind, ownerID);
        });

        const std::vector<TagId> tags = EntityTags(level, instigatorID);

        for (const auto& [kind, ownerID] : owners) {
            if (!IsOwnerActive(level, kind, ownerID)) continue;

            FireEvents<PayloadLuaSignal>(level, kind, ownerID, instigatorID, tags,
                [&](const PayloadLuaSignal& signal) { return signal.name == name; });
        }
    }

    void FireUIButtonEvent(Level& level, const ID entityID, const UIButtonEvent kind) {
        if (!running || !IsEnabledEntity(level, entityID)) return;

        // The player: the first enabled entity with the Player tag.
        ID playerID = INVALID_ID;
        if (const std::optional<TagId> playerTag = TagRegistry::Find(TagRegistry::PLAYER_TAG)) {
            for (const Entity& entity : level.entities) {
                if (!entity.enabled || std::ranges::find(entity.tagIds, *playerTag) == entity.tagIds.end()) continue;
                playerID = entity.id;
                break;
            }
        }

        const std::vector<TagId> tags = EntityTags(level, playerID);
        const RealTimeChainScope scope(true);

        switch (kind) {
            case UIButtonEvent::CLICK:
                FireEvents<PayloadUIButtonClick>(level, EventOwnerKind::ENTITY, entityID, playerID, tags);
                break;
            case UIButtonEvent::RELEASE:
                FireEvents<PayloadUIButtonRelease>(level, EventOwnerKind::ENTITY, entityID, playerID, tags);
                break;
            case UIButtonEvent::HOVER_ENTER:
                FireEvents<PayloadUIButtonHoverEnter>(level, EventOwnerKind::ENTITY, entityID, playerID, tags);
                break;
            case UIButtonEvent::HOVER_EXIT:
                FireEvents<PayloadUIButtonHoverExit>(level, EventOwnerKind::ENTITY, entityID, playerID, tags);
                break;
        }
    }

    std::optional<FieldValue> ReadEntityField(Level& level, const ID entityID, const FieldComponent component,
                                              const int componentIndex, const std::string& field) {
        FieldValue value;
        if (!AccessEngineField(level, {entityID, component, componentIndex, field}, &value, nullptr)) return std::nullopt;
        return value;
    }

    void FirePathfinderArrived(Level& level, const ID entityID, const ID targetID) {
        if (!running || !IsEnabledEntity(level, entityID)) return;
        FireEvents<PayloadPathfinderArrived>(level, EventOwnerKind::ENTITY, entityID, targetID, EntityTags(level, targetID));
    }

    // Fires `Payload` events of the entity, `otherID` as instigator and filter.
    template <typename Payload>
    static void FireEntityNotice(Level& level, const ID entityID, const ID otherID) {
        if (!running || !IsEnabledEntity(level, entityID)) return;
        FireEvents<Payload>(level, EventOwnerKind::ENTITY, entityID, otherID, EntityTags(level, otherID));
    }

    void FirePathfinderBlocked(Level& level, const ID entityID) { FireEntityNotice<PayloadPathfinderBlocked>(level, entityID, INVALID_ID); }
    void FirePathfinderStuck(Level& level, const ID entityID) { FireEntityNotice<PayloadPathfinderStuck>(level, entityID, INVALID_ID); }
    void FirePathfinderStartedMoving(Level& level, const ID entityID) { FireEntityNotice<PayloadPathfinderStartedMoving>(level, entityID, INVALID_ID); }
    void FirePathfinderStoppedMoving(Level& level, const ID entityID) { FireEntityNotice<PayloadPathfinderStoppedMoving>(level, entityID, INVALID_ID); }

    void FireMonsterTargetSpotted(Level& level, const ID entityID, const ID otherID) { FireEntityNotice<PayloadMonsterTargetSpotted>(level, entityID, otherID); }
    void FireMonsterTargetLost(Level& level, const ID entityID) { FireEntityNotice<PayloadMonsterTargetLost>(level, entityID, INVALID_ID); }
    void FireMonsterMeleeReady(Level& level, const ID entityID, const ID otherID) { FireEntityNotice<PayloadMonsterMeleeReady>(level, entityID, otherID); }
    void FireMonsterMissileReady(Level& level, const ID entityID, const ID otherID) { FireEntityNotice<PayloadMonsterMissileReady>(level, entityID, otherID); }
    void FireMonsterPain(Level& level, const ID entityID, const ID otherID) { FireEntityNotice<PayloadMonsterPain>(level, entityID, otherID); }
    void FireMonsterActive(Level& level, const ID entityID, const ID otherID) { FireEntityNotice<PayloadMonsterActive>(level, entityID, otherID); }
    void FireMonsterDied(Level& level, const ID entityID, const ID otherID) { FireEntityNotice<PayloadMonsterDied>(level, entityID, otherID); }

    WalkerDoor CheckDoorForWalker(const Level& level, const ID sectorID, const int floorIndex, const ID walkerID) {
        using Door = PayloadDispatchSectorDoor;

        const Sector* sector = level.GetSector(sectorID);
        if (!sector) return WalkerDoor::NONE;

        const Entity* walker = level.GetEntity(walkerID);

        for (const Dispatch& dispatch : sector->dispatches) {
            const Door* door = std::get_if<Door>(&dispatch.payload);
            if (!door || door->floorIndex != floorIndex || !door->pathfindersCanOpen) continue;

            // Bad guys never close doors.
            if (door->type == Door::CLOSE_WAIT_OPEN || door->type == Door::CLOSE_STAY) continue;

            if (door->locked && (!walker || std::ranges::find(walker->tagIds, door->requiredTag) == walker->tagIds.end())) continue;

            const auto existing = std::ranges::find_if(doors, [&](const DoorState& state) {
                return state.sectorID == sectorID && state.floorIndex == floorIndex;
            });

            if (existing == doors.end()) return WalkerDoor::OPEN;

            switch (existing->phase) {
                case DoorState::OPENING: return WalkerDoor::WAIT;
                case DoorState::CLOSING: return WalkerDoor::OPEN;
                case DoorState::STILL: return existing->open ? WalkerDoor::NONE : WalkerDoor::OPEN;
                default: return WalkerDoor::NONE; // open and still too tight: it won't get any better
            }
        }

        return WalkerDoor::NONE;
    }

    void OpenDoorForWalker(Level& level, const WalkerDoorRequest& request) {
        using Door = PayloadDispatchSectorDoor;

        if (!running || CheckDoorForWalker(level, request.sectorID, request.floorIndex, request.walkerID) != WalkerDoor::OPEN) return;

        const auto existing = std::ranges::find_if(doors, [&](const DoorState& state) {
            return state.sectorID == request.sectorID && state.floorIndex == request.floorIndex;
        });

        if (existing != doors.end()) {
            if (SectorFloor* floor = GetSectorFloor(level, request.sectorID, request.floorIndex)) MoveDoor(level, *floor, *existing, true);
            return;
        }

        const Sector* sector = level.GetSector(request.sectorID);
        if (!sector) return;

        for (const Dispatch& dispatch : sector->dispatches) {
            const Door* door = std::get_if<Door>(&dispatch.payload);
            if (!door || door->floorIndex != request.floorIndex || !door->pathfindersCanOpen) continue;
            if (door->type == Door::CLOSE_WAIT_OPEN || door->type == Door::CLOSE_STAY) continue;

            const Entity* walker = level.GetEntity(request.walkerID);
            if (door->locked && (!walker || std::ranges::find(walker->tagIds, door->requiredTag) == walker->tagIds.end())) continue;

            const Dispatch copy = dispatch; // running it can move the sector's dispatches
            ExecuteDispatch(level, copy, EventOwnerKind::SECTOR, request.sectorID, request.walkerID);
            return;
        }
    }

    void FireEntityDestroyed(Level& level, const ID entityID) {
        if (!running || !IsEnabledEntity(level, entityID)) return;

        FireEvents<PayloadEntityDestroyed>(level, EventOwnerKind::ENTITY, entityID, entityID, EntityTags(level, entityID));
    }

    std::vector<Message> CurrentMessages() {
        std::vector<Message> messages;
        messages.reserve(shownMessages.size());
        for (const ShownMessage& shown : shownMessages) messages.push_back(shown.message);
        return messages;
    }
}
