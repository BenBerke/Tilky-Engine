// The 3D view, as the OpenGL renderer draws it (OpenGLUpdate.cpp,
// OpenGLGateway.cpp, OpenGLBackground.cpp, OpenGLUI.cpp, OpenGLMapBuild.cpp)
// line for line, through Vulkan.hpp's GL-like state.
//
// Compiled per executable (TILKY_VARIANT_SOURCES): the editor overlays are
// left out of the game.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

#include "Headers/Engine/Local/Local.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Gameplay/CameraSystem.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"
#include "Headers/UISystem.hpp"
#include "tracy/Tracy.hpp"

using namespace VulkanInternal;
using namespace RendererGpu;
using namespace RendererGpu::GatewayMath;

namespace {
    // Matches SKY_NONE in Background.fs.glsl.
    constexpr int SKY_NONE = -1;

    constexpr std::array<const char*, SKY_FACE_COUNT> FACE_UNIFORMS = {
        "faceRight", "faceLeft", "faceUp", "faceDown", "faceFront", "faceBack"
    };

    constexpr int BINDING_WALLS = 0;
    constexpr int BINDING_FLATS = 1;
    constexpr int BINDING_SPRITES = 2;
    constexpr int BINDING_MODELS = 3;
    constexpr int BINDING_SECTORS = 4;
    constexpr int BINDING_TEXTURE_REGIONS = 5;
    constexpr int BINDING_COLLIDERS = 6;
    constexpr int BINDING_SECTOR_FLOORS = 7;
    constexpr int BINDING_ENTITY_MARKERS = 8;
    constexpr int BINDING_EDITOR_GIZMOS = 9;
}

// ============================================================================
// Programs and the map
// ============================================================================

bool Vulkan::InitScenePrograms() {
    namespace fs = std::filesystem;
    const auto shader = [](const char* folder, const char* file) {
        return ProjectManager::FindAssetPath(fs::path("Shaders") / folder / file);
    };

    using ModelLoader::ModelVertex;
    projectionProgram = LoadProgram("Rendering", shader("Rendering", "Rendering.vs.glsl"), shader("Rendering", "Rendering.fs.glsl"), nullptr,
                                    {{0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(ModelVertex, position))},
                                     {1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(ModelVertex, normal))},
                                     {2, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(ModelVertex, uv))}});
    backgroundProgram = LoadProgram("Background", shader("Background", "Background.vs.glsl"), shader("Background", "Background.fs.glsl"),
                                    nullptr, {});
    uiProgram = LoadProgram("UI", shader("UI", "UI.vs.glsl"), shader("UI", "UI.fs.glsl"), nullptr,
                            {{0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, VK_FORMAT_R32G32_SFLOAT, 8}});
    textProgram = LoadProgram("Glyph", shader("Glyph", "glyph.vs.glsl"), shader("Glyph", "glyph.fs.glsl"), nullptr,
                              {{0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}});

    if (projectionProgram == nullptr || backgroundProgram == nullptr || uiProgram == nullptr || textProgram == nullptr) {
        spdlog::critical("Vulkan: the renderer's shaders could not be made");
        return false;
    }

    // The UI quad, as OpenGLInit.cpp's InitUI.
    constexpr float vertices[] = {
         0.5f,  0.5f, 1.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 0.0f
    };
    constexpr uint32_t indices[] = {0, 1, 3, 1, 2, 3};

    if (!CreateBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, uiQuadVertices) ||
        !CreateBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true, uiQuadIndices))
        return false;
    std::memcpy(uiQuadVertices.mapped, vertices, sizeof(vertices));
    std::memcpy(uiQuadIndices.mapped, indices, sizeof(indices));

    return true;
}

void Vulkan::UploadFlatTriangles() {
    DeferDestroyBuffer(flatBuffer);

    const size_t bytes = flatTriangles.size() * sizeof(GpuFlatTriangle);
    if (bytes == 0) return;

    if (CreateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true, flatBuffer))
        std::memcpy(flatBuffer.mapped, flatTriangles.data(), bytes);
}

void Vulkan::RefreshFlatTrianglesIfLayoutChanged() {
    if (!mapCreated || !FlatLayoutChanged()) return;

    BuildFlatTrianglesFromSectors();
    UploadFlatTriangles();
}

void Vulkan::UploadGpuWalls() {
    BuildGpuWallsFromMap();
    BindStorage(BINDING_WALLS, UploadFrame(gpuWalls.data(), gpuWalls.size() * sizeof(GpuWall)));
}

