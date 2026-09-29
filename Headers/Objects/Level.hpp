//
// Created by berke on 5/2/2026.
//

#ifndef TILKY_ENGINE_LEVEL_H
#define TILKY_ENGINE_LEVEL_H

#include <AL/al.h>
#include <vector>
#include <string>

#include "Components.hpp"
#include "Entity.hpp"
#include "Loadables.hpp"

struct ListenerSettings {
    float masterGain = 1.0f;
    float dopplerFactor = 1.0f;
    float speedOfSound = 343.3f;
    ALenum distanceModel = AL_INVERSE_DISTANCE_CLAMPED;
};

struct WorldSettings {
    float gravity = 9.8f;
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
};

struct Level {
    LevelID id = 0;
    std::string name;

    std::vector<Entity> entities;

    ID nextEntityID = 1;

    std::vector<Wall> walls;
    std::vector<Sector> sectors;

    // These are needed so that walls/sectors dont get their IDs mixed up from deletion
    std::unordered_map<ID, int> sectorIDToIndex;
    std::unordered_map<ID, int> wallIDToIndex;

    ID nextSectorID = 0;
    ID nextWallID = 0;

    std::vector<Texture> textures;
    std::vector<Sound> sounds;

    ListenerSettings listenerSettings;
    WorldSettings worldSettings;
    RendererSettings rendererSettings;

    ComponentStorage<ComponentTransform> transforms;
    ComponentStorage<ComponentSprite> sprites;
    ComponentStorage<ComponentAudioSource> audioSources;
    ScriptComponentStorage scripts;
    ComponentStorage<ComponentPlayerController> playerControllers;
    ComponentStorage<ComponentCamera> cameras;
    ColliderStorage colliders;
    ComponentStorage<ComponentRigidbody> rigidbodies;
    ComponentStorage<ComponentModel> models;

    ComponentStorage<ComponentUITransform> ui_transforms;
    ComponentStorage<ComponentUISprite> ui_sprites;
    ComponentStorage<ComponentUIText> ui_texts;

#ifndef TILKY_STANDALONE
    Vector2 editorCamPos = {0.0f, 0.0f};
    Vector3 runtimeCamPos = {0.0f, 0.0f, 0.0f};
    Vector2 runtimeCamRot = {.0f, .0f};
#endif
    Entity* GetEntity(ID entityID);
    const Entity* GetEntity(ID entityID) const;
    ID CreateEntity(bool uiEntity);
    ID CreateEntity(Entity& entity);
    void DestroyEntity(ID entityID);
    void DestroyEntity(const Entity& entity);

    Sector* GetSector(ID sectorID);
    const Sector* GetSector(ID sectorID) const;

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

    // Advances every moving floor/ceiling (SectorFloor::UpdateMovement) by
    // `deltaTime` and passes the change on to the sector's children, same as
    // a height written from Lua.
    void UpdateSectorMovement(float deltaTime);

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
};

#endif // TILKY_ENGINE_LEVEL_H