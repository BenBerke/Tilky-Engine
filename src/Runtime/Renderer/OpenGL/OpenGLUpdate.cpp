//
// Created by berke on 5/14/2026.
//

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <spdlog/spdlog.h>

#include "Headers/Engine/InputManager.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Player.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/UISystem.hpp"

void OpenGL::Update() {
    using namespace OpenGLRendererInternal;

    Level& level = LevelManager::CurrentLevel();

    const Vector2 playerPlanarPos = {
        Player::position.x,
        Player::position.z
    };

    const float playerYaw = Player::angle;
    DrawBackground(playerYaw);

    SDL_GetWindowSize(window, &screenWidth, &screenHeight);
    glViewport(0, 0, screenWidth, screenHeight);

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

    if (atlasTexture == 0) spdlog::error("atlasTexture is 0");
    if (textureRegionSSBO == 0) spdlog::error("textureRegionSSBO is 0");
    if (textureRegions.empty()) spdlog::warn("textureRegions is empty");


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