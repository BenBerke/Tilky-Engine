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

    if (screenHeight > 0) [[likely]]
        camera->aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    ComponentTransform renderCameraTransform = *cameraTransform;

    if (!useEditorCamera) {
        for (const ComponentPlayerController& controller : level.playerControllers.components) {
            if (!controller.isActive) continue;

            if (controller.ownerID == camera->ownerID) {
                renderCameraTransform.position.y =
                    cameraTransform->position.y + controller.eyeHeight;
                break;
            }
        }
    }

    CameraSystem::RebuildCameraMatrices(renderCameraTransform, *camera);

    {
        const float cameraYaw = camera->yaw;
        ZoneScopedN("Background");

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        DrawBackground(cameraYaw);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
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

        glUniform1i(glGetUniformLocation(projectionShader->ID, "uTextureCount"),
            static_cast<int>(textureRegions.size()));

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, textureRegionSSBO);
    }

    {
        ZoneScopedN("Upload GPU Sectors");

        BuildGpuSectors();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sectorFloorSSBO);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glUniform1i(renderModeUniform, RENDER_FLAT);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, flatTriangleCount);
    }

    {
        ZoneScopedN("Upload GPU Walls from Map");
        UploadGpuWallsFromMap();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glUniform1i(renderModeUniform, RENDER_WALL);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, gpuWallCount);
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

    {
        ZoneScopedN("Build GPU Decals");
        BuildGpuDecals();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, decalSSBO);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_FALSE);
        glUniform1i(renderModeUniform, RENDER_DECAL);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, decalCount);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

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

    {
        ZoneScopedN("Rendering UI Sprites");
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
                sprite.textureIndex
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