//
// Created by berke on 6/18/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Math/Quaternion/QuaternionMath.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"

#include <algorithm>
#include <cmath>

/// Collider debug view

void OpenGL::BuildGpuColliders() {
    const Level& level = LevelManager::CurrentLevel();

    gpuColliders.clear();

    for (const ComponentCollider& collider : level.colliders.ActiveColliders()) {
        const ComponentTransform* transform = level.transforms.Get(collider.ownerID);

        if (!transform) [[unlikely]] continue;

        GpuCollider gpuCollider;

        // The collider's bottom, as physics and Raycast place it: a sphere or
        // capsule rests on it, a box stands on it. The shader draws them
        // around a centre, so lift that by half the height.
        const Vector3 position = collider.Base(*transform);
        const float height = collider.Height(*transform);
        const float radius = collider.Radius(*transform);
        const Vector3 size = collider.WorldSize(*transform);

        gpuCollider.positionType.x = position.x;
        gpuCollider.positionType.y = position.y + height * 0.5f;
        gpuCollider.positionType.z = position.z;

        // The ColliderType: 0 sphere, 1 box, 2 capsule.
        gpuCollider.positionType.w = static_cast<float>(collider.type);

        // World sizes: x is a sphere's or capsule's radius, xyz a box's size.
        gpuCollider.scale.x = collider.IsRound() ? radius : size.x;
        gpuCollider.scale.y = size.y;
        gpuCollider.scale.z = size.z;
        // A capsule's caps sit this far above and below the centre.
        gpuCollider.scale.w = collider.type == COLLIDERTYPE_CAPSULE ? height * 0.5f - radius : 0.0f;

        gpuColliders.push_back(gpuCollider);
    }

    colliderCount = static_cast<GLsizei>(gpuColliders.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, colliderSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuColliders.size() * sizeof(GpuCollider),
        gpuColliders.empty() ? nullptr : gpuColliders.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, colliderSSBO);

}

/// Runtime editor entity markers

void OpenGL::BuildGpuEntityMarkers() {
    const Level& level = LevelManager::CurrentLevel();

    gpuEntityMarkers.clear();

    for (const ComponentTransform& transform : level.transforms.components)
        gpuEntityMarkers.push_back({transform.position.x, transform.position.y, transform.position.z, 0.0f});

    entityMarkerCount = static_cast<GLsizei>(gpuEntityMarkers.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, entityMarkerSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuEntityMarkers.size() * sizeof(Vector4),
        gpuEntityMarkers.empty() ? nullptr : gpuEntityMarkers.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, entityMarkerSSBO);
}

/// Runtime editor gizmos: a facing arrow on every entity and a frustum on
/// every camera. Drawn in the scene rather than over it, so they read apart
/// from the transform gizmo's screen-sized axis arrows.

namespace {
    using OpenGLRendererInternal::GpuEditorGizmoVertex;

    // Lengths in world units; entities face local +Z. Thickness is in screen
    // pixels, so a gizmo up close doesn't swell - capped in world units so a
    // far one doesn't become a blob either.
    constexpr float FACING_ARROW_LENGTH = 8.0f;
    constexpr float FACING_ARROW_LIFT = 1.0f; // off the feet, so it clears the floor
    constexpr float FACING_ARROW_HEAD_LENGTH = 2.0f;
    constexpr float FACING_ARROW_SHAFT_PIXELS = 10.0f; // width
    constexpr float FACING_ARROW_HEAD_PIXELS = 25.0f;
    constexpr float FACING_ARROW_MAX_SHAFT_RADIUS = 0.4f;
    constexpr float FACING_ARROW_MAX_HEAD_RADIUS = 1.0f;
    constexpr int FACING_ARROW_SIDES = 6;

    // Yellow: none of the axis colours, and solid where colliders are outlines.
    const Vector3 FACING_ARROW_COLOR = {1.0f, 0.82f, 0.1f};