bool Vulkan::CreateMap() {
    spdlog::info("Creating Vulkan renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    BuildFlatTrianglesFromSectors();
    BuildGpuWallsFromMap();
    UploadFlatTriangles();
    mapCreated = true;

    spdlog::info("Built Vulkan map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());
    return true;
}

void Vulkan::ReloadMap() {
    spdlog::info("Reloading Vulkan renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    // Walls store atlas region indices, so the atlas comes first.
    RefreshTexturesFromLevel();
    BuildGpuWallsFromMap();

    BuildFlatTrianglesFromSectors();
    UploadFlatTriangles();

    spdlog::info("Reloaded Vulkan map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());
}

// ============================================================================
// The scene
// ============================================================================

void Vulkan::DrawFlats() {
    const auto draw = [this](const int first, const int count) {
        SetInt("uFlatInstanceOffset", first);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 3, static_cast<uint32_t>(count));
    };

    if (!visibilityCulling || flatSectorRanges.size() != sectorVisible.size()) {
        draw(0, flatTriangleCount);
        return;
    }

    int runFirst = 0;
    int runCount = 0;

    const auto flush = [&] {
        if (runCount <= 0) return;
        draw(runFirst, runCount);
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

void Vulkan::SetSceneCamera(const ComponentCamera& camera, const Vector3& eye) {
    SetMat4RowMajor("uView", camera.view.Data());
    SetMat4RowMajor("uProjection", camera.projection.Data());

    // Distance lighting and which way sprites face are measured from here.
    SetVec3("uCameraWorldPos", eye.x, eye.y, eye.z);
}

void Vulkan::SetupSceneShader(const Level& level, const ComponentCamera& camera, const Vector3& eye) {
    ZoneScopedN("Shader Setup");

    UseProgram(projectionProgram.get());

    SetSceneCamera(camera, eye);

    // Not a view through a gateway (that one sets its plane after this).
    SetVec4("uClipPlane", 0.0f, 0.0f, 0.0f, 1.0f);

    SetTexture("uAtlas", atlas.image != VK_NULL_HANDLE ? &atlas : nullptr, AtlasSampler());
    SetInt("uTextureCount", static_cast<int>(textureRegions.size()));
    SetFloat("uTexelsPerUnit", ProjectManager::GetTexelsPerUnit());

    BindStorage(BINDING_TEXTURE_REGIONS, textureRegionBuffer.address);

    SetInt("uFullbright", fullbright ? 1 : 0);

    // The fade needs End above Start, whatever the sliders say mid-drag.
    const RendererSettings& renderer = level.rendererSettings;
    SetFloat("uDistanceLightStart", renderer.distanceLightStart);
    SetFloat("uDistanceLightEnd", std::max(renderer.distanceLightEnd, renderer.distanceLightStart + 1.0f));
    SetFloat("uDistanceLightDarkest", renderer.distanceLightDarkest);

    SetVec2("uViewportSize", static_cast<float>(screenWidth), static_cast<float>(screenHeight));
}

void Vulkan::DrawSceneGeometry() {
    {
        ZoneScopedN("Draw Flats");

        BindStorage(BINDING_FLATS, flatBuffer.address);

        state.blend = true;
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_GREATER;
        SetInt("renderMode", RENDER_FLAT);

        DrawFlats();
    }

    {
        ZoneScopedN("Upload GPU Walls from Map");

        // Walls can't be culled: they're seen from both sides and change at runtime.
        UploadGpuWalls();

        state.blend = true;
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_GREATER;
        SetInt("renderMode", RENDER_WALL);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 6, static_cast<uint32_t>(gpuWallCount));
        state.blend = false;
    }

    {
        ZoneScopedN("Build GPU Models");

        // Opaque geometry: drawn before sprites so their alpha blends over models.
        BuildGpuModels();

        state.blend = true;
        state.cull = true;
        state.blendFunc = Blend::Alpha;
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_GREATER;

        DrawGpuModels();
        state.blend = false;
        state.cull = false;
    }

    {
        ZoneScopedN("Build GPU Sprites");

        BuildGpuSprites();
        BindStorage(BINDING_SPRITES, UploadFrame(gpuSprites.data(), gpuSprites.size() * sizeof(GpuSprite)));

        state.blend = true;
        state.blendFunc = Blend::Alpha;
        state.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
        state.depthWrite = true;

        SetInt("renderMode", RENDER_SPRITE);
        Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, 4, static_cast<uint32_t>(spriteCount));
        state.blend = false;
    }
}

void Vulkan::DrawNoCameraScreen() {
    if (!UpdateScreenSize()) return;

    SetViewport(screenWidth, screenHeight);
    ClearColor({0.0f, 0.0f, 0.0f, 1.0f});

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

void Vulkan::Update(const bool renderDebug, const bool renderUI) {
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

    SetViewport(screenWidth, screenHeight);

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
        state.stencilTest = true;
        state.stencilWriteMask = 0xFF;
        state.stencilCompare = VK_COMPARE_OP_EQUAL;
        state.stencilReference = 0;
        state.stencilCompareMask = 0xFF;
        state.stencilFail = state.stencilDepthFail = state.stencilPass = VK_STENCIL_OP_KEEP;
    }

    {
        ZoneScopedN("Background");
        DrawBackground(*camera, level.sky);
    }

    {
        ZoneScopedN("Upload GPU Sectors");

        // Floor data is the same in every view, so it is uploaded once.
        BuildGpuSectors();
        BindStorage(BINDING_SECTORS, UploadFrame(gpuSectors.data(), gpuSectors.size() * sizeof(GpuSector)));
        BindStorage(BINDING_SECTOR_FLOORS, UploadFrame(gpuSectorFloors.data(), gpuSectorFloors.size() * sizeof(GpuSectorFloor)));
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

        state.stencilTest = false;
        state.scissorTest = false;
        state.clipDistance = false;
        state.colorMask = 0xF;

        UseProgram(projectionProgram.get());
        SetSceneCamera(*camera, renderCameraTransform.position);
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_GREATER;
    }

#ifndef TILKY_STANDALONE
    {
        ZoneScopedN("Build Colliders");

        // Turning entity markers off hides the collider outlines too.
        if (renderDebug && entityMarkers != EntityMarkers::Off) {
            BuildGpuColliders();
            BindStorage(BINDING_COLLIDERS, UploadFrame(gpuColliders.data(), gpuColliders.size() * sizeof(GpuCollider)));

            if (colliderCount > 0) {
                state.depthWrite = true;
                state.depthTest = true;

                SetInt("renderMode", RENDER_COLLIDER);
                Draw(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, static_cast<uint32_t>(colliderCount * COLLIDER_VERTICES_PER_COLLIDER));
                Draw(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, static_cast<uint32_t>(colliderCount * COLLIDER_VERTICES_PER_COLLIDER));
            }
        }
    }

    if (entityMarkers != EntityMarkers::Off) {
        ZoneScopedN("Editor Gizmos");

        // Not for the camera being looked through: its gizmos sit at the eye.
        BuildGpuEditorGizmos(useEditorCamera ? INVALID_ENTITY_ID : camera->ownerID, renderCameraTransform, *camera);
        BindStorage(BINDING_EDITOR_GIZMOS,
                    UploadFrame(gpuEditorGizmoTriangles.data(), gpuEditorGizmoTriangles.size() * sizeof(GpuEditorGizmoVertex)));

        if (editorGizmoVertexCount > 0) {
            // They write depth so an arrow's own faces sort. Always mode
            // shows them through walls by dropping the scene's depth first;
            // nothing after this pass reads it.
            if (entityMarkers == EntityMarkers::Always) {
                state.depthWrite = true;
                ClearDepth();
            }

            state.depthTest = true;
            state.depthWrite = true;
            state.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;

            SetInt("renderMode", RENDER_EDITOR_GIZMO);
            Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(editorGizmoVertexCount));
        }
    }

    if (entityMarkers != EntityMarkers::Off) {
        ZoneScopedN("Entity Markers");

        BuildGpuEntityMarkers();
        BindStorage(BINDING_ENTITY_MARKERS, UploadFrame(gpuEntityMarkers.data(), gpuEntityMarkers.size() * sizeof(Vector4)));

        if (entityMarkerCount > 0) {
            state.blend = true;
            state.blendFunc = Blend::Alpha;
            state.depthWrite = false;

            if (entityMarkers == EntityMarkers::Always) state.depthTest = false;
            else state.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;

            SetInt("renderMode", RENDER_ENTITY_MARKER);
            Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<uint32_t>(entityMarkerCount * VERTICES_PER_ENTITY_MARKER));

            state.depthTest = true;
            state.depthWrite = true;
            state.blend = false;
        }
    }

    if (!renderUI) return;
#else
    (void)renderDebug;
    (void)renderUI;
#endif
    {
        ZoneScopedN("Rendering UI Sprites");

        state.depthTest = false;
        state.cull = false;
        state.blend = true;
        state.blendFunc = Blend::Alpha;

        UseProgram(uiProgram.get());

        UISystem::UpdateAllTransforms(level, screenWidth, screenHeight);

        // Every Sprite by Draw Order, then every Text. Text drawing switches
        // programs, so the sprite state is set again after it.
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
                    state.blend = true;
                    state.blendFunc = Blend::Alpha;
                    UseProgram(uiProgram.get());
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

                if (spriteStateSet) state.blend = false;
                spriteStateSet = false;

                RenderUIText(text, level.ShownUIText(text), *transform);
            });

        state.blend = false;
    }
}

