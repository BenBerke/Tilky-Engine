#include "Headers/Map/MapTopology.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"

// ============================================================================
// Algorithm overview (stage numbers match the design doc this was built
// from):
//
//   2. Canonicalize   - epsilon predicates; CleanDrawnPoints dedupes the
//                        input and IsDrawnChainSelfIntersecting rejects
//                        self-crossing input up front.
//   3. Planarize       - InsertDrawnVerticesAsGraphVertices splits any
//                        existing wall a drawn point lands inside;
//                        PlanarizeDrawnSegment then walks each drawn
//                        segment, splitting/reusing existing walls at
//                        every crossing or collinear-overlap boundary,
//                        and creates or reuses a wall for every
//                        resulting sub-piece.
//   4/5. Trace faces   - BuildHalfEdges + ComputeNextPointers build a
//                        transient directed half-edge graph over *every*
//                        wall in the level (not just the new ones);
//                        TraceBoundedFaces walks it once to recover
//                        every enclosed, positively-wound face.
//   1/6. Reconcile     - SnapshotSectors captures old sector identity
//                        before any of the above runs; ReconcileFaces
//                        matches each traced face back to the old sector
//                        it's a subset of (or marks it brand new).
//   7. Rebuild         - BuildFinalSectors and AssignAllWallSides turn
//                        the reconciled faces into real Sector/Wall
//                        data; the public ApplyDrawnGeometry commits it
//                        and calls MapQueries::RebuildSectorRuntimeLinks.
//
// Deliberate scope limits, matching the current Sector model (a single
// simple polygon, no holes): a sector-with-a-hole ("donut") shape is not
// supported, and a wall that only ever borders one face on both its
// sides (a dangling spur with nothing on the far end) never becomes part
// of a sector - it just sits there as ordinary open geometry, exactly
// like a partially-drawn chain does today.
// ============================================================================

namespace MapTopology {
    bool PointsEquivalent(const Vector2& a, const Vector2& b, const float epsilon) {
        return Vector2Math::DistanceSquared(a, b) <= epsilon * epsilon;
    }

    bool PointOnSegmentInterior(const Vector2& p, const Vector2& a, const Vector2& b, const float epsilon) {
        const Vector2 ab = b - a;
        const float lenSq = Vector2Math::Dot(ab, ab);
        if (lenSq <= epsilon * epsilon) return false;

        const float len = std::sqrt(lenSq);
        const float t = Vector2Math::Dot(p - a, ab) / lenSq;
        const float worldPosAlong = t * len;

        if (worldPosAlong <= epsilon || worldPosAlong >= len - epsilon) return false;

        const float perpDist = std::abs(Vector2Math::Cross(ab, p - a)) / len;
        return perpDist <= epsilon;
    }

    Vector2 ClosestPointOnSegment(const Vector2& p, const Vector2& a, const Vector2& b) {
        const Vector2 ab = b - a;
        const float lenSq = Vector2Math::Dot(ab, ab);
        if (lenSq <= 0.0000001f) return a;

        const float t = std::clamp(Vector2Math::Dot(p - a, ab) / lenSq, 0.0f, 1.0f);
        return { a.x + ab.x * t, a.y + ab.y * t };
    }

    bool ClosestPointOnAnyWall(
        const std::vector<Wall>& walls,
        const Vector2& point,
        const float maxDistance,
        Vector2* outPoint
    ) {
        bool found = false;
        float bestDistSq = maxDistance * maxDistance;

        for (const Wall& wall : walls) {
            const Vector2 ab = wall.end - wall.start;
            const float lenSq = Vector2Math::Dot(ab, ab);
            if (lenSq <= kEpsilon * kEpsilon) continue; // degenerate wall guard

            // Reject candidates that clamp onto (or past) an endpoint -
            // this function is specifically for the wall's *interior*,
            // deliberately using the segment's own parametric range here
            // rather than PointOnSegmentInterior's tight topology
            // epsilon, which would wrongly require `point` itself to
            // already be sitting almost exactly on the wall's line.
            const float t = Vector2Math::Dot(point - wall.start, ab) / lenSq;
            if (t <= 0.0f || t >= 1.0f) continue;

            const Vector2 candidate = ClosestPointOnSegment(point, wall.start, wall.end);
            const float distSq = Vector2Math::DistanceSquared(point, candidate);

            if (distSq <= bestDistSq) {
                bestDistSq = distSq;
                *outPoint = candidate;
                found = true;
            }
        }

        return found;
    }
}

