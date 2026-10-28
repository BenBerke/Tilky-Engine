//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVEL_H
#define TILKY_ENGINE_LEVEL_H

#include <AL/al.h>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

#include "Components.hpp"
#include "Entity.hpp"
#include "Loadables.hpp"
#include "Sky.hpp"
#include "VectorIndexCache.hpp"
#include "Linedef.hpp"
#include "Event.hpp"
#include "Headers/GlobalEventVariables.hpp"

struct ListenerSettings {
    float masterGain = 1.0f;
    float dopplerFactor = 0.0f;
    float speedOfSound = 343.3f;
    ALenum distanceModel = AL_INVERSE_DISTANCE_CLAMPED;
};

struct WorldSettings {
    float gravity = 9.8f;

    // This level's own event variables (IDs without GLOBAL_EVENT_VARIABLE_BIT;
    // project-wide ones are in GlobalEventVariables). The value is the
    // starting value in the editor and the live one during play. Play restores
    // the editor snapshot and Game.LoadLevel re-reads the file, so they reset
    // on every level load.
    std::unordered_map<EventVariableID, float> eventVariables;

    std::unordered_map<EventVariableID, std::string> eventVariableNames;
    std::unordered_map<std::string, EventVariableID> eventVariableIdsByName;
    EventVariableID nextEventVariableID = 1; // 0 stays unused, deleted IDs aren't reused

    // The level's own events and dispatches (EventOwnerKind::LEVEL).
    std::vector<Event> events;
    std::vector<Dispatch> dispatches;
};

// See src/Runtime/Renderer/OpenGL/OpenGLInit.cpp
enum RendererTextureSettings {
    PIXEL_ART_SHIMMERY,
    PIXEL_ART_LESS_MOIRE,
    PIXEL_ART_SMOOTH_DISTANCE,
    REALISTIC_NORMAL,
    RETRO,
    LOW_RES
};
struct RendererSettings {
    RendererTextureSettings textureSetting = PIXEL_ART_SHIMMERY;

    // Fake contrast, in sector light units (0-255): walls running along Z
    // are lit this much brighter than their sector, walls along X this much
    // darker, and angled walls blend smoothly in between. 0 turns it off.
    float wallContrast = 16.0f;
};

constexpr float MAX_WALL_CONTRAST = 64.0f;

// Smallest gap allowed between a sector floor and its ceiling.
constexpr float MIN_ROOM_HEIGHT = 0.0f;

// One entity's Snap to Wall (src/Editor/WallSnap.hpp). Editor-only: saved
// in the level file's metadata, which the game never reads.
struct WallSnapData {
    static constexpr float DEFAULT_GAP = 0.05f;

    ID wallID = INVALID_ID;
    float side = 1.0f; // +1 or -1: which side of the wall
    float gap = DEFAULT_GAP;
};

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<Entity> entities;

    ID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;
    std::vector<Linedef> linedefs;

    // These are needed so that walls/sectors dont get their IDs mixed up from deletion
    std::unordered_map<ID, int> sectorIDToIndex;
    std::unordered_map<ID, int> wallIDToIndex;
    std::unordered_map<ID, int> linedefIDToIndex;

    ID nextSectorID = 0;
    ID nextWallID = 0;
    ID nextLinedefID = 0;

    std::vector<Texture> textures;
    std::vector<Sound> sounds;

    ListenerSettings listenerSettings;
    WorldSettings worldSettings;
    RendererSettings rendererSettings;
    SkySettings sky;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentAudioSource> audioSources;
    ScriptComponentStorage scripts;
    ComponentStorage<ComponentPlayerController> playerControllers;
    ComponentStorage<ComponentCamera> cameras;
    ColliderStorage colliders;
    ComponentStorage<ComponentRigidbody> rigidbodies;
    ComponentStorage<ComponentModel> models;
    ComponentStorage<ComponentFlipbook> flipbooks;

    ComponentStorage<ComponentUITransform> ui_transforms;
    ComponentStorage<ComponentUISprite> ui_sprites;
    ComponentStorage<ComponentUIText> ui_texts;

