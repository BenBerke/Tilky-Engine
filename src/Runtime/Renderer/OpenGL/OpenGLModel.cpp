//
// Created by berke on 9/26/2026.
//

/// Static 3D models: per-file GPU meshes (uploaded once, never modified) and
/// the per-entity model SSBO, rebuilt from ComponentModel + ComponentTransform.

#include "Headers/Runtime/Renderer/OpenGL/OpenGL.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <ranges>

#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"

namespace {
    using namespace OpenGLRendererInternal;

    // How long a loaded model stays in memory after the last entity using it
    // is removed or switched to another file. Long enough that a script
    // swapping between two models does not reload them from disk every time.
    constexpr Uint64 MODEL_UNUSED_LIFETIME_MS = 5000;

    // SSBO binding of ModelInstanceBuffer in Rendering.vs.glsl. 3 is the only
    // binding below 8 (the guaranteed minimum) nothing else uses.
    constexpr GLuint MODEL_INSTANCE_BINDING = 3;

    // Texture unit the model texture is sampled from; unit 0 is the atlas.
    constexpr GLint MODEL_TEXTURE_UNIT = 1;

    GLuint UploadRgbaTexture(
        const void* pixels,
        const int width,
        const int height,
        const int rowLengthPixels,
        const RendererTextureSettings setting
    ) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLengthPixels);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        // Model UVs routinely leave 0..1 to tile a texture across a surface.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        ApplyTextureSampling(setting);

        glBindTexture(GL_TEXTURE_2D, 0);

        return texture;
    }

    GLuint CreateModelTexture(
        const ModelLoader::ModelTextureSource& source,
        const RendererTextureSettings setting,
        const std::string& modelFileName
    ) {
        if (!source.rgbaPixels.empty())
            return UploadRgbaTexture(source.rgbaPixels.data(), source.width, source.height, source.width, setting);

        SDL_Surface* loadedSurface = nullptr;

        if (!source.encodedData.empty()) {
            SDL_IOStream* stream = SDL_IOFromConstMem(source.encodedData.data(), source.encodedData.size());
            loadedSurface = stream != nullptr ? IMG_Load_IO(stream, true) : nullptr;
        }
        else loadedSurface = IMG_Load(ModelLoader::PathToUtf8(source.filePath).c_str());

        if (loadedSurface == nullptr) {
            spdlog::error(
                "Model '{}': failed to decode texture '{}': {}",
                modelFileName, source.reference, SDL_GetError()
            );
            return 0;
        }

        SDL_Surface* surface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loadedSurface);

        if (surface == nullptr) {
            spdlog::error(
                "Model '{}': failed to convert texture '{}': {}",
                modelFileName, source.reference, SDL_GetError()
            );
            return 0;
        }

        const GLuint texture = UploadRgbaTexture(surface->pixels, surface->w, surface->h, surface->pitch / 4, setting);

        SDL_DestroySurface(surface);

        return texture;
    }

    GpuMesh CreateGpuMesh(const ModelLoader::ModelMesh& mesh) {
        using ModelLoader::ModelVertex;

        GpuMesh gpuMesh;
        gpuMesh.indexCount = static_cast<GLsizei>(mesh.indices.size());
        gpuMesh.materialIndex = mesh.materialIndex;

        glGenVertexArrays(1, &gpuMesh.vao);
        glGenBuffers(1, &gpuMesh.vbo);
        glGenBuffers(1, &gpuMesh.ebo);

        glBindVertexArray(gpuMesh.vao);

        // Static: model vertices never change after upload. Movement and
        // rotation come from the per-entity model SSBO.
        glBindBuffer(GL_ARRAY_BUFFER, gpuMesh.vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(ModelVertex)),
            mesh.vertices.data(),
            GL_STATIC_DRAW
        );

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuMesh.ebo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
            mesh.indices.data(),
            GL_STATIC_DRAW
        );

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                              reinterpret_cast<void*>(offsetof(ModelVertex, position)));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                              reinterpret_cast<void*>(offsetof(ModelVertex, normal)));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                              reinterpret_cast<void*>(offsetof(ModelVertex, uv)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        return gpuMesh;
    }

    void DestroyModelAsset(GpuModelAsset& asset) {
        for (GpuMesh& mesh : asset.meshes) {
            glDeleteVertexArrays(1, &mesh.vao);
            glDeleteBuffers(1, &mesh.vbo);
            glDeleteBuffers(1, &mesh.ebo);
        }

        for (const GLuint texture : asset.textures)
            if (texture != 0) glDeleteTextures(1, &texture);

        asset = {};
    }

    void ToColumnMajor(const Matrix4& matrix, float out[16]) {
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                out[column * 4 + row] = matrix.m[row][column];
    }

    // Inverse transpose of the upper 3x3, column major. It is the cofactor
    // matrix divided by the determinant; the shader normalizes the result, so
    // a degenerate (zero scale) matrix keeps its cofactors instead of failing.
    void ToNormalMatrix(const Matrix4& matrix, float out[9]) {
        const auto& a = matrix.m;

        float cofactor[3][3];
        cofactor[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
        cofactor[0][1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
        cofactor[0][2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
        cofactor[1][0] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
        cofactor[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
        cofactor[1][2] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
        cofactor[2][0] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
        cofactor[2][1] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
        cofactor[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];

        const float determinant = a[0][0] * cofactor[0][0] + a[0][1] * cofactor[0][1] + a[0][2] * cofactor[0][2];
        const float inverse = std::abs(determinant) > 1e-12f ? 1.0f / determinant : 1.0f;

        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                out[column * 3 + row] = cofactor[row][column] * inverse;
    }

    GpuModelInstance BuildModelInstance(const ComponentTransform& transform, const Level& level) {
        const Quaternion q = transform.rotation.Normalized();

        // Rotation matrix columns.
        const float rotation[3][3] = {
            {1.0f - 2.0f * (q.y * q.y + q.z * q.z), 2.0f * (q.x * q.y + q.z * q.w), 2.0f * (q.x * q.z - q.y * q.w)},
            {2.0f * (q.x * q.y - q.z * q.w), 1.0f - 2.0f * (q.x * q.x + q.z * q.z), 2.0f * (q.y * q.z + q.x * q.w)},
            {2.0f * (q.x * q.z + q.y * q.w), 2.0f * (q.y * q.z - q.x * q.w), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)}
        };

        const float scale[3] = {transform.scale.x, transform.scale.y, transform.scale.z};

        GpuModelInstance instance;

        for (int column = 0; column < 3; ++column) {
            // Normal matrix of rotation * scale is rotation * scale^-1, which
            // keeps normals perpendicular under nonuniform scale.
            const float inverseScale = std::abs(scale[column]) > Constants::Epsilon ? 1.0f / scale[column] : 0.0f;

            for (int row = 0; row < 3; ++row) {
                instance.modelMatrix[column * 4 + row] = rotation[column][row] * scale[column];
                instance.normalMatrix[column * 4 + row] = rotation[column][row] * inverseScale;
            }
        }

        instance.modelMatrix[12] = transform.position.x;
        instance.modelMatrix[13] = transform.position.y;
        instance.modelMatrix[14] = transform.position.z;
        instance.modelMatrix[15] = 1.0f;

        instance.color = {1.0f, 1.0f, 1.0f, 1.0f};

        // Same sector light rule as sprites: light 255 = unlit color.
        if (transform.sectorIndex >= 0 && transform.sectorIndex < static_cast<int>(level.sectors.size())) {
            const Vector3& light = level.sectors[transform.sectorIndex].light;

            instance.color = {
                std::max(light.x / 255.0f, 0.0f),
                std::max(light.y / 255.0f, 0.0f),
                std::max(light.z / 255.0f, 0.0f),
                1.0f
            };
        }

        return instance;
    }
}

OpenGL::GpuModelAsset* OpenGL::AcquireModelAsset(const std::string& fileName) {
    if (const auto existing = modelAssets.find(fileName); existing != modelAssets.end()) return &existing->second;

    if (failedModelFiles.contains(fileName)) return nullptr;

    std::string errorMessage;
    const std::optional<ModelLoader::ModelData> model = ModelLoader::Load(fileName, errorMessage);

    if (!model.has_value()) {
        spdlog::error("Failed to load model '{}': {}", fileName, errorMessage);
        failedModelFiles.insert(fileName);
        return nullptr;
    }

    GpuModelAsset asset;

    for (const ModelLoader::ModelTextureSource& source : model->textures)
        asset.textures.push_back(CreateModelTexture(source, modelTextureSetting, fileName));

    for (const ModelLoader::ModelMaterial& material : model->materials) {
        OpenGLRendererInternal::GpuModelMaterial gpuMaterial;
        gpuMaterial.baseColor = material.baseColor;
        gpuMaterial.texture = material.textureIndex >= 0 ? asset.textures[material.textureIndex] : 0;

        asset.materials.push_back(gpuMaterial);
    }

    for (const ModelLoader::ModelMesh& mesh : model->meshes) asset.meshes.push_back(CreateGpuMesh(mesh));

    for (const ModelLoader::ModelDraw& draw : model->draws) {
        OpenGLRendererInternal::GpuModelDraw gpuDraw;
        gpuDraw.meshIndex = draw.meshIndex;

        ToColumnMajor(draw.localTransform, gpuDraw.localMatrix);
        ToNormalMatrix(draw.localTransform, gpuDraw.localNormalMatrix);

        asset.draws.push_back(gpuDraw);
    }

    spdlog::info(
        "Loaded model '{}': {} mesh(es), {} draw(s), {} material(s), {} texture(s)",
        fileName, asset.meshes.size(), asset.draws.size(), asset.materials.size(), asset.textures.size()
    );

    return &modelAssets.emplace(fileName, std::move(asset)).first->second;
}

void OpenGL::ReleaseUnusedModelAssets(const Uint64 nowTicks) {
    for (auto it = modelAssets.begin(); it != modelAssets.end();) {
        if (nowTicks - it->second.lastUsedTicks < MODEL_UNUSED_LIFETIME_MS) {
            ++it;
            continue;
        }

        spdlog::info("Released unused model '{}'", it->first);

        DestroyModelAsset(it->second);
        it = modelAssets.erase(it);
    }
}

void OpenGL::ApplyModelTextureSampling(const RendererTextureSettings setting) {
    modelTextureSetting = setting;

    for (const GpuModelAsset& asset : modelAssets | std::views::values) {
        for (const GLuint texture : asset.textures) {
            if (texture == 0) continue;

            glBindTexture(GL_TEXTURE_2D, texture);
            ApplyTextureSampling(setting);
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGL::DestroyAllModelAssets() {
    for (GpuModelAsset& asset : modelAssets | std::views::values) DestroyModelAsset(asset);

    modelAssets.clear();
    failedModelFiles.clear();

    gpuModelInstances.clear();
    uploadedModelInstances.clear();
    modelBatches.clear();

    if (modelSSBO != 0) {
        glDeleteBuffers(1, &modelSSBO);
        modelSSBO = 0;
    }

    modelSSBOCapacity = 0;
}

void OpenGL::BuildGpuModels() {
    Level& level = LevelManager::CurrentLevel();

    // Before acquiring, so newly loaded textures already use the current setting.
    if (level.rendererSettings.textureSetting != modelTextureSetting)
        ApplyModelTextureSampling(level.rendererSettings.textureSetting);

    const Uint64 nowTicks = SDL_GetTicks();

    modelBatches.clear();
    gpuModelInstances.clear();

    // Entities are grouped per asset so each mesh is drawn once, instanced,
    // for every entity that uses its file.
    std::unordered_map<const GpuModelAsset*, size_t> batchIndexByAsset;
    std::vector<std::vector<GpuModelInstance>> instancesByBatch;

    for (const ComponentModel& modelComponent : level.models.components) {
        if (modelComponent.fileName.empty()) continue;

        const ComponentTransform* transform = level.transforms.Get(modelComponent.ownerID);

        if (transform == nullptr) [[unlikely]] continue;

        GpuModelAsset* asset = AcquireModelAsset(modelComponent.fileName);

        if (asset == nullptr) continue;

        asset->lastUsedTicks = nowTicks;

        const auto [batch, inserted] = batchIndexByAsset.try_emplace(asset, modelBatches.size());

        if (inserted) {
            modelBatches.push_back({asset, 0, 0});
            instancesByBatch.emplace_back();
        }

        instancesByBatch[batch->second].push_back(BuildModelInstance(*transform, level));
    }

    for (size_t i = 0; i < modelBatches.size(); ++i) {
        modelBatches[i].firstInstance = static_cast<GLint>(gpuModelInstances.size());
        modelBatches[i].instanceCount = static_cast<GLsizei>(instancesByBatch[i].size());

        gpuModelInstances.insert(gpuModelInstances.end(), instancesByBatch[i].begin(), instancesByBatch[i].end());
    }

    ReleaseUnusedModelAssets(nowTicks);

    // Only touch the SSBO when a transform, light, or the entity list changed.
    const size_t byteSize = gpuModelInstances.size() * sizeof(GpuModelInstance);

    const bool changed =
        gpuModelInstances.size() != uploadedModelInstances.size() ||
        (byteSize > 0 && std::memcmp(gpuModelInstances.data(), uploadedModelInstances.data(), byteSize) != 0);

    if (!changed) return;

    uploadedModelInstances = gpuModelInstances;

    if (byteSize == 0) return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelSSBO);

    if (static_cast<GLsizeiptr>(byteSize) > modelSSBOCapacity) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(byteSize), gpuModelInstances.data(), GL_DYNAMIC_DRAW);
        modelSSBOCapacity = static_cast<GLsizeiptr>(byteSize);
    }
    else glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(byteSize), gpuModelInstances.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, MODEL_INSTANCE_BINDING, modelSSBO);
}

void OpenGL::DrawGpuModels() const {
    if (modelBatches.empty()) return;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, MODEL_INSTANCE_BINDING, modelSSBO);

    glUniform1i(renderModeUniform, RENDER_MODEL);
    glUniform1i(modelTextureUniform, MODEL_TEXTURE_UNIT);

    glActiveTexture(GL_TEXTURE0 + MODEL_TEXTURE_UNIT);

    for (const GpuModelBatch& batch : modelBatches) {
        glUniform1i(modelInstanceOffsetUniform, batch.firstInstance);

        for (const OpenGLRendererInternal::GpuModelDraw& draw : batch.asset->draws) {
            const GpuMesh& mesh = batch.asset->meshes[draw.meshIndex];
            const OpenGLRendererInternal::GpuModelMaterial& material = batch.asset->materials[mesh.materialIndex];

            glUniformMatrix4fv(modelLocalUniform, 1, GL_FALSE, draw.localMatrix);
            glUniformMatrix3fv(modelLocalNormalUniform, 1, GL_FALSE, draw.localNormalMatrix);
            glUniform4f(
                modelBaseColorUniform,
                material.baseColor.x,
                material.baseColor.y,
                material.baseColor.z,
                material.baseColor.w
            );
            glUniform1i(modelHasTextureUniform, material.texture != 0 ? 1 : 0);

            glBindTexture(GL_TEXTURE_2D, material.texture);

            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr, batch.instanceCount);
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(VAO);
}