namespace {
    using namespace MapTopology;

    constexpr float kMinFaceArea = 1.0f;

    // ---- Stage 2: parametric helpers built on the predicates above ----------

    float ProjectParam(const Vector2& p, const Vector2& a, const Vector2& b) {
        const Vector2 ab = b - a;
        const float lenSq = Vector2Math::Dot(ab, ab);
        if (lenSq <= 0.0000001f) return 0.0f;
        return Vector2Math::Dot(p - a, ab) / lenSq;
    }

    bool ApproximatelyKnownParam(const std::vector<float>& knownParams, const float t, const float segmentLength) {
        return std::ranges::any_of(knownParams, [&](const float known) {
            return std::abs(known - t) * segmentLength <= kEpsilon;
        });
    }

    enum class IntersectKind { None, Point, Collinear };

    struct SegmentIntersection {
        IntersectKind kind = IntersectKind::None;
        Vector2 point{};
        Vector2 overlapStart{};
        Vector2 overlapEnd{};
    };

    // Standard parametric segment/segment intersection (p->p2, q->q2),
    // classified as a single point, a collinear overlap, or none.
    // Segment endpoints touching (rather than crossing) still count as
    // a Point result - callers decide what, if anything, needs splitting.
    SegmentIntersection IntersectSegments(const Vector2& p, const Vector2& p2, const Vector2& q, const Vector2& q2) {
        SegmentIntersection result;

        const Vector2 r = p2 - p;
        const Vector2 s = q2 - q;
        const Vector2 qp = q - p;

        const float rxs = Vector2Math::Cross(r, s);
        const float qpxr = Vector2Math::Cross(qp, r);

        const float rLen = std::sqrt(Vector2Math::Dot(r, r));
        const float sLen = std::sqrt(Vector2Math::Dot(s, s));
        if (rLen <= 0.0000001f || sLen <= 0.0000001f) return result;

        const float collinearTolerance = kEpsilon * std::max(rLen, sLen);

        if (std::abs(rxs) <= collinearTolerance) {
            if (std::abs(qpxr) > collinearTolerance) return result; // parallel, not collinear

            const float rDotR = Vector2Math::Dot(r, r);
            const float t0 = Vector2Math::Dot(qp, r) / rDotR;
            const float t1 = t0 + Vector2Math::Dot(s, r) / rDotR;

            const float tLow = std::min(t0, t1);
            const float tHigh = std::max(t0, t1);

            const float overlapLow = std::max(0.0f, tLow);
            const float overlapHigh = std::min(1.0f, tHigh);

            if ((overlapHigh - overlapLow) * rLen <= kEpsilon) return result; // no meaningful overlap

            result.kind = IntersectKind::Collinear;
            result.overlapStart = { p.x + r.x * overlapLow, p.y + r.y * overlapLow };
            result.overlapEnd   = { p.x + r.x * overlapHigh, p.y + r.y * overlapHigh };
            return result;
        }

        const float t = Vector2Math::Cross(qp, s) / rxs;
        const float u = qpxr / rxs;

        const float tSlackFirst = kEpsilon / rLen;
        const float tSlackSecond = kEpsilon / sLen;

        if (t < -tSlackFirst || t > 1.0f + tSlackFirst) return result;
        if (u < -tSlackSecond || u > 1.0f + tSlackSecond) return result;

        result.kind = IntersectKind::Point;
        result.point = { p.x + r.x * t, p.y + r.y * t };
        return result;
    }

    // ---- Stage 3: wall splitting & vertex insertion --------------------------

