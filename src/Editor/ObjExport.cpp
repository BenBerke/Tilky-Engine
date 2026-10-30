#include "Headers/Editor/ObjExport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <system_error>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Map/TextureTiling.hpp"
#include "Headers/Map/WallPieces.hpp"
#include "Headers/Math/Vector/Vector3Math.hpp"
#include "Headers/Objects/Level.hpp"
#include "Headers/Project/ProjectManager.hpp"

namespace fs = std::filesystem;

namespace ObjExport {
    namespace {
        struct Vertex {
            Vector3 position;
            Vector2 uv; // OBJ's, V up
        };

        struct Face {
            Vertex a, b, c;
        };

        // Faces grouped by the texture they show ("" = none), so each
        // material is one block of the .obj.
        using FacesByTexture = std::map<std::string, std::vector<Face>>;

        // A material name OBJ readers accept: no spaces or slashes.
        std::string MaterialName(const std::string& texture) {
            if (texture.empty()) return "untextured";

            std::string name = texture;
            for (char& c : name)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') c = '_';
            return name;
        }

        // Turns the face so its normal points along `wanted`.
        Face Facing(Face face, const Vector3& wanted) {
            const Vector3 normal = Vector3Math::Cross(face.b.position - face.a.position, face.c.position - face.a.position);
            if (Vector3Math::Dot(normal, wanted) < 0.0f) std::swap(face.b, face.c);
            return face;
        }

        // The floor or ceiling's UV at map point `point`, as the shader works
        // it out: scale, flip, then offset, in repeats of the texture.
        Vector2 FlatUV(const SectorSurface& surface, const Vector2& point) {
            const Vector2 tile = TextureTiling::GetTileSize(surface.texture);

            Vector2 uv = {point.x / tile.x * surface.textureScale.x, point.y / tile.y * surface.textureScale.y};
            if (surface.flipTextureX) uv.x = -uv.x;
            if (surface.flipTextureY) uv.y = -uv.y;

            uv = {uv.x + surface.textureOffset.x / tile.x, uv.y + surface.textureOffset.y / tile.y};
            return {uv.x, -uv.y};
        }

        void AddFlats(const Sector& sector, FacesByTexture& faces) {
            for (const SectorFloor& interval : sector.floors) {
                for (const bool ceiling : {false, true}) {
                    const SectorSurface& surface = ceiling ? interval.ceiling : interval.floor;

                    const auto vertex = [&](const Vector2& point) {
                        return Vertex{{point.x, MapQueries::SurfaceHeightAt(sector, surface, point), point.y}, FlatUV(surface, point)};
                    };

                    for (const Triangle& triangle : sector.triangles) {
                        const Face face = {vertex(triangle.a), vertex(triangle.b), vertex(triangle.c)};
                        faces[surface.texture].push_back(Facing(face, {0.0f, ceiling ? -1.0f : 1.0f, 0.0f}));
                    }
                }
            }
        }

        // The wall's UV at distance `along` from its start and world height
        // `height`, as the shader works it out.
        Vector2 WallUV(const WallSurface& surface, const WallPieces::WallPiece& piece, const float along, const float height) {
            const Vector2 tile = TextureTiling::GetTileSize(surface.texture);

            Vector2 uv = {along / tile.x * surface.textureScale.x, (piece.anchorHeight - height) / tile.y * surface.textureScale.y};
            if (surface.flipTextureX) uv.x = -uv.x;
            if (surface.flipTextureY) uv.y = -uv.y;

            uv = {uv.x + surface.textureOffset.x / tile.x, uv.y + surface.textureOffset.y / tile.y};
            return {uv.x, -uv.y};
        }

