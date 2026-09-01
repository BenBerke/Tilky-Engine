#include "Headers/Math/Geometry/Geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include "Headers/Math/Vector/Vector2Math.hpp"

#include "Headers/Math/Constants.hpp"

namespace Geometry {
    namespace {
        float CrossAtPoint(const Vector2 a, const Vector2 b, const Vector2 c) {
            return Vector2Math::Cross(b - a, c - a);
        }

        float PolygonAreaSigned(const std::vector<Vector2>& vertices) {
            float area = 0.0f;

            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                const int next = (i + 1) % static_cast<int>(vertices.size());
                area += Vector2Math::Cross(vertices[i], vertices[next]);
            }

            return area * 0.5f;
        }

        bool IsInsideTriangle(const Vector2 a, const Vector2 b, const Vector2 c, const Vector2 p) {
            const float cp1 = CrossAtPoint(a, b, p);
            const float cp2 = CrossAtPoint(b, c, p);
            const float cp3 = CrossAtPoint(c, a, p);

            return cp1 >= -Constants::Epsilon && cp2 >= -Constants::Epsilon && cp3 >= -Constants::Epsilon;
        }

        bool SamePoint(const Vector2& a, const Vector2& b) {
            return a.x == b.x && a.y == b.y;
        }

        // Related to non-convex sector triangulation
        bool IsEar(const std::vector<Vector2>& vertices, const int prev, const int curr, const int next) {
            const Vector2 a = vertices[prev];
            const Vector2 b = vertices[curr];
            const Vector2 c = vertices[next];

            if (CrossAtPoint(a, b, c) <= Constants::Epsilon) return false;

            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                if (i == prev || i == curr || i == next) continue;

                // A hole bridge (see BridgeHoleIntoOuter below) walks out
                // to a vertex and back, so that vertex legitimately
                // appears twice in the list. IsInsideTriangle's boundary
                // is inclusive, so without this a duplicate sitting
                // exactly on this ear's own corner would count as "inside"
                // it and wrongly veto an otherwise valid ear.
                if (SamePoint(vertices[i], a) || SamePoint(vertices[i], b) || SamePoint(vertices[i], c)) continue;

                if (IsInsideTriangle(a, b, c, vertices[i])) return false;
            }