    // Splits walls[wallIndex] at world point `at` (which must lie in its
    // interior). The near half keeps the original ID/index; the far half
    // is appended with a fresh ID. Both copy every cosmetic property -
    // front/back included, though AssignAllWallSides recomputes those
    // for every wall from the traced faces regardless, so nothing here
    // needs to reason about which side is "correct" mid-split.
    void SplitWallAt(std::vector<Wall> &walls, const int wallIndex, const Vector2 &at,ID &nextWallID) {
        Wall farHalf = walls[wallIndex];

        farHalf.id = nextWallID++;
        farHalf.start = at;
        farHalf.RefreshDerived();

        walls[wallIndex].end = at;
        walls[wallIndex].RefreshDerived();

        walls.push_back(std::move(farHalf));
    }

    // If `v` lies in the interior of some existing wall, splits that
    // wall there. A point can only ever be interior to at most one wall
    // in a well-formed planar graph, so the first match found is final.
    void InsertVertexIfNeeded(std::vector<Wall>& walls, const Vector2& v, ID& nextWallID) {
        for (int i = 0; i < static_cast<int>(walls.size()); ++i) {
            if (PointOnSegmentInterior(v, walls[i].start, walls[i].end, kEpsilon)) {
                SplitWallAt(walls, i, v, nextWallID);
                return;
            }
        }
    }

    void InsertDrawnVerticesAsGraphVertices(std::vector<Wall>& walls, const std::vector<Vector2>& points, ID& nextWallID) {
        for (const Vector2& v : points) InsertVertexIfNeeded(walls, v, nextWallID);
    }

    int FindMatchingWall(const std::vector<Wall>& walls, const Vector2& a, const Vector2& b) {
        for (int i = 0; i < static_cast<int>(walls.size()); ++i) {
            const Wall& w = walls[i];
            const bool sameDir = PointsEquivalent(w.start, a) && PointsEquivalent(w.end, b);
            const bool oppDir  = PointsEquivalent(w.start, b) && PointsEquivalent(w.end, a);
            if (sameDir || oppDir) return i;
        }
        return -1;
    }

    // Creates (or reuses) a wall spanning exactly a->b. Front/back are
    // left as INVALID_ID on a fresh wall; AssignAllWallSides fills in
    // every wall's sides afterward from the traced faces, so nothing
    // here needs to guess a side.
    void CreateOrReuseWall(
        std::vector<Wall>& walls,
        const Vector2& a,
        const Vector2& b,
        const NewSectorParams& params,
        ID& nextWallID
    ) {
        if (FindMatchingWall(walls, a, b) >= 0) return;

        const Vector4 color{ params.wallColor.x, params.wallColor.y, params.wallColor.z, 255.0f };
        Wall wall(a, b, color, INVALID_ID, INVALID_ID, params.wallTexture);
        wall.id = nextWallID++;

        walls.push_back(wall);
    }

