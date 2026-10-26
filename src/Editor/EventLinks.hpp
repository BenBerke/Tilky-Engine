#pragma once

#include <vector>

#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Event.hpp"
#include "Headers/Objects/Types.hpp"

struct Level;

// The event -> dispatch links the map editor and the runtime editor draw for
// the selected wall, sector or entity. An event links to every dispatch with
// the same tag, wherever it is attached.
namespace EventLinks {
    // From the event's owner to the dispatch's owner (the arrow's head).
    struct Link {
        Vector3 from;
        Vector3 to;
    };

    // Where lines to/from an object start: a wall's middle (halfway up it),
    // a sector's bounds centre (a little above its floor) or an entity's
    // transform. False when the object is gone or has no transform.
    bool AnchorPoint(const Level& level, EventOwnerKind kind, ID ownerID, Vector3& point);

    // Every link touching the object: from its events to the dispatches they
    // reach, and from the events that reach its dispatches to it. Links from
    // an object to itself are left out.
    std::vector<Link> Collect(const Level& level, EventOwnerKind kind, ID ownerID);
}
