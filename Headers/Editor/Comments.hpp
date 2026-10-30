#pragma once

#include <functional>
#include <string>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"
#include "Headers/Objects/Event.hpp"
#include "Headers/Objects/Level.hpp"

// Comments left in a level (Level::comments): notes on walls, sectors,
// linedefs and entities, and pins standing on their own. Editor-only, shared
// by the map editor and the runtime editor - the icons on the canvas or in
// the 3D view, and the Comments panel listing them.
namespace Comments {
    // The comment target for an event owner kind (the level has none).
    [[nodiscard]] EditorCommentTarget TargetFor(EventOwnerKind kind);
    [[nodiscard]] EventOwnerKind OwnerKindFor(EditorCommentTarget target);

    // Where the comment's icon sits in the world: a pin a little above the
    // floor under it, anything else where its event links start
    // (EventLinks::AnchorPoint). False when its object is gone.
    [[nodiscard]] bool Anchor(const Level& level, const EditorComment& comment, Vector3& point);

    // A new empty comment, with an undo step. The Comments panel focuses its
    // text field the next time it draws. Returns its ID.
    ID Add(Level& level, EditorCommentTarget target, ID targetID, const Vector2& position);

    // Projects a world point to window pixels; false when it can't be seen.
    using Projector = std::function<bool(const Vector3& world, Vector2& screen)>;

    // The icon of every comment, with its text as a tooltip while the cursor
    // is on it. `mouseOnView` is false while the cursor is over a panel.
    void DrawIcons(const Level& level, const Projector& project, bool mouseOnView);

    // A row's target was clicked: the editor shows that comment.
    using PickCallback = std::function<void(const EditorComment& comment)>;

    void DrawPanel(Level& level, const char* windowName, bool* open, const PickCallback& onPick);

    // Whether the icons are drawn at all (saved with the user settings).
    extern bool showIcons;
}