    // Edges are solid beams rather than lines: a 1 pixel line got lost
    // against busy textures.
    constexpr float CAMERA_FRUSTUM_DEPTH = 8.0f;
    constexpr float CAMERA_FRUSTUM_EDGE_PIXELS = 10.0f; // width
    constexpr float CAMERA_FRUSTUM_MAX_EDGE_RADIUS = 0.4f;
    constexpr int CAMERA_FRUSTUM_EDGE_SIDES = 6;
    const Vector3 CAMERA_FRUSTUM_COLOR = {1.0f, 0.3f, 0.85f};

    // Fixed light, so the gizmos' shapes and directions read at a glance.
    const Vector3 GIZMO_LIGHT_DIRECTION = Vector3Math::Normalized({0.4f, 1.0f, 0.3f});

    void AddShadedTriangle(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const Vector3& a,
        const Vector3& b,
        const Vector3& c,
        const Vector3& normal,
        const Vector3& baseColor
    ) {
        const float light = 0.45f + 0.55f * std::max(0.0f, Vector3Math::Dot(normal, GIZMO_LIGHT_DIRECTION));
        const Vector4 color = {baseColor.x * light, baseColor.y * light, baseColor.z * light, 1.0f};

        for (const Vector3& point : {a, b, c}) vertices.push_back({{point.x, point.y, point.z, 0.0f}, color});
    }

    // Turns a width in pixels into a radius in world units at a point, for
    // the view the gizmos are drawn through.
    struct PixelScale {
        Vector3 eye;
        Vector3 forward; // unit
        float worldPerPixelPerDepth = 0.0f;

        [[nodiscard]] float Radius(const Vector3& point, const float pixels, const float maxRadius) const {
            constexpr float MIN_DEPTH = 0.05f;
            const float depth = std::max(Vector3Math::Dot(point - eye, forward), MIN_DEPTH);
            return std::min(depth * worldPerPixelPerDepth * pixels * 0.5f, maxRadius);
        }
    };

