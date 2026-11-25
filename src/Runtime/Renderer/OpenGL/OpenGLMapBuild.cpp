#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <unordered_map>
#include <spdlog/spdlog.h>

#include "Headers/Objects/Wall.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Map/LevelManager.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/WallPieces.hpp"

using OpenGLRendererInternal::GpuWall;
using OpenGLRendererInternal::GpuFlatTriangle;

void OpenGL::UploadGpuWallsFromMap() {
    BuildGpuWallsFromMap();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallSSBO);

    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuWalls.size() * sizeof(GpuWall),
        gpuWalls.empty() ? nullptr : gpuWalls.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);
}

// BuildGpuSectors() re-uploads floor data every frame, but the triangle
// instances that draw each floor and ceiling are only baked here. Without this
// a floor added at runtime has GPU data and no triangles, so it never renders.
void OpenGL::RefreshFlatTrianglesIfLayoutChanged() {
    if (flatSSBO == 0) return;

    if (!FlatLayoutChanged()) return;

    BuildFlatTrianglesFromSectors();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        flatTriangles.size() * sizeof(GpuFlatTriangle),
        flatTriangles.empty() ? nullptr : flatTriangles.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);
}

// Reuses the buffers CreateMap() made. Sectors, sprites, colliders and
// models are rebuilt from the level every frame, so only the baked data -
// texture atlas, walls, floor/ceiling triangles - needs redoing here.
void OpenGL::ReloadMap() {
    spdlog::info("Reloading OpenGL renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    // Walls store atlas region indices, so the atlas comes first.
    RefreshTexturesFromLevel();
    UploadGpuWallsFromMap();

    BuildFlatTrianglesFromSectors();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        flatTriangles.size() * sizeof(GpuFlatTriangle),
        flatTriangles.empty() ? nullptr : flatTriangles.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);

    spdlog::info("Reloaded OpenGL map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());
}

bool OpenGL::CreateMap() {
    using namespace OpenGLRendererInternal;

    spdlog::info("Creating OpenGL renderer map data");

    LevelManager::TriangulateCurrentLevelSectors();

    BuildFlatTrianglesFromSectors();
    BuildGpuWallsFromMap();

    spdlog::info("Built OpenGL map GPU data. Walls: {}, flat triangles: {}", gpuWalls.size(), flatTriangles.size());

    glGenBuffers(1, &wallSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, wallSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        gpuWalls.size() * sizeof(GpuWall),
        gpuWalls.empty() ? nullptr : gpuWalls.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, wallSSBO);

    glGenBuffers(1, &flatSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, flatSSBO);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        flatTriangles.size() * sizeof(GpuFlatTriangle),
        flatTriangles.empty() ? nullptr : flatTriangles.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flatSSBO);

    glGenBuffers(1, &spriteSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, spriteSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spriteSSBO);

    glEnable(GL_PROGRAM_POINT_SIZE);

    // Sector
    glGenBuffers(1, &sectorSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sectorSSBO);

    // Sector floor
    glGenBuffers(1, &sectorFloorSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sectorFloorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sectorFloorSSBO);

    glGenBuffers(1, &colliderSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, colliderSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, colliderSSBO);

    glGenBuffers(1, &entityMarkerSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, entityMarkerSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, entityMarkerSSBO);

    glGenBuffers(1, &editorGizmoSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, editorGizmoSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, editorGizmoSSBO);

    // Per-entity model transforms. Grown on demand by BuildGpuModels().
    glGenBuffers(1, &modelSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, modelSSBO);
    modelSSBOCapacity = 0;

    projectionShader->use();

    renderModeUniform = glGetUniformLocation(projectionShader->ID, "renderMode");

    if (renderModeUniform == -1) {
        spdlog::critical("Failed to get projection shader uniform location: renderMode");
        return false;
    }

    viewUniform = glGetUniformLocation(projectionShader->ID, "uView");

    if (viewUniform == -1) {
        spdlog::critical("Failed to get projection shader uniform location: uView");
        return false;
    }

    projectionUniform = glGetUniformLocation(projectionShader->ID, "uProjection");

    if (projectionUniform == -1) {
        spdlog::critical("Failed to get projection shader uniform location: uProjection");
        return false;
    }

    // Not fatal when missing: the driver drops uniforms a shader never reads.
    cameraWorldPosUniform = glGetUniformLocation(projectionShader->ID, "uCameraWorldPos");
    fullbrightUniform = glGetUniformLocation(projectionShader->ID, "uFullbright");
    distanceLightStartUniform = glGetUniformLocation(projectionShader->ID, "uDistanceLightStart");
    distanceLightEndUniform = glGetUniformLocation(projectionShader->ID, "uDistanceLightEnd");
    distanceLightDarkestUniform = glGetUniformLocation(projectionShader->ID, "uDistanceLightDarkest");
    viewportSizeUniform = glGetUniformLocation(projectionShader->ID, "uViewportSize");
    modelInstanceOffsetUniform = glGetUniformLocation(projectionShader->ID, "uModelInstanceOffset");
    flatInstanceOffsetUniform = glGetUniformLocation(projectionShader->ID, "uFlatInstanceOffset");
    modelLocalUniform = glGetUniformLocation(projectionShader->ID, "uModelLocal");
    modelLocalNormalUniform = glGetUniformLocation(projectionShader->ID, "uModelLocalNormal");
    modelBaseColorUniform = glGetUniformLocation(projectionShader->ID, "uModelBaseColor");
    modelHasTextureUniform = glGetUniformLocation(projectionShader->ID, "uModelHasTexture");
    modelTextureUniform = glGetUniformLocation(projectionShader->ID, "uModelTexture");
    clipPlaneUniform = glGetUniformLocation(projectionShader->ID, "uClipPlane");
    gatewayCornersUniform = glGetUniformLocation(projectionShader->ID, "uGatewayCorners");
    gatewayFarDepthUniform = glGetUniformLocation(projectionShader->ID, "uGatewayFarDepth");

    spdlog::info("OpenGL renderer map creation completed successfully");

    return true;
}