// ============================================================================
// Gateways (see OpenGLGateway.cpp)
// ============================================================================

void Vulkan::SetGatewayClip(const GatewayView& view) {
    if (view.clipped) {
        state.clipDistance = true;
        SetVec4("uClipPlane", view.clipPlane.x, view.clipPlane.y, view.clipPlane.z, view.clipPlane.w);
    }
    else {
        state.clipDistance = false;
        SetVec4("uClipPlane", 0.0f, 0.0f, 0.0f, 1.0f);
    }

    state.scissorTest = true;
    std::copy(view.scissor, view.scissor + 4, state.scissor);
}

void Vulkan::DrawGatewayBox(const Gateways::Opening& opening, const float depthBehind, const bool farDepth, const bool whole) {
    const std::array<float, 8 * 4> corners = BoxCorners(opening, depthBehind);

    SetVec4Array("uGatewayCorners", corners.data(), 8);
    SetInt("uGatewayFarDepth", farDepth ? 1 : 0);
    SetInt("renderMode", RENDER_GATEWAY);

    state.blend = false;
    state.cull = false;

    // The opening's own wall is drawn whole: the box is pulled a little
    // toward the camera so it covers it there (reverse Z: nearer is greater).
    state.polygonOffset = !farDepth;
    Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, whole ? 36 : 6); // 6: the front face alone
    state.polygonOffset = false;
}