    // Two unit vectors perpendicular to `axis` and to each other.
    void PerpendicularBasis(const Vector3& axis, Vector3& u, Vector3& v) {
        const Vector3 helper = std::abs(axis.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        u = Vector3Math::Normalized(Vector3Math::Cross(axis, helper));
        v = Vector3Math::Cross(axis, u);
    }

    // An open prism from a to b, sized per end so it stays the same width on
    // screen along its length. Each end is padded by its radius so beams
    // meeting at a corner close the gap between them.
    void AddBeam(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const PixelScale& scale,
        const Vector3& a,
        const Vector3& b,
        const Vector3& color
    ) {
        const Vector3 span = b - a;
        if (Vector3Math::LengthSquared(span) <= Constants::Epsilon) return;

        const float startRadius = scale.Radius(a, CAMERA_FRUSTUM_EDGE_PIXELS, CAMERA_FRUSTUM_MAX_EDGE_RADIUS);
        const float endRadius = scale.Radius(b, CAMERA_FRUSTUM_EDGE_PIXELS, CAMERA_FRUSTUM_MAX_EDGE_RADIUS);

        const Vector3 axis = Vector3Math::Normalized(span);
        const Vector3 start = a - axis * startRadius;
        const Vector3 end = b + axis * endRadius;

        Vector3 u, v;
        PerpendicularBasis(axis, u, v);

        for (int side = 0; side < CAMERA_FRUSTUM_EDGE_SIDES; ++side) {
            const float angle0 = 2.0f * Constants::Pi * static_cast<float>(side) / CAMERA_FRUSTUM_EDGE_SIDES;
            const float angle1 = 2.0f * Constants::Pi * static_cast<float>(side + 1) / CAMERA_FRUSTUM_EDGE_SIDES;
            const float angleMid = (angle0 + angle1) * 0.5f;

            const Vector3 radial0 = u * std::cos(angle0) + v * std::sin(angle0);
            const Vector3 radial1 = u * std::cos(angle1) + v * std::sin(angle1);
            const Vector3 normal = u * std::cos(angleMid) + v * std::sin(angleMid);

            const Vector3 start0 = start + radial0 * startRadius;
            const Vector3 start1 = start + radial1 * startRadius;
            const Vector3 end0 = end + radial0 * endRadius;
            const Vector3 end1 = end + radial1 * endRadius;

            AddShadedTriangle(vertices, start0, start1, end1, normal, color);
            AddShadedTriangle(vertices, start0, end1, end0, normal, color);
        }
    }

    void AddFacingArrow(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const PixelScale& scale,
        const Vector3& origin,
        const Vector3& direction
    ) {
        Vector3 u, v;
        PerpendicularBasis(direction, u, v);

        const Vector3 headBase = origin + direction * (FACING_ARROW_LENGTH - FACING_ARROW_HEAD_LENGTH);
        const Vector3 tip = origin + direction * FACING_ARROW_LENGTH;
        const Vector3 backward = direction * -1.0f;

        // One size for the whole arrow, measured at its middle - it is short
        // enough that the taper wouldn't show.
        const Vector3 middle = origin + direction * (FACING_ARROW_LENGTH * 0.5f);
        const float shaftRadius = scale.Radius(middle, FACING_ARROW_SHAFT_PIXELS, FACING_ARROW_MAX_SHAFT_RADIUS);
        const float headRadius = scale.Radius(middle, FACING_ARROW_HEAD_PIXELS, FACING_ARROW_MAX_HEAD_RADIUS);

        // Slant of the head's sides, for their normals.
        const float headSlope = headRadius / FACING_ARROW_HEAD_LENGTH;

        for (int side = 0; side < FACING_ARROW_SIDES; ++side) {
            const float angle0 = 2.0f * Constants::Pi * static_cast<float>(side) / FACING_ARROW_SIDES;
            const float angle1 = 2.0f * Constants::Pi * static_cast<float>(side + 1) / FACING_ARROW_SIDES;
            const float angleMid = (angle0 + angle1) * 0.5f;

            const Vector3 radial0 = u * std::cos(angle0) + v * std::sin(angle0);
            const Vector3 radial1 = u * std::cos(angle1) + v * std::sin(angle1);
            const Vector3 radialMid = u * std::cos(angleMid) + v * std::sin(angleMid);

            // Shaft side and back cap.
            const Vector3 shaftBack0 = origin + radial0 * shaftRadius;
            const Vector3 shaftBack1 = origin + radial1 * shaftRadius;
            const Vector3 shaftFront0 = headBase + radial0 * shaftRadius;
            const Vector3 shaftFront1 = headBase + radial1 * shaftRadius;

            AddShadedTriangle(vertices, shaftBack0, shaftBack1, shaftFront1, radialMid, FACING_ARROW_COLOR);
            AddShadedTriangle(vertices, shaftBack0, shaftFront1, shaftFront0, radialMid, FACING_ARROW_COLOR);
            AddShadedTriangle(vertices, origin, shaftBack1, shaftBack0, backward, FACING_ARROW_COLOR);

            // Head side and base.
            const Vector3 headEdge0 = headBase + radial0 * headRadius;
            const Vector3 headEdge1 = headBase + radial1 * headRadius;

            AddShadedTriangle(vertices, headEdge0, headEdge1, tip, Vector3Math::Normalized(radialMid + direction * headSlope), FACING_ARROW_COLOR);
            AddShadedTriangle(vertices, headBase, headEdge1, headEdge0, backward, FACING_ARROW_COLOR);
        }
    }

    // A pyramid from the eye along the view, with a triangle past its top
    // edge marking up.
    void AddCameraFrustum(
        std::vector<GpuEditorGizmoVertex>& vertices,
        const PixelScale& scale,
        const Vector3& eye,
        const ComponentCamera& camera,
        const float aspectRatio
    ) {
        const Vector3 forward = Vector3Math::Normalized(CameraSystem::GetCameraForwardEngineSpace(camera.yaw, camera.pitch));
        const float yawRadians = camera.yaw * Constants::DegToRad;
        const Vector3 left = {std::cos(yawRadians), 0.0f, -std::sin(yawRadians)};
        const Vector3 up = Vector3Math::Normalized(Vector3Math::Cross(forward, left));

        const float halfHeight = CAMERA_FRUSTUM_DEPTH * std::tan(camera.fov * Constants::DegToRad * 0.5f);
        const float halfWidth = halfHeight * aspectRatio;
        const Vector3 farCentre = eye + forward * CAMERA_FRUSTUM_DEPTH;

        const Vector3 corners[4] = {
            farCentre + left * halfWidth + up * halfHeight,
            farCentre - left * halfWidth + up * halfHeight,
            farCentre - left * halfWidth - up * halfHeight,
            farCentre + left * halfWidth - up * halfHeight
        };

        for (int i = 0; i < 4; ++i) {
            AddBeam(vertices, scale, eye, corners[i], CAMERA_FRUSTUM_COLOR);
            AddBeam(vertices, scale, corners[i], corners[(i + 1) % 4], CAMERA_FRUSTUM_COLOR);
        }

        const Vector3 upTip = farCentre + up * (halfHeight * 1.6f);
        const Vector3 upLeft = farCentre + left * (halfWidth * 0.4f) + up * (halfHeight * 1.1f);
        const Vector3 upRight = farCentre - left * (halfWidth * 0.4f) + up * (halfHeight * 1.1f);

        AddBeam(vertices, scale, upLeft, upTip, CAMERA_FRUSTUM_COLOR);
        AddBeam(vertices, scale, upTip, upRight, CAMERA_FRUSTUM_COLOR);
        AddBeam(vertices, scale, upRight, upLeft, CAMERA_FRUSTUM_COLOR);
    }
}

void OpenGL::BuildGpuEditorGizmos(
    const ID cameraOwner,
    const ComponentTransform& viewTransform,
    const ComponentCamera& viewCamera
) {
    const Level& level = LevelManager::CurrentLevel();

    gpuEditorGizmoTriangles.clear();

    PixelScale scale;
    scale.eye = viewTransform.position;
    scale.forward = Vector3Math::Normalized(CameraSystem::GetCameraForwardEngineSpace(viewCamera.yaw, viewCamera.pitch));
    scale.worldPerPixelPerDepth = screenHeight > 0
        ? 2.0f * std::tan(viewCamera.fov * Constants::DegToRad * 0.5f) / static_cast<float>(screenHeight)
        : 0.0f;

    for (const ComponentTransform& transform : level.transforms.components) {
        if (transform.ownerID == cameraOwner) continue;

        const Vector3 facing = QuaternionMath::Rotate(transform.rotation, {0.0f, 0.0f, 1.0f});
        if (Vector3Math::LengthSquared(facing) <= Constants::Epsilon) continue;

        AddFacingArrow(
            gpuEditorGizmoTriangles,
            scale,
            transform.position + Vector3{0.0f, FACING_ARROW_LIFT, 0.0f},
            Vector3Math::Normalized(facing)
        );
    }

    // The aspect the camera would get if it were the one drawing.
    const float aspectRatio = screenHeight > 0 ? static_cast<float>(screenWidth) / static_cast<float>(screenHeight) : 1.0f;

    for (const ComponentCamera& camera : level.cameras.components) {
        if (camera.ownerID == cameraOwner) continue;

        const ComponentTransform* transform = level.transforms.Get(camera.ownerID);
        if (transform == nullptr) [[unlikely]] continue;

        // Where the renderer puts the eye: the feet plus an active
        // controller's eye height.
        Vector3 eye = transform->position;

        for (const ComponentPlayerController& controller : level.playerControllers.components) {
            if (controller.ownerID != camera.ownerID || !controller.isActive) continue;

            eye.y += controller.eyeHeight;
            break;
        }

        AddCameraFrustum(gpuEditorGizmoTriangles, scale, eye, camera, aspectRatio);
    }

    editorGizmoVertexCount = static_cast<GLsizei>(gpuEditorGizmoTriangles.size());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, editorGizmoSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(gpuEditorGizmoTriangles.size() * sizeof(GpuEditorGizmoVertex)),
        gpuEditorGizmoTriangles.empty() ? nullptr : gpuEditorGizmoTriangles.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, editorGizmoSSBO);
}
