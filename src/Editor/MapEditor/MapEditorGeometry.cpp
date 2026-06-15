#include "../EditorInternal.hpp"

#include "Headers/Math/Geometry/Geometry.hpp"
#include "Headers/Math/Vector/Vector2Math.hpp"
#include "Headers/Map/LevelManager.hpp"

#include <algorithm>
#include <cmath>

#include "Headers/Map/MapQueries.hpp"
#include "Headers/Objects/Entity.hpp"

// This is an internal file for functions related ot mathematical calculations about the map
// Such as if a given point is inside a sector
namespace MapEditorInternal {
    float GetActiveGridSize() {
        constexpr float minPixelSpacing = 24.0f;

        float activeGridSize = GRID_SIZE;

        const float safeZoom = std::max(editorZoom, 0.0001f);

        while (activeGridSize * safeZoom < minPixelSpacing) {
            activeGridSize *= 2.0f;
        }

        return activeGridSize;
    }

    bool SamePoint(const Vector2& a, const Vector2& b) {
        return a.x == b.x && a.y == b.y;
    }

    bool WithinRadius(const Vector2& a, const Vector2& b, const float radius) {
        return Vector2Math::DistanceSquared(a, b) < radius * radius;
    }

    bool AABBCollisionWithEntity(const ComponentUITransform& transform, const Vector2& mousePosition) {
        const float entityLeft   = transform.resolvedPosition.x;
        const float entityRight  = transform.resolvedPosition.x + transform.resolvedSize.x;
        const float entityTop    = transform.resolvedPosition.y;
        const float entityBottom = transform.resolvedPosition.y + transform.resolvedSize.y;

        return mousePosition.x >= entityLeft &&
               mousePosition.x <= entityRight &&
               mousePosition.y >= entityTop &&
               mousePosition.y <= entityBottom;
    }
    bool AABBCollisionWithEntity(const ComponentTransform& transform, const Vector2& mousePosition) {
        constexpr float mouseSize = 0.1f;
        constexpr float mouseHalfSize = mouseSize * 0.5f;

        const float entityHalfWidth  = transform.scale.x * 0.5f;
        const float entityHalfHeight = transform.scale.y * 0.5f;

        const float entityLeft   = transform.position.x - entityHalfWidth;
        const float entityRight  = transform.position.x + entityHalfWidth;
        const float entityBottom = transform.position.y - entityHalfHeight;
        const float entityTop    = transform.position.y + entityHalfHeight;

        const float mouseLeft   = mousePosition.x - mouseHalfSize;
        const float mouseRight  = mousePosition.x + mouseHalfSize;
        const float mouseBottom = mousePosition.y - mouseHalfSize;
        const float mouseTop    = mousePosition.y + mouseHalfSize;

        return entityLeft <= mouseRight &&
               entityRight >= mouseLeft &&
               entityBottom <= mouseTop &&
               entityTop >= mouseBottom;
    }

    Entity* EntityAt(const Vector2& mouseClick) {
        Level& level = LevelManager::CurrentLevel();

        for (Entity& entity : level.entities) {
            const ComponentTransform* transform = level.transforms.Get(entity.id);
            const ComponentUITransform* uiTransform = level.ui_transforms.Get(entity.id);

            if (transform != nullptr ) {
                if (AABBCollisionWithEntity(*transform, mouseClick)) return &entity;
            }
            else if (uiTransform != nullptr) {
                if (AABBCollisionWithEntity(*uiTransform, mouseClick)) return &entity;
            }
        }

        return nullptr;
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

    Vector2 WorldToScreen(const Vector2& worldPos, const Vector2& cameraPos) {
        return {
            (worldPos.x - cameraPos.x) * editorZoom + screenWidth * 0.5f,
            screenHeight * 0.5f - (worldPos.y - cameraPos.y) * editorZoom
        };
    }

    Vector2 ScreenToWorld(const Vector2& screenPos, const Vector2& cameraPos) {
        return {
            (screenPos.x - screenWidth * 0.5f) / editorZoom + cameraPos.x,
            (screenHeight * 0.5f - screenPos.y) / editorZoom + cameraPos.y
        };
    }

    Vector2 SnapToGrid(const Vector2& worldPos) {
        const float activeGridSize = GetActiveGridSize();

        return {
            std::round(worldPos.x / activeGridSize) * activeGridSize,
            std::round(worldPos.y / activeGridSize) * activeGridSize
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

        Editor::CreateSector(
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

namespace Editor {
    void AddWall(const Wall& wall) {
        Level& level = LevelManager::CurrentLevel();

        Wall copy = wall;

        if (copy.id == INVALID_ID) copy.id = level.nextWallID++;
        else level.nextWallID = std::max(level.nextWallID, copy.id + 1);

        level.walls.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    void AddSector(const Sector& sector) {
        Level& level = LevelManager::CurrentLevel();

        Sector copy = sector;

        if (copy.id == INVALID_ID) copy.id = level.nextSectorID++;
        else level.nextSectorID = std::max(level.nextSectorID, copy.id + 1);

        level.sectors.push_back(copy);

        MapQueries::RebuildSectorRuntimeLinks(level);
    }

    void CreateSector(
        const std::vector<Vector2> &vertices,
        const float ceilHeight,
        const float floorHeight,
        const Vector3 ceilColor,
        const Vector3 floorColor,
        const int ceilTextureIndex,
        const int floorTextureIndex
    ) {
        Sector newSector{};

        newSector.vertices = vertices;
        newSector.triangles = Geometry::Triangulate(newSector.vertices);

        newSector.ceilingHeight = ceilHeight;
        newSector.floorHeight = floorHeight;

        newSector.ceilingColor = ceilColor;
        newSector.floorColor = floorColor;

        newSector.floorCount = 1;

        newSector.ceilingTextureIndices.fill(-1);
        newSector.ceilingTextureIndices[0] = ceilTextureIndex;

        newSector.floorTextureIndex = floorTextureIndex;

        AddSector(newSector);
    }

    // Legacy, probably should be removed in the future
    //todo remove
    void TriangulateSectors() {
        LevelManager::TriangulateCurrentLevelSectors();
    }
}