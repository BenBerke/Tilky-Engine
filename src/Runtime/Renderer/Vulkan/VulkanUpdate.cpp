#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdl3.h"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/UISystem.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/LevelSystem.hpp"
#include "tracy/Tracy.hpp"

void Vulkan::Update(const bool renderDebug, const bool renderUI) {
    using namespace VulkanRendererInternal;

    if (activeCommandBuffer == nullptr) {
        // BeginFrame() bailed this frame (minimized window / swapchain
        // being recreated) - nothing to record into.
        return;
    }

    vk::raii::CommandBuffer& cmd = *activeCommandBuffer;

    Level& level = LevelManager::CurrentLevel();

    ComponentCamera* camera = nullptr;
    ComponentTransform* cameraTransform = nullptr;

    if (useEditorCamera) {
        camera = GetEditorCamera();
        cameraTransform = GetEditorCameraTransform();

        if (camera == nullptr || cameraTransform == nullptr) [[unlikely]] {
            spdlog::error("Vulkan::Update failed: editor camera was not created");
            return;
        }
    }
    else {
        camera = LevelSystem::GetActiveCamera(level);

        if (camera == nullptr) [[unlikely]] {
            spdlog::error("Vulkan::Update failed: no active camera");
            return;
        }

        cameraTransform = level.transforms.Get(camera->ownerID);

        if (cameraTransform == nullptr) [[unlikely]] {
            spdlog::error(
                "Vulkan::Update failed: active camera entity {} does not have a transform",
                camera->ownerID
            );
            return;
        }
    }

    if (!SDL_GetWindowSizeInPixels(window, &screenWidth, &screenHeight)) {
        spdlog::error("SDL_GetWindowSizeInPixels failed: {}", SDL_GetError());
        return;
    }

    // GL called glViewport here every frame; the equivalent (setViewport/
    // setScissor against swapchainExtent) already happened once in
    // BeginFrame(), which runs before Update() and keeps swapchainExtent
    // in sync via RecreateSwapchain - nothing further to set here.

    if (screenHeight > 0) [[likely]]
        camera->aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

    ComponentTransform renderCameraTransform = *cameraTransform;

    //todo stop using playercontroller and check for normal cameras
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

        // Replaces GL's glUniformMatrix4fv(viewUniform/projectionUniform,
        // ...) calls, set once and reused across the flat/wall/sprite/
        // collider draws below - a per-frame UBO is the direct Vulkan
        // equivalent of that "set once, several draws read it" pattern.
        // renderMode does NOT need a field here (unlike the GL uniform of
        // the same purpose) - see the SceneUBO comment in Vulkan.hpp for
        // why: each render mode already has its own pipeline.
        SceneUBO sceneUbo{};

        // GL uploaded camera->view.Data()/camera->projection.Data() with
        // glUniformMatrix4fv(..., GL_TRUE, ...) - GL_TRUE transposes on
        // upload, implying Data() is row-major. Replicated here by
        // transposing into the UBO explicitly, since push/uniform buffers
        // have no equivalent upload-time transpose flag. WORTH VERIFYING
        // against your actual Matrix4 storage convention - this is the
        // single most correctness-critical assumption in this whole port
        // and I don't have Matrix4's source to confirm it directly.
        const auto transposeMatrix4 = [](const float* src, float* dst) {
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    dst[col * 4 + row] = src[row * 4 + col];
        };

        transposeMatrix4(camera->view.Data(), sceneUbo.view);
        transposeMatrix4(camera->projection.Data(), sceneUbo.projection);

        sceneUbo.cameraWorldPos[0] = renderCameraTransform.position.x;
        sceneUbo.cameraWorldPos[1] = renderCameraTransform.position.y;
        sceneUbo.cameraWorldPos[2] = renderCameraTransform.position.z;
        sceneUbo.textureCount = static_cast<float>(textureRegions.size());

        UploadToDynamicBuffer(
            sceneUboBuffers[currentFrame], &sceneUbo, sizeof(sceneUbo),
            vk::BufferUsageFlagBits::eUniformBuffer
        );

        const vk::DescriptorBufferInfo uboInfo{
            .buffer = sceneUboBuffers[currentFrame].buffer, .offset = 0, .range = vk::WholeSize
        };
        const vk::WriteDescriptorSet uboWrite{
            .dstSet = sceneSets[currentFrame], .dstBinding = 9, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &uboInfo
        };
        device.updateDescriptorSets(uboWrite, {});

        // atlas (binding 8) + region SSBO (binding 5) were already written
        // into sceneSets by BuildTextureAtlasFromLevel and don't need
        // touching again here every frame - only the region SSBO's
        // *contents* would ever need a rewrite, and only when the level's
        // texture list changes, not every frame.
    }

    {
        ZoneScopedN("Upload GPU Sectors");

        // GL: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW).
        // Baked into flatPipeline instead - nothing to set here.

        BuildGpuSectors(); // writes SSBO bindings 4 (sector) and 7 (sectorFloor)

        // Re-binding here (rather than once at the top of Update()) is
        // required, not just for tidiness: Vulkan doesn't allow updating a
        // descriptor set after it's bound and before the draw that uses it
        // actually executes, unless the set was created with update-after-
        // bind semantics (it wasn't, here - see InitDescriptors). Every
        // block below follows the same build/upload -> rebind -> draw
        // shape for the same reason.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, scenePipelineLayout, 0, *sceneSets[currentFrame], {});
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, flatPipeline);

        // Empty vertex input, gl_InstanceIndex drives which flat triangle
        // a given instance's 3 vertices belong to - same fullscreen-
        // triangle-from-nowhere trick as the background draw.
        cmd.draw(3, static_cast<uint32_t>(flatTriangleCount), 0, 0);
    }

    {
        ZoneScopedN("Upload GPU Walls from Map");

        // Walls can not have backface culling as all walls are potentially visible from both sides
        // And they can change in runtime so we can not bake them
        // (wallPipeline already has cullMode=None baked in for this reason)

        UploadGpuWallsFromMap(); // writes SSBO binding 0 (wall)

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, scenePipelineLayout, 0, *sceneSets[currentFrame], {});
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, wallPipeline);
        cmd.draw(6, static_cast<uint32_t>(gpuWallCount), 0, 0);
    }

    {
        ZoneScopedN("Build GPU Sprites");

        BuildGpuSprites(); // writes SSBO binding 2 (sprite)

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, scenePipelineLayout, 0, *sceneSets[currentFrame], {});
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, spritePipeline);

        // uCameraWorldPos was a separate uniform in GL, set right before
        // this draw; it's part of the SceneUBO now (set once above) since
        // it doesn't actually change between draws within a frame.
        cmd.draw(4, static_cast<uint32_t>(spriteCount), 0, 0);
    }

