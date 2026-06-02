#include "Headers/Math/Geometry/Geometry.hpp"
#include <algorithm>
#include <cmath>
#include "Headers/Math/Vector/Vector2Math.hpp"

namespace Geometry {
    namespace {
        constexpr float EPSILON = 0.00001f;

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

        bool IsInsideTriangle(
            const Vector2 a,
            const Vector2 b,
            const Vector2 c,
            const Vector2 p
        ) {
            const float cp1 = CrossAtPoint(a, b, p);
            const float cp2 = CrossAtPoint(b, c, p);
            const float cp3 = CrossAtPoint(c, a, p);

            return cp1 >= -EPSILON &&
                   cp2 >= -EPSILON &&
                   cp3 >= -EPSILON;
        }

        bool IsEar(
            const std::vector<Vector2>& vertices,
            const int prev,
            const int curr,
            const int next
        ) {
            const Vector2 a = vertices[prev];
            const Vector2 b = vertices[curr];
            const Vector2 c = vertices[next];

            if (CrossAtPoint(a, b, c) <= EPSILON) {
                return false;
            }

            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                if (i == prev || i == curr || i == next) {
                    continue;
                }

                if (IsInsideTriangle(a, b, c, vertices[i])) {
                    return false;
                }
            }

            return true;
        }
    }

    bool IsPointInPolygon(
        const std::vector<Vector2>& polygon,
        const Vector2& point
    ) {
        bool inside = false;
        const size_t n = polygon.size();

        if (n < 3) {
            return false;
        }

        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const bool isBetweenY =
                (polygon[i].y > point.y) != (polygon[j].y > point.y);

            if (
                isBetweenY &&
                point.x <
                    (polygon[j].x - polygon[i].x) *
                    (point.y - polygon[i].y) /
                    (polygon[j].y - polygon[i].y) +
                    polygon[i].x
            ) {
                inside = !inside;
            }
        }

        return inside;
    }

    float PolygonAreaAbs(
        const std::vector<Vector2>& polygon
    ) {
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

        if (vertices.size() < 3) {
            return triangles;
        }

        if (PolygonAreaSigned(vertices) < 0.0f) {
            std::ranges::reverse(vertices);
        }

        while (vertices.size() > 3) {
            bool earFound = false;

            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                const int prev =
                    (i == 0)
                        ? static_cast<int>(vertices.size()) - 1
                        : i - 1;

                const int next =
                    (i == static_cast<int>(vertices.size()) - 1)
                        ? 0
                        : i + 1;

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

            if (!earFound) {
                break;
            }
        }

        if (vertices.size() == 3) {
            if (CrossAtPoint(vertices[0], vertices[1], vertices[2]) > EPSILON) {
                triangles.push_back({
                    vertices[0],
                    vertices[1],
                    vertices[2]
                });
            }
        }

        return triangles;
    }
}