        void AddWall(const Level& level, const Wall& wall, WallPieces::SectorBoundsCache& boundsCache, FacesByTexture& faces) {
            std::vector<WallPieces::WallPiece> pieces;
            WallPieces::Build(level, wall, boundsCache, pieces);

            const Vector2 direction = wall.end - wall.start;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            if (length <= 0.0f) return;

            // The side of the wall a piece faces: its sector's side.
            const Vector2 normal = {-direction.y / length, direction.x / length};
            const Vector2 middle = (wall.start + wall.end) * 0.5f;
            const int leftSector = MapQueries::FindSectorContainingPoint(level.sectors, middle + normal * 0.01f);

            for (const WallPieces::WallPiece& piece : pieces) {
                const WallSurface& surface = wall.Surface(piece.slot);
                if (piece.slot == WallSurfaceSlot::Middle && surface.texture.empty()) continue;

                const Vector2 start = piece.Start(wall);
                const Vector2 end = piece.End(wall);
                const float startAlong = piece.startT * wall.length;
                const float endAlong = piece.endT * wall.length;

                const Vertex bottomStart = {{start.x, piece.bottomStart, start.y}, WallUV(surface, piece, startAlong, piece.bottomStart)};
                const Vertex topStart = {{start.x, piece.topStart, start.y}, WallUV(surface, piece, startAlong, piece.topStart)};
                const Vertex bottomEnd = {{end.x, piece.bottomEnd, end.y}, WallUV(surface, piece, endAlong, piece.bottomEnd)};
                const Vertex topEnd = {{end.x, piece.topEnd, end.y}, WallUV(surface, piece, endAlong, piece.topEnd)};

                const bool facesLeft = piece.facingSector != nullptr && leftSector >= 0 && &level.sectors[leftSector] == piece.facingSector;
                const Vector3 wanted = facesLeft ? Vector3{normal.x, 0.0f, normal.y} : Vector3{-normal.x, 0.0f, -normal.y};

                faces[surface.texture].push_back(Facing({bottomStart, topStart, bottomEnd}, wanted));
                faces[surface.texture].push_back(Facing({bottomEnd, topStart, topEnd}, wanted));
            }
        }
    }

    Result Export(const Level& level, const std::vector<ID>& sectorIDs, fs::path objPath) {
        Result result;

        if (!objPath.has_extension()) objPath.replace_extension(".obj");

        const auto included = [&sectorIDs](const ID sectorID) {
            return sectorIDs.empty() || std::ranges::find(sectorIDs, sectorID) != sectorIDs.end();
        };

        FacesByTexture faces;

        for (const Sector& sector : level.sectors)
            if (included(sector.id)) AddFlats(sector, faces);

        WallPieces::SectorBoundsCache boundsCache;

        for (const Wall& wall : level.walls) {
            const bool touches = (wall.frontSector != INVALID_ID && included(wall.frontSector)) ||
                                 (wall.backSector != INVALID_ID && included(wall.backSector));
            if (touches) AddWall(level, wall, boundsCache, faces);
        }

        // ---- Textures, next to the files.
        const std::string stem = objPath.stem().string();
        const fs::path textureFolder = objPath.parent_path() / (stem + "_textures");
        std::map<std::string, std::string> textureFiles; // texture -> path relative to the .mtl

        std::error_code ec;

        for (const auto& [texture, textureFaces] : faces) {
            if (texture.empty() || textureFaces.empty()) continue;

            const fs::path source = ProjectManager::GetAssetsPath() / texture;
            if (!fs::exists(source, ec)) continue;

            const std::string fileName = MaterialName(texture) + source.extension().string();
            fs::create_directories(textureFolder, ec);
            fs::copy_file(source, textureFolder / fileName, fs::copy_options::overwrite_existing, ec);

            if (!ec) {
                textureFiles[texture] = stem + "_textures/" + fileName;
                ++result.textureCount;
            }
        }

        // ---- .mtl
        fs::path mtlPath = objPath;
        mtlPath.replace_extension(".mtl");

        std::ofstream mtl(mtlPath);
        if (!mtl) {
            result.error = "Could not write " + mtlPath.string();
            return result;
        }

        for (const auto& [texture, textureFaces] : faces) {
            if (textureFaces.empty()) continue;

            mtl << "newmtl " << MaterialName(texture) << "\n";
            mtl << "Kd 1 1 1\n";
            if (const auto it = textureFiles.find(texture); it != textureFiles.end()) mtl << "map_Kd " << it->second << "\n";
            mtl << "\n";
        }

        // ---- .obj
        std::ofstream obj(objPath);
        if (!obj) {
            result.error = "Could not write " + objPath.string();
            return result;
        }

        obj << "# " << level.name << ", exported from Tilky\n";
        obj << "mtllib " << mtlPath.filename().string() << "\n";

        int vertexIndex = 1;

        for (const auto& [texture, textureFaces] : faces) {
            if (textureFaces.empty()) continue;

            obj << "\nusemtl " << MaterialName(texture) << "\n";

            for (const Face& face : textureFaces) {
                for (const Vertex* vertex : {&face.a, &face.b, &face.c}) {
                    obj << "v " << vertex->position.x << " " << vertex->position.y << " " << vertex->position.z << "\n";
                    obj << "vt " << vertex->uv.x << " " << vertex->uv.y << "\n";
                }

                obj << "f " << vertexIndex << "/" << vertexIndex << " "
                    << vertexIndex + 1 << "/" << vertexIndex + 1 << " "
                    << vertexIndex + 2 << "/" << vertexIndex + 2 << "\n";

                vertexIndex += 3;
                ++result.faceCount;
            }
        }

        result.ok = true;
        return result;
    }
}