    // Processes one user-drawn segment (its own endpoints have already
    // been folded into the graph by InsertDrawnVerticesAsGraphVertices):
    // finds every existing wall it crosses or collinearly overlaps,
    // splits those walls where the crossing is interior to them, and
    // collects every resulting cut point along (a,b) itself. Once no
    // further crossings are found, creates or reuses a wall for each
    // resulting sub-piece in order.
    void PlanarizeDrawnSegment(
        std::vector<Wall>& walls,
        const Vector2& a,
        const Vector2& b,
        const NewSectorParams& params,
        ID& nextWallID
    ) {
        if (PointsEquivalent(a, b)) return; // defensive; caller already deduped consecutive points

        const float abLen = std::sqrt(Vector2Math::Dot(b - a, b - a));
        if (abLen <= 0.0000001f) return;

        std::vector<float> splitParams = { 0.0f, 1.0f };

        bool changed = true;
        while (changed) {
            changed = false;

            for (int i = 0; i < static_cast<int>(walls.size()); ++i) {
                const Vector2 c = walls[i].start;
                const Vector2 d = walls[i].end;
                if (PointsEquivalent(c, d)) continue;

                const SegmentIntersection hit = IntersectSegments(a, b, c, d);
                if (hit.kind == IntersectKind::None) continue;

                if (hit.kind == IntersectKind::Point) {
                    const float t = ProjectParam(hit.point, a, b);

                    if (!ApproximatelyKnownParam(splitParams, t, abLen)) splitParams.push_back(t);

                    if (PointOnSegmentInterior(hit.point, c, d, kEpsilon)) {
                        SplitWallAt(walls, i, hit.point, nextWallID);
                        changed = true;
                        break;
                    }
                }
                else { // Collinear overlap: register its boundary points as cuts for (a,b).
                    for (const Vector2& boundary : { hit.overlapStart, hit.overlapEnd }) {
                        const float t = ProjectParam(boundary, a, b);
                        if (!ApproximatelyKnownParam(splitParams, t, abLen)) splitParams.push_back(t);
                    }
                }
            }
        }

        std::ranges::sort(splitParams);

        for (std::size_t i = 0; i + 1 < splitParams.size(); ++i) {
            const float t0 = splitParams[i];
            const float t1 = splitParams[i + 1];
            if ((t1 - t0) * abLen <= kEpsilon) continue;

            const Vector2 p0{ a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0 };
            const Vector2 p1{ a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1 };

            CreateOrReuseWall(walls, p0, p1, params, nextWallID);
        }
    }

    // ---- Input validation -----------------------------------------------------

    std::vector<Vector2> CleanDrawnPoints(const std::vector<Vector2>& raw) {
        std::vector<Vector2> cleaned;
        cleaned.reserve(raw.size());

        for (const Vector2& p : raw)
            if (cleaned.empty() || !PointsEquivalent(cleaned.back(), p)) cleaned.push_back(p);

        return cleaned;
    }

    bool IsClosedChain(const std::vector<Vector2>& points) {
        return points.size() >= 2 && PointsEquivalent(points.front(), points.back());
    }

    // Only non-adjacent segments are checked - adjacent segments share a
    // vertex by construction. When the chain is closed (first point
    // repeated at the end), the first and last segments are treated as
    // adjacent too, so an ordinary closed polygon isn't rejected at its
    // own seam.
    bool IsDrawnChainSelfIntersecting(const std::vector<Vector2>& points) {
        const int segmentCount = static_cast<int>(points.size()) - 1;
        if (segmentCount < 2) return false;

        const bool closed = IsClosedChain(points);

        for (int i = 0; i < segmentCount; ++i) {
            for (int j = i + 1; j < segmentCount; ++j) {
                const bool adjacent = (j == i + 1) || (closed && i == 0 && j == segmentCount - 1);
                if (adjacent) continue;

                const SegmentIntersection hit = IntersectSegments(points[i], points[i + 1], points[j], points[j + 1]);
                if (hit.kind != IntersectKind::None) return true;
            }
        }

        return false;
    }

    // ---- Stage 4/5: half-edge graph & face tracing ---------------------------

    struct HalfEdge {
        int wallIndex = -1;
        Vector2 from{};
        Vector2 to{};
        float angle = 0.0f;
        int nextHalfEdge = -1;
        bool visited = false;
    };

    std::vector<HalfEdge> BuildHalfEdges(const std::vector<Wall>& walls) {
        std::vector<HalfEdge> halfEdges;
        halfEdges.reserve(walls.size() * 2);

        for (int i = 0; i < static_cast<int>(walls.size()); ++i) {
            const Wall& w = walls[i];
            if (PointsEquivalent(w.start, w.end)) continue; // zero-length wall guard; shouldn't occur, kept defensive

            HalfEdge forward;
            forward.wallIndex = i;
            forward.from = w.start;
            forward.to = w.end;
            forward.angle = std::atan2(forward.to.y - forward.from.y, forward.to.x - forward.from.x);

            HalfEdge reverse;
            reverse.wallIndex = i;
            reverse.from = w.end;
            reverse.to = w.start;
            reverse.angle = std::atan2(reverse.to.y - reverse.from.y, reverse.to.x - reverse.from.x);

            halfEdges.push_back(forward);
            halfEdges.push_back(reverse);
        }

        return halfEdges;
    }

