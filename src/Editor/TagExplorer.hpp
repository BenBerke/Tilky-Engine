#pragma once

#include <functional>

#include "Headers/Objects/Event.hpp"
#include "Headers/Objects/Types.hpp"

struct Level;

// The Tag Explorer panel, shared by the map editor and the runtime editor:
// every project tag with what uses it - the walls, sectors and entities that
// carry it, the events and dispatches linked by it, and the events and
// dispatches whose settings name it (filters, relay and reset targets,
// change tag).
namespace TagExplorer {
    // A row was clicked: the editor selects that object and shows it.
    // `kind` is EventOwnerKind::LEVEL for the level's own events.
    using PickCallback = std::function<void(EventOwnerKind kind, ID id)>;

    // `windowName` is the ImGui window's name, ID included, so each editor
    // gets its own docking slot.
    void Draw(const Level& level, const char* windowName, bool* open, const PickCallback& onPick);
}
