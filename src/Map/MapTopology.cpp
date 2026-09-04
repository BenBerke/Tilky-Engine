#include "Headers/Map/MapTopology.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "Headers/Objects/Level.hpp"
#include "Headers/Objects/Sector.hpp"
#include "Headers/Objects/Wall.hpp"
#include "Headers/Map/MapQueries.hpp"
#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"

// ============================================================================
// Algorithm overview
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
//                        TraceAllFaces walks it once to recover every
//                        closed loop, bounded (positive-area) or not,
//                        tagging each half-edge with which loop it ended
//                        up in.
//   4.5. Resolve nesting - a bounded loop is always a sector candidate,
//                        but an *unbounded*-side loop isn't automatically
//                        "the outside": ComputeWallComponents groups
//                        walls that share an endpoint, and
//                        ResolveFaceNesting checks every unbounded loop
//                        against every bounded loop from a *different*
//                        component (a hole can't be nested in its own
//                        component's own bounded face) to see whether
//                        it's really sitting inside an existing sector -
//                        a pillar or island drawn as its own closed loop
//                        never shares a wall with the sector around it,
//                        so this spatial check is the only way to find
//                        that relationship. FinalizeNestedGeometry then
//                        attaches each resolved hole to its parent and
//                        re-triangulates the parent against it.
//   1/6. Reconcile     - SnapshotSectors captures old sector identity
//                        before any of the above runs; ReconcileFaces
//                        matches each bounded face back to the old
//                        sector it's a subset of (or marks it brand new).
//   7. Rebuild         - BuildFinalSectors and AssignAllWallSides turn
//                        the reconciled faces into real Sector/Wall
//                        data - the latter reads the face each half-edge
//                        was tagged with directly, rather than
//                        re-deriving which side of a wall a face is on
//                        from a single sample point (unreliable once a
//                        face can be concave or hollowed out by a hole).
//                        The public ApplyDrawnGeometry commits the result
//                        and calls MapQueries::RebuildSectorRuntimeLinks.
//
// Remaining scope limit: a wall that only ever borders one face on both
// its sides (a dangling spur with nothing on the far end) never becomes
// part of a sector - it just sits there as ordinary open geometry,
// exactly like a partially-drawn chain does today.
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
    void CreateOrReuseWall(std::vector<Wall>& walls, const Vector2& a, const Vector2& b, const NewSectorParams& params,ID& nextWallID) {
        if (FindMatchingWall(walls, a, b) >= 0) return;

        const Vector4 color = params.wallColor;
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

        // Index into the TraceAllFaces result of whichever loop this
        // specific direction ended up part of; -1 for a half-edge that
        // never closed into a usable loop (a dangling spur). Set during
        // tracing and read back by AssignAllWallSides.
        int faceIndex = -1;
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

    int FindComponentRoot(std::vector<int>& parent, const int x) {
        int root = x;
        while (parent[root] != root) root = parent[root];

        int cur = x;
        while (parent[cur] != root) {
            const int next = parent[cur];
            parent[cur] = root;
            cur = next;
        }

        return root;
    }

    // Groups walls into connected components by shared endpoint, purely
    // so face-nesting resolution (see ResolveFaceNesting) can tell "this
    // loop is a separate, possibly-floating piece of geometry" apart
    // from "this loop is just the far side of the same piece of
    // geometry" - only the former can be nested inside something else;
    // a component's own unbounded-side face can never be spatially
    // inside one of that same component's own bounded faces.
    std::vector<int> ComputeWallComponents(const std::vector<Wall>& walls) {
        std::vector<int> parent(walls.size());
        for (int i = 0; i < static_cast<int>(walls.size()); ++i) parent[i] = i;

        std::unordered_map<VertexKey, int, VertexKeyHash> firstWallAt;

        for (int i = 0; i < static_cast<int>(walls.size()); ++i) {
            for (const Vector2& endpoint : { walls[i].start, walls[i].end }) {
                const auto [it, inserted] = firstWallAt.try_emplace(QuantizeVertex(endpoint), i);
                if (inserted) continue;

                const int a = FindComponentRoot(parent, i);
                const int b = FindComponentRoot(parent, it->second);
                if (a != b) parent[a] = b;
            }
        }

        std::vector<int> component(walls.size());
        for (int i = 0; i < static_cast<int>(walls.size()); ++i) component[i] = FindComponentRoot(parent, i);
        return component;
    }

    struct TracedFace {
        std::vector<Vector2> vertices;
        float signedArea = 0.0f;
        int componentID = -1;

        std::vector<Triangle> triangles;
        Vector2 samplePoint{};

        // Boundary loop of every other loop nesting resolution found
        // spatially inside this one. Only ever populated for a bounded
        // (signedArea > 0) face - see ResolveFaceNesting/
        // FinalizeNestedGeometry.
        std::vector<std::vector<Vector2>> holes;
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
    // disjoint closed loops and tagging each half-edge with the loop
    // (by index into the returned list) it ended up part of. Every
    // closed loop of at least three vertices is kept regardless of the
    // sign of its area: a positive-area loop is a bounded face (a
    // sector candidate); a negative-area one is the unbounded side of
    // whatever wall component it came from, which ResolveFaceNesting
    // below still needs in order to tell a true level boundary apart
    // from a hole that's actually nested inside another sector. Only a
    // near-zero-area loop - a degenerate or dangling trace - is dropped;
    // "detect and reject open chains" from the design doc falls
    // directly out of that filter rather than needing its own case.
    std::vector<TracedFace> TraceAllFaces(std::vector<HalfEdge>& halfEdges, const std::vector<int>& wallComponents) {
        std::vector<TracedFace> faces;

        for (int start = 0; start < static_cast<int>(halfEdges.size()); ++start) {
            if (halfEdges[start].visited) continue;

            std::vector<Vector2> loopVertices;
            std::vector<int> loopHalfEdges;

            int current = start;
            bool closed = false;

            while (!halfEdges[current].visited) {
                halfEdges[current].visited = true;
                loopVertices.push_back(halfEdges[current].from);
                loopHalfEdges.push_back(current);

                const int next = halfEdges[current].nextHalfEdge;
                if (next < 0) break; // malformed/dangling - can't continue

                current = next;

                if (current == start) { closed = true; break; }
            }

            if (!closed || loopVertices.size() < 3) continue;

            float signedArea = 0.0f;
            for (std::size_t i = 0; i < loopVertices.size(); ++i) {
                const Vector2& p0 = loopVertices[i];
                const Vector2& p1 = loopVertices[(i + 1) % loopVertices.size()];
                signedArea += Vector2Math::Cross(p0, p1);
            }
            signedArea *= 0.5f;

            if (std::abs(signedArea) <= kMinFaceArea) continue; // sliver or malformed loop, either side

            TracedFace face;
            face.vertices = std::move(loopVertices);
            face.signedArea = signedArea;
            face.componentID = wallComponents[halfEdges[start].wallIndex];

            const int faceIndex = static_cast<int>(faces.size());
            for (const int heIndex : loopHalfEdges) halfEdges[heIndex].faceIndex = faceIndex;

            faces.push_back(std::move(face));
        }

        return faces;
    }

    // Hole-blind triangulation for every bounded face, purely to get
    // each one a samplePoint before its real holes (if any) are known.
    // Not run on unbounded-side faces: triangulating one of those just
    // fills in the *same* shape as its bounded sibling (Triangulate
    // doesn't care about winding), which is not a point representative
    // of the unbounded region that face actually stands for - see
    // ResolveFaceNesting, which needs a bounded sibling's sample point
    // for exactly that reason.
    void FinalizeFaceGeometry(std::vector<TracedFace>& faces, const std::vector<int>& positiveIndices) {
        for (const int p : positiveIndices) {
            TracedFace& face = faces[p];
            face.triangles = Geometry::Triangulate(face.vertices);
            face.samplePoint = InteriorSamplePoint(face.vertices, face.triangles);
        }
    }

    // For every unbounded-side face, finds the smallest bounded face
    // from a *different* wall component that spatially encloses it -
    // i.e. an existing sector inside whose territory this whole
    // separate loop of walls was drawn - and returns its index into
    // `allFaces`, or -1 if it isn't nested in anything, meaning it
    // really is (part of) the level's true, unbounded exterior.
    // Smallest-enclosing-first attributes a hole to its immediate
    // parent rather than to some larger ancestor several nesting levels
    // up (see the three-nested-sectors case in ApplyDrawnGeometry's
    // header docs).
    //
    // The containment test itself is run against a vertex of the
    // negative face's *own* boundary - not a sample point (from either
    // this face or a bounded sibling): a sample point is just one
    // arbitrary interior point of a face's own broader territory, which
    // can easily fall inside some unrelated *smaller* region nested
    // within that same territory (exactly the hole being resolved,
    // sometimes), giving a false positive the wrong way round. A vertex
    // of this component's own boundary doesn't have that problem: since
    // walls from different components never cross without sharing a
    // vertex (planarization already guarantees that), a whole separate
    // component's boundary is either entirely inside a candidate parent
    // or entirely outside it, so any one of its own boundary vertices
    // is a safe, sufficient test point.
    std::vector<int> ResolveFaceNesting(
        const std::vector<TracedFace>& allFaces,
        const std::vector<int>& positiveIndices,
        const std::vector<int>& negativeIndices
    ) {
        std::vector<int> parentOf(negativeIndices.size(), -1);

        for (std::size_t n = 0; n < negativeIndices.size(); ++n) {
            const TracedFace& negFace = allFaces[negativeIndices[n]];
            const Vector2& testPoint = negFace.vertices.front();

            int bestPositive = -1;
            float bestArea = std::numeric_limits<float>::max();

            for (const int p : positiveIndices) {
                const TracedFace& posFace = allFaces[p];

                // A component's own bounded face(s) can't be its own
                // parent - the point being tested is one of this same
                // component's own boundary vertices.
                if (posFace.componentID == negFace.componentID) continue;
                if (posFace.signedArea >= bestArea) continue;
                if (!Geometry::IsPointInPolygon(posFace.vertices, testPoint)) continue;

                bestPositive = p;
                bestArea = posFace.signedArea;
            }

            parentOf[n] = bestPositive;
        }

        return parentOf;
    }

    // Positive faces that are themselves a hole nested in a sibling from
    // the very same simple loop (its unbounded side resolved to some
    // *other* face in the current graph). Reconciliation uses this to
    // avoid handing a sector's old identity to the inner sector a hole
    // just carved out of it, merely because the old sample point happens
    // to have landed inside that new hole - see ReconcileFaces.
    std::unordered_set<int> FindFacesActingAsHoles(
        const std::vector<TracedFace>& allFaces,
        const std::vector<int>& positiveIndices,
        const std::vector<int>& negativeIndices,
        const std::vector<int>& parentOfNegative
    ) {
        std::unordered_set<int> result;

        for (std::size_t n = 0; n < negativeIndices.size(); ++n) {
            const int parent = parentOfNegative[n];
            if (parent < 0) continue;

            const int component = allFaces[negativeIndices[n]].componentID;

            int onlySibling = -1;
            bool ambiguous = false;
            for (const int p : positiveIndices) {
                if (allFaces[p].componentID != component) continue;
                if (onlySibling >= 0) { ambiguous = true; break; }
                onlySibling = p;
            }

            // A component with more than one bounded face (e.g. a
            // multi-room island floating inside another sector) has no
            // single positive face that "is" the hole - the hole is
            // that whole component's combined silhouette, not any one
            // room in it - so there's nothing unambiguous to exclude.
            if (!ambiguous && onlySibling >= 0 && onlySibling != parent) result.insert(onlySibling);
        }

        return result;
    }

    // Attaches each resolved hole to its parent face and re-triangulates
    // every bounded face against its now-known holes, so a parent's
    // floor/ceiling geometry correctly excludes whatever's nested inside
    // it. Each face's samplePoint is then recomputed from that
    // hole-aware triangulation: the hole-blind one FinalizeFaceGeometry
    // produced can easily sit inside the face's own hole (a donut's
    // outer boundary triangulates straight across the middle), and
    // reconciliation relies on a face's sample point being somewhere
    // that face actually still occupies to match it back to the right
    // old sector.
    void FinalizeNestedGeometry(
        std::vector<TracedFace>& allFaces,
        const std::vector<int>& positiveIndices,
        const std::vector<int>& negativeIndices,
        const std::vector<int>& parentOfNegative
    ) {
        for (std::size_t n = 0; n < negativeIndices.size(); ++n) {
            const int parent = parentOfNegative[n];
            if (parent >= 0) allFaces[parent].holes.push_back(allFaces[negativeIndices[n]].vertices);
        }

        for (const int p : positiveIndices) {
            TracedFace& face = allFaces[p];
            if (face.holes.empty()) continue; // already triangulated, sample point still valid

            face.triangles = Geometry::Triangulate(face.vertices, face.holes);
            face.samplePoint = InteriorSamplePoint(face.vertices, face.triangles);
        }
    }

    // ---- Stage 1: snapshot of sectors as they existed before the edit -------

    struct OldSectorInfo {
        ID id = INVALID_ID;
        std::vector<SectorFloor> floors;
        Vector3 lightValue = {255.0f, 255.0f, 255.0f};
        std::vector<Vector2> vertices;
        std::vector<std::vector<Vector2>> innerLoops;
        Vector2 samplePoint{};
    };

    std::vector<OldSectorInfo> SnapshotSectors(const std::vector<Sector>& sectors) {
        std::vector<OldSectorInfo> snapshot;
        snapshot.reserve(sectors.size());

        for (const Sector& sector : sectors) {
            OldSectorInfo info;

            info.id = sector.id;
            info.floors = sector.floors;
            info.lightValue = sector.light;
            info.vertices = sector.vertices;
            info.innerLoops = sector.innerLoops;
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
    // a bounded face can only ever be a subset of at most one old
    // sector's original area - never a merge of two. That means: (a) a
    // face's own sample point falling inside an old sector's *original*
    // (hole-aware) polygon reliably identifies which old sector it came
    // from, even when that sector was split into several faces or had a
    // hole newly carved into it, and (b) among the faces that came from
    // the same old sector, at most one of them can contain that sector's
    // *original* sample point - whichever one does keeps the old ID, and
    // any siblings get fresh ones.
    //
    // The one wrinkle holes add: if the edit just carved a brand new
    // hole into an old sector, the old sample point can end up sitting
    // inside that new hole - i.e. inside the new inner sector rather
    // than the old sector's own (now-donut-shaped) remainder. Left
    // alone, that would hand the *old* sector's identity to the tiny new
    // pillar and leave the actual remainder looking brand new instead.
    // facesActingAsHoles rules that out: a candidate that's a hole
    // freshly nested in a sibling is never eligible to be the keeper,
    // regardless of where the old sample point landed.
    std::vector<ReconciledFace> ReconcileFaces(
        const std::vector<TracedFace>& allFaces,
        const std::vector<int>& positiveIndices,
        const std::vector<OldSectorInfo>& oldSectors,
        const std::unordered_set<int>& facesActingAsHoles,
        ID& nextSectorID
    ) {
        std::vector<const OldSectorInfo*> groupOf(positiveIndices.size(), nullptr);

        for (std::size_t i = 0; i < positiveIndices.size(); ++i) {
            const TracedFace& face = allFaces[positiveIndices[i]];

            // Smallest containing old sector wins, not simply the first
            // in vector order. Hole-aware containment already stops a
            // parent claiming a face that sits in one of its holes, but
            // only while the parent's recorded holes are still accurate.
            // A caller that edited walls directly (see
            // RebuildSectorsFromWalls) can hand us a snapshot whose hole
            // set is deliberately stale, and there "most specific old
            // sector that contained this face" is the answer that stays
            // right either way.
            float bestArea = std::numeric_limits<float>::max();

            for (const OldSectorInfo& old : oldSectors) {
                if (!Geometry::IsPointInPolygon(old.vertices, old.innerLoops, face.samplePoint)) continue;

                const float area = Geometry::PolygonAreaAbs(old.vertices);
                if (area >= bestArea) continue;

                bestArea = area;
                groupOf[i] = &old;
            }
        }

        std::vector<ReconciledFace> result(positiveIndices.size());

        for (const OldSectorInfo& old : oldSectors) {
            int keeperIndex = -1;

            for (std::size_t i = 0; i < positiveIndices.size() && keeperIndex < 0; ++i) {
                if (groupOf[i] != &old || facesActingAsHoles.contains(positiveIndices[i])) continue;
                const TracedFace& face = allFaces[positiveIndices[i]];
                if (Geometry::IsPointInPolygon(face.vertices, face.holes, old.samplePoint)) keeperIndex = static_cast<int>(i);
            }

            // Fallbacks, weakest condition dropped first: prefer any
            // non-hole sibling even if the old sample point doesn't
            // land in it, then finally accept a hole-sibling rather
            // than lose the sector's properties entirely (only possible
            // if every member of the group is a freshly-nested hole).
            for (std::size_t i = 0; i < positiveIndices.size() && keeperIndex < 0; ++i) {
                if (groupOf[i] == &old && !facesActingAsHoles.contains(positiveIndices[i])) keeperIndex = static_cast<int>(i);
            }
            for (std::size_t i = 0; i < positiveIndices.size() && keeperIndex < 0; ++i) {
                if (groupOf[i] == &old) keeperIndex = static_cast<int>(i);
            }

            for (std::size_t i = 0; i < positiveIndices.size(); ++i) {
                if (groupOf[i] != &old) continue;
                result[i].source = &old;
                result[i].face = &allFaces[positiveIndices[i]];
                result[i].sectorID = (static_cast<int>(i) == keeperIndex) ? old.id : nextSectorID++;
            }
        }

        for (std::size_t i = 0; i < positiveIndices.size(); ++i) {
            if (groupOf[i] != nullptr) continue;
            result[i].source = nullptr;
            result[i].face = &allFaces[positiveIndices[i]];
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
            sector.innerLoops = reconciledFace.face->holes;
            sector.triangles = reconciledFace.face->triangles;

            if (reconciledFace.source != nullptr) {
                sector.floors = reconciledFace.source->floors;
                sector.light = reconciledFace.source->lightValue;
            }
            else {
                sector.floors = params.floors;
                sector.light = params.lightValue;
            }

            sectors.push_back(std::move(sector));
        }

        return sectors;
    }

    // Every bounded face's reconciled sector ID, plus - for every
    // unbounded-side face that nesting resolution attached to a parent -
    // that parent's own resolved ID, so a hole's boundary walls report
    // the sector nested inside it, not "no sector". An unbounded-side
    // face with no parent (the level's true exterior) maps to
    // INVALID_ID, same as it always has.
    std::vector<ID> BuildFaceSectorIDs(
        const std::vector<TracedFace>& allFaces,
        const std::vector<int>& positiveIndices,
        const std::vector<ReconciledFace>& reconciled,
        const std::vector<int>& negativeIndices,
        const std::vector<int>& parentOfNegative
    ) {
        std::vector<ID> sectorIDOfFace(allFaces.size(), INVALID_ID);

        for (std::size_t k = 0; k < positiveIndices.size(); ++k) sectorIDOfFace[positiveIndices[k]] = reconciled[k].sectorID;

        for (std::size_t n = 0; n < negativeIndices.size(); ++n) {
            const int parent = parentOfNegative[n];
            if (parent >= 0) sectorIDOfFace[negativeIndices[n]] = sectorIDOfFace[parent];
        }

        return sectorIDOfFace;
    }

    // Resets every wall's front/back and reassigns them by reading which
    // face each of the wall's two half-edges was tagged with during
    // tracing (front = the face on the right of start->end = the face
    // traced walking end->start; back = the face on the left = the face
    // traced walking start->end - per the design doc's convention).
    // That tag comes straight out of the planar graph traversal, so it's
    // exact for every wall regardless of how concave its face is or
    // whether it borders a hole - unlike checking which side of a wall's
    // *line* a single sample point falls on, which a concave face's own
    // sample point can easily land on the "wrong" side of one of that
    // same face's walls.
    void AssignAllWallSides(
        std::vector<Wall>& walls,
        const std::vector<HalfEdge>& halfEdges,
        const std::vector<ID>& sectorIDOfFace
    ) {
        const auto sectorFor = [&](const int faceIndex) {
            return faceIndex < 0 ? INVALID_ID : sectorIDOfFace[faceIndex];
        };

        for (Wall& w : walls) {
            w.frontSector = INVALID_ID;
            w.backSector = INVALID_ID;
        }

        for (const HalfEdge& he : halfEdges) {
            Wall& w = walls[he.wallIndex];

            if (PointsEquivalent(he.from, w.start)) w.backSector = sectorFor(he.faceIndex);
            else w.frontSector = sectorFor(he.faceIndex);
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

    // Everything after planarization: trace the wall graph, resolve
    // nesting, reconcile against `oldSectors`, and rebuild Sector/Wall
    // data. Shared by the drawing entry point (which planarizes new
    // points into `walls` first) and by RebuildSectorsFromWalls (which
    // passes the wall graph through untouched), so both derive sectors
    // through exactly the same, single implementation.
    PassResult RetraceFromWalls(
        std::vector<Wall> walls,
        const std::vector<OldSectorInfo>& oldSectors,
        const NewSectorParams& params,
        const ID nextWallID,
        ID nextSectorID
    ) {
        PassResult result;

        std::vector<HalfEdge> halfEdges = BuildHalfEdges(walls);
        ComputeNextPointers(halfEdges);

        const std::vector<int> wallComponents = ComputeWallComponents(walls);
        std::vector<TracedFace> allFaces = TraceAllFaces(halfEdges, wallComponents);

        std::vector<int> positiveIndices;
        std::vector<int> negativeIndices;
        for (int i = 0; i < static_cast<int>(allFaces.size()); ++i) {
            std::vector<int>& bucket = allFaces[i].signedArea > 0.0f ? positiveIndices : negativeIndices;
            bucket.push_back(i);
        }

        FinalizeFaceGeometry(allFaces, positiveIndices);

        const std::vector<int> parentOfNegative = ResolveFaceNesting(allFaces, positiveIndices, negativeIndices);
        FinalizeNestedGeometry(allFaces, positiveIndices, negativeIndices, parentOfNegative);

        const std::unordered_set<int> facesActingAsHoles =
            FindFacesActingAsHoles(allFaces, positiveIndices, negativeIndices, parentOfNegative);

        const std::vector<ReconciledFace> reconciled =
            ReconcileFaces(allFaces, positiveIndices, oldSectors, facesActingAsHoles, nextSectorID);

        result.resultSectors = BuildFinalSectors(reconciled, params);

        const std::vector<ID> sectorIDOfFace =
            BuildFaceSectorIDs(allFaces, positiveIndices, reconciled, negativeIndices, parentOfNegative);
        AssignAllWallSides(walls, halfEdges, sectorIDOfFace);

        result.resultWalls = std::move(walls);
        result.resultNextWallID = nextWallID;
        result.resultNextSectorID = nextSectorID;
        result.success = true;
        result.message = "OK";

        return result;
    }

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

        const std::vector<OldSectorInfo> oldSectors = SnapshotSectors(level.sectors);

        InsertDrawnVerticesAsGraphVertices(walls, points, nextWallID);

        for (std::size_t i = 0; i + 1 < points.size(); ++i)
            PlanarizeDrawnSegment(walls, points[i], points[i + 1], params, nextWallID);

        return RetraceFromWalls(std::move(walls), oldSectors, params, nextWallID, level.nextSectorID);
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

    bool RebuildSectorsFromWalls(Level& level) {
        const NewSectorParams defaultParams{}; // only reached if the walls enclose space no old sector covered

        const PassResult pass = RetraceFromWalls(
            level.walls,
            SnapshotSectors(level.sectors),
            defaultParams,
            level.nextWallID,
            level.nextSectorID
        );

        if (!pass.success) return false;

        level.walls = pass.resultWalls;
        level.sectors = pass.resultSectors;
        level.nextWallID = pass.resultNextWallID;
        level.nextSectorID = pass.resultNextSectorID;

        MapQueries::RebuildSectorRuntimeLinks(level);

        return true;
    }

    bool WouldEncloseNewFace(const Level& level, const std::vector<Vector2>& drawnPoints) {
        const NewSectorParams dummyParams{}; // cosmetic only - doesn't affect face count
        const PassResult pass = RunTopologyPass(level, drawnPoints, dummyParams);
        return pass.success && pass.resultSectors.size() > level.sectors.size();
    }
}