#include "WallSnap.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "Headers/Editor/ImGuiDrawFunctions.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Math/Constants.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Map/LevelManager.hpp"

namespace WallSnap {
    namespace {
        bool picking = false;
        std::vector<ID> pickTargets;
        ID pickPrimary = INVALID_ID;

        constexpr float POSITION_EPSILON = 0.0001f;
        constexpr float YAW_EPSILON = 0.00001f;

        const Wall* FindWall(const Level& level, const ID wallID) {
            if (const auto it = level.wallIDToIndex.find(wallID);
                it != level.wallIDToIndex.end() && it->second >= 0 && it->second < static_cast<int>(level.walls.size()) &&
                level.walls[it->second].id == wallID)
                return &level.walls[it->second];

            // The lookup can lag behind an undo that swapped the walls.
            for (const Wall& wall : level.walls)
                if (wall.id == wallID) return &wall;

            return nullptr;
        }

        // Unit normal of the wall on the map, to the left of start -> end.
        // Zero for a wall of no length.
        Vector2 WallNormal(const Wall& wall) {
            const float dx = wall.end.x - wall.start.x;
            const float dy = wall.end.y - wall.start.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= POSITION_EPSILON) return {0.0f, 0.0f};

            return {-dy / length, dx / length};
        }

        // Nearest point to `point` on the wall, clamped to its ends.
        Vector2 ClosestPointOnWall(const Wall& wall, const Vector2 point) {
            const float dx = wall.end.x - wall.start.x;
            const float dy = wall.end.y - wall.start.y;
            const float lengthSq = dx * dx + dy * dy;
            if (lengthSq <= 0.0f) return wall.start;

            const float t = std::clamp(((point.x - wall.start.x) * dx + (point.y - wall.start.y) * dy) / lengthSq, 0.0f, 1.0f);
            return {wall.start.x + dx * t, wall.start.y + dy * t};
        }

        // On the wall's line `gap` off the snap's side, facing out (local +Z
        // along the normal - a static sprite's face). Only the yaw is
        // changed, by a turn about world up, so any tilt or roll is kept.
        bool Constrain(Level& level, ComponentTransform& transform, const Wall& wall, const Snap& snap) {
            const Vector2 normal = WallNormal(wall);
            if (normal.x == 0.0f && normal.y == 0.0f) return false;

            const Vector2 onWall = ClosestPointOnWall(wall, {transform.position.x, transform.position.z});
            const Vector3 position = {
                onWall.x + normal.x * snap.side * snap.gap,
                transform.position.y,
                onWall.y + normal.y * snap.side * snap.gap
            };

            bool changed = false;

            if (std::abs(position.x - transform.position.x) > POSITION_EPSILON ||
                std::abs(position.z - transform.position.z) > POSITION_EPSILON) {
                transform.SetPosition(position);
                changed = true;
            }

            const Vector3 forward = QuaternionMath::Rotate(transform.rotation, {0.0f, 0.0f, 1.0f});

            // Facing straight up or down has no heading to correct.
            if (forward.x * forward.x + forward.z * forward.z > YAW_EPSILON) {
                const float target = std::atan2(normal.x * snap.side, normal.y * snap.side);
                const float turn = std::remainder(target - std::atan2(forward.x, forward.z), 2.0f * Constants::Pi);

                if (std::abs(turn) > YAW_EPSILON) {
                    transform.rotation = (Quaternion::FromAxisAngle(0.0f, 1.0f, 0.0f, turn) * transform.rotation).Normalized();
                    changed = true;
                }
            }

            if (changed) {
                transform.isDirty = true;
                transform.UpdateObjectSectorAndFloor(level.sectors);
            }

            return changed;
        }

