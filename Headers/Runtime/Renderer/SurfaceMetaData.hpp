#ifndef TILKY_ENGINE_SURFACEMETADATA_HPP
#define TILKY_ENGINE_SURFACEMETADATA_HPP

#include <cstdint>

#include "Headers/Objects/Wall.hpp"

// Per-surface render flags. The renderer writes them into a spare GPU slot
// (GpuWall.data.w for a wall piece, GpuSectorFloor.textureData.zw for a
// floor/ceiling) and the shader changes how that surface is drawn. Bit
// flags, so one surface can carry several at once.
//
// Keep in sync with the SURFACE_META_* defines in Rendering.fs.glsl.
enum SurfaceMetaData : uint8_t {
    SURFACE_META_NONE    = 0,
    SURFACE_META_HOVERED = 1 << 0, // red tint
};

enum class MarkedSurfaceKind : uint8_t {
    Wall,
    Floor,
    Ceiling
};

// One surface and the flags to give it. Indices, not pointers, since the
// level's vectors can reallocate between being marked and being drawn.
struct SurfaceMark {
    MarkedSurfaceKind kind = MarkedSurfaceKind::Wall;

    int wallIndex = -1;
    WallSurfaceSlot wallSlot = WallSurfaceSlot::Top; // which piece of the wall

    int sectorIndex = -1;
    int floorIndex = -1;

    uint8_t metaData = SURFACE_META_NONE;
};

#endif
