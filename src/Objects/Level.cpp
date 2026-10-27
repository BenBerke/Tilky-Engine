//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Level.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/ComponentRegistry.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Constants.hpp"

#include <algorithm>

namespace {
    ID EntityIDOf(const Entity& entity) { return entity.id; }
}

Entity* Level::GetEntity(const ID entityID) {
    const size_t index = entityIndexCache.Find(entities, entityID, EntityIDOf);
    return index == VectorIndexCache<ID>::npos ? nullptr : &entities[index];
}

const Entity* Level::GetEntity(const ID entityID) const {
    const size_t index = entityIndexCache.Find(entities, entityID, EntityIDOf);
    return index == VectorIndexCache<ID>::npos ? nullptr : &entities[index];
}

ID Level::CreateEntity(const bool uiEntity) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = "Entity";
    entity.attachedLevelId = id;

    entities.push_back(entity);
    entityIndexCache.Set(entity.id, entities.size() - 1);

    const ID newId = entity.id;

    if (uiEntity) [[unlikely]] {
        ui_transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_UI_TRANSFORM);
    }
    else [[likely]] {
        transforms.Add(newId);
        GetEntity(newId)->componentsMask.set(CMP_TRANSFORM);
    }

    return newId;
}

namespace {
    // Runtime-only state a copy starts without. Everything else is copied.
    template<typename T>
    void ResetCopiedRuntimeState(T&) {}

    void ResetCopiedRuntimeState(ComponentAudioSource& audio) {
        audio.name.clear(); // names the original's OpenAL source; the copy gets its own when started
        audio.isHeld = false;
        audio.applied = {};
    }

    void ResetCopiedRuntimeState(ComponentPlayerController& controller) {
        controller.isActive = false; // the original stays the active one
        controller.velocity = {};
        controller.currentSpeed = 0.0f;
        controller.currentEyeHeight = 0.0f;
    }

    void ResetCopiedRuntimeState(ComponentCamera& camera) {
        camera.isActive = false; // the original stays the active one
        camera.isStepping = false;
        camera.stepOffsetY = 0.0f;
        camera.hasPreviousTransformY = false;
    }

    void ResetCopiedRuntimeState(ComponentRigidbody& rigidbody) {
        rigidbody.velocity = {};
        rigidbody.isGrounded = false;
        rigidbody.slideNormal = {};
    }

    void ResetCopiedRuntimeState(ComponentUITransform& transform) {
        transform.resolvedPosition = {};
        transform.resolvedSize = {};
    }

    void ResetCopiedRuntimeState(ComponentFlipbook& flipbook) {
        flipbook.currentFrame = 0;
        flipbook.frameTime = 0.0f;
        flipbook.pingPongDirection = 1;
        flipbook.playing = false;
        flipbook.applyPending = false;
        flipbook.eventPending = false;
    }

    // The owner's component instance IDs (script instance IDs for scripts), in order.
    template<typename Storage>
    std::vector<std::uint64_t> OwnerInstanceIDs(const Storage& storage, const ID owner) {
        const auto& ids = storage.InstancesOf(owner);
        return {ids.begin(), ids.end()};
    }

    std::vector<std::uint64_t> OwnerInstanceIDs(const ScriptComponentStorage& storage, const ID owner) {
        std::vector<std::uint64_t> ids;
        for (const ComponentScript& script : storage.components)
            if (script.ownerID == owner) ids.push_back(script.instanceID);
        return ids;
    }

    // Copies every component of one type, in order, each with a new instance ID.
    template<typename Storage>
    void CopyComponents(Storage& storage, const ID from, const ID to, ComponentMask& mask, const int bit) {
        const std::vector<ComponentInstanceID> instances = storage.InstancesOf(from);
        for (const ComponentInstanceID instanceID : instances) {
            auto component = *storage.GetInstance(instanceID); // copy first: inserting can reallocate
            component.ownerID = to;
            component.instanceID = INVALID_COMPONENT_INSTANCE_ID;
            ResetCopiedRuntimeState(component);
            storage.InsertLoaded(component);
            mask.set(bit);
        }
    }
}

