#include "Headers/Runtime/Renderer/Renderer/Renderer.hpp"
#include "RendererInternal.hpp"

#include "Headers/Engine/InputManager.hpp"

#include "Headers/Objects/Player.hpp"
#include "Headers/Objects/Wall.hpp"

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Runtime/Renderer/TextureManager.hpp"

namespace Renderer {

    void Update() {
        using namespace RendererInternal;

        Level& level = LevelManager::CurrentLevel();

        const Vector2 playerPlanarPos = {
            Player::position.x,
            Player::position.z
        };

        const float playerYaw = Player::angle;

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glDisable(GL_CULL_FACE);

        glDepthMask(GL_TRUE);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

        TextureManager::BindAllTextures(0);

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
        // If you later add semi-transparent sprites, change this to GL_FALSE
        // and sort sprites back-to-front.
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
        // Top-down debug map
        // ============================================================

        if (InputManager::GetKey(SDL_SCANCODE_TAB)) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);

            DrawDebugRect(
                {0.0f, 0.0f},
                DEBUG_PLAYER_HALF_SIZE,
                DEBUG_PLAYER_HALF_SIZE
            );

            for (const Wall& wall : level.walls) {
                const Vector2 start =
                    WorldToDebugNdc(wall.start, playerPlanarPos);

                const Vector2 end =
                    WorldToDebugNdc(wall.end, playerPlanarPos);

                DrawDebugLine(start, end);
            }

            const float halfFovRad =
                DegToRad(DEBUG_FOV_DEG * 0.5f);

            const float angleRad =
                DegToRad(playerYaw);

            constexpr Vector2 baseForward = {
                0.0f,
                DEBUG_FOV_LINE_LENGTH
            };

            Vector2 leftFov =
                RotatePoint(baseForward, angleRad - halfFovRad);

            Vector2 rightFov =
                RotatePoint(baseForward, angleRad + halfFovRad);

            leftFov.x *= -1.0f;
            rightFov.x *= -1.0f;

            DrawDebugLine({0.0f, 0.0f}, leftFov);
            DrawDebugLine({0.0f, 0.0f}, rightFov);

            glEnable(GL_DEPTH_TEST);
        }
    }
}