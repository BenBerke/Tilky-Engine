#ifndef TILKY_ENGINE_GEOMETRY_HPP
#define TILKY_ENGINE_GEOMETRY_HPP

#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Objects/Sector.hpp"

namespace Geometry {
    bool IsPointInPolygon(const std::vector<Vector2>& polygon,const Vector2& point);

    // Even-odd containment test against `outer` with zero or more holes
    // cut out of it. Each hole is a simple polygon in its own right, and
    // (like the overload above) winding-independent - a point counts as
    // inside only when it is inside `outer` and inside none of `holes`.
    bool IsPointInPolygon(const std::vector<Vector2>& outer, const std::vector<std::vector<Vector2>>& holes, const Vector2& point);

    float PolygonAreaAbs(const std::vector<Vector2>& polygon);

    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices);

    // Ear-clipping triangulation of `vertices` with zero or more holes
    // cut out of it. Every hole is stitched into the outer boundary
    // through a bridge edge to its nearest visible vertex, reducing the
    // shape to a single simple (self-touching) polygon that the
    // overload above already triangulates correctly.
    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices, std::vector<std::vector<Vector2>> holes);
}

#endif // TILKY_ENGINE_GEOMETRY_HPP