// Create entity through copy-paste. Copies every component (all instances of
// each type) whole, minus runtime-only state - see ResetCopiedRuntimeState.
ID Level::CreateEntity(Entity& copy) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = copy.name + "Copy";
    entity.enabled = copy.enabled;
    entity.attachedLevelId = id;
    entity.tagIds = copy.tagIds;
    entity.dispatches = copy.dispatches;
    entity.events = copy.events;

    // Runtime-only: the copy's events haven't fired yet.
    for (Event& event : entity.events) event.triggerCount = 0;

#ifndef TILKY_STANDALONE
    // A copy of a snapped entity is snapped to the same wall too (see
    // WallSnap::Enforce, which puts it on the wall).
    if (const auto snap = wallSnaps.find(copy.id); snap != wallSnaps.end()) {
        const WallSnapData data = snap->second; // the insert below can rehash
        wallSnaps[entity.id] = data;
    }
#endif

    CopyComponents(transforms, copy.id, entity.id, entity.componentsMask, CMP_TRANSFORM);
    CopyComponents(sprites, copy.id, entity.id, entity.componentsMask, CMP_SPRITE);
    CopyComponents(audioSources, copy.id, entity.id, entity.componentsMask, CMP_AUDIO_SOURCE);
    CopyComponents(playerControllers, copy.id, entity.id, entity.componentsMask, CMP_PLAYER_CONTROLLER);
    CopyComponents(cameras, copy.id, entity.id, entity.componentsMask, CMP_CAMERA);
    CopyComponents(colliders, copy.id, entity.id, entity.componentsMask, CMP_COLLIDER);
    CopyComponents(rigidbodies, copy.id, entity.id, entity.componentsMask, CMP_RIGIDBODY);
    CopyComponents(models, copy.id, entity.id, entity.componentsMask, CMP_MODEL);
    CopyComponents(flipbooks, copy.id, entity.id, entity.componentsMask, CMP_FLIPBOOK);
    CopyComponents(ui_transforms, copy.id, entity.id, entity.componentsMask, CMP_UI_TRANSFORM);
    CopyComponents(ui_sprites, copy.id, entity.id, entity.componentsMask, CMP_UI_SPRITE);
    CopyComponents(ui_texts, copy.id, entity.id, entity.componentsMask, CMP_UI_TEXT);

    // A copied flipbook still names the original's sprite (a UI Sprite on a
    // UI entity). Point it at the copy's sprite in the same position instead.
    {
        const bool uiEntity = ui_transforms.Has(copy.id);
        const std::vector<ComponentInstanceID> originalSprites =
            uiEntity ? ui_sprites.InstancesOf(copy.id) : sprites.InstancesOf(copy.id);
        const std::vector<ComponentInstanceID> copiedSprites =
            uiEntity ? ui_sprites.InstancesOf(entity.id) : sprites.InstancesOf(entity.id);

        for (ComponentFlipbook* flipbook : flipbooks.GetAll(entity.id)) {
            const auto it = std::ranges::find(originalSprites, flipbook->spriteInstanceID);
            const size_t index = static_cast<size_t>(it - originalSprites.begin());

            flipbook->spriteInstanceID = index < copiedSprites.size()
                ? copiedSprites[index]
                : INVALID_COMPONENT_INSTANCE_ID;
        }
    }

    // Copies every attached script (not just the first), each getting its
    // own new instance ID via AddScript()/ScriptComponentStorage::Add - see
    // the class-level comment on Entity-to-script duplication. Public
    // field values are copied as-is; an Entity/Behaviour reference field
    // that pointed at `copy` itself still points at the original entity
    // after duplication rather than being remapped to the new copy - the
    // same "self-reference doesn't retarget" caveat most engines have for
    // duplicate/copy-paste.
    for (const ComponentScript* originalScript : copy.GetScripts()) {
        ComponentScript& s = entity.AddScript();

        s.fileName = originalScript->fileName;
        s.enabled = originalScript->enabled;
        s.publicValues = originalScript->publicValues;
        s.schemaHash = originalScript->schemaHash;
    }

    // A copy of a prefab instance is an instance too, with the same
    // overrides. Components were copied in order, so the n-th copy of each
    // type stands in for the n-th original.
    if (copy.prefabLink.IsInstance()) {
        entity.prefabLink = copy.prefabLink;

        auto remap = [&](const int type, const std::vector<std::uint64_t>& from, const std::vector<std::uint64_t>& to) {
            for (PrefabComponentLink& link : entity.prefabLink.components) {
                if (link.componentType != type) continue;
                const auto it = std::ranges::find(from, link.instanceID);
                if (it != from.end() && static_cast<size_t>(it - from.begin()) < to.size())
                    link.instanceID = to[static_cast<size_t>(it - from.begin())];
            }
        };

#define REMAP_PREFAB_LINKS(Type, Bit, Storage, LabelKey) remap(Bit, OwnerInstanceIDs(Storage, copy.id), OwnerInstanceIDs(Storage, entity.id));
        TILKY_COMPONENTS(REMAP_PREFAB_LINKS)
#undef REMAP_PREFAB_LINKS
    }

    entities.push_back(entity);
    entityIndexCache.Set(entity.id, entities.size() - 1);

    return entity.id;
}

