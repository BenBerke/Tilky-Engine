//
// Created by berke on 9/26/2026.
//

#include "Headers/Runtime/Renderer/ModelLoader.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <spdlog/spdlog.h>

#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace {
    fs::path Utf8ToPath(const std::string& text) {
        return fs::path(std::u8string(text.begin(), text.end()));
    }

    std::string LowerCopy(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return text;
    }

    // Records every file the importer opens besides the model itself, so .mtl
    // libraries, .gltf buffers and similar side files can be reported and
    // exported. Assimp does not expose that list any other way.
    class RecordingIOSystem final : public Assimp::DefaultIOSystem {
    public:
        explicit RecordingIOSystem(fs::path modelPath) : modelPath(std::move(modelPath)) {}

        Assimp::IOStream* Open(const char* file, const char* mode) override {
            Assimp::IOStream* stream = DefaultIOSystem::Open(file, mode);

            const fs::path path = Utf8ToPath(file).lexically_normal();

            if (!IsModel(path)) {
                if (stream != nullptr) opened.push_back(path);
                else missing.push_back(path);
            }

            return stream;
        }

        std::vector<fs::path> opened;
        std::vector<fs::path> missing;

    private:
        fs::path modelPath;

        [[nodiscard]] bool IsModel(const fs::path& path) const {
            std::error_code ec;
            if (fs::equivalent(path, modelPath, ec)) return true;

            return path == modelPath;
        }
    };

    // The one texture slot the renderer samples. glTF puts it in BASE_COLOR,
    // every other format in DIFFUSE.
    bool FindColorTexture(const aiMaterial& material, aiString& outReference) {
        for (const aiTextureType type : {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}) {
            if (material.GetTextureCount(type) == 0) continue;

            if (material.GetTexture(type, 0, &outReference) == AI_SUCCESS && outReference.length > 0) return true;
        }

        return false;
    }

    Vector4 GetBaseColor(const aiMaterial& material) {
        aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);

        if (material.Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS) return {color.r, color.g, color.b, color.a};

        if (material.Get(AI_MATKEY_COLOR_DIFFUSE, color) != AI_SUCCESS) color = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);

        float opacity = 1.0f;
        if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) color.a *= opacity;

        return {color.r, color.g, color.b, color.a};
    }

    Matrix4 ToMatrix4(const aiMatrix4x4& matrix) {
        Matrix4 result;

        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                result.m[row][column] = matrix[row][column];

        return result;
    }

    void TransformPoint(const Matrix4& matrix, const aiVector3D& point, float out[3]) {
        for (int row = 0; row < 3; ++row) {
            out[row] = matrix.m[row][0] * point.x +
                       matrix.m[row][1] * point.y +
                       matrix.m[row][2] * point.z +
                       matrix.m[row][3];
        }
    }

    void CollectDraws(
        const aiNode& node,
        const Matrix4& parentTransform,
        const std::vector<int>& meshRemap,
        std::vector<ModelLoader::ModelDraw>& outDraws
    ) {
        const Matrix4 globalTransform = parentTransform * ToMatrix4(node.mTransformation);

        for (unsigned i = 0; i < node.mNumMeshes; ++i) {
            const int meshIndex = meshRemap[node.mMeshes[i]];

            if (meshIndex < 0) continue;

            outDraws.push_back({static_cast<unsigned>(meshIndex), globalTransform});
        }

        for (unsigned i = 0; i < node.mNumChildren; ++i)
            CollectDraws(*node.mChildren[i], globalTransform, meshRemap, outDraws);
    }

    // Scales the model uniformly so its largest side is 1 and moves its bottom
    // center to the origin - see the comment on the ModelLoader namespace.
    void NormalizeToUnitBox(const aiScene& scene, const std::vector<int>& meshRemap, std::vector<ModelLoader::ModelDraw>& draws) {
        constexpr float MAX_FLOAT = std::numeric_limits<float>::max();

        float minimum[3] = {MAX_FLOAT, MAX_FLOAT, MAX_FLOAT};
        float maximum[3] = {-MAX_FLOAT, -MAX_FLOAT, -MAX_FLOAT};

        std::vector<unsigned> sourceMeshByIndex(scene.mNumMeshes, 0);

        for (unsigned source = 0; source < scene.mNumMeshes; ++source)
            if (meshRemap[source] >= 0) sourceMeshByIndex[meshRemap[source]] = source;

        for (const ModelLoader::ModelDraw& draw : draws) {
            const aiMesh& mesh = *scene.mMeshes[sourceMeshByIndex[draw.meshIndex]];

            for (unsigned v = 0; v < mesh.mNumVertices; ++v) {
                float point[3];
                TransformPoint(draw.localTransform, mesh.mVertices[v], point);

                for (int axis = 0; axis < 3; ++axis) {
                    minimum[axis] = std::min(minimum[axis], point[axis]);
                    maximum[axis] = std::max(maximum[axis], point[axis]);
                }
            }
        }

        if (minimum[0] > maximum[0]) return;

        const float largestSide = std::max({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2]});
        const float scale = largestSide > Constants::Epsilon ? 1.0f / largestSide : 1.0f;

        Matrix4 normalization = Matrix4::Identity();
        normalization.m[0][0] = scale;
        normalization.m[1][1] = scale;
        normalization.m[2][2] = scale;
        normalization.m[0][3] = -scale * (minimum[0] + maximum[0]) * 0.5f;
        normalization.m[1][3] = -scale * minimum[1];
        normalization.m[2][3] = -scale * (minimum[2] + maximum[2]) * 0.5f;

        for (ModelLoader::ModelDraw& draw : draws) draw.localTransform = normalization * draw.localTransform;
    }

    int AddTextureSource(
        const aiScene& scene,
        const fs::path& modelPath,
        const std::string& reference,
        const std::string& materialName,
        std::vector<ModelLoader::ModelTextureSource>& textures,
        std::unordered_map<std::string, int>& textureIndexByReference
    ) {
        if (const auto existing = textureIndexByReference.find(reference); existing != textureIndexByReference.end())
            return existing->second;

        ModelLoader::ModelTextureSource source;
        source.reference = reference;

        if (const aiTexture* embedded = scene.GetEmbeddedTexture(reference.c_str())) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(embedded->pcData);

            // mHeight == 0 means pcData is a compressed file of mWidth bytes.
            if (embedded->mHeight == 0) source.encodedData.assign(bytes, bytes + embedded->mWidth);
            else {
                source.width = static_cast<int>(embedded->mWidth);
                source.height = static_cast<int>(embedded->mHeight);
                source.rgbaPixels.reserve(static_cast<size_t>(source.width) * source.height * 4);

                for (unsigned i = 0; i < embedded->mWidth * embedded->mHeight; ++i) {
                    const aiTexel& texel = embedded->pcData[i];
                    source.rgbaPixels.insert(source.rgbaPixels.end(), {texel.r, texel.g, texel.b, texel.a});
                }
            }
        }
        else {
            source.filePath = ModelLoader::ResolveTexturePath(modelPath, reference, ProjectManager::GetAssetsPath());

            if (source.filePath.empty()) {
                spdlog::warn(
                    "Model '{}': texture '{}' used by material '{}' was not found. The material renders untextured",
                    ModelLoader::PathToUtf8(modelPath), reference, materialName
                );
                textureIndexByReference.emplace(reference, -1);
                return -1;
            }
        }

        textures.push_back(std::move(source));

        const int index = static_cast<int>(textures.size()) - 1;
        textureIndexByReference.emplace(reference, index);

        return index;
    }
}

