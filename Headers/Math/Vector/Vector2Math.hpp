#ifndef WOLFY_ENGINE_VECTOR2MATH_H
#define WOLFY_ENGINE_VECTOR2MATH_H

#include <cmath>
#include "Vector2.hpp"

namespace Vector2Math {

    inline float Length(const Vector2& v) {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    inline float LengthSquared(const Vector2& v) {
        return v.x * v.x + v.y * v.y;
    }

    inline Vector2 Normalized(const Vector2& v) {
        const float len = Length(v);
        if (len == 0.0f) return {};
        return {v.x / len, v.y / len};
    }

    inline void Normalize(Vector2& v) {
        const float len = Length(v);
        if (len == 0.0f) return;
        v.x /= len;
        v.y /= len;
    }

    inline float Dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    inline float Cross(const Vector2& a, const Vector2& b) {
        return a.x * b.y - a.y * b.x;
    }

}

#endif // WOLFY_ENGINE_VECTOR2MATH_H