void Vulkan::DrawGatewayViews(const Level& level, const GatewayView& view, const int stencilRef, int& passesLeft) {
    ZoneScopedN("Gateway Views");

    const int maxDepth = std::clamp(level.rendererSettings.gatewayDepth, 0, MAX_GATEWAY_DEPTH);
    const Vector2 eye = {view.eye.x, view.eye.z};

    // Picked before any is drawn: drawing a view through one replaces the
    // visibility this view was culled with.
    const std::vector<SeenGateway> seen = CollectSeenGateways(view);

    const float boxDepth = BoxDepth(view.camera);

    const auto stencil = [this](const int reference, const VkStencilOp pass) {
        state.stencilCompare = VK_COMPARE_OP_EQUAL;
        state.stencilReference = static_cast<uint32_t>(reference);
        state.stencilCompareMask = 0xFF;
        state.stencilFail = VK_STENCIL_OP_KEEP;
        state.stencilDepthFail = VK_STENCIL_OP_KEEP;
        state.stencilPass = pass;
    };

    for (const SeenGateway& entry : seen) {
        const Gateways::Opening& opening = *entry.opening;
        const bool wholeBox = NeedsWholeBox(opening, eye, boxDepth);

        // This view's own state: the last opening drew another view.
        UseProgram(projectionProgram.get());
        SetSceneCamera(view.camera, view.eye);
        SetGatewayClip(view);

        const bool drawView = view.depth < maxDepth && passesLeft > 0 && stencilRef < 254;

        if (!drawView) {
            state.colorMask = 0xF;
            stencil(stencilRef, VK_STENCIL_OP_KEEP);
            state.depthWrite = true;
            state.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
            DrawGatewayBox(opening, boxDepth, false, wholeBox);
            continue;
        }

        --passesLeft;

        // 1. Where the opening shows.
        state.colorMask = 0;
        state.depthWrite = false;
        state.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
        stencil(stencilRef, VK_STENCIL_OP_INCREMENT_AND_CLAMP);
        DrawGatewayBox(opening, boxDepth, false, wholeBox);

        // 2. Nothing in the way there yet.
        stencil(stencilRef + 1, VK_STENCIL_OP_KEEP);
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_ALWAYS;
        DrawGatewayBox(opening, boxDepth, true, wholeBox);

        // 3. The view through it.
        const GatewayView through = ViewThrough(view, entry);

        if (visibilityCulling)
            UpdateVisibleSectorsFrom(level, opening.exitSectorIndex, through.camera.projection * through.camera.view);

        state.colorMask = 0xF;
        stencil(stencilRef + 1, VK_STENCIL_OP_KEEP);

        // The sky shader writes no clip distance.
        state.clipDistance = false;
        state.scissorTest = true;
        std::copy(through.scissor, through.scissor + 4, state.scissor);
        DrawBackground(through.camera, level.sky);

        SetupSceneShader(level, through.camera, through.eye);
        SetGatewayClip(through);
        DrawSceneGeometry();

        DrawGatewayViews(level, through, stencilRef + 1, passesLeft);

        // 4. Back to this view: the opening's depth, then its stencil.
        UseProgram(projectionProgram.get());
        SetSceneCamera(view.camera, view.eye);
        SetGatewayClip(view);

        state.colorMask = 0;
        stencil(stencilRef + 1, VK_STENCIL_OP_KEEP);
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_ALWAYS;
        DrawGatewayBox(opening, boxDepth, true, wholeBox);

        state.depthCompare = VK_COMPARE_OP_GREATER;
        DrawGatewayBox(opening, boxDepth, false, wholeBox);

        state.depthWrite = false;
        state.depthCompare = VK_COMPARE_OP_ALWAYS;
        stencil(stencilRef + 1, VK_STENCIL_OP_DECREMENT_AND_CLAMP);
        DrawGatewayBox(opening, boxDepth, false, wholeBox);

        state.colorMask = 0xF;
        state.depthWrite = true;
        state.depthCompare = VK_COMPARE_OP_GREATER;
        stencil(stencilRef, VK_STENCIL_OP_KEEP);
    }
}

