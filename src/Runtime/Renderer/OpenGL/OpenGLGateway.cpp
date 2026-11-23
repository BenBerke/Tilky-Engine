#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "tracy/Tracy.hpp"

// Views through gateways (Headers/Map/Gateways.hpp), with the stencil
// buffer. A view draws where the stencil holds its number (the camera's own
// is 0). For each gateway opening it sees:
//
//   1. the opening's box raises the stencil by one where it shows,
//   2. the depth there goes to the far plane,
//   3. the world is drawn again from the camera moved through the gateway,
//      only there, without anything in front of the exit wall - and so on
//      for the gateways that view sees, one stencil number higher,
//   4. the opening's own depth goes back and the stencil back down, so
//      what is drawn after it (another gateway) sorts against it.
//
// Past Rendering > Gateway Depth the opening is drawn black instead.

namespace {
    // The view through a gateway keeps what is in front of its exit wall
    // and a hair behind it, so the exit room's walls and floor touching that
    // wall reach the opening's edges. (The exit wall itself, and anything on
    // its line, the shader leaves out.)
    constexpr float CLIP_EPSILON = 0.001f;

    // How far the box reaches behind its wall: more than the near plane, so
    // a camera standing in the opening still sees through it.
    float BoxDepth(const ComponentCamera& camera) {
        return std::max(camera.nearPlane * 4.0f, 0.25f);
    }

    // Only a camera this near the opening needs the whole box. Further away
    // its sides could show beside the opening (on a wall in line with them,
    // past a two-sided gateway), where the view through it has nothing.
    bool NeedsWholeBox(const Gateways::Opening& opening, const Vector2 eye, const float boxDepth) {
        return opening.SideDistance(eye) < boxDepth * 2.0f;
    }

    // Opening::MapPoint undone: a point past the exit wall to where it is
    // seen, beyond the opening.
    Matrix4 ExitToEntry(const Gateways::Opening& opening) {
        const float c = opening.cosTurn;
        const float s = opening.sinTurn;
        const Vector2 entry = opening.centre;
        const Vector2 exit = opening.exitCentre;

        Matrix4 matrix = Matrix4::Identity();
        matrix.m[0][0] = c;
        matrix.m[0][2] = s;
        matrix.m[0][3] = entry.x - c * exit.x - s * exit.y;
        matrix.m[1][3] = -opening.heightOffset;
        matrix.m[2][0] = -s;
        matrix.m[2][2] = c;
        matrix.m[2][3] = entry.y + s * exit.x - c * exit.y;
        return matrix;
    }

    std::array<Vector3, 4> OpeningCorners(const Gateways::Opening& opening) {
        const Vector2 start = opening.StartPoint();
        const Vector2 end = opening.EndPoint();

        return {
            Vector3{start.x, opening.bottom, start.y},
            Vector3{end.x, opening.bottom, end.y},
            Vector3{end.x, opening.top, end.y},
            Vector3{start.x, opening.top, start.y}
        };
    }

    // The pixels (x, y, width, height from the bottom left) the opening can
    // cover, within `bounds`. Corners at or behind the eye could land
    // anywhere, so then it is all of `bounds`. False when it is off them.
    bool OpeningScreenRect(const Gateways::Opening& opening, const Matrix4& viewProjection,
                           const int screenWidth, const int screenHeight, const int bounds[4], int out[4]) {
        const Matrix4& m = viewProjection;

        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
        bool any = false;
        bool behindEye = false;

        for (const Vector3& corner : OpeningCorners(opening)) {
            const float x = m.m[0][0] * corner.x + m.m[0][1] * corner.y + m.m[0][2] * corner.z + m.m[0][3];
            const float y = m.m[1][0] * corner.x + m.m[1][1] * corner.y + m.m[1][2] * corner.z + m.m[1][3];
            const float w = m.m[3][0] * corner.x + m.m[3][1] * corner.y + m.m[3][2] * corner.z + m.m[3][3];

            if (w <= 0.0001f) {
                behindEye = true;
                break;
            }

            const float px = (x / w + 1.0f) * 0.5f * static_cast<float>(screenWidth);
            const float py = (y / w + 1.0f) * 0.5f * static_cast<float>(screenHeight);

            if (!any) {
                minX = maxX = px;
                minY = maxY = py;
                any = true;
                continue;
            }

            minX = std::min(minX, px);
            maxX = std::max(maxX, px);
            minY = std::min(minY, py);
            maxY = std::max(maxY, py);
        }

        int x0 = bounds[0];
        int y0 = bounds[1];
        int x1 = bounds[0] + bounds[2];
        int y1 = bounds[1] + bounds[3];

        if (!behindEye && any) {
            // Clamped before the int cast; a pixel of padding for rounding.
            const auto clampPixel = [](const float value, const int limit) {
                return static_cast<int>(std::clamp(value, -2.0f, static_cast<float>(limit) + 2.0f));
            };

            x0 = std::max(x0, clampPixel(std::floor(minX), screenWidth) - 1);
            y0 = std::max(y0, clampPixel(std::floor(minY), screenHeight) - 1);
            x1 = std::min(x1, clampPixel(std::ceil(maxX), screenWidth) + 1);
            y1 = std::min(y1, clampPixel(std::ceil(maxY), screenHeight) + 1);
        }

        if (x1 <= x0 || y1 <= y0) return false;

        out[0] = x0;
        out[1] = y0;
        out[2] = x1 - x0;
        out[3] = y1 - y0;
        return true;
    }
}

