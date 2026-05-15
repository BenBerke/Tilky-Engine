#pragma once

#include <cmath>

#include "Headers/Math/Vector/Vector3.hpp"

namespace Vector3Math {
    inline float Dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    inline float Length(const Vector3& v) {
        return std::sqrt(Dot(v, v));
    }

    inline Vector3 Normalized(const Vector3& v) {
        const float length = Length(v);

        if (length <= 0.00001f) {
            return {0.0f, 0.0f, 0.0f};
        }

        return {
            v.x / length,
            v.y / length,
            v.z / length
        };
    }
}