void Level::DestroyEntity(const ID entityID) {
    if (entityID == INVALID_ID) return;

    for (Sector& sector : sectors) std::erase(sector.entitiesInside, entityID);

    colliders.RemoveAll(entityID);
    rigidbodies.RemoveAll(entityID);

    sprites.RemoveAll(entityID);
    models.RemoveAll(entityID);
    flipbooks.RemoveAll(entityID);
    audioSources.RemoveAll(entityID);
    scripts.RemoveAll(entityID);
    playerControllers.RemoveAll(entityID);
    cameras.RemoveAll(entityID);
    transforms.RemoveAll(entityID);

    // UI components.
    ui_sprites.RemoveAll(entityID);
    ui_texts.RemoveAll(entityID);
    ui_transforms.RemoveAll(entityID);

    std::erase_if(entities, [entityID](const Entity& entity) {
        return entity.id == entityID;
    });
    entityIndexCache.Clear();
}

void Level::ActivateCamera(const ComponentCamera& camera) {
    const ComponentInstanceID instanceID = camera.instanceID;
    for (ComponentCamera& other : cameras.components) other.isActive = other.instanceID == instanceID;
}

void Level::ActivatePlayerController(const ComponentPlayerController& controller) {
    const ComponentInstanceID instanceID = controller.instanceID;
    for (ComponentPlayerController& other : playerControllers.components)
        other.isActive = other.instanceID == instanceID;
}

void Level::DestroyEntity(const Entity& entity) {
    DestroyEntity(entity.id);
}
Sector* Level::GetSector(const ID sectorID) {
    const auto it = sectorIDToIndex.find(sectorID);
    if (it == sectorIDToIndex.end()) return nullptr;
    return &sectors[it->second];
}

const Sector* Level::GetSector(const ID sectorID) const {
    const auto it = sectorIDToIndex.find(sectorID);
    if (it == sectorIDToIndex.end()) return nullptr;
    return &sectors[it->second];
}

Linedef* Level::GetLinedef(const ID linedefID) {
    const auto it = linedefIDToIndex.find(linedefID);
    if (it == linedefIDToIndex.end() || it->second < 0 || it->second >= static_cast<int>(linedefs.size())) return nullptr;
    return &linedefs[it->second];
}

const Linedef* Level::GetLinedef(const ID linedefID) const {
    const auto it = linedefIDToIndex.find(linedefID);
    if (it == linedefIDToIndex.end() || it->second < 0 || it->second >= static_cast<int>(linedefs.size())) return nullptr;
    return &linedefs[it->second];
}

bool Level::IsSectorAncestor(const ID ancestor, ID sectorID) const {
    // Bounded by the sector count so a corrupted parent loop can't hang us.
    for (size_t steps = 0; sectorID != INVALID_ID && steps <= sectors.size(); ++steps) {
        if (sectorID == ancestor) return true;
        const Sector* sector = GetSector(sectorID);
        if (!sector) return false;
        sectorID = sector->parentID;
    }
    return false;
}