    struct VertexKey {
        long long qx = 0;
        long long qy = 0;

        bool operator==(const VertexKey& other) const { return qx == other.qx && qy == other.qy; }
    };

    struct VertexKeyHash {
        std::size_t operator()(const VertexKey& k) const noexcept {
            return std::hash<long long>()(k.qx) ^ (std::hash<long long>()(k.qy) << 1);
        }
    };

    VertexKey QuantizeVertex(const Vector2& v) {
        return { std::llround(v.x / kEpsilon), std::llround(v.y / kEpsilon) };
    }

    // For a half-edge arriving at a vertex, "next" is the outgoing
    // half-edge at that vertex immediately *before* this edge's twin, in
    // angle-ascending order (wrapping around) - i.e. the sharpest
    // available left turn that isn't the U-turn back the way we came.
    // Walking next-pointers this way traces every face of the planar
    // graph with each face's interior consistently on the *left* of its
    // boundary, which is also this codebase's positive-signed-area/CCW
    // convention, so bounded faces come out already in the winding
    // Geometry::Triangulate expects.
    void ComputeNextPointers(std::vector<HalfEdge>& halfEdges) {
        std::unordered_map<VertexKey, std::vector<int>, VertexKeyHash> outgoingAt;

        for (int i = 0; i < static_cast<int>(halfEdges.size()); ++i)
            outgoingAt[QuantizeVertex(halfEdges[i].from)].push_back(i);

        for (auto& entry : outgoingAt) {
            std::ranges::sort(entry.second, [&](const int lhs, const int rhs) {
                return halfEdges[lhs].angle < halfEdges[rhs].angle;
            });
        }

        for (int i = 0; i < static_cast<int>(halfEdges.size()); ++i) {
            const int twin = i ^ 1;

            const auto it = outgoingAt.find(QuantizeVertex(halfEdges[i].to));
            if (it == outgoingAt.end() || it->second.empty()) continue; // shouldn't happen - `to` always has the twin

            const std::vector<int>& bucket = it->second;
            const auto twinPos = std::ranges::find(bucket, twin);
            if (twinPos == bucket.end()) continue; // shouldn't happen

            const std::size_t twinIndex = static_cast<std::size_t>(twinPos - bucket.begin());
            const std::size_t bucketSize = bucket.size();
            halfEdges[i].nextHalfEdge = bucket[(twinIndex + bucketSize - 1) % bucketSize];
        }
    }

    struct TracedFace {
        std::vector<Vector2> vertices;
        std::vector<Triangle> triangles;
        std::vector<int> wallIndices;
        Vector2 samplePoint{};
    };

    Vector2 InteriorSamplePoint(const std::vector<Vector2>& vertices, const std::vector<Triangle>& triangles) {
        if (!triangles.empty()) {
            const Triangle& t = triangles.front();
            return { (t.a.x + t.b.x + t.c.x) / 3.0f, (t.a.y + t.b.y + t.c.y) / 3.0f };
        }

        // Fallback only - a valid face is triangulated by the caller
        // before this is reached in practice. A plain vertex average can
        // fall outside a concave polygon, so this is deliberately not
        // used as the primary method.
        Vector2 sum{ 0.0f, 0.0f };
        for (const Vector2& v : vertices) { sum.x += v.x; sum.y += v.y; }
        const float n = std::max<float>(1.0f, static_cast<float>(vertices.size()));
        return { sum.x / n, sum.y / n };
    }