#ifndef TILKY_STANDALONE
    Vector2 editorCamPos = {0.0f, 0.0f};
    Vector3 runtimeCamPos = {0.0f, 0.0f, 0.0f};
    Vector2 runtimeCamRot = {.0f, .0f};

    // Snapped entities, by entity ID.
    std::unordered_map<ID, WallSnapData> wallSnaps;
#endif
    Entity* GetEntity(ID entityID);
    const Entity* GetEntity(ID entityID) const;
    ID CreateEntity(bool uiEntity);
    ID CreateEntity(Entity& entity);
    void DestroyEntity(ID entityID);
    void DestroyEntity(const Entity& entity);

    // Only one camera and one player controller are active in the whole
    // level at a time, counting each component (an entity can have several).
    // These tick the given one on and untick every other, including its
    // siblings. Unticking the active one is a plain isActive = false, which
    // leaves none active.
    void ActivateCamera(const ComponentCamera& camera);
    void ActivatePlayerController(const ComponentPlayerController& controller);

    Sector* GetSector(ID sectorID);
    const Sector* GetSector(ID sectorID) const;

    Linedef* GetLinedef(ID linedefID);
    const Linedef* GetLinedef(ID linedefID) const;

    // True if `ancestor` is `sectorID` itself or anywhere up its parent chain.
    bool IsSectorAncestor(ID ancestor, ID sectorID) const;

    // Makes `newParentID` the parent of `childID` and updates both parents'
    // `children`. INVALID_ID as the new parent unparents the sector.
    // Returns false (and changes nothing) if either ID doesn't exist or the
    // change would create a cycle.
    bool SetSectorParent(ID childID, ID newParentID);

    // Refills every sector's `children` from the parentID fields. Call after
    // anything that replaces sectors wholesale (load, topology rebuild, undo).
    // A parentID pointing at a missing sector, or closing a cycle, is reset
    // to INVALID_ID.
    void RebuildSectorChildren();

    // Passes a change made to sector `sectorID` on to all of its descendants.
    // `floorsBefore`/`lightBefore` are the sector's values before the change,
    // its current values are the after. Numeric values (heights, slope
    // strength, colors, texture offset/scale, light) move by the same amount
    // the parent's did, so a child keeps its offset from the parent: parent
    // floor 10 -> 20 takes a child at 5 to 15. Values with no "amount"
    // (texture, slope direction, flips) are copied when the parent's changed.
    // Floor N maps to the child's floor N; per-floor values are skipped when
    // the parent's floor count changed. Descendants listed in `skip` are
    // left alone together with their subtrees - used by multi-edit, where
    // each selected sector already got its own value and passes its own
    // change down.
    void PropagateSectorChanges(ID sectorID, const std::vector<SectorFloor>& floorsBefore,
                                const Vector3& lightBefore, const std::vector<ID>& skip = {});

    // Advances every moving floor/ceiling (SectorFloor::UpdateMovement) and
    // every light fade (Sector::UpdateLightFade) by `deltaTime`, and passes
    // the change on to the sector's children, same as a value written from
    // Lua.
    void UpdateSectorMovement(float deltaTime);

    // Every floor/ceiling move that ended since EventSystem last took them
    // (Sector Move Finished). Runtime only.
    std::vector<SectorMoveEnd> sectorMoveEnds;

    // Stops the floor and/or ceiling move of one of the sector's floors where
    // it is, recording each running one as a STOPPED end.
    void StopSectorMoves(ID sectorID, int floorIndex, bool floor, bool ceiling);

    struct SectorValuesSnapshot {
        ID id = INVALID_ID;
        std::vector<SectorFloor> floors;
        Vector3 light{};
    };

    // For code that edits sectors in many places at once (the editor
    // inspectors): snapshot before, run the edits, then call
    // PropagateSnapshottedSectorChanges. Only sectors with children are
    // snapshotted, since nothing else has anything to pass on. Every sector
    // whose values changed since the snapshot passes its own change down;
    // a changed sector inside another changed sector's subtree is skipped by
    // that walk, since it was edited directly and passes its own change on.
    [[nodiscard]] std::vector<SectorValuesSnapshot> SnapshotParentSectorValues() const;
    void PropagateSnapshottedSectorChanges(const std::vector<SectorValuesSnapshot>& before);

    // Level variable names can't clash with each other or with a global
    // variable, so the event menu never shows the same name twice.
    [[nodiscard]] bool IsEventVariableNameTaken(const std::string& name) const {
        return worldSettings.eventVariableIdsByName.contains(name) || GlobalEventVariables::Find(name).has_value();
    }

    // Fails (nullopt) on an empty or taken name.
    std::optional<EventVariableID> CreateEventVariable(const std::string& name, const float value = 0.0f) {
        auto& w = worldSettings;
        if (name.empty() || IsEventVariableNameTaken(name)) return std::nullopt;
        if (w.nextEventVariableID >= GLOBAL_EVENT_VARIABLE_BIT) return std::nullopt;

        const EventVariableID id = w.nextEventVariableID++;
        w.eventVariables[id] = value;
        w.eventVariableNames[id] = name;
        w.eventVariableIdsByName[name] = id;
        return id;
    }

    bool RenameEventVariable(const EventVariableID id, const std::string& newName) {
        auto& w = worldSettings;
        auto it = w.eventVariableNames.find(id);
        if (it == w.eventVariableNames.end() || newName.empty()) return false;
        if (it->second == newName) return true;
        if (IsEventVariableNameTaken(newName)) return false;

        w.eventVariableIdsByName.erase(it->second);
        it->second = newName;
        w.eventVariableIdsByName[newName] = id;
        return true; // conditions keep working: they reference the id, not the name
    }

    void DeleteEventVariable(const EventVariableID id) {
        auto& w = worldSettings;
        if (auto it = w.eventVariableNames.find(id); it != w.eventVariableNames.end()) {
            w.eventVariableIdsByName.erase(it->second);
            w.eventVariableNames.erase(it);
        }
        w.eventVariables.erase(id);
    }

    // Level or global, picked by the ID's scope bit. nullopt for a variable
    // that no longer exists (show it as missing; it reads as 0).
    [[nodiscard]] std::optional<std::string> GetEventVariableName(const EventVariableID id) const {
        if (IsGlobalEventVariable(id)) return GlobalEventVariables::GetName(id);

        const auto it = worldSettings.eventVariableNames.find(id);
        if (it == worldSettings.eventVariableNames.end()) return std::nullopt;
        return it->second;
    }

    // By name, level first (names never clash, so the order doesn't matter).
    [[nodiscard]] std::optional<EventVariableID> FindEventVariable(const std::string& name) const {
        if (const auto it = worldSettings.eventVariableIdsByName.find(name); it != worldSettings.eventVariableIdsByName.end())
            return it->second;
        return GlobalEventVariables::Find(name);
    }

    // The live value, in this level's table or the global one. nullptr for a
    // variable that doesn't exist.
    [[nodiscard]] float* GetEventVariableValue(const EventVariableID id) {
        auto& values = IsGlobalEventVariable(id) ? GlobalEventVariables::Values() : worldSettings.eventVariables;
        const auto it = values.find(id);
        return it != values.end() ? &it->second : nullptr;
    }

    [[nodiscard]] bool CheckCondition(const Condition& condition) const {
        return condition.Check(worldSettings.eventVariables, GlobalEventVariables::Values());
    }

private:
    // id -> index into `entities`, for GetEntity. See VectorIndexCache.
    VectorIndexCache<ID> entityIndexCache;
};

#endif // TILKY_ENGINE_LEVEL_H