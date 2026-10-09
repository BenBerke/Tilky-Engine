//
// Created by berke on 4/13/2026.
//

#ifndef TILKY_ENGINE_WALL_H
#define TILKY_ENGINE_WALL_H

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "EntityTypes.hpp"
#include "../Math/Vector/Vector2.hpp"
#include "../Math/Vector/Vector2Math.hpp"
#include "../Math/Vector/Vector3.hpp"
#include "../Math/Vector/Vector4.hpp"

// Which world height a wall texture's V = 0 sits at. The texture stays
// pinned there while the piece's edges move, so a moving edge either
// carries the texture with it (the anchored edge) or slides over it.
enum class WallTextureAnchor : int {
    Auto = 0,       // TopEdge, except an upper piece (under the neighbour's ceiling) uses BottomEdge
    TopEdge = 1,
    BottomEdge = 2,
    World = 3,      // world height 0: the texture never moves
};

struct WallTextureAnchorInfo {
    WallTextureAnchor anchor;
    const char* name;     // level files and the Lua WallAnchor table
    const char* labelKey; // editor label (localisation key)
    const char* tooltipKey;
};

inline constexpr std::array WALL_TEXTURE_ANCHORS = {
    WallTextureAnchorInfo{WallTextureAnchor::Auto, "Auto", "wall.anchor.auto", "editor.tooltip.wall.anchor.auto"},
    WallTextureAnchorInfo{WallTextureAnchor::TopEdge, "TopEdge", "wall.anchor.top_edge", "editor.tooltip.wall.anchor.top_edge"},
    WallTextureAnchorInfo{WallTextureAnchor::BottomEdge, "BottomEdge", "wall.anchor.bottom_edge", "editor.tooltip.wall.anchor.bottom_edge"},
    WallTextureAnchorInfo{WallTextureAnchor::World, "World", "wall.anchor.world", "editor.tooltip.wall.anchor.world"},
};

inline const WallTextureAnchorInfo* FindWallTextureAnchor(const WallTextureAnchor anchor) {
    for (const WallTextureAnchorInfo& info : WALL_TEXTURE_ANCHORS) if (info.anchor == anchor) return &info;
    return nullptr;
}

inline std::optional<WallTextureAnchor> FindWallTextureAnchorByName(const std::string_view name) {
    for (const WallTextureAnchorInfo& info : WALL_TEXTURE_ANCHORS) if (name == info.name) return info.anchor;
    return std::nullopt;
}

inline std::optional<WallTextureAnchor> WallTextureAnchorFromInt(const int value) {
    for (const WallTextureAnchorInfo& info : WALL_TEXTURE_ANCHORS) if (static_cast<int>(info.anchor) == value) return info.anchor;
    return std::nullopt;
}

// One texture and its placement. A wall has two: Top covers a solid wall
// and, on a portal, everything except the step under the neighbour's
// floor, which uses Bottom.
struct WallSurface {
    std::string texture;
    Vector2 textureOffset = {0.0f, 0.0f};
    Vector2 textureScale = {1.0f, 1.0f};
    bool flipTextureX = false;
    bool flipTextureY = false;
    WallTextureAnchor anchor = WallTextureAnchor::Auto;
};

enum class WallSurfaceSlot {
    Top,
    Bottom
};

struct Wall {
    ID id = INVALID_ID;

    std::string name;

    std::vector<std::string> tags;
    std::vector<uint16_t> tagIds;

    Vector2 start, end;
    Vector4 color;

    WallSurface top;
    WallSurface bottom;

    // Stable ID of the sector that is to the front or to the left of the Wall in top down view
    // Should be -1 if there is no sector
    ID frontSector  = INVALID_ID;

    // Stable ID of the sector that is to the back or to the right of the Wall in top down view
    // Should be -1 if there is no sector
    ID backSector   = INVALID_ID;

    // Read only — do not change
    Vector2 dir, normal, vector;
    float lengthSq; // lengthSquared
    float length;

    std::array<std::array<Vector3, 4>, 2> quads3D = {};
    int quad3DCount = 0;

    // Per-quad 3D AABBs for physics early-out.
    // Filled by RebuildQuadAabbs() whenever quads3D is written.
    std::array<Vector3, 2> quadAabbMin = {};
    std::array<Vector3, 2> quadAabbMax = {};

    //quads3D[0] // first quad
    //quads3D[1] // second quad

    // quad[0] = bottomStart
    // quad[1] = bottomEnd
    // quad[2] = topEnd
    // quad[3] = topStart

    Wall(
    const Vector2& start,
    const Vector2& end,
    const Vector4 color,
    const ID fs = INVALID_ID,
    const ID bs = INVALID_ID,
    std::string topTexture = {},
    const int floor = 0 // Currently unused
    )
    : start(start),
      end(end),
      color(color),
      frontSector(fs),
      backSector(bs)
    {
        top.texture = std::move(topTexture);
        RefreshDerived();
    }

    // A sector on both sides: the wall can show a Bottom texture too.
    [[nodiscard]] bool IsPortal() const {
        return frontSector != INVALID_ID && backSector != INVALID_ID && frontSector != backSector;
    }

    [[nodiscard]] WallSurface& Surface(const WallSurfaceSlot slot) {
        return slot == WallSurfaceSlot::Bottom ? bottom : top;
    }

    [[nodiscard]] const WallSurface& Surface(const WallSurfaceSlot slot) const {
        return slot == WallSurfaceSlot::Bottom ? bottom : top;
    }

    // Recomputes dir/normal/vector/lengthSq/length from the current
    // start/end. The constructor calls this itself; call it again after
    // directly mutating start or end (e.g. MapTopology splitting a wall
    // during a topology rebuild) so those fields don't go stale - this
    // is the only place that math lives, so it can't drift between the
    // two call sites.
    void RefreshDerived() {
        vector = end - start;
        lengthSq = Vector2Math::Dot(vector, vector);
        length = std::sqrt(lengthSq);

        if (length > Constants::Epsilon) {
            dir = vector / length;
            normal = {-dir.y, dir.x};
        }
        else {
            dir = {0.0f, 0.0f};
            normal = {0.0f, 0.0f};
        }
    }

    void RebuildQuadAabbs() {
        for (int i = 0; i < quad3DCount; ++i) {
            const auto& q = quads3D[i];
            quadAabbMin[i] = {
                std::min({ q[0].x, q[1].x, q[2].x, q[3].x }),
                std::min({ q[0].y, q[1].y, q[2].y, q[3].y }),
                std::min({ q[0].z, q[1].z, q[2].z, q[3].z })
            };
            quadAabbMax[i] = {
                std::max({ q[0].x, q[1].x, q[2].x, q[3].x }),
                std::max({ q[0].y, q[1].y, q[2].y, q[3].y }),
                std::max({ q[0].z, q[1].z, q[2].z, q[3].z })
            };
        }
    }
};

#endif //TILKY_ENGINE_WALL_H