    // Walks every half-edge exactly once, splitting the graph into
    // disjoint closed loops. A loop with positive signed area is a
    // bounded face (a sector candidate); zero/negative-area loops are
    // the unbounded exterior or a degenerate/dangling trace and are
    // dropped - "detect and reject open chains" from the design doc
    // falls directly out of that filter rather than needing its own
    // special case.
    std::vector<TracedFace> TraceBoundedFaces(std::vector<HalfEdge>& halfEdges) {
        std::vector<TracedFace> faces;

        for (int start = 0; start < static_cast<int>(halfEdges.size()); ++start) {
            if (halfEdges[start].visited) continue;

            std::vector<Vector2> loopVertices;
            std::vector<int> loopWalls;

            int current = start;
            bool closed = false;

            while (!halfEdges[current].visited) {
                halfEdges[current].visited = true;
                loopVertices.push_back(halfEdges[current].from);
                loopWalls.push_back(halfEdges[current].wallIndex);

                const int next = halfEdges[current].nextHalfEdge;
                if (next < 0) break; // malformed/dangling - can't continue

                current = next;

                if (current == start) { closed = true; break; }
            }

            if (!closed || loopVertices.size() < 3) continue;

            float area = 0.0f;
            for (std::size_t i = 0; i < loopVertices.size(); ++i) {
                const Vector2& p0 = loopVertices[i];
                const Vector2& p1 = loopVertices[(i + 1) % loopVertices.size()];
                area += Vector2Math::Cross(p0, p1);
            }
            area *= 0.5f;

            if (area <= kMinFaceArea) continue; // exterior trace, sliver, or malformed loop

            std::ranges::sort(loopWalls);
            loopWalls.erase(std::ranges::unique(loopWalls).begin(), loopWalls.end());

            TracedFace face;
            face.vertices = std::move(loopVertices);
            face.wallIndices = std::move(loopWalls);
            faces.push_back(std::move(face));
        }

        return faces;
    }

    void FinalizeFaceGeometry(std::vector<TracedFace>& faces) {
        for (TracedFace& face : faces) {
            face.triangles = Geometry::Triangulate(face.vertices);
            face.samplePoint = InteriorSamplePoint(face.vertices, face.triangles);
        }
    }

    // ---- Stage 1: snapshot of sectors as they existed before the edit -------

    struct OldSectorInfo {
        ID id = INVALID_ID;
        std::vector<SectorFloor> floors;
        float lightValue = 255.0f;
        std::vector<Vector2> vertices;
        Vector2 samplePoint{};
    };

    std::vector<OldSectorInfo> SnapshotSectors(const std::vector<Sector>& sectors) {
        std::vector<OldSectorInfo> snapshot;
        snapshot.reserve(sectors.size());

        for (const Sector& sector : sectors) {
            OldSectorInfo info;

            info.id = sector.id;
            info.floors = sector.floors;
            info.lightValue = sector.lightValue;
            info.vertices = sector.vertices;
            info.samplePoint = InteriorSamplePoint(sector.vertices, sector.triangles);

            snapshot.push_back(std::move(info));
        }

        return snapshot;
    }

    // ---- Stage 6: reconcile traced faces against the old sectors -------------

    struct ReconciledFace {
        ID sectorID = INVALID_ID;
        const OldSectorInfo* source = nullptr; // null => brand new territory
        const TracedFace* face = nullptr;
    };

