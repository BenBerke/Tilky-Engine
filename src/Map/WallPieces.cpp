#include "Headers/Map/WallPieces.hpp"

#include <algorithm>
#include <cstdint>

#include "Headers/Math/Constants.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Level.hpp"

namespace WallPieces {
    namespace {
        constexpr float MIN_WALL_HEIGHT = 0.0001f;

        // A pure one-sided wall with no sector at all still draws, at this
        // fixed height, so it can be seen and picked.
        constexpr float ORPHAN_WALL_HEIGHT = 32.0f;

        enum class WallSpanSide {
            Front,
            Back
        };

        SectorBounds ComputeSectorBounds(const Sector& sector) {
            SectorBounds bounds;

            for (const Triangle& triangle : sector.triangles) {
                const Vector2 points[3] = {triangle.a, triangle.b, triangle.c};

                for (const Vector2& point : points) {
                    if (!bounds.valid) {
                        bounds.minX = point.x;
                        bounds.maxX = point.x;
                        bounds.minY = point.y;
                        bounds.maxY = point.y;
                        bounds.valid = true;

                        continue;
                    }

                    bounds.minX = std::min(bounds.minX, point.x);
                    bounds.maxX = std::max(bounds.maxX, point.x);
                    bounds.minY = std::min(bounds.minY, point.y);
                    bounds.maxY = std::max(bounds.maxY, point.y);
                }
            }

            return bounds;
        }

        // Mirrors GetSlopeOffset() in Rendering_vs.glsl. slopeStrength is used
        // as a direct height-per-unit gradient here because that is what the
        // shader does
        float GetSlopeOffset(const Vector2& point, const SectorBounds& bounds, const SlopeDirection slopeDirection, const float slopeStrength) {
            if (!bounds.valid || slopeStrength == 0.0f) return 0.0f;

            const float gradient = slopeStrength * Constants::DegToRad;

            switch (slopeDirection) {
                case PLUS_X: return (point.x - bounds.minX) * gradient;
                case MINUS_X: return (bounds.maxX - point.x) * gradient;
                case PLUS_Z: return (point.y - bounds.minY) * gradient;
                case MINUS_Z: return (bounds.maxY - point.y) * gradient;
            }

            return 0.0f;
        }

        float GetSurfaceHeight(const SectorSurface& surface, const SectorBounds& bounds, const Vector2& point) {
            return surface.height + GetSlopeOffset(point, bounds, surface.slopeDirection, surface.slopeStrength);
        }

        struct SectorSample {
            const Sector* sector = nullptr;
            SectorBounds bounds;
        };

        // The three points along the wall we evaluate slopes at. Start and end
        // give the geometry, middle decides the topology (which spans exist).
        struct WallSamplePoints {
            Vector2 start;
            Vector2 middle;
            Vector2 end;
        };

        // Which sector surfaces a height plane came from. Several can share
        // one plane (a step where one sector's ceiling meets the other's
        // floor), so this is a set of bits.
        enum HeightOwner : uint8_t {
            FRONT_FLOOR = 1 << 0,
            FRONT_CEILING = 1 << 1,
            BACK_FLOOR = 1 << 2,
            BACK_CEILING = 1 << 3,
        };

        // One floor or ceiling plane, sampled at each of those three points.
        struct HeightSample {
            float start = 0.0f;
            float middle = 0.0f;
            float end = 0.0f;
            uint8_t owners = 0;
        };

        struct WallSpan {
            HeightSample bottom;
            HeightSample top;
            WallSpanSide side;
        };

        bool IsSectorOpenAtHeight(
            const SectorSample& sample,
            const Vector2& point,
            const float height
        ) {
            if (sample.sector == nullptr) return false;

            for (const SectorFloor& floor: sample.sector->floors) {
                const float floorHeight = GetSurfaceHeight(floor.floor, sample.bounds, point);
                const float ceilingHeight = GetSurfaceHeight(floor.ceiling, sample.bounds, point);

                if (height > floorHeight + MIN_WALL_HEIGHT &&
                    height < ceilingHeight - MIN_WALL_HEIGHT) {
                    return true;
                }
            }

            return false;
        }

        void AddSectorHeights(
            const SectorSample& sample,
            const WallSamplePoints& points,
            const uint8_t floorOwner,
            const uint8_t ceilingOwner,
            std::vector<HeightSample>& heights
        ) {
            if (sample.sector == nullptr) return;

            for (const SectorFloor& floor: sample.sector->floors) {
                for (const SectorSurface* surface: {&floor.floor, &floor.ceiling}) {
                    heights.push_back({
                        GetSurfaceHeight(*surface, sample.bounds, points.start),
                        GetSurfaceHeight(*surface, sample.bounds, points.middle),
                        GetSurfaceHeight(*surface, sample.bounds, points.end),
                        surface == &floor.floor ? floorOwner : ceilingOwner
                    });
                }
            }
        }

