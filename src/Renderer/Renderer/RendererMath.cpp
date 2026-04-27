#include "../../Headers/Renderer/Renderer/RendererInternal.hpp"

#include <cmath>

namespace RendererInternal {
    float DegToRad(const float degrees) {
        return degrees * 3.14159265359f / 180.0f;
    }

    Vector2 RotatePoint(const Vector2& p, const float angleRad) {
        const float c = std::cos(angleRad);
        const float s = std::sin(angleRad);

        return {
            p.x * c - p.y * s,
            p.x * s + p.y * c
        };
    }

    Vector2 WorldToDebugNdc(const Vector2& worldPoint, const Vector2& playerPos) {
        const Vector2 relative = {
            worldPoint.x - playerPos.x,
            worldPoint.y - playerPos.y
        };

        return {
            relative.x / DEBUG_MAP_SCALE,
            relative.y / DEBUG_MAP_SCALE
        };
    }

    float GetViewDepth(const Vector4& point, const Vector2& playerPos, const float playerAngle) {
        const Vector2 worldPos = {
            point.x,
            point.y
        };

        const Vector2 relative = {
            worldPos.x - playerPos.x,
            worldPos.y - playerPos.y
        };

        const Vector2 view = RotatePoint(relative, DegToRad(playerAngle));

        return view.y;
    }

    Vector4 LerpVector4(const Vector4& a, const Vector4& b, const float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        };
    }
}