            return true;
        }

        // ---- Hole support: stitching a hole into a single simple polygon ----

        // True if segments (a,b) and (c,d) cross each other transversally.
        // Endpoints merely touching is not a crossing - callers only ever
        // test bridge candidates that are already anchored at shared
        // polygon vertices, and that touching is expected, not a hit.
        bool SegmentsCross(const Vector2 a, const Vector2 b, const Vector2 c, const Vector2 d) {
            const float d1 = CrossAtPoint(c, d, a);
            const float d2 = CrossAtPoint(c, d, b);
            const float d3 = CrossAtPoint(a, b, c);
            const float d4 = CrossAtPoint(a, b, d);

            return ((d1 > 0.0f) != (d2 > 0.0f)) && ((d3 > 0.0f) != (d4 > 0.0f));
        }

        // Whether the segment (a,b) stays clear of every edge of `loop`,
        // aside from edges already touching a or b - touching there is
        // the point of a bridge, not a crossing.
        bool SegmentClearOfLoop(const Vector2 a, const Vector2 b, const std::vector<Vector2>& loop) {
            const int n = static_cast<int>(loop.size());

            for (int i = 0; i < n; ++i) {
                const Vector2 c = loop[i];
                const Vector2 d = loop[(i + 1) % n];

                if (SamePoint(c, a) || SamePoint(c, b) || SamePoint(d, a) || SamePoint(d, b)) continue;
                if (SegmentsCross(a, b, c, d)) return false;
            }

            return true;
        }

        int RightmostVertexIndex(const std::vector<Vector2>& loop) {
            int best = 0;

            for (int i = 1; i < static_cast<int>(loop.size()); ++i) {
                if (loop[i].x > loop[best].x) best = i;
            }

            return best;
        }

        // Finds the index of an `outer` vertex reachable from `from` by a
        // straight bridge that crosses neither `outer` nor `hole`,
        // preferring the nearest such vertex so bridges stay short. A
        // hole fully enclosed by a simple outer polygon always has at
        // least one outer vertex visible from any of its own vertices,
        // so this only returns -1 on malformed input.
        int FindBridgeTarget(const Vector2 from, const std::vector<Vector2>& outer, const std::vector<Vector2>& hole) {
            int best = -1;
            float bestDistSq = std::numeric_limits<float>::max();

            for (int i = 0; i < static_cast<int>(outer.size()); ++i) {
                const Vector2 candidate = outer[i];

                if (!SegmentClearOfLoop(from, candidate, outer)) continue;
                if (!SegmentClearOfLoop(from, candidate, hole)) continue;

                const Vector2 delta = candidate - from;
                const float distSq = delta.x * delta.x + delta.y * delta.y;

                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    best = i;
                }
            }

            return best;
        }

        // Splices `hole` into `outer` through a bridge edge, producing a
        // single simple polygon with a zero-area slit standing in for the
        // hole - the standard technique for feeding a polygon with holes
        // through an ear-clipping triangulator built for simple polygons.
        // `hole` is always walked in the direction opposite `outer` from
        // the bridge point (regardless of the winding it came in with -
        // PolygonAreaSigned normalizes that first), so its interior ends
        // up outside the merged polygon rather than inside it.
        std::vector<Vector2> BridgeHoleIntoOuter(const std::vector<Vector2>& outer, std::vector<Vector2> hole) {
            if (hole.size() < 3) return outer;

            if (PolygonAreaSigned(hole) < 0.0f) std::ranges::reverse(hole);

            const int holeCount = static_cast<int>(hole.size());
            const int holeStart = RightmostVertexIndex(hole);
            const int outerIndex = FindBridgeTarget(hole[holeStart], outer, hole);

            // Malformed input (hole not actually enclosed by outer) - the
            // topology layer guarantees this doesn't happen, so this is
            // only a safety net. Drop the hole rather than corrupt the
            // rest of the triangulation.
            if (outerIndex < 0) return outer;

            std::vector<Vector2> merged;
            merged.reserve(outer.size() + hole.size() + 2);

            for (int i = 0; i <= outerIndex; ++i) merged.push_back(outer[i]);

            // Walk the hole backwards from holeStart back to holeStart,
            // i.e. opposite outer's winding, then return to the bridge
            // point to close the slit.
            for (int i = 0; i <= holeCount; ++i) {
                const int index = ((holeStart - i) % holeCount + holeCount) % holeCount;
                merged.push_back(hole[index]);
            }

            for (int i = outerIndex; i < static_cast<int>(outer.size()); ++i) merged.push_back(outer[i]);

            return merged;
        }
    }

    bool IsPointInPolygon(const std::vector<Vector2>& polygon, const Vector2& point) {
        bool inside = false;
        const size_t n = polygon.size();

        if (n < 3) return false;

        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const bool isBetweenY =(polygon[i].y > point.y) != (polygon[j].y > point.y);

            const float value = (polygon[j].x - polygon[i].x) * (point.y - polygon[i].y) / (polygon[j].y - polygon[i].y) +
                polygon[i].x;

            if (isBetweenY && point.x < value) inside = !inside;
        }

        return inside;
    }

    bool IsPointInPolygon(const std::vector<Vector2>& outer, const std::vector<std::vector<Vector2>>& holes, const Vector2& point) {
        if (!IsPointInPolygon(outer, point)) return false;

        for (const std::vector<Vector2>& hole : holes) {
            if (IsPointInPolygon(hole, point)) return false;
        }

        return true;
    }

    float PolygonAreaAbs(const std::vector<Vector2>& polygon) {
        float area = 0.0f;

        for (int i = 0; i < static_cast<int>(polygon.size()); ++i) {
            const int next = (i + 1) % static_cast<int>(polygon.size());

            area += polygon[i].x * polygon[next].y;
            area -= polygon[next].x * polygon[i].y;
        }

        return std::abs(area) * 0.5f;
    }

    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices) {
        std::vector<Triangle> triangles;

        if (vertices.size() < 3) return triangles;

        if (PolygonAreaSigned(vertices) < 0.0f) std::ranges::reverse(vertices);

        while (vertices.size() > 3) {
            bool earFound = false;

            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                const int prev = (i == 0) ? static_cast<int>(vertices.size()) - 1 : i - 1;

                const int next = (i == static_cast<int>(vertices.size()) - 1) ? 0 : i + 1;

                if (IsEar(vertices, prev, i, next)) {
                    triangles.push_back({
                        vertices[prev],
                        vertices[i],
                        vertices[next]
                    });

                    vertices.erase(vertices.begin() + i);

                    earFound = true;
                    break;
                }
            }

            if (!earFound) break;
        }

        if (vertices.size() == 3) {
            if (CrossAtPoint(vertices[0], vertices[1], vertices[2]) > Constants::Epsilon) {
                triangles.push_back({
                    vertices[0],
                    vertices[1],
                    vertices[2]
                });
            }
        }

        return triangles;
    }

    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices, std::vector<std::vector<Vector2>> holes) {
        if (vertices.size() < 3) return {};
        if (PolygonAreaSigned(vertices) < 0.0f) std::ranges::reverse(vertices);
        for (const std::vector<Vector2>& hole : holes) vertices = BridgeHoleIntoOuter(vertices, hole);
        return Triangulate(std::move(vertices));
    }
}