namespace ModelLoader {
    const std::vector<std::string>& SupportedExtensions() {
        static const std::vector<std::string> extensions = {
            ".obj", ".fbx", ".gltf", ".glb", ".dae", ".3ds", ".ply", ".stl"
        };

        return extensions;
    }

    bool IsModelFile(const fs::path& path) {
        const std::string extension = LowerCopy(path.extension().string());

        return std::ranges::find(SupportedExtensions(), extension) != SupportedExtensions().end();
    }

    std::string PathToUtf8(const fs::path& path) {
        const std::u8string text = path.u8string();

        return {text.begin(), text.end()};
    }

    fs::path ResolveModelPath(const std::string& fileName) {
        return (ProjectManager::GetAssetsPath() / Utf8ToPath(fileName)).lexically_normal();
    }

    fs::path ResolveTexturePath(const fs::path& modelPath, const std::string& reference, const fs::path& assetsRoot) {
        std::string normalized = reference;
        std::ranges::replace(normalized, '\\', '/');

        if (normalized.empty()) return {};

        const fs::path raw = Utf8ToPath(normalized);
        const fs::path modelDirectory = modelPath.parent_path();

        std::vector<fs::path> candidates;

        if (raw.is_absolute()) candidates.push_back(raw);
        else {
            candidates.push_back(modelDirectory / raw);
            if (!assetsRoot.empty()) candidates.push_back(assetsRoot / raw);
        }

        candidates.push_back(modelDirectory / raw.filename());

        for (const fs::path& candidate : candidates) {
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return candidate.lexically_normal();
        }

        return {};
    }

