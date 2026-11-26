//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#ifndef TILKY_STANDALONE
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#endif
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/UISystem.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "Headers/Engine/Local/Local.hpp"
#include "tracy/Tracy.hpp"

void OpenGL::UploadSSBO(const GLuint buffer, const GLuint binding, const void* data, const size_t bytes) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(bytes), bytes == 0 ? nullptr : data, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
}

// One instanced draw per run of visible sectors that sit next to each other
// in flatTriangles; everything in one draw when nothing is culled.
void OpenGL::DrawFlats() const {
    if (!visibilityCulling || flatSectorRanges.size() != sectorVisible.size()) {
        glUniform1i(flatInstanceOffsetUniform, 0);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, flatTriangleCount);
        return;
    }

    GLint runFirst = 0;
    GLsizei runCount = 0;

    const auto flush = [&] {
        if (runCount <= 0) return;

        glUniform1i(flatInstanceOffsetUniform, runFirst);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, runCount);
        runCount = 0;
    };

    for (size_t index = 0; index < flatSectorRanges.size(); ++index) {
        const FlatSectorRange& range = flatSectorRanges[index];

        if (!sectorVisible[index] || range.count <= 0) continue;

        if (runCount > 0 && runFirst + runCount == range.first) runCount += range.count;
        else {
            flush();
            runFirst = range.first;
            runCount = range.count;
        }
    }

    flush();
}

void OpenGL::SetSceneCamera(const ComponentCamera& camera, const Vector3& eye) const {
    glUniformMatrix4fv(viewUniform, 1, GL_TRUE, camera.view.Data());
    glUniformMatrix4fv(projectionUniform, 1, GL_TRUE, camera.projection.Data());

    // Distance lighting and which way sprites face are measured from here.
    glUniform3f(cameraWorldPosUniform, eye.x, eye.y, eye.z);
}

void OpenGL::SetupSceneShader(const Level& level, const ComponentCamera& camera, const Vector3& eye) {
    ZoneScopedN("Shader Setup");

    projectionShader->use();
    glBindVertexArray(VAO);

    SetSceneCamera(camera, eye);

    // Not a view through a gateway (that one sets its plane after this).
    glUniform4f(clipPlaneUniform, 0.0f, 0.0f, 0.0f, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTexture);

    glUniform1i(glGetUniformLocation(projectionShader->ID, "uAtlas"), 0);

    // uModelTexture (sampler2D) must never share uAtlas's unit (a
    // sampler2DArray): Mesa, unlike the Windows drivers, then refuses every
    // draw with GL_INVALID_OPERATION. DrawGpuModels sets it too, but only
    // when the level has models. 1 = OpenGLModel.cpp's MODEL_TEXTURE_UNIT.
    glUniform1i(modelTextureUniform, 1);

    glUniform1i(glGetUniformLocation(projectionShader->ID, "uTextureCount"), static_cast<int>(textureRegions.size()));
    glUniform1f(glGetUniformLocation(projectionShader->ID, "uTexelsPerUnit"), ProjectManager::GetTexelsPerUnit());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);

    glUniform1i(fullbrightUniform, fullbright ? 1 : 0);

    // The fade needs End above Start, whatever the sliders say mid-drag.
    const RendererSettings& renderer = level.rendererSettings;
    glUniform1f(distanceLightStartUniform, renderer.distanceLightStart);
    glUniform1f(distanceLightEndUniform, std::max(renderer.distanceLightEnd, renderer.distanceLightStart + 1.0f));
    glUniform1f(distanceLightDarkestUniform, renderer.distanceLightDarkest);

    glUniform2f(viewportSizeUniform, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
}

void OpenGL::DrawSceneGeometry() {
    using namespace OpenGLRendererInternal;

    {
        ZoneScopedN("Draw Flats");

        //glEnable(GL_CULL_FACE);
        //glCullFace(GL_BACK);
        //glFrontFace(GL_CCW);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sectorFloorSSBO);

        glEnable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glUniform1i(renderModeUniform, RENDER_FLAT);

        DrawFlats();
    }

    {
        ZoneScopedN("Upload GPU Walls from Map");

        // Walls can not have backface culling as all walls are potentially visible from both sides
        // And they can change in runtime so we can not bake them
        //glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);

        UploadGpuWallsFromMap();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
        glEnable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glUniform1i(renderModeUniform, RENDER_WALL);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, gpuWallCount);
        glDisable(GL_BLEND);
    }

    {
        ZoneScopedN("Build GPU Models");

        // Opaque geometry: drawn before sprites so their alpha blends over models.
        BuildGpuModels();

        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);

        DrawGpuModels();
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
    }

    {
        ZoneScopedN("Build GPU Sprites");

        BuildGpuSprites();
        UploadSSBO(spriteSSBO, 2, gpuSprites.data(), gpuSprites.size() * sizeof(GpuSprite));

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_TRUE);

        glUniform1i(renderModeUniform, RENDER_SPRITE);

        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, spriteCount);
        glDisable(GL_BLEND);
    }
}

