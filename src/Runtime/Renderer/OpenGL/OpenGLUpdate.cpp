//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/UISystem.hpp"
#include "Headers/Runtime/CameraSystem.hpp"

void OpenGL::Update() {
    using namespace OpenGLRendererInternal;

    Level& level = LevelManager::CurrentLevel();

    ComponentCamera* camera = level.GetActiveCamera();

    if (camera == nullptr) {
        spdlog::error("OpenGL::Update failed: no active camera");
        return;
    }

    ComponentTransform* cameraTransform =
        level.transforms.Get(camera->ownerID);

    if (cameraTransform == nullptr) {
        spdlog::error(
            "OpenGL::Update failed: active camera entity {} does not have a transform",
            camera->ownerID
        );
        return;
    }

    SDL_GetWindowSize(window, &screenWidth, &screenHeight);
    glViewport(0, 0, screenWidth, screenHeight);

    if (screenHeight > 0) {
        camera->aspectRatio =
            static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    }

    CameraSystem::RebuildCameraMatrices(*cameraTransform, *camera);

    const float cameraYaw = camera->yaw;

    DrawBackground(cameraYaw);

    projectionShader->use();
    glBindVertexArray(VAO);

    glUniformMatrix4fv(viewUniform, 1, GL_TRUE, camera->view.Data());
    glUniformMatrix4fv(projectionUniform, 1, GL_TRUE, camera->projection.Data());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);

    glUniform1i(
        glGetUniformLocation(projectionShader->ID, "uAtlas"),
        0
    );

    glUniform1i(
        glGetUniformLocation(projectionShader->ID, "uTextureCount"),
        static_cast<int>(textureRegions.size())
    );

    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER,
        5,
        textureRegionSSBO
    );

    BuildGpuSectors();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glUniform1i(renderModeUniform, RENDER_FLAT);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, flatTriangleCount);

    UploadGpuWallsFromMap();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glUniform1i(renderModeUniform, RENDER_WALL);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, gpuWallCount);

    BuildGpuSprites();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glUniform1i(renderModeUniform, RENDER_SPRITE);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, spriteCount);
    glDisable(GL_BLEND);

    BuildGpuDecals();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, decalSSBO);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUniform1i(renderModeUniform, RENDER_DECAL);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, decalCount);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    UISystem::UpdateAllTransforms(level, screenWidth, screenHeight);

    for (ComponentUISprite& sprite : level.ui_sprites.components) {
        ComponentUITransform* transform = level.ui_transforms.Get(sprite.ownerID);

        if (transform == nullptr) {
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

    for (ComponentUIText& text : level.ui_texts.components) {
        const ComponentUITransform* transform =
            level.ui_transforms.Get(text.ownerID);

        if (transform == nullptr) {
            spdlog::error("UI Text does not have UI transform");
            continue;
        }

        RenderUIText(text, *transform);
    }
}