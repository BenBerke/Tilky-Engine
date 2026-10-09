#ifndef TILKY_ENGINE_WALLPIECES_HPP
#define TILKY_ENGINE_WALLPIECES_HPP

#include <unordered_map>
#include <vector>

#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"

struct Level;

// Splits walls into the visible pieces between floor and ceiling planes
// and decides which texture (Top/Bottom) each piece shows and where that
// texture is anchored. The renderer draws these pieces; the runtime editor
// uses them to tell which texture a click landed on.
namespace WallPieces {
    // Axis aligned bounds of a sector's triangulated area, in map XY.
    // This must match GetSectorBounds() in Rendering.vs.glsl exactly -
    // the shader derives slope offsets from the same rectangle, so any
    // difference here shows up as a seam between a sloped flat and the
    // wall that is supposed to close it.
    struct SectorBounds {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool valid = false;
    };

    // Walls are visited once per side, so most sectors get looked up
    // several times per rebuild. Bounds are pure triangle data, so
    // computing them once per sector is enough. Only valid while the
    // level's sectors don't move in memory.
    class SectorBoundsCache {
    public:
        const SectorBounds& Get(const Sector* sector);

    private:
        std::unordered_map<const Sector*, SectorBounds> cache;
        SectorBounds emptyBounds;
    };

    struct WallPiece {
        // Heights at the wall's start and end points.
        float bottomStart = 0.0f;
        float bottomEnd = 0.0f;
        float topStart = 0.0f;
        float topEnd = 0.0f;

        // True when the piece is seen from the front sector.
        bool frontFacing = true;

        WallSurfaceSlot slot = WallSurfaceSlot::Top;

        // World height where the texture's V = 0 sits, already resolved
        // from the surface's anchor setting.
        float anchorHeight = 0.0f;
    };

    // Appends the visible pieces of `wall` to `pieces`.
    void Build(const Level& level, const Wall& wall, SectorBoundsCache& boundsCache, std::vector<WallPiece>& pieces);

    // The texture slot showing at `height` on the wall, at map point `point`
    // (projected onto the wall). Top when no piece covers that height.
    WallSurfaceSlot SlotAtHeight(const Level& level, const Wall& wall, const Vector2& point, float height);
}

#endif //TILKY_ENGINE_WALLPIECES_HPP