bool Level::SetSectorParent(const ID childID, const ID newParentID) {
    Sector* child = GetSector(childID);
    if (!child) return false;
    if (child->parentID == newParentID) return true;

    if (newParentID != INVALID_ID) {
        if (!GetSector(newParentID)) return false;
        // Covers childID == newParentID too.
        if (IsSectorAncestor(childID, newParentID)) return false;
    }

    if (Sector* oldParent = GetSector(child->parentID)) std::erase(oldParent->children, childID);

    child->parentID = newParentID;

    if (Sector* newParent = GetSector(newParentID)) newParent->children.push_back(childID);

    return true;
}

void Level::RebuildSectorChildren() {
    for (Sector& sector : sectors) sector.children.clear();

    for (Sector& sector : sectors) {
        if (sector.parentID == INVALID_ID) continue;

        Sector* parent = GetSector(sector.parentID);
        if (!parent || parent == &sector) {
            sector.parentID = INVALID_ID;
            continue;
        }
        parent->children.push_back(sector.id);
    }

    // Break any cycle left by bad data so every chain ends at a root.
    for (Sector& sector : sectors) {
        if (sector.parentID == INVALID_ID) continue;
        if (Sector* parent = GetSector(sector.parentID); IsSectorAncestor(sector.id, parent->id)) {
            std::erase(parent->children, sector.id);
            sector.parentID = INVALID_ID;
        }
    }
}

namespace {
    void ApplySurfaceChange(const SectorSurface& before, const SectorSurface& after, SectorSurface& child) {
        // Adding a zero delta leaves the child's value bit-for-bit unchanged,
        // so there's no need to test which numeric fields actually moved.
        child.height += after.height - before.height;
        child.slopeStrength += after.slopeStrength - before.slopeStrength;
        child.color += after.color - before.color;
        child.textureOffset += after.textureOffset - before.textureOffset;
        child.textureScale += after.textureScale - before.textureScale;

        if (after.texture != before.texture) child.texture = after.texture;
        if (after.slopeDirection != before.slopeDirection) child.slopeDirection = after.slopeDirection;
        if (after.flipTextureX != before.flipTextureX) child.flipTextureX = after.flipTextureX;
        if (after.flipTextureY != before.flipTextureY) child.flipTextureY = after.flipTextureY;
    }

    // Exact compares on purpose: this spots whether an edit wrote a field
    // since the snapshot, not whether two values are close.
    bool SameSurface(const SectorSurface& a, const SectorSurface& b) {
        return a.height == b.height &&
               a.color.x == b.color.x && a.color.y == b.color.y && a.color.z == b.color.z && a.color.w == b.color.w &&
               a.texture == b.texture &&
               a.slopeDirection == b.slopeDirection &&
               a.slopeStrength == b.slopeStrength &&
               a.textureOffset.x == b.textureOffset.x && a.textureOffset.y == b.textureOffset.y &&
               a.textureScale.x == b.textureScale.x && a.textureScale.y == b.textureScale.y &&
               a.flipTextureX == b.flipTextureX &&
               a.flipTextureY == b.flipTextureY;
    }

    bool SameValues(const Level::SectorValuesSnapshot& snapshot, const Sector& sector) {
        if (snapshot.light.x != sector.light.x || snapshot.light.y != sector.light.y || snapshot.light.z != sector.light.z)
            return false;

        if (snapshot.floors.size() != sector.floors.size()) return false;

        for (size_t i = 0; i < sector.floors.size(); ++i) {
            if (!SameSurface(snapshot.floors[i].floor, sector.floors[i].floor)) return false;
            if (!SameSurface(snapshot.floors[i].ceiling, sector.floors[i].ceiling)) return false;
        }

        return true;
    }
}

