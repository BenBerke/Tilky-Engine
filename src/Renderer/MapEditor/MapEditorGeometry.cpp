#include "MapEditorInternal.hpp"

#include "Headers/Math/Vector/Vector2Math.hpp"
#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <cmath>

namespace MapEditorInternal {
    bool SamePoint(const Vector2& a, const Vector2& b) {
        return a.x == b.x && a.y == b.y;
    }

    bool WithinRadius(const Vector2& a, const Vector2& b, const float radius) {
        return Vector2Math::DistanceSquared(a, b) < radius * radius;
    }

    bool CornerExistsAt(const Vector2& point) {
        for (const Vector2& placedCorner : placedCorners) {
            if (SamePoint(placedCorner, point)) {
                return true;
            }
        }

        return false;
    }

    bool IsCornerConnectedToLine(const Vector2& point) {
        Level& level = LevelManager::CurrentLevel();

        for (const Wall& wall : level.walls) {
            if (SamePoint(wall.start, point) || SamePoint(wall.end, point)) {
                return true;
            }
        }

        if (drawingLine && SamePoint(lineStartWorld, point)) {
            return true;
        }

        return false;
    }

    bool HasLineBetween(const Vector2& a, const Vector2& b) {
        Level& level = LevelManager::CurrentLevel();

        for (const Wall& wall : level.walls) {
            const bool sameDirection = SamePoint(wall.start, a) && SamePoint(wall.end, b);
            const bool oppositeDirection = SamePoint(wall.start, b) && SamePoint(wall.end, a);

            if (sameDirection || oppositeDirection) {
                return true;
            }
        }

        return false;
    }

    std::vector<Vector2> GetSectorVerticesWithoutClosingDuplicate() {
        std::vector<Vector2> result = sectorBeingCreated;

        if (result.size() >= 2 && SamePoint(result.front(), result.back())) {
            result.pop_back();
        }

        return result;
    }

    bool IsSectorClosed(const std::vector<Vector2>& vertices) {
        if (vertices.size() < 3) {
            return false;
        }

        if (sectorBeingCreated.size() >= 4 &&
            SamePoint(sectorBeingCreated.front(), sectorBeingCreated.back())) {
            return true;
        }

        for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
            const Vector2& a = vertices[i];
            const Vector2& b = vertices[(i + 1) % vertices.size()];

            if (!HasLineBetween(a, b)) {
                return false;
            }
        }