void OpenGL::SetGatewayClip(const GatewayView& view) const {
    if (view.clipped) {
        glEnable(GL_CLIP_DISTANCE0);
        glUniform4f(clipPlaneUniform, view.clipPlane.x, view.clipPlane.y, view.clipPlane.z, view.clipPlane.w);
    }
    else {
        glDisable(GL_CLIP_DISTANCE0);
        glUniform4f(clipPlaneUniform, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(view.scissor[0], view.scissor[1], view.scissor[2], view.scissor[3]);
}

void OpenGL::DrawGatewayBox(const Gateways::Opening& opening, const float depthBehind, const bool farDepth, const bool whole) const {
    using namespace OpenGLRendererInternal;

    const std::array<Vector3, 4> front = OpeningCorners(opening);
    const Vector3 back = {-opening.inward.x * depthBehind, 0.0f, -opening.inward.y * depthBehind};

    std::array<float, 8 * 4> corners{};
    for (int i = 0; i < 4; ++i) {
        const Vector3& corner = front[i];
        const Vector3 behind = corner + back;

        corners[i * 4 + 0] = corner.x;
        corners[i * 4 + 1] = corner.y;
        corners[i * 4 + 2] = corner.z;
        corners[i * 4 + 3] = 1.0f;

        corners[(i + 4) * 4 + 0] = behind.x;
        corners[(i + 4) * 4 + 1] = behind.y;
        corners[(i + 4) * 4 + 2] = behind.z;
        corners[(i + 4) * 4 + 3] = 1.0f;
    }

    glUniform4fv(gatewayCornersUniform, 8, corners.data());
    glUniform1i(gatewayFarDepthUniform, farDepth ? 1 : 0);
    glUniform1i(renderModeUniform, RENDER_GATEWAY);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // The opening's own wall is drawn whole: the box is pulled a little
    // toward the camera so it covers it there (reverse Z: nearer is greater).
    if (!farDepth) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 4.0f);
    }
    glDrawArrays(GL_TRIANGLES, 0, whole ? 36 : 6); // 6: the front face alone
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void OpenGL::DrawGatewayViews(const Level& level, const GatewayView& view, const int stencilRef, int& passesLeft) {
    ZoneScopedN("Gateway Views");

    const int maxDepth = std::clamp(level.rendererSettings.gatewayDepth, 0, MAX_GATEWAY_DEPTH);
    const Matrix4 viewProjection = view.camera.projection * view.camera.view;
    const Vector2 eye = {view.eye.x, view.eye.z};

    // Picked before any is drawn: drawing a view through one replaces the
    // visibility this view was culled with.
    struct Seen {
        const Gateways::Opening* opening;
        int scissor[4];
    };
    std::vector<Seen> seen;

    for (const Gateways::Opening& opening : gatewayOpenings) {
        // Looked out of, so it is behind this view.
        if (opening.wall == view.exitWall) continue;
        if (IsSectorCulled(opening.sectorIndex)) continue;

        // Only its room's side shows the view; behind it is the void.
        if (opening.SideDistance(eye) <= 0.0f) continue;

        if (view.clipped) {
            bool inFront = false;
            for (const Vector3& corner : OpeningCorners(opening))
                inFront = inFront || corner.x * view.clipPlane.x + corner.y * view.clipPlane.y +
                                     corner.z * view.clipPlane.z + view.clipPlane.w > 0.0f;
            if (!inFront) continue;
        }

        Seen entry{&opening, {}};
        if (!OpeningScreenRect(opening, viewProjection, screenWidth, screenHeight, view.scissor, entry.scissor)) continue;

        seen.push_back(entry);
    }

    const float boxDepth = BoxDepth(view.camera);

    for (const Seen& entry : seen) {
        const Gateways::Opening& opening = *entry.opening;
        const bool wholeBox = NeedsWholeBox(opening, eye, boxDepth);

        // This view's own state: the last opening drew another view.
        projectionShader->use();
        glBindVertexArray(VAO);
        SetSceneCamera(view.camera, view.eye);
        SetGatewayClip(view);

        // Without a stencil buffer a view can't be kept inside its opening.
        const bool drawView = hasStencil && view.depth < maxDepth && passesLeft > 0 && stencilRef < 254;

        if (!drawView) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glStencilFunc(GL_EQUAL, stencilRef, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GEQUAL);
            DrawGatewayBox(opening, boxDepth, false, wholeBox);
            continue;
        }

        --passesLeft;

        // 1. Where the opening shows.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_GEQUAL);
        glStencilFunc(GL_EQUAL, stencilRef, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        DrawGatewayBox(opening, boxDepth, false, wholeBox);

        // 2. Nothing in the way there yet.
        glStencilFunc(GL_EQUAL, stencilRef + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_ALWAYS);
        DrawGatewayBox(opening, boxDepth, true, wholeBox);

        // 3. The view through it.
        GatewayView through;
        through.camera = view.camera;
        through.camera.yaw = opening.MapYaw(view.camera.yaw);
        through.camera.forward = opening.MapDirection(view.camera.forward);
        through.eye = opening.MapPoint(view.eye);

        // This view's matrix composed with the gateway's, not rebuilt from
        // the moved eye and yaw: that differs by a few thousandths, enough
        // for the opening's edges and the view's to miss by a pixel.
        through.camera.view = view.camera.view * ExitToEntry(opening);

        through.exitWall = opening.exit;
        through.clipPlane = {
            opening.exitInward.x,
            0.0f,
            opening.exitInward.y,
            -(opening.exitInward.x * opening.exitCentre.x + opening.exitInward.y * opening.exitCentre.y) + CLIP_EPSILON
        };
        through.clipped = true;
        through.depth = view.depth + 1;
        std::copy(std::begin(entry.scissor), std::end(entry.scissor), through.scissor);

        if (visibilityCulling)
            UpdateVisibleSectorsFrom(level, opening.exitSectorIndex, through.camera.projection * through.camera.view);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_EQUAL, stencilRef + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        // The sky shader writes no clip distance.
        glDisable(GL_CLIP_DISTANCE0);
        glEnable(GL_SCISSOR_TEST);
        glScissor(through.scissor[0], through.scissor[1], through.scissor[2], through.scissor[3]);
        DrawBackground(through.camera, level.sky);

        SetupSceneShader(level, through.camera, through.eye);
        SetGatewayClip(through);
        DrawSceneGeometry();

        DrawGatewayViews(level, through, stencilRef + 1, passesLeft);

        // 4. Back to this view: the opening's depth, then its stencil.
        projectionShader->use();
        glBindVertexArray(VAO);
        SetSceneCamera(view.camera, view.eye);
        SetGatewayClip(view);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_EQUAL, stencilRef + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_ALWAYS);
        DrawGatewayBox(opening, boxDepth, true, wholeBox);

        glDepthFunc(GL_GREATER);
        DrawGatewayBox(opening, boxDepth, false, wholeBox);

        glDepthMask(GL_FALSE);
        glDepthFunc(GL_ALWAYS);
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
        DrawGatewayBox(opening, boxDepth, false, wholeBox);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilFunc(GL_EQUAL, stencilRef, 0xFF);
    }
}
