// Static 3D models (see OpenGLModel.cpp): per-file meshes uploaded once and
// the per-entity instance buffer, rebuilt from ComponentModel +
// ComponentTransform every frame.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <bit>
#include <cstring>
#include <ranges>

#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

#include "Headers/Map/LevelManager.hpp"
#include "Headers/Objects/Components.hpp"
#include "Headers/Runtime/Renderer/ModelLoader.hpp"

using namespace VulkanInternal;
using namespace RendererGpu;

namespace {
    constexpr int MODEL_INSTANCE_BINDING = 3;

    int MipCount(const int width, const int height) {
        return static_cast<int>(std::bit_width(static_cast<unsigned>(std::max(std::max(width, height), 1))));
    }

    // Tightly packed RGBA8 rows from a surface's pitch.
    std::vector<unsigned char> PackRows(const void* pixels, const int width, const int height, const int rowLengthPixels) {
        std::vector<unsigned char> packed(static_cast<size_t>(width) * height * 4);
        for (int row = 0; row < height; ++row)
            std::memcpy(packed.data() + static_cast<size_t>(row) * width * 4,
                        static_cast<const unsigned char*>(pixels) + static_cast<size_t>(row) * rowLengthPixels * 4,
                        static_cast<size_t>(width) * 4);
        return packed;
    }
}

VulkanInternal::ModelAsset* Vulkan::AcquireModelAsset(const std::string& fileName) {
    if (const auto existing = modelAssets.find(fileName); existing != modelAssets.end()) return &existing->second;

    if (failedModelFiles.contains(fileName)) return nullptr;

    std::string errorMessage;
    const std::optional<ModelLoader::ModelData> model = ModelLoader::Load(fileName, errorMessage);

    if (!model.has_value()) {
        spdlog::error("Failed to load model '{}': {}", fileName, errorMessage);
        failedModelFiles.insert(fileName);
        return nullptr;
    }

    ModelAsset asset;

    for (const ModelLoader::ModelTextureSource& source : model->textures) {
        Image image;
        bool loaded = false;

        if (!source.rgbaPixels.empty()) {
            loaded = UploadImage(source.rgbaPixels.data(), source.width, source.height, VK_FORMAT_R8G8B8A8_UNORM,
                                 MipCount(source.width, source.height), image);
        }
        else {
            SDL_Surface* loadedSurface = nullptr;

            if (!source.encodedData.empty()) {
                SDL_IOStream* stream = SDL_IOFromConstMem(source.encodedData.data(), source.encodedData.size());
                loadedSurface = stream != nullptr ? IMG_Load_IO(stream, true) : nullptr;
            }
            else loadedSurface = IMG_Load(ModelLoader::PathToUtf8(source.filePath).c_str());

            SDL_Surface* surface = loadedSurface != nullptr ? SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32) : nullptr;
            if (loadedSurface != nullptr) SDL_DestroySurface(loadedSurface);

            if (surface == nullptr) {
                spdlog::error("Model '{}': failed to decode texture '{}': {}", fileName, source.reference, SDL_GetError());
            }
            else {
                const std::vector<unsigned char> packed = PackRows(surface->pixels, surface->w, surface->h, surface->pitch / 4);
                loaded = UploadImage(packed.data(), surface->w, surface->h, VK_FORMAT_R8G8B8A8_UNORM, MipCount(surface->w, surface->h), image);
                SDL_DestroySurface(surface);
            }
        }

        if (loaded) {
            image.slot = AllocateTextureSlot(false);
            WriteTextureSlot(image, false);
        }
        asset.textures.push_back(image);
    }

    for (const ModelLoader::ModelMaterial& material : model->materials) {
        ModelMaterial gpuMaterial;
        gpuMaterial.baseColor = material.baseColor;
        gpuMaterial.texture = material.textureIndex >= 0 && asset.textures[material.textureIndex].image != VK_NULL_HANDLE
                                  ? material.textureIndex : -1;
        asset.materials.push_back(gpuMaterial);
    }

    for (const ModelLoader::ModelMesh& mesh : model->meshes) {
        using ModelLoader::ModelVertex;

        Mesh gpuMesh;
        gpuMesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
        gpuMesh.materialIndex = mesh.materialIndex;

        const size_t vertexBytes = mesh.vertices.size() * sizeof(ModelVertex);
        const size_t indexBytes = mesh.indices.size() * sizeof(std::uint32_t);

        // Host visible: model vertices never change after upload.
        if (CreateBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, gpuMesh.vertices) && vertexBytes > 0)
            std::memcpy(gpuMesh.vertices.mapped, mesh.vertices.data(), vertexBytes);
        if (CreateBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true, gpuMesh.indices) && indexBytes > 0)
            std::memcpy(gpuMesh.indices.mapped, mesh.indices.data(), indexBytes);

        asset.meshes.push_back(gpuMesh);
    }

    for (const ModelLoader::ModelDraw& draw : model->draws) {
        ModelDraw gpuDraw;
        gpuDraw.meshIndex = draw.meshIndex;

        Models::ToColumnMajor(draw.localTransform, gpuDraw.localMatrix);
        Models::ToNormalMatrix(draw.localTransform, gpuDraw.localNormalMatrix);

        asset.draws.push_back(gpuDraw);
    }

    spdlog::info("Loaded model '{}': {} mesh(es), {} draw(s), {} material(s), {} texture(s)", fileName, asset.meshes.size(),
                 asset.draws.size(), asset.materials.size(), asset.textures.size());

    return &modelAssets.emplace(fileName, std::move(asset)).first->second;
}