    std::optional<ModelData> Load(const std::string& fileName, std::string& errorMessage) {
        const fs::path modelPath = ResolveModelPath(fileName);

        std::error_code ec;
        if (!fs::is_regular_file(modelPath, ec)) {
            errorMessage = "Model file does not exist: " + PathToUtf8(modelPath);
            return std::nullopt;
        }

        Assimp::Importer importer;

        // Points and lines can not be drawn as triangles; drop them.
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

        // FlipUVs: textures are uploaded top row first, so v = 0 is the top.
        constexpr unsigned IMPORT_FLAGS =
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenSmoothNormals |
            aiProcess_SortByPType |
            aiProcess_FlipUVs |
            aiProcess_GenUVCoords |
            aiProcess_TransformUVCoords |
            aiProcess_ImproveCacheLocality |
            aiProcess_RemoveRedundantMaterials |
            aiProcess_FindInvalidData |
            aiProcess_ValidateDataStructure;

        const aiScene* scene = importer.ReadFile(PathToUtf8(modelPath), IMPORT_FLAGS);

        if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
            errorMessage = "Assimp could not import " + PathToUtf8(modelPath) + ": " + importer.GetErrorString();
            return std::nullopt;
        }

        ModelData model;

        std::unordered_map<std::string, int> textureIndexByReference;

        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            const aiMaterial& source = *scene->mMaterials[i];

            ModelMaterial material;
            material.name = source.GetName().C_Str();
            material.baseColor = GetBaseColor(source);

            if (aiString reference; FindColorTexture(source, reference)) {
                material.textureIndex = AddTextureSource(
                    *scene, modelPath, reference.C_Str(), material.name, model.textures, textureIndexByReference
                );
            }