        return true;
    }

    void AddSectorSelectionPoint(const Vector2& point) {
        if (sectorBeingCreated.size() >= 3 && SamePoint(point, sectorBeingCreated.front())) {
            sectorBeingCreated.push_back(point);
            return;
        }

        for (const Vector2& existingPoint : sectorBeingCreated) {
            if (SamePoint(existingPoint, point)) {
                return;
            }
        }

        sectorBeingCreated.push_back(point);
    }

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos) {
        return {
            screenPos.x - SCREEN_WIDTH * 0.5f + cameraPos.x,
            SCREEN_HEIGHT * 0.5f - screenPos.y + cameraPos.y
        };
    }

    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos) {
        return {
            worldPos.x - cameraPos.x + SCREEN_WIDTH * 0.5f,
            SCREEN_HEIGHT * 0.5f - (worldPos.y - cameraPos.y)
        };
    }

    Vector2 SnapToGrid(const Vector2& worldPos) {
        return {
            std::round(worldPos.x / GRID_SIZE) * GRID_SIZE,
            std::round(worldPos.y / GRID_SIZE) * GRID_SIZE
        };
    }

    bool IsPointInsidePolygon(const std::vector<Vector2>& polygon, const Vector2& point) {
        bool inside = false;

        for (int i = 0, j = static_cast<int>(polygon.size()) - 1;
             i < static_cast<int>(polygon.size());
             j = i++) {

            const Vector2& a = polygon[i];
            const Vector2& b = polygon[j];

            const bool crossesY = (a.y > point.y) != (b.y > point.y);

            if (crossesY) {
                const float intersectX =
                    (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;

                if (point.x < intersectX) {
                    inside = !inside;
                }
            }
        }

        return inside;
    }

    void FinishSectorSelection() {
        const std::vector<Vector2> finalVertices = GetSectorVerticesWithoutClosingDuplicate();

        if (finalVertices.size() < 3) {
            SDL_Log("Sector cancelled: fewer than 3 points");
            sectorBeingCreated.clear();
            return;
        }

        if (!IsSectorClosed(finalVertices)) {
            SDL_Log("Sector cancelled: shape is not closed");
            sectorBeingCreated.clear();
            return;
        }

        MapEditor::CreateSector(
            finalVertices,
            40.0f,
            0.0f,
            {255.0f, 255.0f, 255.0f},
            {255.0f, 255.0f, 255.0f},
            -1,
            -1
        );

        SDL_Log("Sector created with %d vertices", static_cast<int>(finalVertices.size()));

        sectorBeingCreated.clear();
    }

    float DistancePointToSegmentSq(const Vector2& point, const Vector2& a, const Vector2& b) {
        const Vector2 ab = b - a;
        const Vector2 ap = point - a;

        const float abLengthSq = Vector2Math::Dot(ab, ab);

        if (abLengthSq <= 0.00001f) {
            const Vector2 diff = point - a;
            return Vector2Math::Dot(diff, diff);
        }

        float t = Vector2Math::Dot(ap, ab) / abLengthSq;
        t = std::clamp(t, 0.0f, 1.0f);

        const Vector2 closestPoint = {
            a.x + ab.x * t,
            a.y + ab.y * t
        };

        const Vector2 diff = point - closestPoint;
        return Vector2Math::Dot(diff, diff);
    }

    int GetWallAtPoint(const Vector2& worldPoint) {
        Level& level = LevelManager::CurrentLevel();

        constexpr float clickRadius = 12.0f;
        constexpr float clickRadiusSq = clickRadius * clickRadius;

        int closestWallIndex = -1;
        float closestDistanceSq = clickRadiusSq;

        for (int i = 0; i < static_cast<int>(level.walls.size()); ++i) {
            const Wall& wall = level.walls[i];
            if (wall.floor != currentFloor) continue;

            const float distanceSq = DistancePointToSegmentSq(
                worldPoint,
                wall.start,
                wall.end
            );

            if (distanceSq < closestDistanceSq) {
                closestDistanceSq = distanceSq;
                closestWallIndex = i;
            }
        }

        return closestWallIndex;
    }
}

namespace {
    constexpr float EPSILON = 0.00001f;

    float CrossAtPoint(const Vector2 a, const Vector2 b, const Vector2 c) {
        return Vector2Math::Cross(b - a, c - a);
    }

    float PolygonArea(const std::vector<Vector2>& vertices) {
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

        return cp1 >= -EPSILON &&
               cp2 >= -EPSILON &&
               cp3 >= -EPSILON;
    }

    bool IsEar(const std::vector<Vector2>& vertices, const int prev, const int curr, const int next) {
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

namespace MapEditor {
    std::vector<Triangle> Triangulate(std::vector<Vector2> vertices) {
        std::vector<Triangle> triangles;

        if (vertices.size() < 3) {
            return triangles;
        }

        if (PolygonArea(vertices) < 0.0f) {
            std::ranges::reverse(vertices);
        }

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

    void AddWall(const Wall& wall) {
        LevelManager::CurrentLevel().walls.push_back(wall);
    }

    void AddSector(const Sector& sector) {
        LevelManager::CurrentLevel().sectors.push_back(sector);
    }

    void CreateSector(
        const std::vector<Vector2>& vertices,
        const float ceilHeight,
        const float floorHeight,
        const Vector3 ceilColor,
        const Vector3 floorColor,
        const int ceilTextureIndex,
        const int floorTextureIndex
    ) {
        Sector newSector = {
            vertices,
            {},
            ceilHeight,
            floorHeight,
            ceilColor,
            floorColor,
            ceilTextureIndex,
            floorTextureIndex
        };

        newSector.triangles = Triangulate(newSector.vertices);

        AddSector(newSector);
    }

    void TriangulateSectors() {
        Level& level = LevelManager::CurrentLevel();

        for (Sector& sector : level.sectors) {
            sector.triangles.clear();

            if (sector.vertices.size() < 3) {
                continue;
            }

            sector.triangles = Triangulate(sector.vertices);
        }
    }
}