        // Two planes only count as the same plane if they agree along the
        // whole wall - equal in the middle but diverging at the ends is a
        // real gap that still needs covering.
        bool SamePlane(const HeightSample& a, const HeightSample& b) {
            return std::abs(a.start - b.start) <= MIN_WALL_HEIGHT &&
                   std::abs(a.middle - b.middle) <= MIN_WALL_HEIGHT &&
                   std::abs(a.end - b.end) <= MIN_WALL_HEIGHT;
        }

        // Duplicates are folded into one plane that keeps every owner.
        void SortAndMergeDuplicateHeights(std::vector<HeightSample>& heights) {
            std::ranges::sort(
                heights,
                [](const HeightSample& a, const HeightSample& b) {
                    return a.middle < b.middle;
                }
            );

            std::vector<HeightSample> merged;
            merged.reserve(heights.size());

            for (const HeightSample& height : heights) {
                if (!merged.empty() && SamePlane(merged.back(), height)) merged.back().owners |= height.owners;
                else merged.push_back(height);
            }

            heights = std::move(merged);
        }

        void PushOrMergeWallSpan(
            std::vector<WallSpan>& spans,
            const HeightSample& bottom,
            const HeightSample& top,
            const WallSpanSide side
        ) {
            if (!spans.empty()) {
                WallSpan& previous = spans.back();

                if (previous.side == side && SamePlane(previous.top, bottom)) {
                    previous.top = top;

                    return;
                }
            }

            spans.push_back({bottom, top, side});
        }

        struct SpanProbe {
            Vector2 point;
            float bottom = 0.0f;
            float top = 0.0f;
        };

        // Slopes can make a slab pinch to nothing at one end while still being
        // open at the other, so the openness test runs where the slab is
        // thickest rather than always at the wall's middle.
        SpanProbe PickThickestProbe(
            const HeightSample& bottom,
            const HeightSample& top,
            const WallSamplePoints& points
        ) {
            const SpanProbe probes[3] = {
                {points.start, bottom.start, top.start},
                {points.middle, bottom.middle, top.middle},
                {points.end, bottom.end, top.end}
            };

            SpanProbe best = probes[0];

            for (int i = 1; i < 3; ++i) {
                if (probes[i].top - probes[i].bottom > best.top - best.bottom) {
                    best = probes[i];
                }
            }

            return best;
        }

        std::vector<WallSpan> BuildWallSpans(
            const SectorSample& frontSector,
            const SectorSample& backSector,
            const WallSamplePoints& points
        ) {
            std::vector<HeightSample> heights;

            AddSectorHeights(frontSector, points, FRONT_FLOOR, FRONT_CEILING, heights);
            AddSectorHeights(backSector, points, BACK_FLOOR, BACK_CEILING, heights);

            SortAndMergeDuplicateHeights(heights);

            std::vector<WallSpan> spans;

            for (size_t i = 0; i + 1 < heights.size(); ++i) {
                const HeightSample& bottom = heights[i];
                const HeightSample& top = heights[i + 1];

                const SpanProbe probe = PickThickestProbe(bottom, top, points);

                if (probe.top - probe.bottom <= MIN_WALL_HEIGHT) continue;

                const float sampleHeight = (probe.bottom + probe.top) * 0.5f;

                const bool frontOpen = IsSectorOpenAtHeight(frontSector, probe.point, sampleHeight);

                const bool backOpen = IsSectorOpenAtHeight(backSector, probe.point, sampleHeight);

                if (frontOpen == backOpen) continue;

                PushOrMergeWallSpan(spans, bottom, top,
                frontOpen ? WallSpanSide::Front : WallSpanSide::Back
                );
            }

            return spans;
        }

        float ResolveAnchorHeight(
            const WallTextureAnchor anchor,
            const WallTextureAnchor autoAnchor,
            const WallPiece& piece
        ) {
            switch (anchor == WallTextureAnchor::Auto ? autoAnchor : anchor) {
                case WallTextureAnchor::BottomEdge: return std::min(piece.bottomStart, piece.bottomEnd);
                case WallTextureAnchor::World: return 0.0f;
                case WallTextureAnchor::TopEdge:
                case WallTextureAnchor::Auto:
                default: return std::max(piece.topStart, piece.topEnd);
            }
        }