    // This operation only ever adds/splits walls, never removes them, so
    // a traced face can only ever be a subset of at most one old
    // sector's original area - never a merge of two. That means: (a) a
    // face's own sample point falling inside an old sector's *original*
    // polygon reliably identifies which old sector it came from, even
    // when that sector was split into several faces, and (b) among the
    // faces that came from the same old sector, at most one of them can
    // contain that sector's *original* sample point - whichever one does
    // keeps the old ID, and any siblings get fresh ones.
    std::vector<ReconciledFace> ReconcileFaces(
        std::vector<TracedFace>& faces,
        const std::vector<OldSectorInfo>& oldSectors,
        ID& nextSectorID
    ) {
        std::vector<const OldSectorInfo*> groupOf(faces.size(), nullptr);

        for (std::size_t i = 0; i < faces.size(); ++i) {
            for (const OldSectorInfo& old : oldSectors) {
                if (Geometry::IsPointInPolygon(old.vertices, faces[i].samplePoint)) {
                    groupOf[i] = &old;
                    break;
                }
            }
        }

        std::vector<ReconciledFace> result(faces.size());

        for (const OldSectorInfo& old : oldSectors) {
            int keeperIndex = -1;

            for (std::size_t i = 0; i < faces.size(); ++i) {
                if (groupOf[i] != &old) continue;
                if (Geometry::IsPointInPolygon(faces[i].vertices, old.samplePoint)) {
                    keeperIndex = static_cast<int>(i);
                    break;
                }
            }

            // Fallback for the (extremely unlikely) case where the old
            // sample point doesn't land cleanly inside any sibling -
            // keep the first one in the group rather than losing the
            // sector's properties entirely.
            if (keeperIndex < 0) {
                for (std::size_t i = 0; i < faces.size(); ++i) {
                    if (groupOf[i] == &old) { keeperIndex = static_cast<int>(i); break; }
                }
            }

            for (std::size_t i = 0; i < faces.size(); ++i) {
                if (groupOf[i] != &old) continue;
                result[i].source = &old;
                result[i].face = &faces[i];
                result[i].sectorID = (static_cast<int>(i) == keeperIndex) ? old.id : nextSectorID++;
            }
        }

        for (std::size_t i = 0; i < faces.size(); ++i) {
            if (groupOf[i] != nullptr) continue;
            result[i].source = nullptr;
            result[i].face = &faces[i];
            result[i].sectorID = nextSectorID++;
        }

        return result;
    }

    // ---- Stage 7: rebuild Sector/Wall data from the reconciled faces ---------

    std::vector<Sector> BuildFinalSectors(const std::vector<ReconciledFace> &reconciled, const NewSectorParams &params) {
        std::vector<Sector> sectors;
        sectors.reserve(reconciled.size());

        for (const ReconciledFace &reconciledFace: reconciled) {
            Sector sector;

            sector.id = reconciledFace.sectorID;
            sector.vertices = reconciledFace.face->vertices;
            sector.triangles = reconciledFace.face->triangles;

            if (reconciledFace.source != nullptr) {
                sector.floors = reconciledFace.source->floors;
                sector.lightValue = reconciledFace.source->lightValue;
            }
            else {
                sector.floors = params.floors;
                sector.lightValue = params.lightValue;
            }

            sectors.push_back(std::move(sector));
        }

        return sectors;
    }

    // Resets every wall's front/back and reassigns them from scratch
    // using each face's sample point against the wall's own start->end
    // direction (right = front, left = back, per the design doc's
    // convention) - independent of how the wall or the face happened to
    // be drawn/wound.
    void AssignAllWallSides(std::vector<Wall>& walls, const std::vector<ReconciledFace>& reconciled) {
        for (Wall& w : walls) {
            w.frontSector = INVALID_ID;
            w.backSector = INVALID_ID;
        }

        for (const ReconciledFace& rf : reconciled) {
            for (const int wallIndex : rf.face->wallIndices) {
                Wall& w = walls[wallIndex];
                const float side = Vector2Math::Cross(w.end - w.start, rf.face->samplePoint - w.start);

                if (side < -kEpsilon) {
                    if (w.frontSector != INVALID_ID && w.frontSector != rf.sectorID) {
                        spdlog::error(
                            "MapTopology: wall {} front already assigned to sector {}, refusing to overwrite with {}",
                            w.id, w.frontSector, rf.sectorID
                        );
                        continue;
                    }
                    w.frontSector = rf.sectorID;
                }
                else if (side > kEpsilon) {
                    if (w.backSector != INVALID_ID && w.backSector != rf.sectorID) {
                        spdlog::error(
                            "MapTopology: wall {} back already assigned to sector {}, refusing to overwrite with {}",
                            w.id, w.backSector, rf.sectorID
                        );
                        continue;
                    }
                    w.backSector = rf.sectorID;
                }
                else {
                    spdlog::warn("MapTopology: sector {} sample point sits on wall {} - side assignment skipped", rf.sectorID, w.id);
                }
            }
        }
    }