        bool ConstrainOne(Level& level, const ID entityID) {
            const auto it = level.wallSnaps.find(entityID);
            if (it == level.wallSnaps.end()) return false;

            ComponentTransform* transform = level.transforms.Get(entityID);
            const Wall* wall = FindWall(level, it->second.wallID);
            if (transform == nullptr || wall == nullptr) return false;

            return Constrain(level, *transform, *wall, it->second);
        }
    }

    Snaps& All() {
        return LevelManager::CurrentLevel().wallSnaps;
    }

    const Snap* Find(const ID entityID) {
        const Snaps& snaps = All();
        const auto it = snaps.find(entityID);
        return it != snaps.end() ? &it->second : nullptr;
    }

    bool IsPicking() {
        return picking;
    }

    void BeginPicking(const std::vector<ID>& entities, const ID primary) {
        if (entities.empty()) return;

        picking = true;
        pickTargets = entities;
        pickPrimary = primary;

        ImGuiDrawFunctions::ShowNotification(Localisation::Get("editor.toast.select_a_wall").c_str(), false, 3.0f);
    }

    void CancelPicking() {
        picking = false;
        pickTargets.clear();
        pickPrimary = INVALID_ID;
    }

    void CancelPickingIfSelectionChanged(const std::vector<ID>& selection) {
        if (!picking) return;

        std::vector<ID> a = selection;
        std::vector<ID> b = pickTargets;
        std::ranges::sort(a);
        std::ranges::sort(b);

        if (a != b) CancelPicking();
    }

    int FinishPicking(Level& level, const Wall& wall, const Vector2 point, const std::optional<float> height, const Vector2 viewer) {
        const std::vector<ID> targets = pickTargets;
        const ID primary = pickPrimary;
        CancelPicking();

        const Vector2 normal = WallNormal(wall);
        if (normal.x == 0.0f && normal.y == 0.0f) return 0;

        // The side the viewer is on; on the wall's line counts as the front.
        const Vector2 onWall = ClosestPointOnWall(wall, viewer);
        const float facing = (viewer.x - onWall.x) * normal.x + (viewer.y - onWall.y) * normal.y;
        const float side = facing < 0.0f ? -1.0f : 1.0f;

        int snapped = 0;

        for (const ID entityID : targets) {
            ComponentTransform* transform = level.transforms.Get(entityID);
            if (transform == nullptr) continue;

            if (entityID == primary) {
                transform->SetPosition({point.x, height.value_or(transform->position.y), point.y});
                transform->isDirty = true;
            }

            // Keeps the gap of an entity moved from another wall.
            Snap& snap = level.wallSnaps[entityID];
            snap.wallID = wall.id;
            snap.side = side;

            if (Entity* entity = level.GetEntity(entityID))
                for (ComponentSprite* sprite : entity->GetComponents<ComponentSprite>())
                    sprite->isStatic = true;

            ConstrainOne(level, entityID);
            ++snapped;
        }

        return snapped;
    }

    void Disable(const std::vector<ID>& entities) {
        Snaps& snaps = All();
        for (const ID entityID : entities) snaps.erase(entityID);
    }

    int SwitchSides(Level& level, const std::vector<ID>& entities) {
        int switched = 0;

        for (const ID entityID : entities) {
            const auto it = level.wallSnaps.find(entityID);
            if (it == level.wallSnaps.end()) continue;

            it->second.side = -it->second.side;
            ConstrainOne(level, entityID);
            ++switched;
        }

        return switched;
    }

    void SetGap(Level& level, const std::vector<ID>& entities, const float gap) {
        for (const ID entityID : entities) {
            const auto it = level.wallSnaps.find(entityID);
            if (it == level.wallSnaps.end()) continue;

            it->second.gap = gap;
            ConstrainOne(level, entityID);
        }
    }

    bool Enforce(Level& level) {
        Snaps& snaps = level.wallSnaps;
        if (snaps.empty()) return false;

        bool moved = false;

        for (auto it = snaps.begin(); it != snaps.end();) {
            ComponentTransform* transform = level.transforms.Get(it->first);
            const Wall* wall = FindWall(level, it->second.wallID);

            if (transform == nullptr || wall == nullptr) {
                it = snaps.erase(it);
                continue;
            }

            if (Constrain(level, *transform, *wall, it->second)) moved = true;
            ++it;
        }

        return moved;
    }
}
