#ifndef TILKY_ENGINE_GEOMETRY_HPP
#define TILKY_ENGINE_GEOMETRY_HPP

#include <vector>

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Objects/Sector.hpp"

namespace Geometry {
    bool IsPointInPolygon(const std::vector<Vector2>& polygon,const Vector2& point);

    float PolygonAreaAbs(const std::vector<Vector2>& polygon);

    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices);
}

#endif // TILKY_ENGINE_GEOMETRY_HPP