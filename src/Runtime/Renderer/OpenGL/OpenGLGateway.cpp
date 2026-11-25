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

using namespace RendererGpu::GatewayMath;

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

    const std::array<float, 8 * 4> corners = BoxCorners(opening, depthBehind);

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
    const Vector2 eye = {view.eye.x, view.eye.z};

    // Picked before any is drawn: drawing a view through one replaces the
    // visibility this view was culled with.
    const std::vector<SeenGateway> seen = CollectSeenGateways(view);

    const float boxDepth = BoxDepth(view.camera);

    for (const SeenGateway& entry : seen) {
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
        const GatewayView through = ViewThrough(view, entry);

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