void Level::PropagateSectorChanges(const ID sectorID, const std::vector<SectorFloor>& floorsBefore,
                                   const Vector3& lightBefore, const std::vector<ID>& skip) {
    const Sector* parent = GetSector(sectorID);
    if (!parent || parent->children.empty()) return;

    // Descendants never include the parent itself, and propagating doesn't
    // touch the sectors vector, so these stay valid for the whole walk.
    const std::vector<SectorFloor>& floorsAfter = parent->floors;
    const Vector3 lightDelta = parent->light - lightBefore;
    const bool floorCountMatches = floorsBefore.size() == floorsAfter.size();

    std::vector<ID> pending = parent->children;

    while (!pending.empty()) {
        const ID childID = pending.back();
        pending.pop_back();

        if (std::ranges::find(skip, childID) != skip.end()) continue;

        Sector* child = GetSector(childID);
        if (!child) continue;

        child->light += lightDelta;

        if (floorCountMatches) {
            const size_t floorCount = std::min(floorsAfter.size(), child->floors.size());

            for (size_t floorIndex = 0; floorIndex < floorCount; ++floorIndex) {
                SectorFloor& childFloor = child->floors[floorIndex];

                ApplySurfaceChange(floorsBefore[floorIndex].floor, floorsAfter[floorIndex].floor, childFloor.floor);
                ApplySurfaceChange(floorsBefore[floorIndex].ceiling, floorsAfter[floorIndex].ceiling, childFloor.ceiling);

                // Moving only the parent's floor can push a child's floor
                // through its own ceiling. Raise the ceiling rather than
                // leave the room inverted.
                if (childFloor.floor.height >= childFloor.ceiling.height)
                    childFloor.ceiling.height = childFloor.floor.height + MIN_ROOM_HEIGHT;
            }
        }

        pending.insert(pending.end(), child->children.begin(), child->children.end());
    }
}

namespace {
    // One collider of an entity inside a moving sector, measured before the
    // sector moves.
    struct SectorBody {
        Vector2 point; // map (x, z) of the collider's bottom
        float bottom = 0.0f;
        float height = 0.0f;
        int floorIndex = -1; // the floor interval it is in
    };

    std::vector<SectorBody> CollectSectorBodies(const Level& level, const Sector& sector) {
        std::vector<SectorBody> bodies;

        for (const ID entityID : sector.entitiesInside) {
            const ComponentTransform* transform = level.transforms.Get(entityID);
            if (transform == nullptr) continue;

            for (const ComponentInstanceID instanceID : level.colliders.InstancesOf(entityID)) {
                const ComponentCollider* collider = level.colliders.GetInstance(instanceID);
                if (!collider->isActive || collider->isTrigger) continue;

                const Vector3 base = collider->Base(*transform);
                const Vector2 point = {base.x, base.z};

                bodies.push_back({point, base.y, collider->Height(*transform),MapQueries::FindFloorAtHeight(sector, point, base.y)});
            }
        }

        return bodies;
    }

    // How far `body` pokes through the ceiling with the floor and ceiling at
    // these heights (slopes kept). A floor that has risen into the body
    // carries it up first, so a lift only hits a body once it squeezes it.
    float CeilingOverlap(const Sector& sector, const SectorFloor& floor, const SectorBody& body,
                         const float floorHeight, const float ceilingHeight) {
        const float floorAt = floorHeight + MapQueries::SurfaceHeightAt(sector, floor.floor, body.point) - floor.floor.height;
        const float ceilingAt = ceilingHeight + MapQueries::SurfaceHeightAt(sector, floor.ceiling, body.point) - floor.ceiling.height;
        return std::max(body.bottom, floorAt) + body.height - ceilingAt;
    }

    // True if moving from the `before` heights to the `after` heights pushes
    // any body in floor interval `floorIndex` further into its ceiling.
    bool MoveHitsBody(const Sector& sector, const SectorFloor& floor, const int floorIndex,
                      const std::vector<SectorBody>& bodies,
                      const float floorBefore, const float ceilingBefore,
                      const float floorAfter, const float ceilingAfter) {
        return std::ranges::any_of(bodies, [&](const SectorBody& body) {
            if (body.floorIndex != floorIndex) return false;

            const float after = CeilingOverlap(sector, floor, body, floorAfter, ceilingAfter);
            return after > Constants::LargeEpsilon &&
                   after > CeilingOverlap(sector, floor, body, floorBefore, ceilingBefore);
        });
    }