void Vulkan::DestroyModelAsset(ModelAsset& asset) {
    for (Mesh& mesh : asset.meshes) {
        DeferDestroyBuffer(mesh.vertices);
        DeferDestroyBuffer(mesh.indices);
    }
    for (Image& texture : asset.textures) DeferDestroyImage(texture);
    asset = {};
}

void Vulkan::ReleaseUnusedModelAssets(const Uint64 nowTicks) {
    for (auto it = modelAssets.begin(); it != modelAssets.end();) {
        if (nowTicks - it->second.lastUsedTicks < Models::MODEL_UNUSED_LIFETIME_MS) {
            ++it;
            continue;
        }

        spdlog::info("Released unused model '{}'", it->first);

        DestroyModelAsset(it->second);
        it = modelAssets.erase(it);
    }
}

void Vulkan::DestroyAllModelAssets() {
    for (ModelAsset& asset : modelAssets | std::views::values) DestroyModelAsset(asset);

    modelAssets.clear();
    failedModelFiles.clear();
    gpuModelInstances.clear();
    modelBatches.clear();
    modelInstanceAddress = 0;
}

void Vulkan::BuildGpuModels() {
    Level& level = LevelManager::CurrentLevel();

    // The sampler follows the level's setting (OpenGL re-sets its textures).
    modelTextureSetting = level.rendererSettings.textureSetting;

    const Uint64 nowTicks = SDL_GetTicks();

    modelBatches.clear();
    gpuModelInstances.clear();

    // Entities are grouped per asset so each mesh is drawn once, instanced,
    // for every entity that uses its file.
    std::unordered_map<const ModelAsset*, size_t> batchIndexByAsset;
    std::vector<std::vector<GpuModelInstance>> instancesByBatch;

    for (const ComponentModel& modelComponent : level.models.components) {
        if (modelComponent.fileName.empty()) continue;

        // A disabled entity's models are hidden in game, still shown to the editor camera.
        if (!modelComponent.ownerEnabled && !useEditorCamera) continue;

        const ComponentTransform* transform = level.transforms.Get(modelComponent.ownerID);

        if (IsEntityCulled(*transform)) continue;

        ModelAsset* asset = AcquireModelAsset(modelComponent.fileName);

        if (asset == nullptr) continue;

        asset->lastUsedTicks = nowTicks;

        const auto [batch, inserted] = batchIndexByAsset.try_emplace(asset, modelBatches.size());

        if (inserted) {
            modelBatches.push_back({asset, 0, 0});
            instancesByBatch.emplace_back();
        }

        GpuModelInstance instance = Models::BuildModelInstance(*transform, modelComponent.offset, level, fullbright);
        instance.color.w = static_cast<float>(EntityMetaData(modelComponent.ownerID));

        instancesByBatch[batch->second].push_back(instance);
    }

    for (size_t i = 0; i < modelBatches.size(); ++i) {
        modelBatches[i].firstInstance = static_cast<int>(gpuModelInstances.size());
        modelBatches[i].instanceCount = static_cast<int>(instancesByBatch[i].size());

        gpuModelInstances.insert(gpuModelInstances.end(), instancesByBatch[i].begin(), instancesByBatch[i].end());
    }

    ReleaseUnusedModelAssets(nowTicks);

    // This frame's copy (each frame in flight needs its own).
    modelInstanceAddress = UploadFrame(gpuModelInstances.data(), gpuModelInstances.size() * sizeof(GpuModelInstance));
}

void Vulkan::DrawGpuModels() {
    if (modelBatches.empty()) return;

    BindStorage(MODEL_INSTANCE_BINDING, modelInstanceAddress);
    SetInt("renderMode", RENDER_MODEL);

    const uint32_t sampler = SAMPLER_MODEL_FIRST + static_cast<uint32_t>(modelTextureSetting);

    for (const ModelBatch& batch : modelBatches) {
        SetInt("uModelInstanceOffset", batch.firstInstance);

        for (const ModelDraw& draw : batch.asset->draws) {
            const Mesh& mesh = batch.asset->meshes[draw.meshIndex];
            const ModelMaterial& material = batch.asset->materials[mesh.materialIndex];
            const Image* texture = material.texture >= 0 ? &batch.asset->textures[material.texture] : nullptr;

            SetMat4ColumnMajor("uModelLocal", draw.localMatrix);
            SetMat3ColumnMajor("uModelLocalNormal", draw.localNormalMatrix);
            SetVec4("uModelBaseColor", material.baseColor.x, material.baseColor.y, material.baseColor.z, material.baseColor.w);
            SetInt("uModelHasTexture", texture != nullptr ? 1 : 0);
            SetTexture("uModelTexture", texture, sampler);

            DrawIndexed(mesh.vertices, sizeof(ModelLoader::ModelVertex), mesh.indices, mesh.indexCount,
                        static_cast<uint32_t>(batch.instanceCount));
        }
    }
}