// ============================================================================
// Sky (see OpenGLBackground.cpp)
// ============================================================================

const VulkanInternal::Image* Vulkan::SkyTexture(const std::string& fileName, Vector2* outSize) {
    const int textureIndex = GetOrCreateTextureIndex(fileName);
    if (textureIndex < 0 || textureIndex >= GetTextureCount()) return nullptr;

    const GPUTexture& texture = GetTexture(textureIndex);
    const Image* image = StandaloneImage(texture.id);
    if (image == nullptr) return nullptr;

    if (outSize != nullptr) *outSize = {static_cast<float>(texture.width), static_cast<float>(texture.height)};
    return image;
}

void Vulkan::DrawBackground(const ComponentCamera& camera, const SkySettings& sky) {
    if (backgroundProgram == nullptr) return;

    // Pick the textures first; anything missing falls back to the solid color.
    int mode = static_cast<int>(sky.mode);
    Vector2 textureSize = {1.0f, 1.0f};
    std::array<const Image*, SKY_FACE_COUNT> images = {};

    if (sky.mode == SkyMode::Cubemap) {
        for (int face = 0; face < SKY_FACE_COUNT; ++face) {
            images[face] = SkyTexture(sky.cubemapFaces[face], nullptr);
            if (images[face] == nullptr) mode = SKY_NONE;
        }
    }
    else {
        images[0] = SkyTexture(sky.texture, &textureSize);
        if (images[0] == nullptr) mode = SKY_NONE;
    }

    // Equirectangular clamps at the poles; DOOM's cylinder repeats both ways.
    const uint32_t flatSampler = sky.mode == SkyMode::Cylinder ? SAMPLER_STANDALONE : SAMPLER_STANDALONE_REPEAT_CLAMP;

    // Same basis as Matrix4::LookAt, so the sky lines up with the world.
    const Vector3 forward = Vector3Math::Normalized(camera.forward);
    const Vector3 right = Vector3Math::Normalized(Vector3Math::Cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vector3 up = Vector3Math::Cross(right, forward);

    state.depthTest = false;
    state.depthWrite = false;
    state.blend = false;

    Program* previous = program;
    UseProgram(backgroundProgram.get());

    SetInt("skyMode", mode);
    SetVec3("cameraForward", forward.x, forward.y, forward.z);
    SetVec3("cameraRight", right.x, right.y, right.z);
    SetVec3("cameraUp", up.x, up.y, up.z);
    SetFloat("tanHalfFovY", std::tan(camera.fov * Constants::DegToRad * 0.5f));
    SetFloat("aspect", camera.aspectRatio);
    SetFloat("rotation", (sky.rotation + sky.spin) * Constants::DegToRad);
    SetFloat("horizonOffset", sky.horizonOffset * Constants::DegToRad);
    SetVec3("tint", sky.tint.x / 255.0f, sky.tint.y / 255.0f, sky.tint.z / 255.0f);
    SetVec3("fallbackColor", sky.fallbackColor.x / 255.0f, sky.fallbackColor.y / 255.0f, sky.fallbackColor.z / 255.0f);
    SetVec2("skyTextureSize", textureSize.x, textureSize.y);

    SetTexture("skyTexture", sky.mode == SkyMode::Cubemap ? nullptr : images[0], flatSampler);
    for (int face = 0; face < SKY_FACE_COUNT; ++face)
        SetTexture(FACE_UNIFORMS[face], sky.mode == SkyMode::Cubemap ? images[face] : nullptr, SAMPLER_STANDALONE_CLAMP);

    Draw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 3);

    UseProgram(previous);
    state.blend = true;
    state.depthWrite = true;
    state.depthTest = true;
    state.depthCompare = VK_COMPARE_OP_GREATER;
}