    // Undoes this frame's step of a blocked move, then reacts per its mode.
    void BlockMove(SurfaceMove& move, float& height, const float heightBefore) {
        height = heightBefore;

        switch (move.collision) {
            case SECTOR_STOP: move.active = false; break;
            case SECTOR_RETRACT: SectorFloor::Retract(move); break;
            // Waits in place and tries again next frame. Still active even
            // if this step would have finished the move.
            case SECTOR_DONT_MOVE: move.active = true; break;
            case SECTOR_FORCE_MOVE: break;
        }
    }

    bool ChecksEntities(const SurfaceMove& move) {
        return move.active && move.collision != SECTOR_FORCE_MOVE;
    }
}

void Level::UpdateSectorMovement(const float deltaTime) {
    for (Sector& sector : sectors) {
        const bool floorsMoving = std::ranges::any_of(sector.floors, &SectorFloor::IsMoving);
        if (!floorsMoving && !sector.lightFade.active) continue;

        std::vector<SectorFloor> floorsBefore;
        if (!sector.children.empty()) floorsBefore = sector.floors;
        const Vector3 lightBefore = sector.light;

        bool changed = sector.UpdateLightFade(deltaTime);

        // Measured before anything moves, and only if some move cares.
        const bool checksEntities = std::ranges::any_of(sector.floors, [](const SectorFloor& floor) {
            return ChecksEntities(floor.floorMove) || ChecksEntities(floor.ceilingMove);
        });
        const std::vector<SectorBody> bodies = checksEntities ? CollectSectorBodies(*this, sector) : std::vector<SectorBody>{};

        for (size_t i = 0; i < sector.floors.size(); ++i) {
            SectorFloor& floor = sector.floors[i];

            const float lowest = i > 0
                ? sector.floors[i - 1].ceiling.height
                : -std::numeric_limits<float>::infinity();
            const float highest = i + 1 < sector.floors.size()
                ? sector.floors[i + 1].floor.height
                : std::numeric_limits<float>::infinity();

            const float floorBefore = floor.floor.height;
            const float ceilingBefore = floor.ceiling.height;
            const bool checkFloor = ChecksEntities(floor.floorMove);
            const bool checkCeiling = ChecksEntities(floor.ceilingMove);

            floor.UpdateMovement(deltaTime, lowest, highest);

            // Each surface is judged on its own step, the other held still.
            // Only a rising floor or a lowering ceiling can hit a body.
            const int floorIndex = static_cast<int>(i);
            if (checkFloor && floor.floor.height > floorBefore &&
                MoveHitsBody(sector, floor, floorIndex, bodies, floorBefore, ceilingBefore, floor.floor.height, ceilingBefore))
                BlockMove(floor.floorMove, floor.floor.height, floorBefore);

            if (checkCeiling && floor.ceiling.height < ceilingBefore &&
                MoveHitsBody(sector, floor, floorIndex, bodies, floorBefore, ceilingBefore, floorBefore, floor.ceiling.height))
                BlockMove(floor.ceilingMove, floor.ceiling.height, ceilingBefore);

            changed |= floor.floor.height != floorBefore || floor.ceiling.height != ceilingBefore;
        }

        if (changed && !sector.children.empty())
            PropagateSectorChanges(sector.id, floorsBefore, lightBefore);
    }
}

std::vector<Level::SectorValuesSnapshot> Level::SnapshotParentSectorValues() const {
    std::vector<SectorValuesSnapshot> snapshots;

    for (const Sector& sector : sectors) {
        if (sector.children.empty()) continue;
        snapshots.push_back({sector.id, sector.floors, sector.light});
    }

    return snapshots;
}

void Level::PropagateSnapshottedSectorChanges(const std::vector<SectorValuesSnapshot>& before) {
    // Collect every directly edited sector first. Propagating as we go would
    // make a changed child look edited by its parent's walk too.
    std::vector<const SectorValuesSnapshot*> changed;
    std::vector<ID> changedIDs;

    for (const SectorValuesSnapshot& snapshot : before) {
        const Sector* sector = GetSector(snapshot.id);
        if (!sector || SameValues(snapshot, *sector)) continue;

        changed.push_back(&snapshot);
        changedIDs.push_back(snapshot.id);
    }

    for (const SectorValuesSnapshot* snapshot : changed)
        PropagateSectorChanges(snapshot->id, snapshot->floors, snapshot->light, changedIDs);
}
