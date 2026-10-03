//
// Created by berke on 5/26/2026.
//

#include "Headers/Objects/Level.hpp"

#include <algorithm>

Entity* Level::GetEntity(const ID entityID) {
    for (Entity& entity : entities) if (entity.id == entityID) return &entity;
    return nullptr;
}

const Entity* Level::GetEntity(const ID entityID) const {
    for (const Entity& entity : entities) if (entity.id == entityID) return &entity;
    return nullptr;
}

ID Level::CreateEntity(const bool uiEntity) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = "Entity";
    entity.attachedLevelId = id;

    entities.push_back(entity);

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

// Create entity through copy-paste
// Whenever a component is added or changed, this has to be updated too
// It would be good if we switch to C++26 and have reflections
ID Level::CreateEntity(Entity& copy) {
    Entity entity;
    entity.id = nextEntityID++;
    entity.name = copy.name + "Copy";
    entity.enabled = copy.enabled;

    if (copy.HasComponent<ComponentTransform>()) {
        auto *s = entity.AddComponent<ComponentTransform>();
        const ComponentTransform *cs = copy.GetComponent<ComponentTransform>();

        s->position = cs->position;
        s->relativeHeight = cs->relativeHeight;
        s->forward = cs->forward;
        s->scale = cs->scale;
        s->sectorIndex = cs->sectorIndex;
        s->isDirty = cs->isDirty;
        s->rotation = cs->rotation;
    }

    if (copy.HasComponent<ComponentSprite>()) {
        auto *s = entity.AddComponent<ComponentSprite>();
        const ComponentSprite *cs = copy.GetComponent<ComponentSprite>();

        s->textureFileNames = cs->textureFileNames;
        s->sideCount = cs->sideCount;
        s->isStatic = cs->isStatic;
    }

    if (copy.HasComponent<ComponentAudioSource>()) {
        auto *s = entity.AddComponent<ComponentAudioSource>();
        const ComponentAudioSource *ca = copy.GetComponent<ComponentAudioSource>();

        // name is left empty: it names the original's OpenAL source, and the
        // copy gets its own when the audio system starts it.
        s->soundFileName = ca->soundFileName;
        s->pitch = ca->pitch;
        s->gain = ca->gain;
        s->looping = ca->looping;
        s->playOnStart = ca->playOnStart;
        s->referenceDistance = ca->referenceDistance;
        s->maxDistance = ca->maxDistance;
        s->rollOffFactor = ca->rollOffFactor;
        s->innerConeAngle = ca->innerConeAngle;
        s->outerConeAngle = ca->outerConeAngle;
        s->outerGain = ca->outerGain;
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

    if (copy.HasComponent<ComponentPlayerController>()) {
        auto *s = entity.AddComponent<ComponentPlayerController>();
        const ComponentPlayerController *cs = copy.GetComponent<ComponentPlayerController>();

        s->isActive = false; // the original stays the active one
        s->speed = cs->speed;
        s->runningSpeed = cs->runningSpeed;
        s->jumpPower = cs->jumpPower;
        s->eyeHeight = cs->eyeHeight;
        s->friction = cs->friction;
        s->sensitivityX = cs->sensitivityX;
        s->sensitivityY = cs->sensitivityY;
        s->noClip = cs->noClip;
        // velocity, currentSpeed, currentEyeHeight intentionally left as default (read-only runtime state)
    }

    if (copy.HasComponent<ComponentCamera>()) {
        auto *s = entity.AddComponent<ComponentCamera>();
        const ComponentCamera *cs = copy.GetComponent<ComponentCamera>();

        s->isActive = false; // the original stays the active one
        s->yaw = cs->yaw;
        s->pitch = cs->pitch;
        s->fov = cs->fov;
        s->aspectRatio = cs->aspectRatio;
        s->nearPlane = cs->nearPlane;
        s->farPlane = cs->farPlane;
        // forward, target, view, projection intentionally left as default (runtime derived state)
    }

    if (copy.HasComponent<ComponentCollider>()) {
        auto *s = entity.AddComponent<ComponentCollider>();
        const ComponentCollider *cs = copy.GetComponent<ComponentCollider>();

        s->type = cs->type;
        s->isActive = cs->isActive;
        s->isTrigger = cs->isTrigger;
        s->scale = cs->scale;
        s->stepSize = cs->stepSize;
    }

    if (copy.HasComponent<ComponentRigidbody>()) {
        auto *s = entity.AddComponent<ComponentRigidbody>();
        const ComponentRigidbody *cs = copy.GetComponent<ComponentRigidbody>();

        s->isStatic = cs->isStatic;
        s->mass = cs->mass;
        s->gravityScale = cs->gravityScale;
        s->friction = cs->friction;
        // velocity intentionally left as default (runtime state)
    }

    if (copy.HasComponent<ComponentModel>()) {
        auto *s = entity.AddComponent<ComponentModel>();
        const ComponentModel *cs = copy.GetComponent<ComponentModel>();

        s->fileName = cs->fileName;
    }

    // UI Components
    if (copy.HasComponent<ComponentUITransform>()) {
        auto *s = entity.AddComponent<ComponentUITransform>();
        const ComponentUITransform *cs = copy.GetComponent<ComponentUITransform>();

        s->anchorMin = cs->anchorMin;
        s->anchorMax = cs->anchorMax;
        s->pivot = cs->pivot;
        s->position = cs->position;
        s->scale = cs->scale;
        s->rotation = cs->rotation;
        // resolvedPosition, resolvedSize intentionally left as default (runtime derived state)
    }

    if (copy.HasComponent<ComponentUISprite>()) {
        auto *s = entity.AddComponent<ComponentUISprite>();
        const ComponentUISprite *cs = copy.GetComponent<ComponentUISprite>();

        s->texture = cs->texture;
    }

    if (copy.HasComponent<ComponentUIText>()) {
        auto *s = entity.AddComponent<ComponentUIText>();
        const ComponentUIText *cs = copy.GetComponent<ComponentUIText>();

        s->text = cs->text;
    }

    entities.push_back(entity);

    return entity.id;
}

void Level::DestroyEntity(const ID entityID) {
    if (entityID == INVALID_ID) return;

    for (Sector& sector : sectors) std::erase(sector.entitiesInside, entityID);

    colliders.Remove(entityID);
    rigidbodies.Remove(entityID);

    sprites.Remove(entityID);
    models.Remove(entityID);
    audioSources.Remove(entityID);
    scripts.RemoveAll(entityID); // Remove() takes a script instance ID, not an owner ID
    playerControllers.Remove(entityID);
    cameras.Remove(entityID);
    transforms.Remove(entityID);

    // UI components.
    ui_sprites.Remove(entityID);
    ui_texts.Remove(entityID);
    ui_transforms.Remove(entityID);

    std::erase_if(entities, [entityID](const Entity& entity) {
        return entity.id == entityID;
    });
}

void Level::ActivateCamera(const ID entityID) {
    for (ComponentCamera& camera : cameras.components) camera.isActive = camera.ownerID == entityID;
}

void Level::ActivatePlayerController(const ID entityID) {
    for (ComponentPlayerController& controller : playerControllers.components)
        controller.isActive = controller.ownerID == entityID;
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
    // Same minimum the sector inspector enforces between a floor and its ceiling.
    constexpr float MIN_ROOM_HEIGHT = 0.01f;

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

void Level::UpdateSectorMovement(const float deltaTime) {
    for (Sector& sector : sectors) {
        const bool floorsMoving = std::ranges::any_of(sector.floors, &SectorFloor::IsMoving);
        if (!floorsMoving && !sector.lightFade.active) continue;

        std::vector<SectorFloor> floorsBefore;
        if (!sector.children.empty()) floorsBefore = sector.floors;
        const Vector3 lightBefore = sector.light;

        bool changed = sector.UpdateLightFade(deltaTime);

        for (size_t i = 0; i < sector.floors.size(); ++i) {
            const float lowest = i > 0
                ? sector.floors[i - 1].ceiling.height
                : -std::numeric_limits<float>::infinity();
            const float highest = i + 1 < sector.floors.size()
                ? sector.floors[i + 1].floor.height
                : std::numeric_limits<float>::infinity();

            changed |= sector.floors[i].UpdateMovement(deltaTime, lowest, highest);
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
