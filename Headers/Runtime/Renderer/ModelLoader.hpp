//
// Created by berke on 9/26/2026.
//

#ifndef TILKY_ENGINE_MODELLOADER_HPP
#define TILKY_ENGINE_MODELLOADER_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Headers/Math/Matrix/Matrix4.hpp"
#include "Headers/Math/Vector/Vector4.hpp"

// CPU side of static 3D models: Assimp import, texture lookup and dependency
// discovery. Renderer independent - OpenGLModel.cpp uploads the result, the
// asset browser and exporter use the dependency functions.
//
// A model is referenced by its path relative to the project's Assets folder,
// extension included (same as textures), e.g. "Models/crate.glb".
//
// Imported geometry is normalized so the whole model fits a 1x1x1 box with its
// bottom center at the origin. ComponentTransform then works exactly like it
// does for sprites and colliders: position is the model's feet, rotation turns
// it, and scale is its size in world units (the default 32 makes the largest
// side 32 units).
namespace ModelLoader {
    // Interleaved vertex uploaded to the model VBO. Attribute locations 0/1/2
    // in Shaders/Rendering/Rendering.vs.glsl.
    struct ModelVertex {
        float position[3];
        float normal[3];
        float uv[2];
    };

    static_assert(sizeof(ModelVertex) == sizeof(float) * 8);

    struct ModelMesh {
        std::vector<ModelVertex> vertices;
        std::vector<std::uint32_t> indices;
        unsigned materialIndex = 0;
    };

    // Exactly one of filePath / encodedData / rgbaPixels is filled.
    struct ModelTextureSource {
        std::string reference;                  // Path as written in the model file ("*0" for embedded textures)
        std::filesystem::path filePath;         // External texture resolved on disk
        std::vector<unsigned char> encodedData; // Embedded compressed image (png, jpg, ...)
        std::vector<unsigned char> rgbaPixels;  // Embedded uncompressed image, RGBA8
        int width = 0;                          // rgbaPixels only
        int height = 0;                         // rgbaPixels only
    };

    struct ModelMaterial {
        std::string name;
        Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
        int textureIndex = -1; // Into ModelData::textures, -1 = untextured
    };

    // One mesh placed by one node. localTransform already contains the node
    // hierarchy and the unit-box normalization.
    struct ModelDraw {
        unsigned meshIndex = 0;
        Matrix4 localTransform = Matrix4::Identity();
    };

    struct ModelData {
        std::vector<ModelMesh> meshes;
        std::vector<ModelMaterial> materials;
        std::vector<ModelTextureSource> textures;
        std::vector<ModelDraw> draws;
    };

    enum class DependencyKind {
        Texture, // Color texture a material samples
        File     // Any other file the importer opened (.mtl, .bin, ...)
    };

    enum class DependencyStatus {
        Found,
        Embedded,
        Missing
    };

    struct ModelDependency {
        DependencyKind kind = DependencyKind::Texture;
        DependencyStatus status = DependencyStatus::Missing;
        std::string reference;              // As written in the model file
        std::filesystem::path resolvedPath; // Empty unless Found
        std::string materialName;           // Texture dependencies only
    };

    // Lower case, with the leading dot.
    const std::vector<std::string>& SupportedExtensions();
    bool IsModelFile(const std::filesystem::path& path);

    // Absolute path of a model reference inside the current project's Assets.
    std::filesystem::path ResolveModelPath(const std::string& fileName);

    // Finds a texture a model refers to. Tried in order: the path as written
    // when absolute, relative to the model's folder, relative to assetsRoot
    // (skipped when empty), and finally just its file name next to the model.
    // The last one is where the exporter and the asset browser import place
    // textures that lived outside the project. Empty when nothing exists.
    std::filesystem::path ResolveTexturePath(
        const std::filesystem::path& modelPath,
        const std::string& reference,
        const std::filesystem::path& assetsRoot
    );

    std::optional<ModelData> Load(const std::string& fileName, std::string& errorMessage);

    // Lists every color texture and extra file modelPath needs, whether each
    // one exists, and where. Returns false only when the model itself can not
    // be read.
    bool CollectDependencies(
        const std::filesystem::path& modelPath,
        const std::filesystem::path& assetsRoot,
        std::vector<ModelDependency>& outDependencies,
        std::string& errorMessage
    );

    std::string PathToUtf8(const std::filesystem::path& path);
}

#endif //TILKY_ENGINE_MODELLOADER_HPP
