//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/UISystem.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "tracy/Tracy.hpp"

void OpenGL::UpdateVisibleSectors(
    const Level& level,
    const ComponentCamera& camera,
    const ComponentTransform& renderCameraTransform
) {
    const size_t sectorCount = level.sectors.size();

    // The camera entity's own sector as the hint - it's almost always right.
    const int cameraSector = MapQueries::FindSectorContainingPoint(
        level.sectors,
        {renderCameraTransform.position.x, renderCameraTransform.position.z},
        renderCameraTransform.sectorIndex
    );

    MapQueries::GetVisibleSectors(
        level,
        cameraSector,
        camera.projection * camera.view,
        static_cast<float>(screenWidth),
        static_cast<float>(screenHeight),
        visibleSectors
    );

    sectorVisible.assign(sectorCount, 0);
    sectorShowsEntities.assign(sectorCount, 0);

    for (const Sector* sector : visibleSectors) {
        const size_t index = static_cast<size_t>(sector - level.sectors.data());

        sectorVisible[index] = 1;
        sectorShowsEntities[index] = 1;

        for (const Sector* neighbour : sector->neighbors)
            if (neighbour != nullptr) sectorShowsEntities[static_cast<size_t>(neighbour - level.sectors.data())] = 1;
    }

    visibilityCulling = true;
}

// Indices the culling data doesn't cover (outside every sector, or a level
// that changed size since the last frame) are drawn, never hidden.
bool OpenGL::IsSectorCulled(const int sectorIndex) const {
    return visibilityCulling &&
           sectorIndex >= 0 &&
           sectorIndex < static_cast<int>(sectorVisible.size()) &&
           !sectorVisible[sectorIndex];
}

// A wall is seen from one of its sectors, so it is hidden only when every
// sector it has is. A wall with no sector at all is always drawn.
bool OpenGL::IsWallCulled(const Level& level, const Wall& wall) const {
    if (!visibilityCulling) return false;

    bool hasSector = false;

    for (const ID sectorID : {wall.frontSector, wall.backSector}) {
        const Sector* sector = MapQueries::GetSectorByID(level, sectorID);
        if (sector == nullptr) continue;

        hasSector = true;
        if (!IsSectorCulled(static_cast<int>(sector - level.sectors.data()))) return false;
    }

    return hasSector;
}

