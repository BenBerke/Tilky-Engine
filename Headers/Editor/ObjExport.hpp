#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Headers/Objects/Types.hpp"

struct Level;

// Writes a level's geometry as a Wavefront .obj with a .mtl beside it: every
// floor, ceiling and wall piece, with the UVs the renderer gives them. The
// textures used are copied into a folder next to the files, so the export
// opens anywhere. Entities are left out.
namespace ObjExport {
    struct Result {
        bool ok = false;
        std::string error;
        int faceCount = 0;
        int textureCount = 0;
    };

    // `sectorIDs` limits the export to those sectors and the walls around
    // them; empty exports the whole level. `objPath` gets a .obj extension
    // when it has none.
    Result Export(const Level& level, const std::vector<ID>& sectorIDs, std::filesystem::path objPath);
}