#ifndef TILKY_STANDALONE
    {
        ZoneScopedN("Build Colliders");

        if (renderDebug) {
            BuildGpuColliders(); // writes SSBO binding 6 (collider)

            if (colliderCount > 0) {
                constexpr uint32_t COLLIDER_VERTICES_PER_COLLIDER = 24 * 6;

                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, scenePipelineLayout, 0, *sceneSets[currentFrame], {});
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, colliderPipeline);

                // Non-instanced (unlike flat/wall/sprite above) - matches
                // the original exactly, including what looks like a
                // copy-pasted duplicate draw call. Kept as-is rather than
                // silently "fixed", since I can't tell from here whether
                // that was deliberate.
                cmd.draw(static_cast<uint32_t>(colliderCount) * COLLIDER_VERTICES_PER_COLLIDER, 1, 0, 0);
                cmd.draw(static_cast<uint32_t>(colliderCount) * COLLIDER_VERTICES_PER_COLLIDER, 1, 0, 0);
            }
        }
    }
    if (!renderUI) return;
#endif
    {
        ZoneScopedN("Rendering UI Sprites");

        // GL: glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); baked
        // into uiPipeline instead.

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

// BeginImGuiFrame/EndImGuiFrame are called externally, separately from
// BeginFrame/Update/EndFrame (matching the GL version's IRenderer
// contract) - but unlike GL, ImGui_ImplVulkan_RenderDrawData needs an
// *active* command buffer that's currently inside a
// vkCmdBeginRendering/vkCmdEndRendering pair. In practice that means
// EndImGuiFrame() needs to be called somewhere between this frame's
// BeginFrame() and EndFrame() - typically right after Update(), so ImGui
// draws as an overlay before the swapchain image is transitioned for
// present. GL had no such constraint since it has no concept of "a
// currently-open command buffer" at all.
void Vulkan::BeginImGuiFrame() const {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Vulkan::EndImGuiFrame() const {
    ImGui::Render();

    if (activeCommandBuffer == nullptr) {
        spdlog::error("Vulkan::EndImGuiFrame called outside BeginFrame/EndFrame - ImGui draw data was dropped this frame");
        return;
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(**activeCommandBuffer));
}