    // ---- Orchestration --------------------------------------------------------

    struct PassResult {
        bool success = false;
        std::string message;
        std::vector<Wall> resultWalls;
        std::vector<Sector> resultSectors;
        ID resultNextWallID = 0;
        ID resultNextSectorID = 0;
    };

    PassResult RunTopologyPass(const Level& level, const std::vector<Vector2>& rawPoints, const NewSectorParams& params) {
        PassResult result;

        if (params.floors.empty()) {
            result.message = "A sector must contain at least one floor.";
            return result;
        }

        for (size_t floorIndex = 0; floorIndex < params.floors.size(); ++floorIndex) {
            const SectorFloor& floor = params.floors[floorIndex];

            if (floor.floor.height >= floor.ceiling.height) {
                result.message = "A sector floor ceiling must be above its floor.";
                return result;
            }

            if (floorIndex > 0 && params.floors[floorIndex - 1].ceiling.height > floor.floor.height) {
                result.message = "Sector floor intervals must not overlap.";
                return result;
            }
        }

        const std::vector<Vector2> points = CleanDrawnPoints(rawPoints);

        if (points.size() < 2) {
            result.message = "Need at least two distinct points to draw geometry.";
            return result;
        }

        if (IsDrawnChainSelfIntersecting(points)) {
            result.message = "Rejected: drawn geometry is self-intersecting.";
            return result;
        }

        std::vector<Wall> walls = level.walls;
        ID nextWallID = level.nextWallID;
        ID nextSectorID = level.nextSectorID;

        const std::vector<OldSectorInfo> oldSectors = SnapshotSectors(level.sectors);

        InsertDrawnVerticesAsGraphVertices(walls, points, nextWallID);

        for (std::size_t i = 0; i + 1 < points.size(); ++i)
            PlanarizeDrawnSegment(walls, points[i], points[i + 1], params, nextWallID);

        std::vector<HalfEdge> halfEdges = BuildHalfEdges(walls);
        ComputeNextPointers(halfEdges);

        std::vector<TracedFace> faces = TraceBoundedFaces(halfEdges);
        FinalizeFaceGeometry(faces);

        const std::vector<ReconciledFace> reconciled = ReconcileFaces(faces, oldSectors, nextSectorID);

        result.resultSectors = BuildFinalSectors(reconciled, params);
        AssignAllWallSides(walls, reconciled);

        result.resultWalls = std::move(walls);
        result.resultNextWallID = nextWallID;
        result.resultNextSectorID = nextSectorID;
        result.success = true;
        result.message = "OK";

        return result;
    }
}

namespace MapTopology {
    ApplyResult ApplyDrawnGeometry(Level& level, const std::vector<Vector2>& drawnPoints, const NewSectorParams& params) {
        const PassResult pass = RunTopologyPass(level, drawnPoints, params);

        ApplyResult result;
        result.success = pass.success;
        result.message = pass.message;

        if (!pass.success) return result;

        std::unordered_set<ID> oldIDs;
        oldIDs.reserve(level.sectors.size());
        for (const Sector& s : level.sectors) oldIDs.insert(s.id);

        for (const Sector& s : pass.resultSectors)
            if (!oldIDs.contains(s.id)) result.affectedSectorIDs.push_back(s.id);

        const std::size_t oldSectorCount = level.sectors.size();

        level.walls = pass.resultWalls;
        level.sectors = pass.resultSectors;
        level.nextWallID = pass.resultNextWallID;
        level.nextSectorID = pass.resultNextSectorID;

        MapQueries::RebuildSectorRuntimeLinks(level);

        result.createdNewFace = level.sectors.size() > oldSectorCount;

        return result;
    }

    bool WouldEncloseNewFace(const Level& level, const std::vector<Vector2>& drawnPoints) {
        const NewSectorParams dummyParams{}; // cosmetic only - doesn't affect face count
        const PassResult pass = RunTopologyPass(level, drawnPoints, dummyParams);
        return pass.success && pass.resultSectors.size() > level.sectors.size();
    }
}