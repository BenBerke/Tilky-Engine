#ifndef TILKY_ENGINE_TRANSFORMGIZMO_HPP
#define TILKY_ENGINE_TRANSFORMGIZMO_HPP

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector3.hpp"

struct ComponentTransform;

// The runtime editor's Unity-style transform gizmo, drawn on the entity being
// edited. The move tool has an arrow per world axis and a square per plane
// between two axes; the rotate tool has a ring per local axis. Handles are
// coloured by axis (a plane by the axis it is perpendicular to) with the Map
// Editor's gizmo colours, and drawn over the 3D view with ImGui. Moves snap
// to the Map Editor grid and rotations to 15 degrees unless `snap` is false.
namespace TransformGizmo {
    enum class Tool {
        Move,
        Rotate
    };

    // The camera and viewport the gizmo is seen through - the same basis the
    // runtime editor builds its mouse rays from.
    struct View {
        Vector3 eye;
        Vector3 forward; // unit
        Vector3 left;    // unit, horizontal
        Vector3 up;      // unit
        float tanHalfFov = 1.0f;
        Vector2 viewportSize;
    };

    // Starts a drag when `mouse` (window pixels) is over a handle. Returns
    // whether it did.
    bool BeginDrag(const View& view, Tool tool, const ComponentTransform& transform, Vector2 mouse);

    // Applies the drag in progress for the current mouse position. Returns
    // true when the transform changed.
    bool UpdateDrag(const View& view, ComponentTransform& transform, Vector2 mouse, bool snap);

    void EndDrag();
    [[nodiscard]] bool IsDragging();

    // Projects a world point to window pixels. False when it is behind the camera.
    bool ProjectPoint(const View& view, const Vector3& point, Vector2& screen);

    // Draws onto ImGui's background draw list (above the 3D view, below the
    // panels), so call it inside an ImGui frame. With `showHover`, the handle
    // under `mouse` is highlighted; the one being dragged always is.
    void Draw(const View& view, Tool tool, const ComponentTransform& transform, Vector2 mouse, bool showHover);
}

#endif //TILKY_ENGINE_TRANSFORMGIZMO_HPP
