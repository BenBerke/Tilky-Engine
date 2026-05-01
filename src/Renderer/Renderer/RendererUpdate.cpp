#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "../../Headers/Renderer/Renderer/Renderer.hpp"
#include "RendererInternal.hpp"

#include "Headers/Engine/InputManager.hpp"

#include "Headers/Objects/Player.hpp"
#include "Headers/Objects/Wall.hpp"

#include "Headers/Renderer/MapEditor.hpp"
#include "Headers/Renderer/TextureManager.hpp"

namespace Renderer {

    void Update(const Vector2& playerPos, const float angle) {
        using namespace RendererInternal;

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glDisable(GL_CULL_FACE);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

       // ImGui_ImplOpenGL3_NewFrame();
       // ImGui_ImplSDL3_NewFrame();
        //ImGui::NewFrame();

        DrawBackground(angle);

        projectionShader->use();
        glBindVertexArray(VAO);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glUniform2f(playerPosUniform, playerPos.x, playerPos.y);
        glUniform1f(playerAngleUniform, angle);
        glUniform1f(playerHeightUniform, Player::currentEyeHeight);
        glUniform1f(playerCamZUniform, Player::camZ);

        BuildVisibleFlatTriangles(playerPos, angle);
        BuildGpuSectors();
        UploadGpuWallsFromMap();

        TextureManager::BindAllTextures(0);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
        glDepthFunc(GL_LESS);
        glUniform1i(renderModeUniform, RENDER_FLAT);

        glDrawArraysInstanced(
            GL_TRIANGLES,
            0,
            3,
            flatTriangleCount
        );

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
        glDepthFunc(GL_LEQUAL);
        glUniform1i(renderModeUniform, RENDER_WALL);

        glDrawArraysInstanced(
            GL_TRIANGLE_STRIP,
            0,
            4,
            gpuWallCount
        );

        BuildGpuSprites();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);
        glDepthFunc(GL_LEQUAL);
        glUniform1i(renderModeUniform, RENDER_SPRITE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDepthMask(GL_TRUE);

        glDrawArraysInstanced(
            GL_TRIANGLE_STRIP,
            0,
            4,
            spriteCount
        );

        BuildGpuDecals();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, decalSSBO);
        glDepthFunc(GL_LEQUAL);
        glUniform1i(renderModeUniform, RENDER_DECAL);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);

        glDrawArraysInstanced(
            GL_TRIANGLE_STRIP,
            0,
            4,
            decalCount
        );

        if (InputManager::GetKey(SDL_SCANCODE_TAB)) {
            glDisable(GL_DEPTH_TEST);

            DrawDebugRect({0.0f, 0.0f}, DEBUG_PLAYER_HALF_SIZE, DEBUG_PLAYER_HALF_SIZE);

            for (const Wall& wall : MapEditor::walls) {
                const Vector2 start = WorldToDebugNdc(wall.start, playerPos);
                const Vector2 end = WorldToDebugNdc(wall.end, playerPos);

                DrawDebugLine(start, end);
            }

            const float halfFovRad = DegToRad(DEBUG_FOV_DEG * 0.5f);
            const float angleRad = DegToRad(angle);

            constexpr Vector2 baseForward = {0.0f, DEBUG_FOV_LINE_LENGTH};

            Vector2 leftFov = RotatePoint(baseForward, angleRad - halfFovRad);
            Vector2 rightFov = RotatePoint(baseForward, angleRad + halfFovRad);

            leftFov.x *= -1.0f;
            rightFov.x *= -1.0f;

            DrawDebugLine({0.0f, 0.0f}, leftFov);
            DrawDebugLine({0.0f, 0.0f}, rightFov);
        }

        //ImGui::Begin("Game UI");

        // ImGui::Text("FPS: %.1f", GameTime::GetFPS());
        // ImGui::Text("Sector: %d", Player::currentSector);
        // ImGui::Text("Floor: %d", Player::currentFloor);
        // ImGui::Text("Eye Height: %.2f", Player::currentEyeHeight);
        // ImGui::Text("NoClip: %s", Player::noClip ? "true" : "false");

       // ImGui::End();

        //ImGui::Render();

        glDisable(GL_DEPTH_TEST);

        uiShader->use();
        glBindVertexArray(uiVAO);

        //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);

        constexpr float percentage = std::clamp(65.0f / 100.0f, 0.0f, 1.0f);

        // Background
        DrawUIRectangle(
            {20.0f, 20.0f},
            {300.0f, 30.0f},
            {40.0f, 40.0f, 40.0f, 220.0f}
        );

        // Health fill
        DrawUIRectangle(
            {23.0f, 23.0f},
            {294.0f * percentage, 24.0f},
            {220.0f, 30.0f, 30.0f, 255.0f}
        );

        //ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}