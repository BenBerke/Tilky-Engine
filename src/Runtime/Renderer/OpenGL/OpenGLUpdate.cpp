//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/UISystem.hpp"
#include "Headers/Engine/GameTime.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "tracy/Tracy.hpp"

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
    } else {
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

    if (screenHeight > 0) [[likely]]
            camera->aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    ComponentTransform renderCameraTransform = *cameraTransform;

    //todo TILKY_TODO stop using playercontroller and check for normal cameras

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

        const float actualTransformY =
                cameraTransform->position.y;

        if (!camera->hasPreviousTransformY) {
            camera->previousTransformY = actualTransformY;
            camera->hasPreviousTransformY = true;
        }

        const float transformDeltaY =
                actualTransformY - camera->previousTransformY;

        const ComponentCollider *collider =
                level.colliders.Get(camera->ownerID);

        const ComponentRigidbody *rigidbody =
                level.rigidbodies.Get(camera->ownerID);

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
            float previousVisualY =
                    camera->previousTransformY;

            // Preserve an unfinished smoothing animation.
            if (camera->isStepping) {
                previousVisualY =
                        camera->smoothStepStartY +
                        camera->stepOffsetY +
                        (
                            camera->previousTransformY -
                            camera->smoothStepTargetY
                        );
            }

            camera->smoothStepStartY = previousVisualY;
            camera->smoothStepTargetY = actualTransformY;
            camera->stepOffsetY = 0.0f;
            camera->isStepping = true;
        }

        float currentCameraY = actualTransformY;

        if (camera->smoothStep && camera->isStepping) {
            const float targetOffset =
                    camera->smoothStepTargetY -
                    camera->smoothStepStartY;

            const float interpolation =
                    1.0f -
                    std::exp(
                        -std::max(
                            camera->smoothingStrength,
                            0.01f
                        ) *
                        GameTime::deltaTime
                    );

            camera->stepOffsetY +=
                    (targetOffset - camera->stepOffsetY) *
                    interpolation;

            // Apply later jumping/falling directly.
            const float movementAfterStep =
                    actualTransformY -
                    camera->smoothStepTargetY;

            currentCameraY =
                    camera->smoothStepStartY +
                    camera->stepOffsetY +
                    movementAfterStep;

            if (std::abs(
                    targetOffset -
                    camera->stepOffsetY
                ) <= 0.001f) {
                currentCameraY = actualTransformY;

                camera->smoothStepStartY = actualTransformY;
                camera->smoothStepTargetY = actualTransformY;
                camera->stepOffsetY = 0.0f;
                camera->isStepping = false;
            }
        } else if (!camera->smoothStep) {
            camera->smoothStepStartY = actualTransformY;
            camera->smoothStepTargetY = actualTransformY;
            camera->stepOffsetY = 0.0f;
            camera->isStepping = false;
        }

        camera->previousTransformY = actualTransformY;

        renderCameraTransform.position.y =
                currentCameraY + eyeHeight;
    }


    CameraSystem::RebuildCameraMatrices(renderCameraTransform, *camera);

    {
        ZoneScopedN("Background");
        //todo make this a world setting
        //todo imrpove
        constexpr float DEFAULT_PARALLAX = .0f;
        constexpr float BACKGROUND_SCROLL = 1.0f;
        //const float yaw = camera->yaw;
        constexpr float yaw = .0f;
        DrawBackground(-camera->pitch, yaw, camera->fov, DEFAULT_PARALLAX, BACKGROUND_SCROLL);
    }

    {
        ZoneScopedN("Shader Setup");
        projectionShader->use();
        glBindVertexArray(VAO);

        glUniformMatrix4fv(viewUniform, 1, GL_TRUE, camera->view.Data());
        glUniformMatrix4fv(projectionUniform, 1, GL_TRUE, camera->projection.Data());

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);

        glUniform1i(glGetUniformLocation(projectionShader->ID, "uAtlas"), 0);

        glUniform1i(glGetUniformLocation(projectionShader->ID, "uTextureCount"), static_cast<int>(textureRegions.size()));

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);
    }

    {
        ZoneScopedN("Upload GPU Sectors");

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        BuildGpuSectors();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sectorFloorSSBO);

        glEnable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glUniform1i(renderModeUniform, RENDER_FLAT);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, flatTriangleCount);
    }

    {
        ZoneScopedN("Upload GPU Walls from Map");

        // Walls can not have backface culling as all walls are potentially visible from both sides
        // And they can change in runtime so we can not bake them
        glDisable(GL_CULL_FACE);
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
        ZoneScopedN("Build GPU Sprites");

        BuildGpuSprites();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_TRUE);

        glUniform1i(renderModeUniform, RENDER_SPRITE);
        glUniform3f(
            glGetUniformLocation(projectionShader->ID, "uCameraWorldPos"),
            renderCameraTransform.position.x,
            renderCameraTransform.position.y,
            renderCameraTransform.position.z
        );

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
            const ComponentUITransform* transform =
                level.ui_transforms.Get(text.ownerID);

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