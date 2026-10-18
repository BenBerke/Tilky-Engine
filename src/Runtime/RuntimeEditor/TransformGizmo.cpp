#include "Headers/Runtime/RuntimeEditor/TransformGizmo.hpp"

#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Objects/Components.hpp"

#include "../../Editor/EditorInternal.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
    using TransformGizmo::Tool;
    using TransformGizmo::View;

    enum class Handle {
        None,
        AxisX, AxisY, AxisZ,
        PlaneYZ, PlaneXZ, PlaneXY, // named by the two axes they move along
        RingX, RingY, RingZ
    };

    // Handle size relative to the viewport, so the gizmo is the same size on
    // screen at any distance: the arrows are this fraction of half the
    // viewport height.
    constexpr float GIZMO_SIZE = 0.25f;

    constexpr float NEAR_DEPTH = 0.05f;
    constexpr float PICK_RADIUS = 8.0f; // pixels
    constexpr float PLANE_INNER = 0.2f; // plane squares, as fractions of the arrow length
    constexpr float PLANE_OUTER = 0.45f;
    constexpr int RING_SEGMENTS = 64;

    constexpr float ROTATE_DEGREES_PER_PIXEL = 0.5f;
    constexpr float ROTATE_SNAP_DEGREES = 15.0f;

    // An arrow shorter than this on screen points almost straight at the
    // camera, and a plane facing it this edge-on can't be dragged sensibly,
    // so both are hidden then.
    constexpr float MIN_ARROW_PIXELS = 12.0f;
    constexpr float MIN_PLANE_FACING = 0.15f;

    const Vector3 WORLD_AXES[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

    struct Frame {
        Vector3 origin;
        float length = 0.0f;
        Vector3 axes[3]; // world axes for Move, the entity's local axes for Rotate
    };

    // Drag state, captured on button-down so the whole drag works from where
    // it started rather than accumulating per-frame error.
    Handle activeHandle = Handle::None;
    Vector3 startPosition;
    Quaternion startRotation;
    Vector3 dragAxis;       // the arrow's axis, the plane's normal or the ring's axis
    float startAxisParameter = 0.0f;
    Vector3 startPlaneHit;
    Vector2 startMouse;
    Vector2 rotateScreenTangent; // unit screen direction of positive rotation at the grab point

    float Component(const Vector3& v, const int axis) {
        return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
    }

    void SetComponent(Vector3& v, const int axis, const float value) {
        if (axis == 0) v.x = value;
        else if (axis == 1) v.y = value;
        else v.z = value;
    }

    bool Project(const View& view, const Vector3& point, Vector2& screen) {
        const Vector3 toPoint = point - view.eye;
        const float depth = Vector3Math::Dot(toPoint, view.forward);
        if (depth < NEAR_DEPTH) return false;

        const float aspect = view.viewportSize.x / view.viewportSize.y;
        const float ndcX = -Vector3Math::Dot(toPoint, view.left) / (depth * view.tanHalfFov * aspect);
        const float ndcY = Vector3Math::Dot(toPoint, view.up) / (depth * view.tanHalfFov);

        screen = {(ndcX + 1.0f) * 0.5f * view.viewportSize.x, (1.0f - ndcY) * 0.5f * view.viewportSize.y};
        return true;
    }

    // The inverse of Project() - same math as the runtime editor's mouse ray.
    Vector3 MouseRay(const View& view, const Vector2 mouse) {
        const float ndcX = 2.0f * mouse.x / view.viewportSize.x - 1.0f;
        const float ndcY = 1.0f - 2.0f * mouse.y / view.viewportSize.y;
        const float aspect = view.viewportSize.x / view.viewportSize.y;

        return Vector3Math::Normalized(
            view.forward +
            view.left * (-ndcX * view.tanHalfFov * aspect) +
            view.up * (ndcY * view.tanHalfFov)
        );
    }

    bool MakeFrame(const View& view, const Tool tool, const ComponentTransform& transform, Frame& frame) {
        if (view.viewportSize.x <= 0.0f || view.viewportSize.y <= 0.0f) return false;

        frame.origin = transform.position;

        const float depth = Vector3Math::Dot(frame.origin - view.eye, view.forward);
        if (depth < NEAR_DEPTH) return false;

        frame.length = depth * view.tanHalfFov * GIZMO_SIZE;

        for (int i = 0; i < 3; ++i)
            frame.axes[i] = tool == Tool::Rotate
                ? Vector3Math::Normalized(QuaternionMath::Rotate(transform.rotation, WORLD_AXES[i]))
                : WORLD_AXES[i];

        return true;
    }

    float DistanceToSegment(const Vector2& point, const Vector2& a, const Vector2& b) {
        const Vector2 ab = b - a;
        const float lengthSq = ab.x * ab.x + ab.y * ab.y;

        float t = lengthSq > 0.0f ? ((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / lengthSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        const float dx = point.x - (a.x + ab.x * t);
        const float dy = point.y - (a.y + ab.y * t);
        return std::sqrt(dx * dx + dy * dy);
    }

    bool InsideConvexQuad(const Vector2& point, const std::array<Vector2, 4>& quad) {
        bool anyPositive = false;
        bool anyNegative = false;

        for (int i = 0; i < 4; ++i) {
            const Vector2& a = quad[i];
            const Vector2& b = quad[(i + 1) % 4];
            const float cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);

            if (cross > 0.0f) anyPositive = true;
            if (cross < 0.0f) anyNegative = true;
        }

        return !(anyPositive && anyNegative);
    }

    bool RayPlane(const Vector3& planePoint, const Vector3& normal, const Vector3& rayOrigin,
                  const Vector3& rayDirection, Vector3& hit);

    // How far along `origin + axis * t` the mouse ray points. The ray is
    // intersected with the plane that contains the axis and faces the camera
    // most, then projected onto the axis - which, unlike the closest point
    // between the two lines, stays exactly under the cursor in perspective.
    // Fails when the axis points (nearly) straight at the camera.
    bool AxisParameter(const Vector3& origin, const Vector3& axis, const Vector3& rayOrigin,
                       const Vector3& rayDirection, float& t) {
        const Vector3 toOrigin = Vector3Math::Normalized(origin - rayOrigin);
        const Vector3 normal = toOrigin - axis * Vector3Math::Dot(toOrigin, axis);

        if (Vector3Math::LengthSquared(normal) < 1e-4f) return false;

        Vector3 hit;
        if (!RayPlane(origin, Vector3Math::Normalized(normal), rayOrigin, rayDirection, hit)) return false;

        t = Vector3Math::Dot(hit - origin, axis);
        return true;
    }

    bool RayPlane(const Vector3& planePoint, const Vector3& normal, const Vector3& rayOrigin,
                  const Vector3& rayDirection, Vector3& hit) {
        const float facing = Vector3Math::Dot(normal, rayDirection);
        if (std::abs(facing) < 1e-4f) return false;

        const float distance = Vector3Math::Dot(normal, planePoint - rayOrigin) / facing;
        if (distance <= 0.0f) return false;

        hit = rayOrigin + rayDirection * distance;
        return true;
    }

    // Plane handle `planeIndex` is perpendicular to axis `planeIndex` and
    // lies between the other two.
    void PlaneAxes(const int planeIndex, int& a, int& b) {
        a = planeIndex == 0 ? 1 : 0;
        b = planeIndex == 2 ? 1 : 2;
    }

    bool PlaneFacesCamera(const View& view, const Frame& frame, const int planeIndex) {
        const Vector3 toOrigin = Vector3Math::Normalized(frame.origin - view.eye);
        return std::abs(Vector3Math::Dot(frame.axes[planeIndex], toOrigin)) >= MIN_PLANE_FACING;
    }

    bool ProjectPlaneQuad(const View& view, const Frame& frame, const int planeIndex, std::array<Vector2, 4>& quad) {
        int a, b;
        PlaneAxes(planeIndex, a, b);

        const float inner = frame.length * PLANE_INNER;
        const float outer = frame.length * PLANE_OUTER;
        const Vector3& axisA = frame.axes[a];
        const Vector3& axisB = frame.axes[b];

        const Vector3 corners[4] = {
            frame.origin + axisA * inner + axisB * inner,
            frame.origin + axisA * outer + axisB * inner,
            frame.origin + axisA * outer + axisB * outer,
            frame.origin + axisA * inner + axisB * outer
        };

        for (int i = 0; i < 4; ++i) if (!Project(view, corners[i], quad[i])) return false;
        return true;
    }

    bool ProjectArrow(const View& view, const Frame& frame, const int axis, Vector2& start, Vector2& tip) {
        if (!Project(view, frame.origin, start)) return false;
        if (!Project(view, frame.origin + frame.axes[axis] * frame.length, tip)) return false;

        const Vector2 span = tip - start;
        return span.x * span.x + span.y * span.y >= MIN_ARROW_PIXELS * MIN_ARROW_PIXELS;
    }

    // `position` is in segments around the ring, and may be fractional.
    Vector3 RingPoint(const Frame& frame, const int axis, const float position) {
        int a, b;
        PlaneAxes(axis, a, b);

        const float angle = 2.0f * Constants::Pi * position / static_cast<float>(RING_SEGMENTS);
        return frame.origin + (frame.axes[a] * std::cos(angle) + frame.axes[b] * std::sin(angle)) * frame.length;
    }

    // Like DistanceToSegment, also returning how far along a..b (0..1) the
    // closest point is.
    float DistanceToSegment(const Vector2& point, const Vector2& a, const Vector2& b, float& along) {
        const Vector2 ab = b - a;
        const float lengthSq = ab.x * ab.x + ab.y * ab.y;

        along = lengthSq > 0.0f ? ((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / lengthSq : 0.0f;
        along = std::clamp(along, 0.0f, 1.0f);

        const float dx = point.x - (a.x + ab.x * along);
        const float dy = point.y - (a.y + ab.y * along);
        return std::sqrt(dx * dx + dy * dy);
    }

    Handle AxisHandle(const int axis) { return static_cast<Handle>(static_cast<int>(Handle::AxisX) + axis); }
    Handle PlaneHandle(const int plane) { return static_cast<Handle>(static_cast<int>(Handle::PlaneYZ) + plane); }
    Handle RingHandle(const int axis) { return static_cast<Handle>(static_cast<int>(Handle::RingX) + axis); }

    // For a ring, also returns where on it (in segments, see RingPoint) the
    // point closest to `mouse` is.
    Handle Pick(const View& view, const Tool tool, const Frame& frame, const Vector2 mouse, float* ringPosition = nullptr) {
        if (tool == Tool::Move) {
            // Planes first: they sit between the arrows and are the smaller target.
            for (int plane = 0; plane < 3; ++plane) {
                if (!PlaneFacesCamera(view, frame, plane)) continue;

                std::array<Vector2, 4> quad;
                if (ProjectPlaneQuad(view, frame, plane, quad) && InsideConvexQuad(mouse, quad)) return PlaneHandle(plane);
            }

            Handle best = Handle::None;
            float bestDistance = PICK_RADIUS;

            for (int axis = 0; axis < 3; ++axis) {
                Vector2 start, tip;
                if (!ProjectArrow(view, frame, axis, start, tip)) continue;

                const float distance = DistanceToSegment(mouse, start, tip);
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    best = AxisHandle(axis);
                }
            }

            return best;
        }

        Handle best = Handle::None;
        float bestDistance = PICK_RADIUS;
        float bestFacing = -1.0f;

        // Rings cross each other on screen (an edge-on ring is a line through
        // the others), so on a near-tie the ring facing the camera wins: it is
        // the one whose drag direction is readable there.
        constexpr float TIE_PIXELS = 1.0f;
        const Vector3 toOrigin = Vector3Math::Normalized(frame.origin - view.eye);

        for (int axis = 0; axis < 3; ++axis) {
            const float facing = std::abs(Vector3Math::Dot(frame.axes[axis], toOrigin));

            Vector2 previous;
            bool previousVisible = Project(view, RingPoint(frame, axis, 0.0f), previous);

            for (int segment = 1; segment <= RING_SEGMENTS; ++segment) {
                Vector2 current;
                const bool currentVisible = Project(view, RingPoint(frame, axis, static_cast<float>(segment)), current);

                if (previousVisible && currentVisible) {
                    float along;
                    const float distance = DistanceToSegment(mouse, previous, current, along);

                    const bool clearlyCloser = distance < bestDistance - TIE_PIXELS;
                    const bool tiedButFacing = distance < bestDistance + TIE_PIXELS && distance <= PICK_RADIUS &&
                                               facing > bestFacing;

                    if (clearlyCloser || tiedButFacing ||
                        (best == RingHandle(axis) && distance < bestDistance)) {
                        bestDistance = distance;
                        bestFacing = facing;
                        best = RingHandle(axis);
                        if (ringPosition != nullptr) *ringPosition = static_cast<float>(segment - 1) + along;
                    }
                }

                previous = current;
                previousVisible = currentVisible;
            }
        }

        return best;
    }

    ImU32 ToImColor(const Vector3& color, const float alpha = 1.0f) {
        return IM_COL32(
            static_cast<int>(color.x),
            static_cast<int>(color.y),
            static_cast<int>(color.z),
            static_cast<int>(alpha * 255.0f)
        );
    }

    const Vector3& AxisColor(const int axis) {
        using namespace MapEditorInternal;
        return axis == 0 ? gizmoXColor : axis == 1 ? gizmoYColor : gizmoZColor;
    }
}

namespace TransformGizmo {
    bool BeginDrag(const View& view, const Tool tool, const ComponentTransform& transform, const Vector2 mouse) {
        Frame frame;
        if (!MakeFrame(view, tool, transform, frame)) return false;

        float ringPosition = 0.0f;
        const Handle handle = Pick(view, tool, frame, mouse, &ringPosition);
        if (handle == Handle::None) return false;

        const Vector3 ray = MouseRay(view, mouse);

        if (handle >= Handle::AxisX && handle <= Handle::AxisZ) {
            dragAxis = frame.axes[static_cast<int>(handle) - static_cast<int>(Handle::AxisX)];
            if (!AxisParameter(frame.origin, dragAxis, view.eye, ray, startAxisParameter)) return false;
        }
        else if (handle >= Handle::PlaneYZ && handle <= Handle::PlaneXY) {
            dragAxis = frame.axes[static_cast<int>(handle) - static_cast<int>(Handle::PlaneYZ)];
            if (!RayPlane(frame.origin, dragAxis, view.eye, ray, startPlaneHit)) return false;
        }
        else {
            // Rotation follows the mouse along the ring's tangent where it was
            // grabbed, which keeps working even when the ring is edge-on.
            const int axis = static_cast<int>(handle) - static_cast<int>(Handle::RingX);
            dragAxis = frame.axes[axis];

            const Vector3 grabPoint = RingPoint(frame, axis, ringPosition);
            const Vector3 tangent = Vector3Math::Normalized(Vector3Math::Cross(dragAxis, grabPoint - frame.origin));

            Vector2 grabScreen, aheadScreen;
            if (!Project(view, grabPoint, grabScreen) ||
                !Project(view, grabPoint + tangent * (frame.length * 0.1f), aheadScreen)) return false;

            const Vector2 screenTangent = aheadScreen - grabScreen;
            const float tangentLength = std::sqrt(screenTangent.x * screenTangent.x + screenTangent.y * screenTangent.y);
            if (tangentLength < 1e-3f) return false;

            rotateScreenTangent = screenTangent / tangentLength;
        }

        activeHandle = handle;
        startPosition = transform.position;
        startRotation = transform.rotation;
        startMouse = mouse;
        return true;
    }

    bool UpdateDrag(const View& view, ComponentTransform& transform, const Vector2 mouse, const bool snap) {
        if (activeHandle == Handle::None) return false;

        const Vector3 ray = MouseRay(view, mouse);
        const float gridSize = std::max(MapEditorInternal::GRID_SIZE, MapEditorInternal::MIN_GRID_SIZE);
        const bool snapToGrid = snap && MapEditorInternal::gridSnapEnabled;

        if (activeHandle >= Handle::AxisX && activeHandle <= Handle::AxisZ) {
            float t;
            if (!AxisParameter(startPosition, dragAxis, view.eye, ray, t)) return false;

            Vector3 position = startPosition + dragAxis * (t - startAxisParameter);

            if (snapToGrid) {
                const int axis = static_cast<int>(activeHandle) - static_cast<int>(Handle::AxisX);
                SetComponent(position, axis, std::round(Component(position, axis) / gridSize) * gridSize);
            }

            if (position == transform.position) return false;

            transform.SetPosition(position);
            return true;
        }

        if (activeHandle >= Handle::PlaneYZ && activeHandle <= Handle::PlaneXY) {
            Vector3 hit;
            if (!RayPlane(startPosition, dragAxis, view.eye, ray, hit)) return false;

            Vector3 position = startPosition + (hit - startPlaneHit);

            const int plane = static_cast<int>(activeHandle) - static_cast<int>(Handle::PlaneYZ);

            // Pin the axis the plane is perpendicular to; only float error
            // could move it.
            SetComponent(position, plane, Component(startPosition, plane));

            if (snapToGrid) {
                int a, b;
                PlaneAxes(plane, a, b);
                SetComponent(position, a, std::round(Component(position, a) / gridSize) * gridSize);
                SetComponent(position, b, std::round(Component(position, b) / gridSize) * gridSize);
            }

            if (position == transform.position) return false;

            transform.SetPosition(position);
            return true;
        }

        const Vector2 moved = mouse - startMouse;
        float degrees = (moved.x * rotateScreenTangent.x + moved.y * rotateScreenTangent.y) * ROTATE_DEGREES_PER_PIXEL;

        if (snap) degrees = std::round(degrees / ROTATE_SNAP_DEGREES) * ROTATE_SNAP_DEGREES;

        // Rotating about a world-space axis is applied on the left.
        const Quaternion rotation = (Quaternion::FromAxisAngle(dragAxis.x, dragAxis.y, dragAxis.z, degrees * Constants::DegToRad) * startRotation).Normalized();

        const Quaternion& current = transform.rotation;
        if (rotation.x == current.x && rotation.y == current.y && rotation.z == current.z && rotation.w == current.w) return false;

        transform.rotation = rotation;
        return true;
    }

    void EndDrag() {
        activeHandle = Handle::None;
    }

    bool IsDragging() {
        return activeHandle != Handle::None;
    }

    bool ProjectPoint(const View& view, const Vector3& point, Vector2& screen) {
        return Project(view, point, screen);
    }

    void Draw(const View& view, const Tool tool, const ComponentTransform& transform, const Vector2 mouse, const bool showHover) {
        Frame frame;
        if (!MakeFrame(view, tool, transform, frame)) return;

        const Handle highlighted = activeHandle != Handle::None
            ? activeHandle
            : showHover ? Pick(view, tool, frame, mouse) : Handle::None;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        const Vector3& highlightColor = MapEditorInternal::highlightedHandleColor;

        if (tool == Tool::Move) {
            for (int plane = 0; plane < 3; ++plane) {
                if (!PlaneFacesCamera(view, frame, plane)) continue;

                std::array<Vector2, 4> quad;
                if (!ProjectPlaneQuad(view, frame, plane, quad)) continue;

                const bool lit = highlighted == PlaneHandle(plane);
                const Vector3& color = lit ? highlightColor : AxisColor(plane);

                ImVec2 points[4];
                for (int i = 0; i < 4; ++i) points[i] = {quad[i].x, quad[i].y};

                drawList->AddConvexPolyFilled(points, 4, ToImColor(color, lit ? 0.6f : 0.3f));
                drawList->AddPolyline(points, 4, ToImColor(color), ImDrawFlags_Closed, 1.5f);
            }

            constexpr float HEAD_LENGTH = 14.0f;
            constexpr float HEAD_HALF_WIDTH = 6.0f;

            for (int axis = 0; axis < 3; ++axis) {
                Vector2 start, tip;
                if (!ProjectArrow(view, frame, axis, start, tip)) continue;

                const Vector3& color = highlighted == AxisHandle(axis) ? highlightColor : AxisColor(axis);
                const ImU32 imColor = ToImColor(color);

                const Vector2 span = tip - start;
                const float length = std::sqrt(span.x * span.x + span.y * span.y);
                const Vector2 direction = span / length;
                const Vector2 headBase = tip - direction * std::min(HEAD_LENGTH, length * 0.5f);

                drawList->AddLine({start.x, start.y}, {headBase.x, headBase.y}, imColor, 3.0f);
                drawList->AddTriangleFilled(
                    {tip.x, tip.y},
                    {headBase.x - direction.y * HEAD_HALF_WIDTH, headBase.y + direction.x * HEAD_HALF_WIDTH},
                    {headBase.x + direction.y * HEAD_HALF_WIDTH, headBase.y - direction.x * HEAD_HALF_WIDTH},
                    imColor
                );
            }

            return;
        }

        for (int axis = 0; axis < 3; ++axis) {
            const bool lit = highlighted == RingHandle(axis);
            const ImU32 imColor = ToImColor(lit ? highlightColor : AxisColor(axis));
            const float thickness = lit ? 3.5f : 2.5f;

            Vector2 previous;
            bool previousVisible = Project(view, RingPoint(frame, axis, 0.0f), previous);

            for (int segment = 1; segment <= RING_SEGMENTS; ++segment) {
                Vector2 current;
                const bool currentVisible = Project(view, RingPoint(frame, axis, static_cast<float>(segment)), current);

                if (previousVisible && currentVisible)
                    drawList->AddLine({previous.x, previous.y}, {current.x, current.y}, imColor, thickness);

                previous = current;
                previousVisible = currentVisible;
            }
        }
    }
}
