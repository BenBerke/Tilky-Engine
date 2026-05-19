//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGLRenderer.hpp"

#include <spdlog/spdlog.h>

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Player.hpp"
#include "Headers/Objects/Wall.hpp"

void OpenGLRenderer::RenderFrame() {
    using namespace OpenGLRendererInternal;

    Level& level = LevelManager::CurrentLevel();

    const Vector2 playerPlanarPos = {
        Player::position.x,
        Player::position.z
    };

    const float playerYaw = Player::angle;
    DrawBackground(playerYaw);

    projectionShader->use();
    glBindVertexArray(VAO);

    glUniformMatrix4fv(
        viewUniform,
        1,
        GL_TRUE,
        Player::view.Data()
    );

    glUniformMatrix4fv(
        projectionUniform,
        1,
        GL_TRUE,
        Player::projection.Data()
    );

    BindAllTextures(0);

    // ============================================================
    // Floors / ceilings
    // ============================================================

    BuildGpuSectors();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glUniform1i(renderModeUniform, RENDER_FLAT);

    glDrawArraysInstanced(
        GL_TRIANGLES,
        0,
        3,
        flatTriangleCount
    );

    // ============================================================
    // Walls
    // ============================================================

    UploadGpuWallsFromMap();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glUniform1i(renderModeUniform, RENDER_WALL);

    glDrawArraysInstanced(
        GL_TRIANGLES,
        0,
        6,
        gpuWallCount
    );

    // ============================================================
    // Sprites
    // ============================================================

    BuildGpuSprites();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDepthFunc(GL_LEQUAL);

    // For alpha-cutout sprites, writing depth is okay.
    // If semi-transparent sprites are added later, sort them and use GL_FALSE.
    glDepthMask(GL_TRUE);

    glUniform1i(renderModeUniform, RENDER_SPRITE);

    glDrawArraysInstanced(
        GL_TRIANGLE_STRIP,
        0,
        4,
        spriteCount
    );

    glDisable(GL_BLEND);

    // ============================================================
    // Decals
    // ============================================================

    BuildGpuDecals();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, decalSSBO);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDepthFunc(GL_LEQUAL);

    // Decals are surface details, so they should not write depth.
    glDepthMask(GL_FALSE);

    glUniform1i(renderModeUniform, RENDER_DECAL);

    glDrawArraysInstanced(
        GL_TRIANGLES,
        0,
        6,
        decalCount
    );

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // ============================================================
    // UI
    // ============================================================

    for (ComponentUISprite& sprite : level.ui_sprites.components) {
        const ComponentUITransform* transform = level.ui_transforms.Get(sprite.ownerID);

        if (transform == nullptr) {
            spdlog::error("UI Sprite does not have UI transform");
            continue;
        }

        DrawUIRectangle(
            transform->position,
            transform->scale,
            {255.0f, 255.0f, 255.0f, 255.0f},
            sprite.textureIndex
        );
    }
}