            model.materials.push_back(std::move(material));
        }

        if (model.materials.empty()) model.materials.emplace_back();

        // Index in scene->mMeshes -> index in model.meshes, -1 if skipped.
        std::vector<int> meshRemap(scene->mNumMeshes, -1);

        for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
            const aiMesh& source = *scene->mMeshes[i];

            if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 || source.mNumVertices == 0) continue;

            ModelMesh mesh;
            mesh.materialIndex = source.mMaterialIndex < model.materials.size() ? source.mMaterialIndex : 0;

            mesh.vertices.resize(source.mNumVertices);

            for (unsigned v = 0; v < source.mNumVertices; ++v) {
                ModelVertex& vertex = mesh.vertices[v];

                vertex.position[0] = source.mVertices[v].x;
                vertex.position[1] = source.mVertices[v].y;
                vertex.position[2] = source.mVertices[v].z;

                if (source.HasNormals()) {
                    vertex.normal[0] = source.mNormals[v].x;
                    vertex.normal[1] = source.mNormals[v].y;
                    vertex.normal[2] = source.mNormals[v].z;
                }
                else vertex.normal[0] = vertex.normal[1] = vertex.normal[2] = 0.0f;

                if (source.HasTextureCoords(0)) {
                    vertex.uv[0] = source.mTextureCoords[0][v].x;
                    vertex.uv[1] = source.mTextureCoords[0][v].y;
                }
                else vertex.uv[0] = vertex.uv[1] = 0.0f;
            }

            mesh.indices.reserve(static_cast<size_t>(source.mNumFaces) * 3);

            for (unsigned f = 0; f < source.mNumFaces; ++f) {
                const aiFace& face = source.mFaces[f];

                if (face.mNumIndices != 3) continue;

                mesh.indices.insert(mesh.indices.end(), {face.mIndices[0], face.mIndices[1], face.mIndices[2]});
            }

            if (mesh.indices.empty()) continue;

            meshRemap[i] = static_cast<int>(model.meshes.size());
            model.meshes.push_back(std::move(mesh));
        }

        CollectDraws(*scene->mRootNode, Matrix4::Identity(), meshRemap, model.draws);

        if (model.draws.empty()) {
            errorMessage = "Model " + PathToUtf8(modelPath) + " contains no triangle meshes";
            return std::nullopt;
        }

        NormalizeToUnitBox(*scene, meshRemap, model.draws);

        return model;
    }

    bool CollectDependencies(
        const fs::path& modelPath,
        const fs::path& assetsRoot,
        std::vector<ModelDependency>& outDependencies,
        std::string& errorMessage
    ) {
        outDependencies.clear();

        std::error_code ec;
        if (!fs::is_regular_file(modelPath, ec)) {
            errorMessage = "Model file does not exist: " + PathToUtf8(modelPath);
            return false;
        }

        Assimp::Importer importer;

        // The importer owns the IO system; the pointer stays valid until the importer is destroyed.
        auto* ioSystem = new RecordingIOSystem(modelPath.lexically_normal());
        importer.SetIOHandler(ioSystem);

        const aiScene* scene = importer.ReadFile(PathToUtf8(modelPath), 0);

        if (scene == nullptr) {
            errorMessage = "Assimp could not import " + PathToUtf8(modelPath) + ": " + importer.GetErrorString();
            return false;
        }

        std::unordered_set<std::string> seen;

        for (const fs::path& path : ioSystem->opened) {
            if (!seen.insert("file:" + PathToUtf8(path)).second) continue;

            outDependencies.push_back({DependencyKind::File, DependencyStatus::Found, PathToUtf8(path.filename()), path, {}});
        }

        for (const fs::path& path : ioSystem->missing) {
            if (!seen.insert("file:" + PathToUtf8(path)).second) continue;

            outDependencies.push_back({DependencyKind::File, DependencyStatus::Missing, PathToUtf8(path.filename()), {}, {}});
        }

        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            const aiMaterial& material = *scene->mMaterials[i];

            aiString reference;
            if (!FindColorTexture(material, reference)) continue;

            const std::string referenceText = reference.C_Str();

            if (!seen.insert("texture:" + referenceText).second) continue;

            ModelDependency dependency;
            dependency.kind = DependencyKind::Texture;
            dependency.reference = referenceText;
            dependency.materialName = material.GetName().C_Str();

            if (scene->GetEmbeddedTexture(reference.C_Str()) != nullptr) dependency.status = DependencyStatus::Embedded;
            else {
                dependency.resolvedPath = ResolveTexturePath(modelPath, referenceText, assetsRoot);
                dependency.status = dependency.resolvedPath.empty() ? DependencyStatus::Missing : DependencyStatus::Found;
            }

            outDependencies.push_back(std::move(dependency));
        }

        return true;
    }
}