        // `top`/`bottom` are the span's edge planes. The sector on the
        // span's hidden side ("the neighbour") decides what the piece is:
        //  - its top edge is the neighbour's floor: the step under that
        //    floor (lower wall). Shows Bottom and follows the floor.
        //  - its bottom edge is the neighbour's ceiling: the wall above that
        //    ceiling (upper wall). Shows Top and follows the ceiling.
        //  - anything else (no neighbour, or a slab between two of the
        //    neighbour's floors) shows Top, anchored at its top edge.
        void PushPiece(
            std::vector<WallPiece>& pieces,
            const Wall& wall,
            const HeightSample& bottom,
            const HeightSample& top,
            const WallSpanSide side,
            const bool hasNeighbour
        ) {
            WallPiece piece;

            piece.bottomStart = bottom.start;
            piece.bottomEnd = bottom.end;

            // Crossing slopes could invert a quad at one end; clamping keeps
            // the piece degenerate there instead of flipping it inside out.
            piece.topStart = std::max(top.start, piece.bottomStart);
            piece.topEnd = std::max(top.end, piece.bottomEnd);

            if (piece.topStart - piece.bottomStart <= MIN_WALL_HEIGHT &&
                piece.topEnd - piece.bottomEnd <= MIN_WALL_HEIGHT) {
                return;
            }

            piece.frontFacing = side == WallSpanSide::Front;

            const uint8_t neighbourFloor = piece.frontFacing ? BACK_FLOOR : FRONT_FLOOR;
            const uint8_t neighbourCeiling = piece.frontFacing ? BACK_CEILING : FRONT_CEILING;

            const bool underNeighbourFloor = hasNeighbour && (top.owners & neighbourFloor) != 0;
            const bool overNeighbourCeiling = hasNeighbour && (bottom.owners & neighbourCeiling) != 0;

            piece.slot = wall.IsPortal() && underNeighbourFloor && !overNeighbourCeiling
                ? WallSurfaceSlot::Bottom
                : WallSurfaceSlot::Top;

            const WallTextureAnchor autoAnchor = overNeighbourCeiling && !underNeighbourFloor
                ? WallTextureAnchor::BottomEdge
                : WallTextureAnchor::TopEdge;

            piece.anchorHeight = ResolveAnchorHeight(wall.Surface(piece.slot).anchor, autoAnchor, piece);

            pieces.push_back(piece);
        }
    }

    const SectorBounds& SectorBoundsCache::Get(const Sector* sector) {
        if (sector == nullptr) return emptyBounds;

        const auto existing = cache.find(sector);

        if (existing != cache.end()) return existing->second;

        return cache.emplace(sector, ComputeSectorBounds(*sector)).first->second;
    }

    void Build(const Level& level, const Wall& wall, SectorBoundsCache& boundsCache, std::vector<WallPiece>& pieces) {
        const Sector* frontSectorPtr = MapQueries::GetSectorByID(level, wall.frontSector);

        const Sector* backSectorPtr = MapQueries::GetSectorByID(level, wall.backSector);

        if (frontSectorPtr == backSectorPtr) backSectorPtr = nullptr;

        const SectorSample frontSector{frontSectorPtr, boundsCache.Get(frontSectorPtr)};

        const SectorSample backSector{backSectorPtr, boundsCache.Get(backSectorPtr)};

        const WallSamplePoints points{
            wall.start,
            Vector2{
                (wall.start.x + wall.end.x) * 0.5f,
                (wall.start.y + wall.end.y) * 0.5f
            },
            wall.end
        };

        const std::vector<WallSpan> spans = BuildWallSpans(frontSector, backSector, points);

        if (spans.empty() && frontSectorPtr == nullptr && backSectorPtr == nullptr) {
            PushPiece(
                pieces,
                wall,
                HeightSample{0.0f, 0.0f, 0.0f},
                HeightSample{ORPHAN_WALL_HEIGHT, ORPHAN_WALL_HEIGHT, ORPHAN_WALL_HEIGHT},
                WallSpanSide::Front,
                false
            );

            return;
        }

        for (const WallSpan& span : spans) {
            const bool hasNeighbour = span.side == WallSpanSide::Front
                ? backSectorPtr != nullptr
                : frontSectorPtr != nullptr;

            PushPiece(pieces, wall, span.bottom, span.top, span.side, hasNeighbour);
        }
    }

    WallSurfaceSlot SlotAtHeight(const Level& level, const Wall& wall, const Vector2& point, const float height) {
        constexpr float TOLERANCE = 0.01f;

        SectorBoundsCache boundsCache;
        std::vector<WallPiece> pieces;

        Build(level, wall, boundsCache, pieces);

        float t = 0.0f;

        if (wall.lengthSq > 0.0f) {
            const Vector2 toPoint = point - wall.start;
            t = std::clamp(Vector2Math::Dot(toPoint, wall.vector) / wall.lengthSq, 0.0f, 1.0f);
        }

        for (const WallPiece& piece : pieces) {
            const float bottom = piece.bottomStart + (piece.bottomEnd - piece.bottomStart) * t;
            const float top = piece.topStart + (piece.topEnd - piece.topStart) * t;

            if (height >= bottom - TOLERANCE && height <= top + TOLERANCE) return piece.slot;
        }

        return WallSurfaceSlot::Top;
    }
}
