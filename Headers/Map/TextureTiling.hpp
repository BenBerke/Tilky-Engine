#ifndef TILKY_ENGINE_TEXTURETILING_HPP
#define TILKY_ENGINE_TEXTURETILING_HPP

#include <string>

#include "Headers/Math/Vector/Vector2.hpp"

// How big one repeat of a texture is in the world. At scale 1 a texture
// covers its pixel size / the project's texels per unit, so it keeps its
// aspect ratio. Mirrors GetTextureTileSize() in Rendering.vs.glsl.
namespace TextureTiling {
    // World units one repeat covers when a texture is missing or unreadable.
    // Same as FALLBACK_TILE_SIZE in Rendering.vs.glsl.
    inline constexpr float FALLBACK_TILE_SIZE = 32.0f;

    // World size of one repeat of `texture` (a path under Assets) at scale 1.
    // The image size is read once and cached until the file changes.
    Vector2 GetTileSize(const std::string& texture);
}

#endif //TILKY_ENGINE_TEXTURETILING_HPP