// ============================================================================
// Screen rectangles (see OpenGLUI.cpp)
// ============================================================================

void Vulkan::DrawScreenRectangle(const Vector2 position, const Vector2 size, const Vector4 color, const std::string& texture) {
    if (size.x <= 0.0f || size.y <= 0.0f || color.w <= 0.0f || uiProgram == nullptr) return;

    const DrawState saved = state;
    Program* previous = program;

    state.depthTest = false;
    state.cull = false;
    state.blend = true;
    state.blendFunc = Blend::Alpha;

    UseProgram(uiProgram.get());
    DrawUIRectangle(position, size, {color.x * 255.0f, color.y * 255.0f, color.z * 255.0f, color.w * 255.0f}, 0.0f, texture);

    state.blend = saved.blend;
    state.blendFunc = saved.blendFunc;
    state.cull = saved.cull;
    state.depthTest = saved.depthTest;
    UseProgram(previous);
}

void Vulkan::DrawUIRectangle(const Vector2& position, const Vector2& size, const Vector4& color, const float rotation,
                             const std::string& texture) {
    if (size.x <= 0.0f || size.y <= 0.0f) {
        spdlog::critical("DrawUIRectangle failed: invalid size ({}, {}) for texture '{}'", size.x, size.y, texture);
        return;
    }

    // An empty texture is a solid rectangle of `color`.
    SetVec2("uScreenSize", static_cast<float>(screenWidth), static_cast<float>(screenHeight));
    SetVec2("uPosition", position.x, position.y);
    SetVec2("uSize", size.x, size.y);
    SetVec4("uColor", color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, color.w / 255.0f);
    SetFloat("rotation", rotation * Constants::DegToRad);

    int textureIndex = -1;
    if (!texture.empty()) textureIndex = GetTextureRegionIndex(texture);

    const bool useTexture = textureIndex >= 0 && textureIndex < static_cast<int>(textureRegions.size());

    if (!texture.empty() && textureRegions.empty())
        spdlog::critical("DrawUIRectangle failed: textureRegions is empty while drawing '{}'", texture);

    if (!texture.empty() && textureIndex < 0)
        spdlog::critical("DrawUIRectangle failed: texture '{}' was not found in the atlas", texture);

    if (textureIndex >= static_cast<int>(textureRegions.size()) && textureIndex >= 0)
        spdlog::critical("DrawUIRectangle failed: texture '{}' resolved to invalid index {} with only {} atlas regions", texture,
                         textureIndex, textureRegions.size());

    SetInt("uUseTexture", useTexture ? 1 : 0);

    if (useTexture) {
        if (atlas.image == VK_NULL_HANDLE || textureRegionBuffer.buffer == VK_NULL_HANDLE) {
            spdlog::critical("DrawUIRectangle failed: no atlas");
            return;
        }

        SetTexture("uAtlas", &atlas, AtlasSampler());
        SetInt("uTextureIndex", textureIndex);
        SetInt("uTextureCount", static_cast<int>(textureRegions.size()));
        BindStorage(BINDING_TEXTURE_REGIONS, textureRegionBuffer.address);
    }

    DrawIndexed(uiQuadVertices, 4 * sizeof(float), uiQuadIndices, 6);
}