void OpenGL::DrawNoCameraScreen() {
    if (!UpdateScreenSize()) return;

    glViewport(0, 0, screenWidth, screenHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // The default font, centred, at a size that follows the window's height.
    const std::string& text = Localisation::Get("screen.no_active_camera");
    const float pixelSize = std::max(16.0f, static_cast<float>(screenHeight) * 0.05f);
    const FontMetrics metrics = GetFontMetrics("", pixelSize);
    const float width = MeasureTextLine("", pixelSize, text);

    const Vector2 baseline = {
        (static_cast<float>(screenWidth) - width) * 0.5f,
        (static_cast<float>(screenHeight) - metrics.lineHeight) * 0.5f + metrics.ascender
    };

    DrawTextLine("", pixelSize, text, baseline, {1.0f, 1.0f, 1.0f, 1.0f});
}

void OpenGL::Update(const bool renderDebug, const bool renderUI) {
    using namespace OpenGLRendererInternal;

    // Textures last frame found missing from the atlas (exported games).
    ServiceLateTextures();

    Level& level = LevelManager::CurrentLevel();

    ComponentCamera* camera = nullptr;
    ComponentTransform* cameraTransform = nullptr;

    switch (ResolveFrameCamera(level, camera, cameraTransform)) {
        case FrameCamera::Failed: return;
        case FrameCamera::NoCamera:
            DrawNoCameraScreen();
            return;
        case FrameCamera::Ready: break;
    }

    if (!UpdateScreenSize()) return;

    glViewport(0, 0, screenWidth, screenHeight);

    if (screenHeight > 0) [[likely]] camera->aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    const ComponentTransform renderCameraTransform = ComputeRenderCameraTransform(level, *camera, *cameraTransform);

    CameraSystem::RebuildCameraMatrices(renderCameraTransform, *camera);

    // This frame's gateways.
    gatewayOpenings.clear();
    if (Gateways::AnyLinked(level)) Gateways::Collect(level, gatewayOpenings);

    {
        ZoneScopedN("Visible Sectors");

        visibilityCulling = false;
        if (!useEditorCamera) UpdateVisibleSectors(level, *camera, renderCameraTransform);
    }

    // Everything the camera sees directly is stencil 0; each view through a
    // gateway draws only where its opening marked the stencil.
    const bool drawGateways = !gatewayOpenings.empty();

    if (drawGateways) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_EQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    }

    {
        ZoneScopedN("Background");
        DrawBackground(*camera, level.sky);
    }

    {
        ZoneScopedN("Upload GPU Sectors");

        // Floor data is the same in every view, so it is uploaded once.
        BuildGpuSectors();
        UploadSSBO(sectorSSBO, 4, gpuSectors.data(), gpuSectors.size() * sizeof(GpuSector));
        UploadSSBO(sectorFloorSSBO, 7, gpuSectorFloors.data(), gpuSectorFloors.size() * sizeof(GpuSectorFloor));
        RefreshFlatTrianglesIfLayoutChanged();
    }

    SetupSceneShader(level, *camera, renderCameraTransform.position);
    DrawSceneGeometry();

    if (drawGateways) {
        ZoneScopedN("Gateways");

        // Put back after: the editor overlays below are of this view.
        const bool culling = visibilityCulling;
        const std::vector<uint8_t> visible = sectorVisible;
        const std::vector<uint8_t> showsEntities = sectorShowsEntities;

        GatewayView view;
        view.camera = *camera;
        view.eye = renderCameraTransform.position;
        view.scissor[2] = screenWidth;
        view.scissor[3] = screenHeight;

        int passesLeft = MAX_GATEWAY_VIEWS;
        DrawGatewayViews(level, view, 0, passesLeft);

        visibilityCulling = culling;
        sectorVisible = visible;
        sectorShowsEntities = showsEntities;

        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CLIP_DISTANCE0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        projectionShader->use();
        glBindVertexArray(VAO);
        SetSceneCamera(*camera, renderCameraTransform.position);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
    }

#ifndef TILKY_STANDALONE
    {
        ZoneScopedN("Build Colliders");

        // Turning entity markers off hides the collider outlines too.
        if (renderDebug && entityMarkers != EntityMarkers::Off) {
            BuildGpuColliders();
            UploadSSBO(colliderSSBO, 6, gpuColliders.data(), gpuColliders.size() * sizeof(GpuCollider));

            if (colliderCount > 0) {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, colliderSSBO);

                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);

                glUniform1i(renderModeUniform, RENDER_COLLIDER);
                glDrawArrays(GL_LINES, 0, colliderCount * COLLIDER_VERTICES_PER_COLLIDER);

                glUniform1i(renderModeUniform, RENDER_COLLIDER);

                glDrawArrays(GL_LINES, 0, colliderCount * COLLIDER_VERTICES_PER_COLLIDER);
            }
        }
    }

    if (entityMarkers != EntityMarkers::Off) {
        ZoneScopedN("Editor Gizmos");

        // Not for the camera being looked through: its gizmos sit at the eye.
        BuildGpuEditorGizmos(useEditorCamera ? INVALID_ENTITY_ID : camera->ownerID, renderCameraTransform, *camera);
        UploadSSBO(editorGizmoSSBO, 9, gpuEditorGizmoTriangles.data(), gpuEditorGizmoTriangles.size() * sizeof(GpuEditorGizmoVertex));

        if (editorGizmoVertexCount > 0) {
            // They write depth so an arrow's own faces sort. Always mode
            // shows them through walls by dropping the scene's depth first;
            // nothing after this pass reads it.
            if (entityMarkers == EntityMarkers::Always) {
                glDepthMask(GL_TRUE);
                glClear(GL_DEPTH_BUFFER_BIT);
            }

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_GEQUAL);

            glUniform1i(renderModeUniform, RENDER_EDITOR_GIZMO);
            glDrawArrays(GL_TRIANGLES, 0, editorGizmoVertexCount);
        }
    }

    if (entityMarkers != EntityMarkers::Off) {
        ZoneScopedN("Entity Markers");

        BuildGpuEntityMarkers();
        UploadSSBO(entityMarkerSSBO, 8, gpuEntityMarkers.data(), gpuEntityMarkers.size() * sizeof(Vector4));

        if (entityMarkerCount > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            if (entityMarkers == EntityMarkers::Always) glDisable(GL_DEPTH_TEST);
            else glDepthFunc(GL_GEQUAL);

            glUniform1i(renderModeUniform, RENDER_ENTITY_MARKER);
            glDrawArrays(GL_TRIANGLES, 0, entityMarkerCount * VERTICES_PER_ENTITY_MARKER);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    if (!renderUI) return;
#endif
    DrawLevelUI(level);
}

void OpenGL::DrawLevelUI(Level& level) {
    {
        ZoneScopedN("Rendering UI Sprites");

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        uiShader->use();

        if (uiShader == nullptr) {
            spdlog::critical("DrawUIRectangle failed: uiShader is null");
            return;
        }

        if (uiShader->ID == 0) {
            spdlog::critical("DrawUIRectangle failed: uiShader program ID is 0");
            return;
        }

        if (uiVAO == 0 || glIsVertexArray(uiVAO) == GL_FALSE) {
            spdlog::critical("DrawUIRectangle failed: invalid uiVAO {}", uiVAO);
            return;
        }

        UISystem::UpdateAllTransforms(level, screenWidth, screenHeight);

        // Every Sprite by Draw Order, then every Text. Text drawing switches
        // shaders, so the sprite state is set again after it.
        bool spriteStateSet = true;

        UISystem::ForEachInDrawOrder(level,
            [&](const ComponentUISprite& sprite) {
                // The owner's UI Button state look (white and empty without one).
                const Vector4 color = {
                    sprite.color.x * sprite.buttonTint.x, sprite.color.y * sprite.buttonTint.y,
                    sprite.color.z * sprite.buttonTint.z, sprite.color.w * sprite.buttonTint.w
                };

                if (!sprite.isActive || !sprite.ownerEnabled || color.w <= 0.0f) return;
                if (level.IsUIHidden(sprite.ownerID)) return;

                const std::string& texture = sprite.buttonTexture.empty() ? sprite.texture : sprite.buttonTexture;
                if (texture.empty() && !sprite.fillWhenEmpty) return;

                const ComponentUITransform* transform = level.ui_transforms.Get(sprite.ownerID);

                if (transform == nullptr) [[unlikely]] {
                    spdlog::error("UI Sprite does not have UI transform");
                    return;
                }

                if (!spriteStateSet) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    uiShader->use();
                    spriteStateSet = true;
                }

                DrawUIRectangle(
                    transform->resolvedPosition,
                    transform->resolvedSize,
                    {color.x * 255.0f, color.y * 255.0f, color.z * 255.0f, color.w * 255.0f},
                    transform->rotation,
                    texture
                );
            },
            [&](const ComponentUIText& text) {
                if (!text.ownerEnabled || level.IsUIHidden(text.ownerID)) return;

                const ComponentUITransform* transform = level.ui_transforms.Get(text.ownerID);

                if (transform == nullptr) [[unlikely]] {
                    spdlog::error("UI Text does not have UI transform");
                    return;
                }

                if (spriteStateSet) glDisable(GL_BLEND);
                spriteStateSet = false;

                RenderUIText(text, level.ShownUIText(text), *transform);
            });

        glDisable(GL_BLEND);
    }
}

void OpenGL::DrawLoadingScreen(Level& uiLevel) {
    if (!UpdateScreenSize()) return;

    glViewport(0, 0, screenWidth, screenHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    DrawLevelUI(uiLevel);
}


#ifndef TILKY_STANDALONE
void OpenGL::BeginImGuiFrame() const {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void OpenGL::EndImGuiFrame() const {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
#else
// The game draws no ImGui; these only fill IRenderer's vtable.
void OpenGL::BeginImGuiFrame() const {}
void OpenGL::EndImGuiFrame() const {}
#endif