bool OpenGL::IsEntityCulled(const ComponentTransform& transform) const {
    return visibilityCulling &&
           transform.sectorIndex >= 0 &&
           transform.sectorIndex < static_cast<int>(sectorShowsEntities.size()) &&
           !sectorShowsEntities[transform.sectorIndex];
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

void OpenGL::Update(const bool renderDebug, const bool renderUI) {
    using namespace OpenGLRendererInternal;

    Level& level = LevelManager::CurrentLevel();

    ComponentCamera* camera = nullptr;
    ComponentTransform* cameraTransform = nullptr;

    if (useEditorCamera) {
        camera = GetEditorCamera();
        cameraTransform = GetEditorCameraTransform();

        if (camera == nullptr || cameraTransform == nullptr) [[unlikely]] {
            spdlog::error("OpenGL::Update failed: editor camera was not created");
            return;
        }
    }
    else {
        camera = LevelSystem::GetActiveCamera(level);

        if (camera == nullptr) [[unlikely]] {
            spdlog::error("OpenGL::Update failed: no active camera");
            return;
        }

        cameraTransform = level.transforms.Get(camera->ownerID);

        if (cameraTransform == nullptr) [[unlikely]] {
            spdlog::error(
                "OpenGL::Update failed: active camera entity {} does not have a transform",
                camera->ownerID
            );
            return;
        }
    }

    if (!SDL_GetWindowSizeInPixels(window, &screenWidth, &screenHeight)) {
        spdlog::error("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError());
        return;
    }

    glViewport(0, 0, screenWidth, screenHeight);

    if (screenHeight > 0) [[likely]] camera->aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    ComponentTransform renderCameraTransform = *cameraTransform;

    if (!useEditorCamera) {
        float eyeHeight = 0.0f;

        for (const ComponentPlayerController &controller:
             level.playerControllers.components) {
            if (!controller.isActive) continue;

            if (controller.ownerID == camera->ownerID) {
                eyeHeight = controller.eyeHeight;
                break;
            }
        }

        const float actualTransformY = cameraTransform->position.y;

        if (!camera->hasPreviousTransformY) {
            camera->previousTransformY = actualTransformY;
            camera->hasPreviousTransformY = true;
        }

        const float transformDeltaY = actualTransformY - camera->previousTransformY;

        const ComponentCollider *collider = level.colliders.Get(camera->ownerID);

        const ComponentRigidbody *rigidbody = level.rigidbodies.Get(camera->ownerID);

        // Detect the upward teleport performed by PhysicsSystem.
        const bool steppedUp =
                camera->smoothStep &&
                collider != nullptr &&
                rigidbody != nullptr &&
                rigidbody->isGrounded &&
                collider->stepSize > 0.0f &&
                transformDeltaY > 0.01f &&
                transformDeltaY <=
                collider->stepSize + Constants::Epsilon;

        if (steppedUp) {
            float previousVisualY = camera->previousTransformY;

            // Preserve an unfinished smoothing animation.
            if (camera->isStepping)
                previousVisualY = camera->smoothStepStartY + camera->stepOffsetY +
                    (camera->previousTransformY - camera->smoothStepTargetY);


            camera->smoothStepStartY = previousVisualY;
            camera->smoothStepTargetY = actualTransformY;
            camera->stepOffsetY = 0.0f;
            camera->isStepping = true;
        }

        float currentCameraY = actualTransformY;

        if (camera->smoothStep && camera->isStepping) {
            const float targetOffset = camera->smoothStepTargetY - camera->smoothStepStartY;

            const float interpolation = 1.0f - std::exp(-std::max(camera->smoothingStrength,0.01f) *GameTime::deltaTime);

            camera->stepOffsetY += (targetOffset - camera->stepOffsetY) * interpolation;

            // Apply later jumping/falling directly.
            const float movementAfterStep = actualTransformY - camera->smoothStepTargetY;

            currentCameraY = camera->smoothStepStartY + camera->stepOffsetY + movementAfterStep;

            if (std::abs(targetOffset - camera->stepOffsetY) <= 0.001f) {
                currentCameraY = actualTransformY;

                camera->smoothStepStartY = actualTransformY;
                camera->smoothStepTargetY = actualTransformY;
                camera->stepOffsetY = 0.0f;
                camera->isStepping = false;
            }
        }
        else if (!camera->smoothStep) {
            camera->smoothStepStartY = actualTransformY;
            camera->smoothStepTargetY = actualTransformY;
            camera->stepOffsetY = 0.0f;
            camera->isStepping = false;
        }

        camera->previousTransformY = actualTransformY;

        renderCameraTransform.position.y = currentCameraY + eyeHeight;
    }


    CameraSystem::RebuildCameraMatrices(renderCameraTransform, *camera);

    {
        ZoneScopedN("Visible Sectors");

        visibilityCulling = false;
        if (!useEditorCamera) UpdateVisibleSectors(level, *camera, renderCameraTransform);
    }

    {
        ZoneScopedN("Background");
        DrawBackground(*camera, level.sky);
    }

    {
        ZoneScopedN("Shader Setup");
        projectionShader->use();
        glBindVertexArray(VAO);

        glUniformMatrix4fv(viewUniform, 1, GL_TRUE, camera->view.Data());
        glUniformMatrix4fv(projectionUniform, 1, GL_TRUE, camera->projection.Data());

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTexture);

        glUniform1i(glGetUniformLocation(projectionShader->ID, "uAtlas"), 0);

        glUniform1i(glGetUniformLocation(projectionShader->ID, "uTextureCount"), static_cast<int>(textureRegions.size()));

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);

        // Every mode's distance lighting reads this, so set it before the first draw.
        glUniform3f(
            cameraWorldPosUniform,
            renderCameraTransform.position.x,
            renderCameraTransform.position.y,
            renderCameraTransform.position.z
        );
    }

    {
        ZoneScopedN("Upload GPU Sectors");

        //glEnable(GL_CULL_FACE);
        //glCullFace(GL_BACK);
        //glFrontFace(GL_CCW);

        BuildGpuSectors();
        RefreshFlatTrianglesIfLayoutChanged();

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

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_TRUE);

        glUniform1i(renderModeUniform, RENDER_SPRITE);

        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, spriteCount);
        glDisable(GL_BLEND);
    }

#ifndef TILKY_STANDALONE
    {
        ZoneScopedN("Build Colliders");

        if (renderDebug) {
            BuildGpuColliders();

            if (colliderCount > 0) {
                constexpr GLsizei COLLIDER_VERTICES_PER_COLLIDER = 24 * 6;

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
    if (!renderUI) return;
#endif
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

        for (ComponentUISprite& sprite : level.ui_sprites.components) {
            if (!sprite.isActive) continue;

            const ComponentUITransform* transform = level.ui_transforms.Get(sprite.ownerID);

            if (transform == nullptr) [[unlikely]] {
                spdlog::error("UI Sprite does not have UI transform");
                continue;
            }

            DrawUIRectangle(
                transform->resolvedPosition,
                transform->resolvedSize,
                {255.0f, 255.0f, 255.0f, 255.0f},
                transform->rotation,
                sprite.texture
            );
        }
    }

    {
        ZoneScopedN("Rendering UI Text");
        for (ComponentUIText& text : level.ui_texts.components) {
            const ComponentUITransform* transform = level.ui_transforms.Get(text.ownerID);

            if (transform == nullptr) [[unlikely]] {
                spdlog::error("UI Text does not have UI transform");
                continue;
            }

            RenderUIText(text, *transform);
        }
    }
}

void OpenGL::BeginImGuiFrame() const {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void OpenGL::EndImGuiFrame() const {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}