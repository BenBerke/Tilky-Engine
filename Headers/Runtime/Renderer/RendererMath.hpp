#ifndef TILKY_ENGINE_RENDERERMATH_HPP
#define TILKY_ENGINE_RENDERERMATH_HPP

#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector4.hpp"

namespace RendererMath {
    inline constexpr float DEBUG_MAP_SCALE = 200.0f;

    float DegToRad(float degrees);

    Vector2 RotatePoint(
        const Vector2& point,
        float angleRad
    );

    Vector2 WorldToDebugNdc(
        const Vector2& worldPoint,
        const Vector2& playerPos
    );

    float GetViewDepth(
        const Vector4& point,
        const Vector2& playerPos,
        float playerAngle
    );

    Vector4 LerpVector4(
        const Vector4& a,
        const Vector4& b,
        float t
    );
}

#endif