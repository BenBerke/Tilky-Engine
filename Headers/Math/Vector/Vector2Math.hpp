//
// Created by berke on 5/29/2026.
//

#ifndef TILKY_ENGINE_VECTOR2MATH_HPP
#define TILKY_ENGINE_VECTOR2MATH_HPP

#include "Vector2.hpp"

namespace Vector2Math {
    inline float LengthSquared(const Vector2& v) {
        return v.x * v.x + v.y * v.y;
    }

    inline float Length(const Vector2& v) {
        return std::sqrt(LengthSquared(v));
    }

    inline Vector2
    Normalized(const Vector2& v) {
        const float len = Length(v);
        if (len == 0.0f) return {};
        return {v.x / len, v.y / len};
    }

    inline void Normalize(Vector2& v) {
        const float len = Length(v);
        if (len == 0.0f) return;
        v.x /= len; v.y /= len;
    }

    inline float Dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    inline float Cross(const Vector2& a, const Vector2& b) {
        return a.x * b.y - a.y * b.x;
    }

    inline float DistanceSquared(const Vector2& a, const Vector2& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    inline float Distance(const Vector2& a, const Vector2& b) {
        return std::sqrt(DistanceSquared(a, b));
    }
}

#endif //TILKY_ENGINE_VECTOR2MATH_HPP