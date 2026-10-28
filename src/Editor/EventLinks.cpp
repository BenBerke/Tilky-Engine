#include "EventLinks.hpp"

#include <algorithm>
#include <limits>
#include <tuple>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Level.hpp"

namespace {
    // How far above its floor a sector's anchor sits, so the line isn't
    // buried in the floor in 3D.
    constexpr float SECTOR_ANCHOR_LIFT = 0.5f;

    struct Owner {
        EventOwnerKind kind;
        ID id;
        const std::vector<Event>* events;
        const std::vector<Dispatch>* dispatches;
    };

    std::vector<Owner> AllOwners(const Level& level) {
        std::vector<Owner> owners;
        owners.reserve(level.walls.size() + level.linedefs.size() + level.sectors.size() + level.entities.size());

        for (const Wall& wall : level.walls)
            owners.push_back({EventOwnerKind::LINE, wall.id, &wall.events, &wall.dispatches});
        for (const Linedef& linedef : level.linedefs)
            owners.push_back({EventOwnerKind::LINEDEF, linedef.id, &linedef.events, &linedef.dispatches});
        for (const Sector& sector : level.sectors)
            owners.push_back({EventOwnerKind::SECTOR, sector.id, &sector.events, &sector.dispatches});
        for (const Entity& entity : level.entities)
            owners.push_back({EventOwnerKind::ENTITY, entity.id, &entity.events, &entity.dispatches});

        return owners;
    }

    bool HasEventTag(const std::vector<Event>& events, const TagId tag) {
        return std::ranges::any_of(events, [tag](const Event& event) { return event.tagId == tag; });
    }

    bool HasDispatchTag(const std::vector<Dispatch>& dispatches, const TagId tag) {
        return std::ranges::any_of(dispatches, [tag](const Dispatch& dispatch) { return dispatch.tagId == tag; });
    }
}

namespace EventLinks {
    bool AnchorPoint(const Level& level, const EventOwnerKind kind, const ID ownerID, Vector3& point) {
        switch (kind) {
            case EventOwnerKind::LINE: {
                const Wall* wall = MapQueries::GetWallByID(level, ownerID);
                if (wall == nullptr) return false;

                const Vector2 middle = (wall->start + wall->end) * 0.5f;
                float height = 0.0f;

                const Sector* sector = level.GetSector(wall->frontSector != INVALID_ID ? wall->frontSector : wall->backSector);
                if (sector != nullptr && !sector->floors.empty()) {
                    const float floor = MapQueries::SurfaceHeightAt(*sector, sector->floors[0].floor, middle);
                    const float ceiling = MapQueries::SurfaceHeightAt(*sector, sector->floors[0].ceiling, middle);
                    height = (floor + ceiling) * 0.5f;
                }

                point = {middle.x, height, middle.y};
                return true;
            }
            case EventOwnerKind::LINEDEF: {
                const Linedef* linedef = level.GetLinedef(ownerID);
                if (linedef == nullptr) return false;

                // Halfway up the sector the middle of the line is in, like a wall.
                const Vector2 middle = (linedef->start + linedef->end) * 0.5f;
                float height = 0.0f;

                const int sectorIndex = MapQueries::FindSectorContainingPoint(level.sectors, middle);
                if (sectorIndex >= 0 && sectorIndex < static_cast<int>(level.sectors.size())) {
                    const Sector& sector = level.sectors[sectorIndex];

                    if (!sector.floors.empty()) {
                        const float floor = MapQueries::SurfaceHeightAt(sector, sector.floors[0].floor, middle);
                        const float ceiling = MapQueries::SurfaceHeightAt(sector, sector.floors[0].ceiling, middle);
                        height = (floor + ceiling) * 0.5f;
                    }
                }

                point = {middle.x, height, middle.y};
                return true;
            }
            case EventOwnerKind::SECTOR: {
                const Sector* sector = level.GetSector(ownerID);
                if (sector == nullptr || sector->vertices.empty()) return false;

                Vector2 min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                Vector2 max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
                for (const Vector2& vertex : sector->vertices) {
                    min = {std::min(min.x, vertex.x), std::min(min.y, vertex.y)};
                    max = {std::max(max.x, vertex.x), std::max(max.y, vertex.y)};
                }

                const Vector2 centre = (min + max) * 0.5f;
                const float floor = sector->floors.empty()
                    ? 0.0f
                    : MapQueries::SurfaceHeightAt(*sector, sector->floors[0].floor, centre);

                point = {centre.x, floor + SECTOR_ANCHOR_LIFT, centre.y};
                return true;
            }
            case EventOwnerKind::ENTITY: {
                const ComponentTransform* transform = level.transforms.Get(ownerID);
                if (transform == nullptr) return false;

                point = transform->position;
                return true;
            }
            // The level isn't anywhere, so it has no link lines.
            case EventOwnerKind::LEVEL:
                return false;
        }
        return false;
    }

    std::vector<Link> Collect(const Level& level, const EventOwnerKind kind, const ID ownerID) {
        std::vector<Link> links;

        const std::vector<Owner> owners = AllOwners(level);
        const auto self = std::ranges::find_if(owners, [&](const Owner& owner) {
            return owner.kind == kind && owner.id == ownerID;
        });
        if (self == owners.end()) return links;

        Vector3 selfPoint;
        if (!AnchorPoint(level, kind, ownerID, selfPoint)) return links;

        for (const Owner& other : owners) {
            if (other.kind == kind && other.id == ownerID) continue;

            // Only the other object's tags decide; each pair is drawn once
            // per direction however many events/dispatches share the tag.
            const bool outgoing = std::ranges::any_of(*self->events, [&](const Event& event) {
                return HasDispatchTag(*other.dispatches, event.tagId);
            });
            const bool incoming = std::ranges::any_of(*self->dispatches, [&](const Dispatch& dispatch) {
                return HasEventTag(*other.events, dispatch.tagId);
            });
            if (!outgoing && !incoming) continue;

            Vector3 otherPoint;
            if (!AnchorPoint(level, other.kind, other.id, otherPoint)) continue;

            if (outgoing) links.push_back({selfPoint, otherPoint});
            if (incoming) links.push_back({otherPoint, selfPoint});
        }

        return links;
    }
}
